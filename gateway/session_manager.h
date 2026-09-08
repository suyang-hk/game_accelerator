// 会话表 + conv 分配 + 注册/查找/延迟拆除/空闲回收
// 持有者: 把 target fd 挂到 event loop 的 epoll 上,会话增删时回调 sync_timer,让 loop 决定 5ms KCP tick 要不要开
#ifndef SESSION_MANAGER_H
#define SESSION_MANAGER_H

#include <cstdint>
#include <functional>
#include <netinet/in.h>
#include <unordered_map>
#include <vector>

#include "session.h"

class SessionManager {
public:
  SessionManager(int client_fd, int epfd, std::function<void()> sync_timer);
  ~SessionManager() = default;

  void for_each_active(const std::function<void(Session &)> &fn);
  bool empty() const;
  void set_idle_ms(uint64_t ms);

  void register_client(const sockaddr_in &src);  // REGISTER 握手
  Session *lookup(const sockaddr_in &src, const uint8_t *pkt);
  bool open_target(Session *s);                  // 建到游戏服的 fd
  Session *by_fd(int fd);

  void mark_dying(Session *s);        // 先记下来,不立刻拆
  bool teardown_pending() const;
  void flush_teardown();              // 一批 epoll 事件处理完再真正 erase
  void sweep_idle(uint64_t now);      // 回收超过 idle_ms_ 没流量的会话

private:
  uint32_t alloc_conv();
  std::unordered_map<uint32_t, Session>      sessions_;       // conv -> 会话
  std::unordered_map<int, Session *>          fd_to_session_;  // 目标fd -> 会话
  std::vector<uint32_t>                       to_erase_;       // 等拆除的 conv
  uint32_t    next_conv_       = 1000;
  uint64_t    idle_ms_         = 30000;
  uint64_t    last_idle_sweep_ = 0;
  int         client_fd_;
  int         epfd_;
  std::function<void()> sync_timer_;
};

#endif  // SESSION_MANAGER_H
