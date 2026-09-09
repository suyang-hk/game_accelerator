// 最小 UDP sendto 探针: 打印 sendto/recvfrom 的返回与 errno
#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
int main(int argc, char **argv) {
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (argc < 2) {  // 默认带 128KB 缓冲; 传 min 则用默认内核缓冲
    int sz = 128 * 1024;
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &sz, sizeof(sz));
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sz, sizeof(sz));
  }
  sockaddr_in gw = {};
  gw.sin_family = AF_INET;
  gw.sin_port = htons(argc > 2 ? (uint16_t)atoi(argv[2]) : 8000);
  inet_pton(AF_INET, "127.0.0.1", &gw.sin_addr);
  uint8_t reg = 4;  // PACKET_REGISTER
  for (int i = 0; i < 3; i++) {
    errno = 0;
    ssize_t r = sendto(fd, &reg, 1, 0, (sockaddr *)&gw, sizeof(gw));
    printf("sendto(%s)#%d ret=%zd errno=%d (%s)\n",
           (argc > 2 ? argv[2] : "8000"), i, r, errno,
           errno ? strerror(errno) : "none");
  }
  timeval tv = {0, 300000};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  for (int i = 0; i < 3; i++) {
    errno = 0;
    uint8_t b[32] = {0};
    ssize_t g = recvfrom(fd, b, sizeof(b), 0, nullptr, nullptr);
    printf("recv#%d ret=%zd errno=%d (%s) firstbyte=%u\n", i, g, errno,
           errno ? strerror(errno) : "none", g > 0 ? b[0] : 0);
    if (g > 0) break;
  }
  close(fd);
  return 0;
}
