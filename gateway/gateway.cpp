#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <netinet/in.h>
#include <stdio.h>
#include<random>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <unordered_map>

#define CLIENT_BIND_IP "0.0.0.0"
#define CLIENT_PORT 8000
#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 9000
#define BUF_SIZE 65535

struct Session {
  uint32_t conv;

  sockaddr_in client_addr;
  sockaddr_in server_addr;
  int server_fd;

  uint64_t last_alive;
};

static uint64_t now_ms() {
       return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();

}

static bool same_addr(const sockaddr_in &a, const sockaddr_in &b) {
  return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port;
}

static void addr_to(char out[INET_ADDRSTRLEN], const sockaddr_in &a) {
       inet_ntop(AF_INET, &a.sin_addr, out, INET6_ADDRSTRLEN);
}

static sockaddr_in pick_target(uint32_t) {
  sockaddr_in t{};
  t.sin_family = AF_INET;
  t.sin_port = htons(SERVER_PORT);
  inet_pton(AF_INET, SERVER_IP, &t.sin_addr);
  return t;
}

// Mint a random 32-bit conv that does not collide with a live session.
static uint32_t gen_conv(const std::unordered_map<uint32_t, Session>& sessions) {
    static std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<uint32_t> dist;
    uint32_t c;
    do { c = dist(rng); } while (sessions.count(c));
    return c;
}



static Session *find_by_client(std::unordered_map<uint32_t, Session> &sessions,
                               const sockaddr_in &client) {
  for (auto &kv : sessions) {
    if (same_addr(kv.second.client_addr, client)) {
      return &kv.second;
    }
       }

       return nullptr;
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

  printf("[gateway] client socket listening on %s:%d\n",
           CLIENT_BIND_IP, CLIENT_PORT);

  std::unordered_map<uint32_t, Session> sessions;


  char buf[BUF_SIZE];
  while (true) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(client_fd, &rfds);
    int max_fd = client_fd;
    for (const auto &kv : sessions) {
      FD_SET(kv.second.server_fd, &rfds);
      if (kv.second.server_fd > max_fd) max_fd = kv.second.server_fd;
    }

    int r = select(max_fd + 1, &rfds, nullptr, nullptr, nullptr);
    if (r < 0) {perror("select"); break;}

    //------client ->gateway -> server--
    if (FD_ISSET(client_fd, &rfds)) {
      sockaddr_in src{};
      socklen_t src_len = sizeof(src);
      ssize_t n =
          recvfrom(client_fd, buf, sizeof(buf), 0, (sockaddr *)&src, &src_len);
      if (n < 0) {perror("recv(client)"); continue;}

      Session *s = find_by_client(sessions, src);
      if (s == nullptr) {
        Session sess{};
        sess.conv = gen_conv(sessions);
        sess.client_addr = src;
        sess.server_addr = pick_target(sess.conv);
        sess.server_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (sess.server_fd < 0) { perror("socket(relay)"); continue;}
        sess.last_alive = now_ms();

       auto inserted_ = sessions.emplace(sess.conv, sess);
       s = &inserted_.first->second;

       char c_ip[INET_ADDRSTRLEN], s_ip[INET6_ADDRSTRLEN];
       addr_to(c_ip, sess.client_addr);
       addr_to(s_ip, sess.server_addr);
                printf("[gateway] new session conv=%u client=%s:%d -> "
                       "target=%s:%d\n",
                       sess.conv, c_ip, ntohs(sess.client_addr.sin_port),
                       s_ip, ntohs(sess.server_addr.sin_port));
 
      }

      s->last_alive = now_ms();
      sendto(s->server_fd, buf, (size_t)n, 0, (sockaddr*)&s->server_addr, sizeof(s->server_addr));
      char c_ip[INET_ADDRSTRLEN];
      addr_to(c_ip, s->client_addr);
      printf("[gateway] conv=%u client=%s:%d -> target (%zd bytes)\n",
              s->conv, c_ip, ntohs(s->client_addr.sin_port), n);
 
    }

    //-----server->gateway->client
    for (auto &kv : sessions) {
      Session& s = kv.second;
      if (!FD_ISSET(s.server_fd, &rfds)) continue;

      sockaddr_in from{};
      socklen_t from_len = sizeof(from);
      ssize_t n = recvfrom(s.server_fd, buf, sizeof(buf), 0, (sockaddr*)&from, &from_len);
       if (n < 0) {perror("recv(relay)"); continue;}

       s.last_alive = now_ms();
       sendto(client_fd, buf, (size_t)n, 0, (sockaddr*)&s.client_addr, sizeof(s.client_addr));

       char c_ip[INET_ADDRSTRLEN];
       addr_to(c_ip, s.client_addr);
       printf("[gateway] conv=%u target -> client=%s:%d (%zd bytes)\n",
              s.conv, c_ip, ntohs(s.client_addr.sin_port), n);
    }
}

close(client_fd);
for (auto &kv : sessions) {
  close(kv.second.server_fd);
}

return 0;
  
#if 0
//socket facing server
  int server_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (server_fd < 0) {perror("socket(server)"); return 1;}

  sockaddr_in server_addr{};
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(SERVER_PORT);
  inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);
  printf("[gateway] real server target %s:%d\n", SERVER_IP, SERVER_PORT);

  // single-client state
  sockaddr_in client_addr{};
  bool has_client = false;

  char buf[BUF_SIZE];
  while(true) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(client_fd, &rfds);
    FD_SET(server_fd, &rfds);
    int max_fd = std::max(client_fd, server_fd);

    int r = select(max_fd + 1, &rfds, nullptr, nullptr, nullptr);
    if (r < 0) {perror("select"); break;}

    // client -=->server
    if (FD_ISSET(client_fd, &rfds)) {
      sockaddr_in src{};
      socklen_t src_len = sizeof(src);
      ssize_t n = recvfrom(client_fd, buf, sizeof(buf), 0, (sockaddr*)&src, &src_len);
      if (n < 0) {perror("recvfrom(client)"); continue;}

      char ip[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &src.sin_addr, ip, sizeof(ip));

      if (!has_client) {
        client_addr = src;
        has_client = true;
        printf("[gateway] registered client %s:%d\n",
              ip, ntohs(src.sin_port));
      } else if (client_addr.sin_port != src.sin_port ||
                 client_addr.sin_addr.s_addr != src.sin_addr.s_addr) {
        // single-client only: drop packets from any other source
        printf("[gateway] drop from unknown client %s:%d\n",
               ip, ntohs(src.sin_port));
        continue;
      }

      // first packet falls through here too: it must be forwarded as well
      sendto(server_fd, buf, (size_t)n, 0, (sockaddr*)&server_addr, sizeof(server_addr));
      printf("[gateway] %s:%d -> server (%zd bytes)\n",
              ip, ntohs(src.sin_port), n);

    }

    // server --> client (only when a reply is actually ready)
    if (FD_ISSET(server_fd, &rfds)) {
      sockaddr_in src{};
      socklen_t src_len = sizeof(src);
      ssize_t n = recvfrom(server_fd, buf, sizeof(buf), 0, (sockaddr*)&src, &src_len);
      if (n < 0) {perror("recvfrom(server)"); continue;}

      if (!has_client) {
        printf("[gateway] reply from server but no client "
                       "registered, dropped\n");
        continue;
      }

      sendto(client_fd, buf, (size_t)n, 0, (sockaddr*)&client_addr, sizeof(client_addr));
      printf("[gateway] server -> %s:%d (%zd bytes)\n",
                inet_ntoa(client_addr.sin_addr),
                ntohs(client_addr.sin_port), n);
    }
  }



  close(server_fd);
  close(client_fd);
  return 0;

#endif

  }