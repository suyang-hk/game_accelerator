// 连 kv_store(TCP 控制面)的客户端。只在网关里用来把 route 名换成 ip:port。
#ifndef KV_CLIENT_H
#define KV_CLIENT_H

#include <cstdint>
#include <string>

class KVClient {
public:
  ~KVClient() { disconnect(); }

  bool connect(const std::string &ip, uint16_t port);
  void disconnect();
  bool set(const std::string &key, const std::string &value);
  bool get(const std::string &key, std::string &value);
  bool del(const std::string &key);

private:
  bool request_with_retry(const std::string &cmd, std::string &reply);
  bool request(const std::string &cmd, std::string &reply);
  bool ensure_connected();
  int         fd_   = -1;
  std::string ip_;
  uint16_t    port_ = 0;
};

#endif  // KV_CLIENT_H
