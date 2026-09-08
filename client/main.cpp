// 客户端入口: 解析 route [消息],建隧道发一条或进交互模式,退出时发 CLOSE
#include <cstdio>
#include <cstring>

#include "../common/tunnel_protocol.h"
#include "tunnel_client.h"

#define GATEWAY_IP   "127.0.0.1"
#define GATEWAY_PORT 8000

static void usage() {
  fprintf(stderr,
          "usage: udp_client <route_name> [message]\n"
          "  e.g.: udp_client test.com hello\n");
}

static void print_echo(uint32_t conv, const uint8_t *buf, size_t n) {
  uint8_t type;
  const uint8_t *data = nullptr;
  uint16_t len = 0;
  if (!tunnel::parse(buf, n, &type, &data, &len)) {
    printf("[client] malformed reply\n");
    return;
  }
  if (type == tunnel::PACKET_DATA) {
    char tmp[4096 + 1];
    size_t show = len < sizeof(tmp) - 1 ? len : sizeof(tmp) - 1;
    memcpy(tmp, data, show);
    tmp[show] = '\0';
    printf("[client] echo conv=%u: %s\n", conv, tmp);
  } else {
    printf("[client] unexpected reply type %u\n", type);
  }
}

int main(int argc, char **argv) {
  if (argc < 2) { usage(); return 1; }

  const char *route = argv[1];
  const char *one_shot = (argc >= 3) ? argv[2] : nullptr;

  if (strlen(route) > tunnel::ROUTE_MAX) {
    fprintf(stderr, "[client] route name too long (max %u)\n", tunnel::ROUTE_MAX);
    return 1;
  }

  TunnelClient c;
  if (!c.connect(GATEWAY_IP, GATEWAY_PORT)) return 1;

  if (!c.open_route(route)) return 1;
  printf("[client] CONNECT conv=%u route=%s (KCP)\n", c.conv(), route);

  auto roundtrip = [&](const char *text) -> bool {
    c.send_data(text, (uint16_t)strlen(text));
    printf("[client] DATA conv=%u sent: %s\n", c.conv(), text);
    uint8_t rbuf[65535];
    size_t rn = 0;
    if (c.wait_reply(rbuf, &rn, 3000)) {
      print_echo(c.conv(), rbuf, rn);
      return true;
    }
    printf("[client] (no reply within timeout)\n");
    return false;
  };

  if (one_shot) {
    roundtrip(one_shot);
  } else {
    printf("[client] interactive, lines become DATA on conv=%u "
           "(Ctrl-D / \"quit\" closes)\n", c.conv());
    char line[65535];
    while (fgets(line, sizeof(line), stdin)) {
      line[strcspn(line, "\n")] = '\0';
      if (line[0] == '\0') continue;
      if (strcmp(line, "quit") == 0) break;
      roundtrip(line);
    }
  }

  c.send_close();
  printf("[client] CLOSE conv=%u\n", c.conv());
  return 0;
}
