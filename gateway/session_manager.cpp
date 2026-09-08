#include "session_manager.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../common/net_utils.h"
#include "../common/tunnel_protocol.h"
#include "kcp_session.h"

SessionManager::SessionManager(int client_fd, int epfd,
                               std::function<void()> sync_timer)
    : client_fd_(client_fd), epfd_(epfd), sync_timer_(std::move(sync_timer)) {}

bool SessionManager::empty() const { return sessions_.empty(); }

void SessionManager::for_each_active(
    const std::function<void(Session &)> &fn) {
  for (auto &kv : sessions_)
    if (!kv.second.dying) fn(kv.second);
}

void SessionManager::set_idle_ms(uint64_t ms) { idle_ms_ = ms; }

uint32_t SessionManager::alloc_conv() {
  for (;;) {
    uint32_t c = next_conv_++;
    if (sessions_.find(c) == sessions_.end()) return c;
  }
}

// REGISTER: 给该端点一个 conv(幂等——同一个还没 CONNECT 的端点重复注册
// 还是回同一个 conv),建 Session 并 init 它的 KCP,再回 REGISTERED。
// 注意 kcp init 必须在 emplace 完成、拿到 map 里的稳定地址之后做,
// 因为 ikcp_create 会把 user 记成 &s->kcp,节点一搬走就悬空。
void SessionManager::register_client(const sockaddr_in &src) {
  char ip[INET_ADDRSTRLEN];
  addr_to(ip, src);

  uint32_t conv = 0;
  bool reused = false;
  for (auto &kv : sessions_) {
    Session &o = kv.second;
    if (o.dying || o.target_fd >= 0) continue;
    if (same_addr(o.client_addr, src)) { conv = o.conv; reused = true; break; }
  }

  if (!reused) {
    conv = alloc_conv();
    auto res = sessions_.emplace(conv, Session(conv, src));
    Session *s = &res.first->second;
    kcp_session_init(s->kcp, conv, src, client_fd_);
    sync_timer_();
    printf("[gateway] REGISTER %s:%d -> conv=%u (session pending)\n",
           ip, ntohs(src.sin_port), conv);
  } else {
    printf("[gateway] REGISTER %s:%d re-answered conv=%u\n",
           ip, ntohs(src.sin_port), conv);
  }

  uint8_t out[1 + 4];
  size_t sz = tunnel::pack_registered(out, conv);
  sendto(client_fd_, out, sz, 0, (const sockaddr *)&src, sizeof(src));
}

// 按 KCP 段里的 conv 找会话;没注册 / 不是包的主人 都直接丢
Session *SessionManager::lookup(const sockaddr_in &src, const uint8_t *pkt) {
  uint32_t conv = ikcp_getconv(pkt);
  auto it = sessions_.find(conv);
  if (it == sessions_.end()) {
    printf("[gateway] conv=%u not registered, datagram dropped\n", (unsigned)conv);
    return nullptr;
  }
  Session *s = &it->second;
  if (!same_addr(s->client_addr, src)) {
    char ip[INET_ADDRSTRLEN];
    addr_to(ip, src);
    printf("[gateway] conv=%u datagram from non-owner %s:%d, dropped\n",
           conv, ip, ntohs(src.sin_port));
    return nullptr;
  }
  return s;
}

// 给会话建一个发往目标游戏服的 UDP fd,挂到 epoll 上等它的回复
bool SessionManager::open_target(Session *s) {
  s->target_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (s->target_fd < 0) { perror("socket(relay)"); return false; }
  set_nonblock(s->target_fd);

  epoll_event ev{};
  ev.events = EPOLLIN;
  ev.data.fd = s->target_fd;
  epoll_ctl(epfd_, EPOLL_CTL_ADD, s->target_fd, &ev);
  fd_to_session_.emplace(s->target_fd, s);

  char c_ip[INET_ADDRSTRLEN], t_ip[INET_ADDRSTRLEN];
  addr_to(c_ip, s->client_addr);
  addr_to(t_ip, s->target_addr);
  printf("[gateway] CONNECT conv=%u client=%s:%d -> target=%s:%d\n",
         s->conv, c_ip, ntohs(s->client_addr.sin_port),
         t_ip, ntohs(s->target_addr.sin_port));
  return true;
}

Session *SessionManager::by_fd(int fd) {
  auto it = fd_to_session_.find(fd);
  return it == fd_to_session_.end() ? nullptr : it->second;
}

void SessionManager::mark_dying(Session *s) {
  s->dying = true;
  to_erase_.push_back(s->conv);
}

bool SessionManager::teardown_pending() const { return !to_erase_.empty(); }

// 延迟拆除: 在 epoll 一批事件处理完后调用。真正 erase 前先把 target fd从 epoll 摘掉、关掉,再从表里擦掉(此时 Session 析构会 release KCP)
void SessionManager::flush_teardown() {
  for (uint32_t conv : to_erase_) {
    auto it = sessions_.find(conv);
    if (it == sessions_.end()) continue;
    Session &s = it->second;
    if (s.target_fd >= 0) {
      epoll_ctl(epfd_, EPOLL_CTL_DEL, s.target_fd, nullptr);
      fd_to_session_.erase(s.target_fd);
      close(s.target_fd);
    }
    sessions_.erase(it);
  }
  to_erase_.clear();
  sync_timer_();
}

// 空闲回收。last_active 只在真实流量时刷新(见 gateway.cpp),所以这里可以放心用 now - last_active 判断;节流到 1s 扫一次,不抢 KCP tick 的节奏
void SessionManager::sweep_idle(uint64_t now) {
  if (now - last_idle_sweep_ < 1000) return;
  last_idle_sweep_ = now;
  if (sessions_.empty()) return;
  for (auto &kv : sessions_) {
    Session &s = kv.second;
    if (s.dying) continue;
    if (now - s.last_active > idle_ms_) {
      s.dying = true;
      to_erase_.push_back(s.conv);
      printf("[gateway] conv=%u idle > %llu ms, session reaped (timeout)\n",
             (unsigned)s.conv, (unsigned long long)idle_ms_);
    }
  }
}
