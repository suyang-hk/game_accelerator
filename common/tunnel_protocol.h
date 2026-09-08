// 客户端和网关之间的隧道报文(KCP 之上),均以 1 字节类型开头
// REGISTER/REGISTERED 是裸 UDP 握手,不走 KCP
#ifndef TUNNEL_PROTOCOL_H
#define TUNNEL_PROTOCOL_H

#include <arpa/inet.h>
#include <cstdint>
#include <cstring>
#include <netinet/in.h>

namespace tunnel {

enum : uint8_t {
    PACKET_CONNECT    = 1,  // 建连, 后跟 route
    PACKET_DATA       = 2,  // 业务数据, 后跟 2 字节长度 + payload
    PACKET_CLOSE      = 3,
    PACKET_REGISTER   = 4,  // 裸 UDP: 客户端申请 conv
    PACKET_REGISTERED = 5,  // 裸 UDP: 网关回 conv(网络序 4 字节)
};

inline constexpr uint16_t HEAD_LEN         = 1;
inline constexpr uint16_t CONNECT_HEAD_LEN = 2;
inline constexpr uint16_t DATA_HEAD_LEN    = 3;
inline constexpr uint16_t ROUTE_MAX        = 255;

inline size_t pack_connect(uint8_t* out, const char* route, uint8_t route_len) {
    out[0] = PACKET_CONNECT;
    out[1] = route_len;
    memcpy(out + CONNECT_HEAD_LEN, route, route_len);
    return CONNECT_HEAD_LEN + route_len;
}

inline size_t pack_data(uint8_t* out, const void* data, uint16_t len) {
    out[0] = PACKET_DATA;
    uint16_t l = htons(len);
    memcpy(out + 1, &l, 2);
    memcpy(out + DATA_HEAD_LEN, data, len);
    return DATA_HEAD_LEN + len;
}

inline size_t pack_close(uint8_t* out) {
    out[0] = PACKET_CLOSE;
    return HEAD_LEN;
}

inline size_t pack_register(uint8_t* out) {
    out[0] = PACKET_REGISTER;
    return 1;
}

inline size_t pack_registered(uint8_t* out, uint32_t conv) {
    out[0] = PACKET_REGISTERED;
    uint32_t c = htonl(conv);
    memcpy(out + 1, &c, 4);
    return 1 + 4;
}

inline bool read_registered(const uint8_t* buf, size_t n, uint32_t* conv) {
    if (n < 1 + 4 || buf[0] != PACKET_REGISTERED) return false;
    uint32_t c;
    memcpy(&c, buf + 1, 4);
    *conv = ntohl(c);
    return true;
}

// 拆出类型和数据指针,越界返回 false
inline bool parse(const uint8_t* buf, size_t n, uint8_t* type,
                  const uint8_t** data = nullptr, uint16_t* len = nullptr) {
    if (n < HEAD_LEN) return false;
    *type = buf[0];
    if (data) *data = nullptr;
    if (len)  *len  = 0;
    if (*type == PACKET_DATA) {
        if (n < DATA_HEAD_LEN) return false;
        uint16_t l;
        memcpy(&l, buf + 1, 2);
        *len = ntohs(l);
        if ((size_t)DATA_HEAD_LEN + *len > n) return false;
        if (data) *data = buf + DATA_HEAD_LEN;
    } else if (*type == PACKET_CONNECT) {
        if (n < CONNECT_HEAD_LEN) return false;
        if ((size_t)CONNECT_HEAD_LEN + buf[1] > n) return false;
    }
    return true;
}

inline size_t read_connect_route(const uint8_t* buf, char* out, size_t cap) {
    size_t rl = buf[1];
    if (rl >= cap) rl = cap - 1;
    memcpy(out, buf + CONNECT_HEAD_LEN, rl);
    out[rl] = '\0';
    return rl;
}

}  // namespace tunnel
#endif  // TUNNEL_PROTOCOL_H
