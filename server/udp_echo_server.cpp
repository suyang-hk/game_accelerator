// udp_echo_server.cpp
// Phase 1: plain UDP echo server. Listens on 127.0.0.1:9000.
// Every received datagram is printed and sent straight back to its sender.
#include <arpa/inet.h>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 9000
#define BUF_SIZE 65535

int main() {
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {perror("socket"); return 1;}

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(SERVER_PORT);
  inet_pton(AF_INET, SERVER_IP, &addr.sin_addr);

  if (bind(fd, (sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind");
    close(fd);
    return 1;
  }

    printf("[server] listening on %s:%d\n", SERVER_IP, SERVER_PORT);

    char buf[BUF_SIZE];
    while (true) {
      sockaddr_in peer{};
      socklen_t peer_len = sizeof(peer);
      ssize_t n = recvfrom(fd, buf, sizeof(buf), 0, (sockaddr*)&peer, &peer_len);
      if (n < 0) {perror("recvfrom"); continue;}
      buf[n] = '\0';

      char peer_ip[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &peer.sin_addr, peer_ip, sizeof(peer_ip));
      printf("[server] recv from %s:%d : %s\n",
            peer_ip, ntohs(peer.sin_port), buf);

      sendto(fd, buf, (size_t)n, 0, (sockaddr *)&peer, peer_len);
    }

    close(fd);
    return 0;
}