#include "tunnel_client.h"

#include <arpa/inet.h>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "../common/net_utils.h"
#include "../common/tunnel_protocol.h"

int TunnelClient::kcp_output(const char *buf, int len, ikcpcb *, void *user) {
  TunnelClient *tc = static_cast<TunnelClient *>(user);
  sendto(tc->fd_, buf, (size_t)len, 0,
         (sockaddr *)&tc->gw_, sizeof(tc->gw_));
  return len;
}

void TunnelClient::kcp_pump() {
  ikcp_update(kcp_, now32());
  ikcp_flush(kcp_);
}

void TunnelClient::send_tun(const uint8_t *pkt, size_t n) {
  ikcp_update(kcp_, now32());
  ikcp_send(kcp_, (const char *)pkt, (int)n);
  ikcp_flush(kcp_);
}

// 裸 UDP 注册(还没 KCP): 发 REGISTER,等网关回 REGISTERED + 分配的 conv
bool TunnelClient::register_with_gateway(uint32_t *out_conv) {
  uint8_t reg[tunnel::HEAD_LEN];
  size_t reg_len = tunnel::pack_register(reg);

  const int attempts = 5;
  for (int i = 0; i < attempts; i++) {
    sendto(fd_, reg, reg_len, 0, (sockaddr *)&gw_, sizeof(gw_));

    timeval tv{0, 300000};  
    fd_set rf;
    FD_ZERO(&rf);
    FD_SET(fd_, &rf);
    if (select(fd_ + 1, &rf, nullptr, nullptr, &tv) <= 0) continue;
    if (!FD_ISSET(fd_, &rf)) continue;

    uint8_t buf[16];
    sockaddr_in from{};
    socklen_t from_len = sizeof(from);
    ssize_t n = recvfrom(fd_, buf, sizeof(buf), 0,
                         (sockaddr *)&from, &from_len);
    if (n <= 0) continue;
    // 只认网关端点回的包
    if (from.sin_addr.s_addr != gw_.sin_addr.s_addr ||
        from.sin_port != gw_.sin_port) continue;
    if (tunnel::read_registered(buf, (size_t)n, out_conv)) {
      printf("[client] registered conv=%u\n", *out_conv);
      return true;
    }
  }
  fprintf(stderr, "[client] registration failed: no REGISTERED from gateway\n");
  return false;
}

bool TunnelClient::connect(const char *ip, uint16_t port) {
  fd_ = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd_ < 0) { perror("socket error"); return false; }

  gw_.sin_family = AF_INET;
  gw_.sin_port = htons(port);
  inet_pton(AF_INET, ip, &gw_.sin_addr);

  uint32_t conv = 0;
  if (!register_with_gateway(&conv)) {
    close(fd_);
    fd_ = -1;
    return false;
  }
  conv_ = conv;

  kcp_ = ikcp_create(conv, this);  // user 记 this,输出回调回包
  ikcp_setoutput(kcp_, &TunnelClient::kcp_output);
  ikcp_nodelay(kcp_, 1, 10, 2, 1);
  return true;
}

bool TunnelClient::open_route(const char *route) {
  if (strlen(route) > tunnel::ROUTE_MAX) {
    fprintf(stderr, "[client] route name too long (max %u)\n", tunnel::ROUTE_MAX);
    return false;
  }
  size_t sz = tunnel::pack_connect(wbuf_, route, (uint8_t)strlen(route));
  send_tun(wbuf_, sz);
  return true;
}

void TunnelClient::send_data(const char *text, uint16_t len) {
  size_t sz = tunnel::pack_data(wbuf_, text, len);
  send_tun(wbuf_, sz);
}

bool TunnelClient::wait_reply(uint8_t *out, size_t *out_len, int timeout_ms) {
  uint64_t start = now_ms();
  while (true) {
    kcp_pump();
    int got;
    // 网关只会回 DATA 隧道报文
    while ((got = ikcp_recv(kcp_, (char *)out, 65535)) > 0) {
      *out_len = (size_t)got;
      return true;
    }
    long rem = timeout_ms - (long)(now_ms() - start);
    if (rem <= 0) return false;

    timeval tv{};
    tv.tv_sec = rem / 1000;
    tv.tv_usec = (rem % 1000) * 1000;
    fd_set rf;
    FD_ZERO(&rf);
    FD_SET(fd_, &rf);
    int s = select(fd_ + 1, &rf, nullptr, nullptr, &tv);
    if (s > 0 && FD_ISSET(fd_, &rf)) {
      uint8_t buf[65535];
      ssize_t n = recvfrom(fd_, buf, sizeof(buf), 0, nullptr, nullptr);
      if (n > 0) {
        ikcp_input(kcp_, (const char *)buf, (long)n);
        kcp_pump();
      }
    }
  }
}

void TunnelClient::send_close() {
  size_t z = tunnel::pack_close(wbuf_);
  send_tun(wbuf_, z);
  usleep(100 * 1000);  // 留 100ms 给 KCP 把 CLOSE 发出去
  kcp_pump();
}

void TunnelClient::disconnect() {
  if (kcp_) { ikcp_release(kcp_); kcp_ = nullptr; }
  if (fd_ >= 0) { close(fd_); fd_ = -1; }
}
