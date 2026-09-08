// 一段 客户端<->游戏服 的中继会话,以网关分配的 conv 为键
// 只 CONNECT 之后 target_fd >= 0 才算"活着",否则是挂着等 CONNECT 的 pending 态
// dying 只是标记,真正的析构统一由 session_manager 在 epoll 一批处理完再 erase
#ifndef SESSION_H
#define SESSION_H

#include <cstdint>
#include <netinet/in.h>

#include "kcp_session.h"

struct Session {
  uint32_t    conv = 0;          // 网关分配,会话表主键
  KcpSession  kcp;               // KCP + 对端地址;kcp.kcp 在 emplace 后 init
  sockaddr_in client_addr{};     // 客户端端点
  sockaddr_in target_addr{};     // 目标游戏服端点(CONNECT 时解析出来)
  int         target_fd = -1;    // 发往目标服的 socket,-1 表示还没 CONNECT
  bool        dying = false;     // 已判死,等延迟拆除
  uint64_t    last_active = 0;   // 最后一次真实流量时刻(ms)

  Session() = default;
  Session(uint32_t c, const sockaddr_in &addr);
  ~Session();
};

#endif  // SESSION_H
