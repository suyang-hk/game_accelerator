#include "net_utils.h"

#include <arpa/inet.h>
#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>

uint64_t now_ms() {
  return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}

// KCP 内部用 32 位时间,直接截断即可
uint32_t now32() { return (uint32_t)now_ms(); }

bool same_addr(const sockaddr_in &a, const sockaddr_in &b) {
  return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port;
}

void addr_to(char out[INET_ADDRSTRLEN], const sockaddr_in &a) {
  inet_ntop(AF_INET, &a.sin_addr, out, INET_ADDRSTRLEN);
}

bool node_to_addr(const std::string &node, sockaddr_in *out) {
  size_t colon = node.rfind(':');
  if (colon == std::string::npos) return false;
  std::string ip = node.substr(0, colon);
  uint16_t port  = (uint16_t)strtoul(node.c_str() + colon + 1, nullptr, 10);
  out->sin_family = AF_INET;
  if (inet_pton(AF_INET, ip.c_str(), &out->sin_addr) != 1) return false;
  out->sin_port = htons(port);
  return true;
}

void set_nonblock(int fd) {
  int fl = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}
