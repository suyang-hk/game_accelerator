#include <arpa/inet.h>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "../third_party/kcp/ikcp.h"
#include "../common/tunnel_protocol.h"

#define GATEWAY_IP "127.0.0.1"
#define GATEWAY_PORT 8000
#define BUF_SIZE 65535

struct Ctx { int fd; sockaddr_in gw; };
static Ctx g_ctx;

static uint64_t now_ms() {
  return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}


static uint32_t now32() { return (uint32_t)now_ms(); }

static int kcp_output(const char *buf, int len, ikcpcb *, void *) {
  sendto(g_ctx.fd, buf, (size_t)len, 0, (sockaddr*)&g_ctx.gw, sizeof(g_ctx.gw));
  return len;
}

static void kcp_pump(ikcpcb *kcp) { ikcp_update(kcp, now32()); ikcp_flush(kcp);}

static void send_tun(ikcpcb *kcp, const uint8_t *pkt, size_t n) {
  ikcp_update(kcp, now32());
  ikcp_send(kcp, (const char*)pkt, (int)n);
  ikcp_flush(kcp);
}

static bool wait_reply(ikcpcb *kcp, uint8_t *out, size_t *out_len,
                       int timeout_ms) {
  uint64_t start = now_ms();
  while (true) {
    kcp_pump(kcp);
    int got;
    while ((got = ikcp_recv(kcp, (char *)out, (int)BUF_SIZE)) > 0) {
      *out_len = (size_t)got;   
      return true;
    }
    long rem = timeout_ms - (long)(now_ms() - start);
    if (rem <= 0) return false;
    timeval tv{};
    tv.tv_sec = rem / 1000;
    tv.tv_usec = (rem % 1000) * 1000;
    fd_set rf;
    FD_ZERO(&rf);
    FD_SET(g_ctx.fd, &rf);
    int s = select(g_ctx.fd + 1, &rf, nullptr, nullptr, &tv);
    if (s > 0 && FD_ISSET(g_ctx.fd, &rf)) {
      uint8_t buf[BUF_SIZE];
      ssize_t n = recvfrom(g_ctx.fd, buf, sizeof(buf), 0, nullptr, nullptr);
      if (n > 0) {
        ikcp_input(kcp, (const char *)buf, (long)n);
        kcp_pump(kcp);
      }
    }
  }
}

static void usage() {
    fprintf(stderr,
            "usage: udp_client <conv> <dst_ip> <dst_port> [message]\n"
            "  e.g.: udp_client 1001 127.0.0.1 9000 hello\n");
}

static void print_echo(const uint8_t *buf, size_t n) {
  uint8_t type;
  uint32_t conv;
  const uint8_t *data = nullptr;
  uint16_t len = 0;
  if (!tunnel::parse(buf, n, &type, &conv, &data, &len)) {
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

int main(int argc, char** argv) {
    if (argc < 4) {usage(); return 1;}
  
    const uint32_t conv = (uint32_t)strtoul(argv[1], nullptr, 10);
    const char* dst_ip = argv[2];
    const uint16_t dst_port = (uint16_t)strtoul(argv[3], nullptr, 10);
    const char* one_shot = (argc >= 5) ? argv[4] : nullptr;

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { perror("socket error"); return 1;}

    g_ctx.fd = fd;
    g_ctx.gw.sin_family = AF_INET;
    g_ctx.gw.sin_port = htons(GATEWAY_PORT);
    inet_pton(AF_INET, GATEWAY_IP, &g_ctx.gw.sin_addr);

    ikcpcb *kcp = ikcp_create(conv, nullptr);
    ikcp_setoutput(kcp, kcp_output);
    ikcp_nodelay(kcp, 1, 10, 2, 1);

    uint8_t wire[BUF_SIZE];   // tunnel packet going out
    uint8_t rbuf[BUF_SIZE];   // tunnel packet coming back
    size_t rn = 0;

  // ----  CONNECT  ----
  uint32_t dst_ip_n;
  inet_pton(AF_INET, dst_ip, &dst_ip_n);
  size_t csz = tunnel::pack_connect(wire, conv, dst_ip_n, htons(dst_port));
  send_tun(kcp, wire, csz);
  printf("[client] CONNECT conv=%u dst=%s:%u (KCP)\n", conv, dst_ip, dst_port);

  auto roundtrip = [&](const char *text) -> bool {
    uint16_t len = (uint16_t)strlen(text);
    size_t dsz = tunnel::pack_data(wire, conv, text, len);
    send_tun(kcp, wire, dsz);
    printf("[client] DATA conv=%u sent: %s\n", conv, text);
    if (wait_reply(kcp, rbuf, &rn, 3000)) {
      print_echo(rbuf, rn);
      return true;
    }
    printf("[client] (no reply within timeout)\n");
    return false;
  };

  if (one_shot) {
    roundtrip(one_shot);
  } else {
    printf("[client] interactive, lines become DATA on conv=%u "
           "(Ctrl-D / \"quit\" closes)\n", conv);
    char line[BUF_SIZE];
    while (fgets(line, sizeof(line), stdin)) {
      line[strcspn(line, "\n")] = '\0';
      if (line[0] == '\0') continue;
      if (strcmp(line, "quit") == 0) break;
      roundtrip(line);
    }
  }

  // ---- CLOSE ----
  size_t z = tunnel::pack_close(wire, conv);
  send_tun(kcp, wire, z);
  printf("[client] CLOSE conv=%u\n", conv);
  usleep(100 * 1000);
  kcp_pump(kcp);

  ikcp_release(kcp);
  close(fd);
  return 0;

   return 0;
}