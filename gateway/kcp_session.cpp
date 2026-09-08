#include "kcp_session.h"

#include <sys/socket.h>
#include <unistd.h>

namespace {
int kcp_output(const char *buf, int len, ikcpcb *, void *user) {
  KcpSession *ks = static_cast<KcpSession *>(user);
  sendto(ks->client_fd, buf, (size_t)len, 0,
         (const sockaddr *)&ks->peer, sizeof(ks->peer));
  return len;
}
}  // namespace

void kcp_session_init(KcpSession &ks, uint32_t conv,
                      const sockaddr_in &peer, int client_fd) {
  ks.peer = peer;
  ks.client_fd = client_fd;
  ks.kcp = ikcp_create(conv, &ks);  // user 记 ks,输出回调用它回包
  ikcp_setoutput(ks.kcp, kcp_output);
  ikcp_nodelay(ks.kcp, 1, 10, 2, 1);
}

void kcp_session_release(KcpSession &ks) {
  if (ks.kcp) {
    ikcp_release(ks.kcp);
    ks.kcp = nullptr;
  }
}
