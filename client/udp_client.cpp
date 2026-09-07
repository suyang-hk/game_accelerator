#include <arpa/inet.h>
#include <asm-generic/socket.h>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>


#include "../common/tunnel_protocol.h"

#define GATEWAY_IP "127.0.0.1"
#define GATEWAY_PORT 8000
#define BUF_SIZE 65535

static void usage() {
    fprintf(stderr,
            "usage: udp_client <conv> <dst_ip> <dst_port> [message]\n"
            "  e.g.: udp_client 1001 127.0.0.1 9000 hello\n");
}

int main(int argc, char** argv) {
    if (argc < 4) {usage(); return 1;}
  
    const uint32_t conv = (uint32_t)strtoul(argv[1], nullptr, 10);
    const char* dst_ip = argv[2];
    const uint16_t dst_port = (uint16_t)strtoul(argv[3], nullptr, 10);
    const char* one_shot = (argc >= 5) ? argv[4] : nullptr;

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { perror("socket error"); return 1;}

    sockaddr_in gw{};
    gw.sin_family = AF_INET;
    gw.sin_port = htons(GATEWAY_PORT);
    inet_pton(AF_INET, GATEWAY_IP, &gw.sin_addr);

    timeval tv{3, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint8_t wire[BUF_SIZE];

    //------connect-----
    uint32_t dst_ip_n;
    inet_pton(AF_INET, dst_ip, &dst_ip_n);
    size_t conn_sz = tunnel::pack_connect(wire, conv, dst_ip_n, htons(dst_port));
    sendto(fd, wire, conn_sz, 0, (sockaddr*)&gw, sizeof(gw));
    printf("[client] CONNECT conv=%u dst=%s:%u\n", conv, dst_ip, dst_port);

    auto roundtrip = [&](const char *text) -> bool {
      uint16_t len = (uint16_t)strlen(text);
      size_t data_pack_sz = tunnel::pack_data(wire, conv, text, len);
      sendto(fd, wire, data_pack_sz, 0, (sockaddr*)&gw, sizeof(gw));
      printf("[client] DATA conv=%u sent: %s\n", conv, text);

      uint8_t rbuf[BUF_SIZE];
      sockaddr_in from{};
      socklen_t from_len = sizeof(from);
      ssize_t n = recvfrom(fd, rbuf, sizeof(rbuf), 0, (sockaddr*)&from, &from_len);
      if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                printf("[client] (no reply within timeout)\n");
            else
                perror("recvfrom");
            return false;
        }

    uint8_t rtype;
    uint32_t rconv;
    const uint8_t *data;
    uint16_t data_len;
    if (!tunnel::parse(rbuf, (size_t)n, &rtype, &rconv, &data, &data_len)) {
        printf("[client] malformed reply\n");
            return false;
    }

    if (rtype == tunnel::PACKET_DATA) {
        char tmp[4096 + 1];
        size_t show = data_len < sizeof(tmp) - 1 ? data_len : sizeof(tmp) - 1;
        memcpy(tmp, data, show);
        tmp[show] = '\0';
        printf("[client] echo conv=%u: %s\n", rconv, tmp);
        return true;
    }
    printf("[client] unexpected reply type %u\n", rtype);
        return false;
    };

    if (one_shot) {
        roundtrip(one_shot);
    } else {
        printf("[client] interactive, lines become DATA on conv=%u "
               "(Ctrl-D / \"quit\" closes)\n", conv);
        char line[BUF_SIZE];
        while (fgets(line, sizeof(line), stdin)) {
            line[strcspn(line, "\n")] = '\0';
            if (line[0] == '\0') continue;
            if (strcmp(line, "quit") == 0) break;
            roundtrip(line);
        }
    }

    // ---- CLOSE---
    size_t z = tunnel::pack_close(wire, conv);
    sendto(fd, wire, z, 0, (sockaddr*)&gw, sizeof(gw));
    printf("[client] CLOSE conv=%u\n", conv);


    close(fd);
    return 0;
}