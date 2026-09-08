// 客户端隧道封装: 向网关 REGISTER 拿 conv、起 KCP、走 CONNECT/DATA/CLOSE。
// main 只要拿着它发消息等回显就行。
#ifndef TUNNEL_CLIENT_H
#define TUNNEL_CLIENT_H

#include <cstddef>
#include <cstdint>
#include <netinet/in.h>

#include "../third_party/kcp/ikcp.h"

class TunnelClient {
public:
  TunnelClient() = default;
  ~TunnelClient() { disconnect(); }

  bool connect(const char *ip, uint16_t port);          // 注册 + 起 KCP
  bool open_route(const char *route);                   // CONNECT
  void send_data(const char *text, uint16_t len);       // DATA
  bool wait_reply(uint8_t *out, size_t *out_len, int timeout_ms);
  void send_close();                                    // CLOSE + 等 KCP 发完
  void disconnect();
  uint32_t conv() const { return conv_; }

private:
  static int kcp_output(const char *buf, int len, ikcpcb *, void *user);
  void kcp_pump();
  void send_tun(const uint8_t *pkt, size_t n);
  bool register_with_gateway(uint32_t *out_conv);
  int         fd_   = -1;
  ikcpcb     *kcp_  = nullptr;
  sockaddr_in gw_{};
  uint32_t    conv_ = 0;
  uint8_t     wbuf_[65535];
};

#endif  // TUNNEL_CLIENT_H
