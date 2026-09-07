#ifndef TUNNEL_PROTOCOL_H
#define TUNNEL_PROTOCOL_H


#include <arpa/inet.h>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <type_traits>

namespace tunnel {

enum : uint8_t {
  PACKET_CONNECT = 1,
  PACKET_DATA = 2,
  PACKET_CLOSE = 3,

    };

inline constexpr uint16_t HEAD_LEN = 5; // type + conv
inline constexpr uint16_t CONNECT_LEN = 11; //headlen + dst_ip+sdt_port
inline constexpr uint16_t DATA_HEAD_LEN = 7; //headlen + len

inline size_t pack_connect(uint8_t *out, uint32_t conv, uint32_t dst_ip_n,
                           uint16_t dst_port_n) {
  out[0] = PACKET_CONNECT;
  uint32_t c = htonl(conv);
  memcpy(out + 1, &c, 4);
  memcpy(out + 5, &dst_ip_n, 4);
  memcpy(out + 9, &dst_port_n, 2);
  return CONNECT_LEN;
}

inline size_t pack_data(uint8_t *out, uint32_t conv, const void *data,
                        uint16_t len) {
  out[0] = PACKET_DATA;
  uint32_t c = htonl(conv);
  memcpy(out + 1, &c, 4);
  uint16_t l = htons(len);
  memcpy(out + 5, &l, 2);
  memcpy(out + 7, data, len);
  return DATA_HEAD_LEN + len;
}

inline size_t pack_close(uint8_t *out, uint32_t conv) {
  out[0] = PACKET_CLOSE;
  uint32_t c = htonl(conv);
  memcpy(out + 1, &c, 4);
  return HEAD_LEN;
}

//-----------------decode--------------------------
inline bool parse(const uint8_t *buf, size_t n, uint8_t *type, uint32_t *conv,
                  const uint8_t **data = nullptr, uint16_t *len = nullptr) {

  if (n < HEAD_LEN)
    return false;

  *type = buf[0];
  uint32_t c;
  memcpy(&c, buf + 1, 4);
  *conv = ntohl(c);

  if (data)
    *data = nullptr;
  if (len) *len = 0;

  if (*type == PACKET_DATA) {
    if (n < DATA_HEAD_LEN) return false;
    uint16_t l;
    memcpy(&l, buf + 5, 2);
    *len = ntohs(l);
    if ((size_t)DATA_HEAD_LEN + *len > n) return false;
    if (data) *data = buf + DATA_HEAD_LEN;
  } else if (*type == PACKET_CONNECT) {
    if (n < CONNECT_LEN) return false;
  }

  return true;

}

inline void read_connect_dst(const uint8_t *buf, sockaddr_in *dst) {
  dst->sin_family = AF_INET;
  memcpy(&dst->sin_addr.s_addr, buf + 5, 4);
  memcpy(&dst->sin_port, buf + 9, 2);
}

}


#endif