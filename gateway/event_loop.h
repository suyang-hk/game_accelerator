#ifndef EVENT_LOOP_H
#define EVENT_LOOP_H

class Gateway;
class SessionManager;

class EventLoop {
public:
  explicit EventLoop(int client_fd);
  ~EventLoop();

  int epfd() const { return epfd_; }
  void bind(SessionManager &sessions, Gateway &gw);
  void sync_timer();   // 会话增删后调: 空表就停 tick,有会话就按 5ms 转
  void run();

private:
  void timer_set_period(int period_ms);
  int               epfd_ = -1;
  int               timer_fd_ = -1;
  int               client_fd_ = -1;
  SessionManager   *sessions_ = nullptr;
  Gateway          *gw_       = nullptr;
};

#endif  // EVENT_LOOP_H
