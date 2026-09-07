// udp_client.cpp
// Phase 1: UDP client. Sends a message to the gateway (127.0.0.1:8000),
// prints the echoed reply.
//
//   ./udp_client hello          -> send "hello" once, print reply, exit
//   ./udp_client                -> read lines from stdin, echo each, until EOF
#include <arpa/inet.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define GATEWAY_IP "127.0.0.1"
#define GATEWAY_PORT 8000
#define BUF_SIZE 65535

int main(int argc, char** argv) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { perror("socket error"); return 1;}

    sockaddr_in gw{};
    gw.sin_family = AF_INET;
    gw.sin_port = htons(GATEWAY_PORT);
    inet_pton(AF_INET, GATEWAY_IP, &gw.sin_addr);

    char buf[BUF_SIZE];

    auto send_recv = [&](const char *msg) {
      sendto(fd, msg, strlen(msg), 0, (sockaddr*)&gw, sizeof(gw));
      printf("[client] sent: %s\n", msg);

      sockaddr_in from{};
      socklen_t from_len = sizeof(from);
      ssize_t n =
          recvfrom(fd, buf, sizeof(buf), 0, (sockaddr *)&from, &from_len);
      if (n < 0) {perror("recvfrom"); return ;}
      buf[n] = '\0';
      printf("[client] echo: %s\n", buf);
    };

    if (argc >= 2) {
      send_recv(argv[1]);
    } else {
        printf("[client] interactive mode, send lines to %s:%d "
               "(Ctrl-D to quit)\n", GATEWAY_IP, GATEWAY_PORT);
        while (fgets(buf, sizeof(buf), stdin)) {
          buf[strcspn(buf, "\n")] = '\0';
          if (buf[0] == '\0') continue;
          if (strcmp(buf, "quit") == 0)
            break;
          send_recv(buf);
        }
    }

    close(fd);
    return 0;
}