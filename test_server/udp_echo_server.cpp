// 测试用的 UDP 回声服: 收到什么原样回什么,网关用它当中继目标验证
#include <arpa/inet.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#define SERVER_IP "127.0.0.1"
#define BUF_SIZE  65535

int main(int argc, char** argv) {
    int port = (argc >= 2) ? atoi(argv[1]) : 9000;

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { perror("socket"); return 1; }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    inet_pton(AF_INET, SERVER_IP, &addr.sin_addr);
    if (bind(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(fd);
        return 1;
    }
    printf("[server] listening on %s:%d\n", SERVER_IP, port);

    char buf[BUF_SIZE];
    while (true) {
        sockaddr_in peer{};
        socklen_t   peer_len = sizeof(peer);
        ssize_t n = recvfrom(fd, buf, sizeof(buf), 0,
                             (sockaddr*)&peer, &peer_len);
        if (n < 0) { perror("recvfrom"); continue; }
        buf[n] = '\0';

        char peer_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &peer.sin_addr, peer_ip, sizeof(peer_ip));
        printf("[server] recv from %s:%d : %s\n",
               peer_ip, ntohs(peer.sin_port), buf);
        sendto(fd, buf, (size_t)n, 0, (sockaddr*)&peer, peer_len);
    }

    close(fd);
    return 0;
}
