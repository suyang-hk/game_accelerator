#include "event_loop.h"

#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <unistd.h>

#include "../common/net_utils.h"
#include "../third_party/kcp/ikcp.h"
#include "gateway.h"
#include "session.h"
#include "session_manager.h"

#define EPOLL_MAX   64
#define KCP_TICK_MS 5

EventLoop::EventLoop(int client_fd) : client_fd_(client_fd) {
  epfd_ = epoll_create1(0);
  if (epfd_ < 0) { perror("epoll_create1"); return; }

  epoll_event ev{};
  ev.events = EPOLLIN;
  ev.data.fd = client_fd_;
  epoll_ctl(epfd_, EPOLL_CTL_ADD, client_fd_, &ev);

  timer_fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
  if (timer_fd_ < 0) { perror("timerfd_create"); return; }
  ev.events = EPOLLIN;
  ev.data.fd = timer_fd_;
  epoll_ctl(epfd_, EPOLL_CTL_ADD, timer_fd_, &ev);
}

EventLoop::~EventLoop() {
  if (timer_fd_ >= 0) close(timer_fd_);
  if (epfd_ >= 0) close(epfd_);
}

void EventLoop::bind(SessionManager &sessions, Gateway &gw) {
  sessions_ = &sessions;
  gw_ = &gw;
}

void EventLoop::timer_set_period(int period_ms) {
  itimerspec its{};
  if (period_ms > 0) {
    its.it_value.tv_nsec = (long)period_ms * 1000000L;
    its.it_interval.tv_nsec = (long)period_ms * 1000000L;
  }
  timerfd_settime(timer_fd_, 0, &its, nullptr);
}

// period_ms=0 停表
void EventLoop::sync_timer() {
  timer_set_period(sessions_ && sessions_->empty() ? 0 : KCP_TICK_MS);
}

void EventLoop::run() {
  epoll_event events[EPOLL_MAX];
  while (true) {
    int n = epoll_wait(epfd_, events, EPOLL_MAX, -1);
    if (n < 0) { perror("epoll_wait"); break; }

    for (int i = 0; i < n; i++) {
      int fd = events[i].data.fd;
      if (fd == client_fd_) {
        gw_->handle_tunnel_datagram();
      } else if (fd == timer_fd_) {
        uint64_t expirations;
        while (read(timer_fd_, &expirations, sizeof(expirations)) > 0) {}
        uint32_t cur = now32();
        sessions_->for_each_active(
            [&](Session &s) { ikcp_update(s.kcp.kcp, cur); });
        sessions_->sweep_idle(now_ms());
      } else {
        Session *s = sessions_->by_fd(fd);
        if (s) gw_->handle_target_reply(s, fd);
      }
    }
    // 一批事件派发完,再统一拆该拆的会话(见 session_manager.h)
    if (sessions_->teardown_pending()) sessions_->flush_teardown();
  }
}
