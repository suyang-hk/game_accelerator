#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <unordered_map>
#include <unistd.h>
#include <vector>

#include "../third_party/kcp/ikcp.h"
#include "../common/tunnel_protocol.h"


#define CLIENT_BIND_IP "0.0.0.0"
#define CLIENT_PORT 8000
#define SERVER_IP "127.0.0.1"
#define BUF_SIZE 65535
#define EPOLL_MAX      64
#define KCP_TICK_MS    5

struct Session {
  uint32_t conv;
  ikcpcb *kcp;
  sockaddr_in client_addr;
  sockaddr_in target_addr;
  int target_fd;
  bool dying;
  uint64_t last_active;
};

static int client_fd;
static int epfd;
static int timer_fd;

static std::unordered_map<uint32_t, Session> sessions;
static std::unordered_map<int, Session*> fd_to_session;
static std::vector<uint32_t> to_erase;

static uint64_t now_ms() {
       return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();

}


static uint32_t now32() { return (uint32_t)now_ms(); }

static bool same_addr(const sockaddr_in &a, const sockaddr_in &b) {
  return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port;
}

static void addr_to(char out[INET_ADDRSTRLEN], const sockaddr_in &a) {
       inet_ntop(AF_INET, &a.sin_addr, out, INET_ADDRSTRLEN);
}

static void set_nonblock(int fd) {
  int fl = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

static void timer_set_period(int period_ms) {
  itimerspec its{};
  if (period_ms > 0) {
    its.it_value.tv_nsec = (long)period_ms * 1000000L;
    its.it_interval.tv_nsec = (long)period_ms * 1000000L;
  }
  timerfd_settime(timer_fd, 0, &its, nullptr);
}

static void sync_timer() { timer_set_period(sessions.empty() ? 0 : KCP_TICK_MS); }

static int kcp_output(const char *buf, int len, ikcpcb *, void *user) {
  Session *s = (Session *)user;
  sendto(client_fd, buf, (size_t)len, 0,
         (sockaddr *)&s->client_addr, sizeof(s->client_addr));
  return len;
}

static Session *get_or_make_session(const sockaddr_in &src,
                                    const uint8_t *pkt) {
  uint32_t conv = ikcp_getconv(pkt);

  auto it = sessions.find(conv);
  if (it == sessions.end()) {
    Session ns{};
    ns.conv = conv;
    ns.client_addr = src;
    ns.target_fd = -1;
    ns.last_active = now_ms();
    auto res = sessions.emplace(conv, ns);
    Session *s = &res.first->second;

    s->kcp = ikcp_create(conv, s);
    ikcp_setoutput(s->kcp, kcp_output);
    ikcp_nodelay(s->kcp, 1, 10, 2, 1);   
    sync_timer();                        

    char ip[INET_ADDRSTRLEN];
    addr_to(ip, src);
    printf("[gateway] new KCP session conv=%u owned by %s:%d (target pending)\n",
           conv, ip, ntohs(src.sin_port));
    return s;
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

static bool open_target(Session *s) {
  s->target_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (s->target_fd < 0) {
    perror("socket(relay)");
    return false;
  }
  set_nonblock(s->target_fd);

  epoll_event ev{};
  ev.events = EPOLLIN;
  ev.data.fd = s->target_fd;
  epoll_ctl(epfd, EPOLL_CTL_ADD, s->target_fd, &ev);
  fd_to_session.emplace(s->target_fd, s);

  char c_ip[INET_ADDRSTRLEN], t_ip[INET_ADDRSTRLEN];
  addr_to(c_ip, s->client_addr);
  addr_to(t_ip, s->target_addr);
  printf("[gateway] CONNECT conv=%u client=%s:%d -> target=%s:%d\n",
         s->conv, c_ip, ntohs(s->client_addr.sin_port),
         t_ip, ntohs(s->target_addr.sin_port));
  return true;
}

static bool handle_tunnel_msg(Session *s, const uint8_t *buf, size_t n) {
  uint8_t type;
  uint32_t conv;
  const uint8_t *data = nullptr;
  uint16_t data_len = 0;
  if (!tunnel::parse(buf, n, &type, &conv, &data, &data_len)) {
    printf("[gateway] malformed tunnel packet (%zu bytes), dropped\n", n);
    return false;
  }

  if (type == tunnel::PACKET_CONNECT) {
    if (s->target_fd >= 0) {
      printf("[gateway] CONNECT conv=%u already open, ignored\n", s->conv);
      return false;
    }
    tunnel::read_connect_dst(buf, &s->target_addr);
    s->last_active = now_ms();
    return !open_target(s);   
  } else if (type == tunnel::PACKET_DATA) {
    if (s->target_fd < 0) {
      printf("[gateway] DATA conv=%u before CONNECT, dropped\n", s->conv);
      return false;
    }
    s->last_active = now_ms();
    sendto(s->target_fd, data, data_len, 0,
           (sockaddr *)&s->target_addr, sizeof(s->target_addr));
    char c_ip[INET_ADDRSTRLEN];
    addr_to(c_ip, s->client_addr);
    printf("[gateway] DATA conv=%u client=%s:%d -> target (%u bytes)\n",
           s->conv, c_ip, ntohs(s->client_addr.sin_port), data_len);
  } else if (type == tunnel::PACKET_CLOSE) {
    s->dying = true;
    to_erase.push_back(s->conv);
    printf("[gateway] CLOSE conv=%u session closed (teardown deferred)\n",
           s->conv);
    return true;
  } else {
    printf("[gateway] unknown packet type %u, dropped\n", type);
  }
  return false;
}

static void flush_teardown() {
  for (uint32_t conv : to_erase) {
    auto it = sessions.find(conv);
    if (it == sessions.end()) continue;
    Session &s = it->second;
    if (s.target_fd >= 0) {
      epoll_ctl(epfd, EPOLL_CTL_DEL, s.target_fd, nullptr);
      fd_to_session.erase(s.target_fd);
      close(s.target_fd);
    }
    ikcp_release(s.kcp);
    sessions.erase(it);
  }
  to_erase.clear();
  sync_timer();
}

static void handle_tunnel_datagram() {
  static uint8_t pkt[BUF_SIZE];
  static uint8_t tmsg[BUF_SIZE];   // tunnel packet decoded out of the KCP

  while (true) {
    sockaddr_in src{};
    socklen_t src_len = sizeof(src);
    ssize_t n = recvfrom(client_fd, pkt, sizeof(pkt), 0,
                         (sockaddr *)&src, &src_len);
    if (n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) break;  
      perror("recvfrom(client)");
      break;
    }
    if (n < 24) {   // IKCP_OVERHEAD = 24; 
      printf("[gateway] short datagram (%zd bytes), ignored\n", n);
      continue;
    }

    Session *s = get_or_make_session(src, pkt);
    if (!s || s->dying) continue;   
    s->last_active = now_ms();
    ikcp_input(s->kcp, (const char *)pkt, (long)n);
    ikcp_update(s->kcp, now32());
    ikcp_flush(s->kcp);

    bool gone = false;   
    while (true) {
      int got = ikcp_recv(s->kcp, (char *)tmsg, (int)sizeof(tmsg));
      if (got <= 0) break;
      if (handle_tunnel_msg(s, tmsg, (size_t)got)) { gone = true; break; }
    }
    if (!gone) s->last_active = now_ms();
  }
}

static void handle_target_reply(Session *s, int fd) {
  if (s->dying) return;

  static uint8_t rply[BUF_SIZE];
  static uint8_t tmsg[BUF_SIZE];   
  while (true) {
    ssize_t n = recvfrom(fd, rply, sizeof(rply), 0, nullptr, nullptr);
    if (n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) break;  
      perror("recvfrom(relay)");
      break;
    }
    if (n > (ssize_t)(BUF_SIZE - tunnel::DATA_HEAD_LEN))
      n = BUF_SIZE - tunnel::DATA_HEAD_LEN;  
    s->last_active = now_ms();

    size_t sz = tunnel::pack_data(tmsg, s->conv, rply, (uint16_t)n);
    ikcp_update(s->kcp, now32());
    ikcp_send(s->kcp, (const char *)tmsg, (int)sz);
    ikcp_flush(s->kcp);
    char c_ip[INET_ADDRSTRLEN];
    addr_to(c_ip, s->client_addr);
    printf("[gateway] reply conv=%u target -> client=%s:%d (%zd bytes)\n",
           s->conv, c_ip, ntohs(s->client_addr.sin_port), n);
  }
}



int main() {
// ----socket facing client
  int client_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (client_fd < 0) {perror("socket(client)"); return 1;}

  sockaddr_in client_bind{};
  client_bind.sin_family = AF_INET;
  client_bind.sin_port = htons(CLIENT_PORT);
  inet_pton(AF_INET, CLIENT_BIND_IP, &client_bind.sin_addr);

  if (bind(client_fd, (sockaddr *)&client_bind, sizeof(client_bind)) < 0) {
    perror("bind(client)");
    close(client_fd);
    return 1;
  }

   set_nonblock(client_fd);
  printf("[gateway] ONE client socket listening on %s:%d (epoll)\n",
         CLIENT_BIND_IP, CLIENT_PORT);

  epfd = epoll_create1(0);
  if (epfd < 0) { perror("epoll_create1"); close(client_fd); return 1; }

  epoll_event ev{};
  ev.events = EPOLLIN;
  ev.data.fd = client_fd;
  epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &ev);

  timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
  if (timer_fd < 0) { perror("timerfd_create"); return 1; }
  ev.events = EPOLLIN;
  ev.data.fd = timer_fd;
  epoll_ctl(epfd, EPOLL_CTL_ADD, timer_fd, &ev);

  epoll_event events[EPOLL_MAX];
  while (true) {
    int n = epoll_wait(epfd, events, EPOLL_MAX, -1);
    if (n < 0) { perror("epoll_wait"); break; }

    for (int i = 0; i < n; i++) {
      int fd = events[i].data.fd;
      if (fd == client_fd) {
        handle_tunnel_datagram();
      } else if (fd == timer_fd) {
        uint64_t expirations;
        while (read(timer_fd, &expirations, sizeof(expirations)) > 0) {}  
        uint32_t cur = now32();
        for (auto &kv : sessions)   
          if (!kv.second.dying) { kv.second.last_active = now_ms(); ikcp_update(kv.second.kcp, cur); }
      } else {
        auto fit = fd_to_session.find(fd);   
        if (fit != fd_to_session.end()) handle_target_reply(fit->second, fd);
      }
    }

    if (!to_erase.empty()) flush_teardown();
  }

  close(client_fd);
  close(timer_fd);
  close(epfd);
  return 0;
  }