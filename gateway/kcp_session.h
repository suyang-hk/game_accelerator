// 一个 conv 一个 KCP 对象。kcp 的输出回调(user 指向本结构)只认 peer 和
// client_fd,不关心具体会话,所以这里不必带上 Session。
#ifndef KCP_SESSION_H
#define KCP_SESSION_H

#include <cstdint>
#include <netinet/in.h>

#include "../third_party/kcp/ikcp.h"

struct KcpSession {
  ikcpcb     *kcp = nullptr;   // 为 null 表示还没 init(或已释放)
  sockaddr_in peer{};          // KCP 出包要回发的对端
  int         client_fd = -1;  // 共享的隧道 socket
};

void kcp_session_init(KcpSession &ks, uint32_t conv,
                      const sockaddr_in &peer, int client_fd);
void kcp_session_release(KcpSession &ks);

#endif  // KCP_SESSION_H
