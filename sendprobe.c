// 最小 UDP sendto 探针: 打印 sendto/recvfrom 的返回与 errno
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
int main(void) {
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  int sz = 128 * 1024;
  setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &sz, sizeof(sz));
  setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sz, sizeof(sz));
  sockaddr_in gw = {};
  gw.sin_family = AF_INET;
  gw.sin_port = htons(8000);
  inet_pton(AF_INET, "127.0.0.1", &gw.sin_addr);
  uint8_t reg = 4;  // PACKET_REGISTER
  for (int i = 0; i < 3; i++) {
    errno = 0;
    ssize_t r = sendto(fd, &reg, 1, 0, (sockaddr *)&gw, sizeof(gw));
    printf("sendto#%d ret=%zd errno=%d (%s)\n", i, r, errno, strerror(errno));
  }
  // 非阻塞读, 300ms
  struct timeval tv = {0, 300000};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  for (int i = 0; i < 3; i++) {
    errno = 0;
    uint8_t b[32] = {0};
    ssize_t g = recvfrom(fd, b, sizeof(b), 0, NULL, NULL);
    printf("recv#%d ret=%zd errno=%d (%s) firstbyte=%u\n", i, g, errno,
           errno ? strerror(errno) : "none", g > 0 ? b[0] : 0);
    if (g > 0) break;
  }
  close(fd);
  return 0;
}
