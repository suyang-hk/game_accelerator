#include <arpa/inet.h>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unordered_map>
#include <unistd.h>

#include "../common/tunnel_protocol.h"


#define CLIENT_BIND_IP "0.0.0.0"
#define CLIENT_PORT 8000
#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 9000
#define BUF_SIZE 65535

struct Session {
  uint32_t conv;

  sockaddr_in client_addr;
  sockaddr_in target_addr;
  int server_fd;

  uint64_t last_active;
};

static uint64_t now_ms() {
       return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();

}

static bool same_addr(const sockaddr_in &a, const sockaddr_in &b) {
  return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port;
}

static void addr_to(char out[INET_ADDRSTRLEN], const sockaddr_in &a) {
       inet_ntop(AF_INET, &a.sin_addr, out, INET_ADDRSTRLEN);
}



// // Mint a random 32-bit conv that does not collide with a live session.
// static uint32_t gen_conv(const std::unordered_map<uint32_t, Session>& sessions) {
//     static std::mt19937 rng{std::random_device{}()};
//     std::uniform_int_distribution<uint32_t> dist;
//     uint32_t c;
//     do { c = dist(rng); } while (sessions.count(c));
//     return c;
// }





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

    uint8_t pkt[BUF_SIZE];     // tunnel packet read from a client
    uint8_t rply[BUF_SIZE];    // raw payload read back from a target
    uint8_t resp[BUF_SIZE];    // tunnel packet built to send back to a client


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
      ssize_t n = recvfrom(client_fd, pkt,
                           sizeof(pkt), 0, (sockaddr *)&src, &src_len);
      if (n < 0) {perror("recv(client)"); continue;}

      uint8_t type;
      uint32_t conv;
      const uint8_t *data = nullptr;
      uint16_t data_len = 0;
      if (!tunnel::parse(pkt, (size_t)n, &type, &conv, &data, &data_len)) {
                printf("[gateway] malformed packet (%zd bytes), dropped\n", n);
                continue;
      }

      char src_ip[INET_ADDRSTRLEN];
      addr_to(src_ip, src);

      if (type == tunnel::PACKET_CONNECT) {
          if (sessions.count(conv)) {
              printf("[gateway] CONNECT conv=%u already exists, ignored\n", conv);
              continue;
            }
          Session sess{};
          sess.conv = conv;
          sess.client_addr = src;
          tunnel::read_connect_dst(pkt, &sess.target_addr);
          sess.server_fd = socket(AF_INET, SOCK_DGRAM, 0);
          if (sess.server_fd < 0) { perror("socket(relay)"); continue; }
          sess.last_active = now_ms();

          sessions.emplace(sess.conv, sess);

          char t_ip[INET_ADDRSTRLEN];
          addr_to(t_ip, sess.target_addr);
          printf("[gateway] CONNECT conv=%u client=%s:%d -> "
                "target=%s:%d\n",
                conv, src_ip, ntohs(src.sin_port),
                t_ip, ntohs(sess.target_addr.sin_port));
      } else if (type == tunnel::PACKET_DATA) {
        auto it = sessions.find(conv);
        if (it == sessions.end()) {
          printf("[gateway] DATA for unknown conv=%u, dropped\n", conv);
          continue;
          }

          Session &s = it->second;
          if (!same_addr(s.client_addr, src)) {
              printf("[gateway] DATA conv=%u from unregistered source "
                    "%s:%d, dropped\n", conv, src_ip, ntohs(src.sin_port));
                    continue;
                }
                s.last_active= now_ms();
          sendto(s.server_fd, data, data_len, 0,
                (sockaddr*)&s.target_addr, sizeof(s.target_addr));
          printf("[gateway] DATA conv=%u client=%s:%d -> target "
               "(%u bytes)\n", conv, src_ip, ntohs(src.sin_port), data_len);
      } else if (type == tunnel::PACKET_CLOSE) {
        auto it = sessions.find(conv);
        if (it == sessions.end()) {
          printf("[gateway] CLOSE for unknown conv=%u, ignored\n", conv);
          continue;
          }
        Session& s = it->second;
        if (!same_addr(s.client_addr, src)) {
          printf("[gateway] CLOSE conv=%u from unregistered source, "
                "ignored\n", conv);
          continue;
          }
        close(s.server_fd);
        sessions.erase(it);
        printf("[gateway] CLOSE conv=%u session closed\n", conv);
      } else {
        printf("[gateway] unknown packet type %u, dropped\n", type);
      }

    }

    for (auto &kv : sessions) {
      Session &s = kv.second;
      if (!FD_ISSET(s.server_fd, &rfds)) continue;

      sockaddr_in from{};
      socklen_t from_len = sizeof(from);

      ssize_t n = recvfrom(s.server_fd, rply, sizeof(rply), 0, (sockaddr*)&from, &from_len);
      if (n < 0) { perror("recvfrom(relay)"); continue; }

      s.last_active = now_ms();
      size_t pack_data_sz = tunnel::pack_data(resp, s.conv, rply, (uint16_t)n);
      sendto(client_fd, resp, pack_data_sz, 0, (sockaddr*)&s.client_addr, sizeof(s.client_addr));
      char c_ip[INET_ADDRSTRLEN];
      addr_to(c_ip, s.client_addr);
      printf("[gateway] reply conv=%u target -> client=%s:%d (%zd bytes)\n",
            s.conv, c_ip, ntohs(s.client_addr.sin_port), n);
    }

  }
  close(client_fd);
  for (auto& kv : sessions) close(kv.second.server_fd);

return 0;
  }