// 时间、地址、socket 小工具,网关和客户端共用
#ifndef NET_UTILS_H
#define NET_UTILS_H

#include <netinet/in.h>
#include <string>

uint64_t now_ms();
uint32_t now32();

bool same_addr(const sockaddr_in &a, const sockaddr_in &b);
void addr_to(char out[INET_ADDRSTRLEN], const sockaddr_in &a);

bool node_to_addr(const std::string &node, sockaddr_in *out);

void set_nonblock(int fd);

#endif  // NET_UTILS_H
