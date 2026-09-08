#ifndef GATEWAY_H
#define GATEWAY_H

#include <cstddef>
#include <cstdint>
#include <memory>

#include "event_loop.h"
#include "kv_client.h"
#include "mem_pool.h"
#include "session_manager.h"

class Gateway {
public:
  int run();
  void handle_tunnel_datagram();             // client fd 可读: REGISTER 或 KCP 段
  void handle_target_reply(Session *s, int fd);  // 游戏服回了数据

private:
  bool handle_tunnel_msg(Session *s, const uint8_t *buf, size_t n);
  int bind_tunnel_socket();
  KVClient kv_;                    // 控制面,查 route:<name> -> ip:port
  std::unique_ptr<MemoryPool> pool_;   // 8 MiB 块池,在 run() 里堆上建
  int           client_fd_ = -1;   // 所有 conv 共用的一个隧道 socket
  std::unique_ptr<EventLoop>     loop_;
  std::unique_ptr<SessionManager> sessions_;
};

#endif  // GATEWAY_H
