// kv_store 的协议很简: 命令用空格分词、回复没有换行分隔,所以读回复只能等 socket 静默 ~2ms 就当一条读完断开后下次请求会自动重连
#include "kv_client.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <chrono>
#include <string>

namespace {
uint64_t now_ms() {
  return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool read_reply(int fd, std::string &out) {
  out.clear();
  bool got = false;
  const uint64_t deadline = now_ms() + 1000;
  char buf[256];
  while (now_ms() < deadline) {
    fd_set rf;
    FD_ZERO(&rf);
    FD_SET(fd, &rf);
    timeval tv{0, 2000};  // 2ms 静默
    int r = select(fd + 1, &rf, nullptr, nullptr, &tv);
    if (r < 0) return got;
    if (r == 0) {
      if (got) return true;  
      continue;
    }
    ssize_t n = recv(fd, buf, sizeof(buf), 0);
    if (n <= 0) return got;
    got = true;
    out.append(buf, (size_t)n);
  }
  return got;
}
}  // namespace

bool KVClient::connect(const std::string &ip, uint16_t port) {
  disconnect();
  ip_   = ip;
  port_ = port;
  return ensure_connected();
}

void KVClient::disconnect() {
  if (fd_ >= 0) {
    close(fd_);
    fd_ = -1;
  }
}

bool KVClient::ensure_connected() {
  if (fd_ >= 0) return true;
  if (port_ == 0) return false;

  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) { perror("kv socket"); return false; }
  sockaddr_in sa{};
  sa.sin_family = AF_INET;
  sa.sin_port   = htons(port_);
  if (inet_pton(AF_INET, ip_.c_str(), &sa.sin_addr) != 1) {
    close(fd);
    return false;
  }
  if (::connect(fd, (sockaddr *)&sa, sizeof(sa)) < 0) {
    perror("kv connect");
    close(fd);
    return false;
  }
  fd_ = fd;
  return true;
}

bool KVClient::request(const std::string &cmd, std::string &reply) {
  if (!ensure_connected()) return false;
  if (send(fd_, cmd.data(), (int)cmd.size(), MSG_NOSIGNAL) != (ssize_t)cmd.size()) {
    disconnect();
    return false;
  }
  if (!read_reply(fd_, reply)) {
    disconnect();
    return false;
  }
  return true;
}

// 失败就断开重连再来一次,扛 kv_store 偶发断连
bool KVClient::request_with_retry(const std::string &cmd, std::string &reply) {
  if (request(cmd, reply)) return true;
  disconnect();
  return ensure_connected() && request(cmd, reply);
}

bool KVClient::set(const std::string &key, const std::string &value) {
  std::string reply;
  if (!request_with_retry("SET " + key + " " + value, reply)) return false;
  return reply == "SUCCESS";
}

bool KVClient::get(const std::string &key, std::string &value) {
  std::string reply;
  if (!request_with_retry("GET " + key, reply)) return false;
  if (reply == "NO EXIST") return false;
  while (!reply.empty() && (reply.back() == '\r' || reply.back() == '\n'))
    reply.pop_back();
  value = reply;
  return true;
}

bool KVClient::del(const std::string &key) {
  std::string reply;
  if (!request_with_retry("DEL " + key, reply)) return false;
  return reply == "SUCCESS";
}
