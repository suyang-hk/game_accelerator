#include "gateway.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../common/net_utils.h"
#include "../common/tunnel_protocol.h"
#include "../third_party/kcp/ikcp.h"
#include "session.h"

#define CLIENT_BIND_IP "0.0.0.0"
#define CLIENT_PORT    8000
#define KVSTORE_IP     "127.0.0.1"
#define KVSTORE_PORT   9096
#define SESSION_IDLE_MS 30000  // 默认空闲超时,可用环境变量 GATEWAY_IDLE_MS 覆盖

int Gateway::bind_tunnel_socket() {
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) { perror("socket(client)"); return -1; }

  sockaddr_in b{};
  b.sin_family = AF_INET;
  b.sin_port = htons(CLIENT_PORT);
  inet_pton(AF_INET, CLIENT_BIND_IP, &b.sin_addr);
  if (bind(fd, (sockaddr *)&b, sizeof(b)) < 0) {
    perror("bind(client)");
    close(fd);
    return -1;
  }
  set_nonblock(fd);
  printf("[gateway] ONE client socket listening on %s:%d (epoll)\n",
         CLIENT_BIND_IP, CLIENT_PORT);
  return fd;
}

bool Gateway::handle_tunnel_msg(Session *s, const uint8_t *buf, size_t n) {
  uint8_t type;
  const uint8_t *data = nullptr;
  uint16_t data_len = 0;
  if (!tunnel::parse(buf, n, &type, &data, &data_len)) {
    printf("[gateway] malformed tunnel packet (%zu bytes), dropped\n", n);
    return false;
  }

  if (type == tunnel::PACKET_CONNECT) {
    if (s->target_fd >= 0) {
      printf("[gateway] CONNECT conv=%u already open, ignored\n", s->conv);
      return false;
    }
    char route[tunnel::ROUTE_MAX + 1];
    tunnel::read_connect_route(buf, route, sizeof(route));
    std::string node;
    if (!kv_.get(std::string("route:") + route, node) ||
        !node_to_addr(node, &s->target_addr)) {
      sessions_->mark_dying(s);
      printf("[gateway] CONNECT conv=%u route=%s unresolved "
             "(kv reply: %s), session dropped\n",
             s->conv, route, node.empty() ? "no such route" : node.c_str());
      return true;
    }
    printf("[gateway] KVStore resolved route:%s -> %s\n", route, node.c_str());
    s->last_active = now_ms();
    if (!sessions_->open_target(s)) return true; 
    return false;
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
    sessions_->mark_dying(s);
    printf("[gateway] CLOSE conv=%u session closed (teardown deferred)\n",
           s->conv);
    return true;
  } else {
    printf("[gateway] unknown packet type %u, dropped\n", type);
  }
  return false;
}

void Gateway::handle_tunnel_datagram() {
  while (true) {
    uint8_t *pkt = (uint8_t *)pool_->alloc();
    if (!pkt) return;  // 池耗尽

    sockaddr_in src{};
    socklen_t src_len = sizeof(src);
    ssize_t n = recvfrom(client_fd_, pkt, MemoryPool::BLOCK, 0,
                         (sockaddr *)&src, &src_len);
    if (n < 0) {
      pool_->free(pkt);
      if (errno == EAGAIN || errno == EWOULDBLOCK) break;  // 读完了
      perror("recvfrom(client)");
      break;
    }
    if (n == (ssize_t)MemoryPool::BLOCK)
      printf("[gateway] client datagram >= %zu bytes may be truncated\n",
             MemoryPool::BLOCK);

    if (n == 1 && pkt[0] == tunnel::PACKET_REGISTER) {
      sessions_->register_client(src);
      pool_->free(pkt);
      continue;
    }
    // KCP 头开销至少 24 字节
    if (n < 24) {
      pool_->free(pkt);
      printf("[gateway] short datagram (%zd bytes), ignored\n", n);
      continue;
    }

    Session *s = sessions_->lookup(src, pkt);  // 没注册的 conv 在里面直接丢
    if (!s || s->dying) { pool_->free(pkt); continue; }
    s->last_active = now_ms();
    ikcp_input(s->kcp.kcp, (const char *)pkt, (long)n);
    ikcp_update(s->kcp.kcp, now32());
    ikcp_flush(s->kcp.kcp);
    pool_->free(pkt);

    uint8_t *tmsg = (uint8_t *)pool_->alloc();
    if (!tmsg) return;
    bool gone = false;  
    while (true) {
      int got = ikcp_recv(s->kcp.kcp, (char *)tmsg, (int)MemoryPool::BLOCK);
      if (got <= 0) {
        if (got == -3)  // 报文比池块大(目前没分档,先记个日志)
          printf("[gateway] conv=%u tunnel msg exceeds pool block, dropped\n",
                 s->conv);
        break;
      }
      if (handle_tunnel_msg(s, tmsg, (size_t)got)) { gone = true; break; }
    }
    pool_->free(tmsg);
    if (!gone) s->last_active = now_ms();
  }
}

void Gateway::handle_target_reply(Session *s, int fd) {
  if (s->dying) return;
  const size_t max_reply = MemoryPool::BLOCK - tunnel::DATA_HEAD_LEN;

  while (true) {
    uint8_t *rply = (uint8_t *)pool_->alloc();
    if (!rply) return;
    ssize_t n = recvfrom(fd, rply, max_reply, 0, nullptr, nullptr);
    if (n < 0) {
      pool_->free(rply);
      if (errno == EAGAIN || errno == EWOULDBLOCK) break;
      perror("recvfrom(relay)");
      break;
    }
    if (n == (ssize_t)max_reply)
      printf("[gateway] server reply >= %zu bytes truncated (pool block)\n",
             max_reply);
    s->last_active = now_ms();

    uint8_t *tmsg = (uint8_t *)pool_->alloc();  // 回包再包一层 DATA 头
    if (!tmsg) { pool_->free(rply); return; }
    size_t sz = tunnel::pack_data(tmsg, rply, (uint16_t)n);
    ikcp_update(s->kcp.kcp, now32());
    ikcp_send(s->kcp.kcp, (const char *)tmsg, (int)sz);  
    ikcp_flush(s->kcp.kcp);
    pool_->free(tmsg);
    pool_->free(rply);

    char c_ip[INET_ADDRSTRLEN];
    addr_to(c_ip, s->client_addr);
    printf("[gateway] reply conv=%u target -> client=%s:%d (%zd bytes)\n",
           s->conv, c_ip, ntohs(s->client_addr.sin_port), n);
  }
}

int Gateway::run() {
  pool_ = std::make_unique<MemoryPool>();
  client_fd_ = bind_tunnel_socket();
  if (client_fd_ < 0) return 1;

  if (kv_.connect(KVSTORE_IP, KVSTORE_PORT))
    printf("[gateway] KVClient connected to %s:%u\n", KVSTORE_IP, KVSTORE_PORT);
  else
    printf("[gateway] warning: KVStore not up at %s:%u yet -- CONNECTs will fail\n",
           KVSTORE_IP, KVSTORE_PORT);

  uint64_t idle_ms = SESSION_IDLE_MS;
  if (const char *e = getenv("GATEWAY_IDLE_MS"))
    idle_ms = (uint64_t)strtoull(e, nullptr, 10);

  loop_ = std::make_unique<EventLoop>(client_fd_);
  sessions_ = std::make_unique<SessionManager>(
      client_fd_, loop_->epfd(), [this] { loop_->sync_timer(); });
  sessions_->set_idle_ms(idle_ms);
  printf("[gateway] idle timeout %llu ms | packet pool %u x %u B\n",
         (unsigned long long)idle_ms,
         (unsigned)MemoryPool::CAP, (unsigned)MemoryPool::BLOCK);

  loop_->bind(*sessions_, *this);
  loop_->run();  
  close(client_fd_);
  return 0;
}
