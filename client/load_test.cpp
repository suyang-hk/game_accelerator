// 网关压测工具(独立于主线客户端,仅作测量)
//  reg    <count>            并发注册: count 个独立 UDP 端点同时向网关 REGISTER,统计吞吐与 conv 唯一性
//  relay  <sessions> <each> [payload] [route] [cap]
//                            全链路(echo)吞吐: sessions 条会话各发 each 条 DATA,网关转发到回声服再弹回,
//                            统计 pps / 吞吐 / 丢包;route 默认 test.com,cap=单会话在途上限(默认 64)
// 握手与 client/tunnel_client.cpp 逐字节一致: REGISTER(裸 UDP) -> REGISTERED(带 conv) ->
// ikcp_create(conv) + nodelay(1,10,2,1);DATA 走 KCP,回包拆 PACKET_DATA。
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <errno.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_set>
#include <vector>

#include "../common/net_utils.h"
#include "../common/tunnel_protocol.h"
#include "../third_party/kcp/ikcp.h"

static const char   *GW_IP   = "127.0.0.1";
static const uint16_t GW_PORT = 8000;
static uint8_t        g_buf[65535];
static uint64_t g_deadline_ms = 120000;  // 每 chunk 等待上限,LOAD_DEADLINE_MS 覆盖

struct Ctx {
  int            fd      = -1;
  ikcpcb        *kcp     = nullptr;
  sockaddr_in    gw{};
  uint32_t       conv    = 0;
  bool           reg_ok  = false;
  uint64_t       total   = 0;  // 要发的 DATA 总数
  uint64_t       sent    = 0;
  uint64_t       echo_ok = 0;  // 收到且 payload 校验通过的 echo
  uint64_t       echo_bad = 0; // 长度/内容不符
  std::vector<uint8_t> payload;
};

// KCP 输出回调,user 记 Ctx*,把 KCP 段发回网关
static int out_cb(const char *buf, int len, ikcpcb *, void *user) {
  Ctx *c = static_cast<Ctx *>(user);
  sendto(c->fd, buf, (size_t)len, 0, (sockaddr *)&c->gw, sizeof(c->gw));
  return len;
}

static int make_sock(sockaddr_in *gw) {
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  // 读路径不能饿死: 压测端单线程先紧循环发包再 poll/epoll 读, 期间网关回包会灌满小 rcvbuf
  // 造成"网关侧在回、压测端在丢"的假丢包; 8MB 与快速回声服同量级, 吸收整轮发包的回包
  int sz = 8 * 1024 * 1024;
  setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &sz, sizeof(sz));
  setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sz, sizeof(sz));
  set_nonblock(fd);
  gw->sin_family = AF_INET;
  gw->sin_port   = htons(GW_PORT);
  inet_pton(AF_INET, GW_IP, &gw->sin_addr);
  return fd;
}

// 注册一个子区间 [lo,hi): fire 一轮,漏网(缓冲满被丢)的每 15ms 快补发
static size_t register_chunk(std::vector<Ctx> &v, size_t lo, size_t hi,
                             uint64_t deadline) {
  size_t n = hi - lo;
  std::vector<pollfd> pf(n);
  uint8_t reg[1];
  size_t rl = tunnel::pack_register(reg);

  auto fire = [&] {
    for (size_t i = 0; i < n; i++) {
      size_t k = lo + i;
      pf[i].fd = v[k].fd; pf[i].events = POLLIN; pf[i].revents = 0;
      if (!v[k].reg_ok)
        sendto(v[k].fd, reg, rl, 0, (sockaddr *)&v[k].gw, sizeof(v[k].gw));
    }
  };
  fire();

  size_t done = 0;
  while (done < n) {
    long rem = (long)(deadline - now_ms());
    if (rem <= 0) break;
    int to = (int)std::min<long>(rem, 15);
    int r  = poll(pf.data(), (nfds_t)n, to);
    if (r < 0) { if (errno == EINTR) continue; break; }
    if (r == 0) { fire(); continue; }
    for (size_t i = 0; i < n; i++) {
      if (!(pf[i].revents & (POLLIN | POLLERR | POLLHUP))) continue;
      pf[i].revents = 0;
      size_t k = lo + i;
      if (v[k].reg_ok) continue;
      sockaddr_in from{}; socklen_t fl = sizeof(from);
      ssize_t g = recvfrom(v[k].fd, g_buf, sizeof(g_buf), 0,
                           (sockaddr *)&from, &fl);
      if (g > 0 && tunnel::read_registered(g_buf, (size_t)g, &v[k].conv)) {
        v[k].reg_ok = true; done++;
      }
    }
  }
  return done;
}

// 读光该会话 socket 上的 KCP 段 -> input -> 收隧道报文,统计 echo
static void drain_ctx(Ctx &c) {
  bool got = false;
  for (;;) {
    ssize_t n = recvfrom(c.fd, g_buf, sizeof(g_buf), 0, nullptr, nullptr);
    if (n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) break;
      perror("recvfrom"); break;
    }
    if (n < 24) continue;  // 不足 KCP 头
    ikcp_input(c.kcp, (const char *)g_buf, (long)n);
    got = true;
  }
  if (got) { ikcp_update(c.kcp, now32()); ikcp_flush(c.kcp); }

  int gotm;
  while ((gotm = ikcp_recv(c.kcp, (char *)g_buf, sizeof(g_buf))) > 0) {
    uint8_t type; const uint8_t *data = nullptr; uint16_t dlen = 0;
    if (!tunnel::parse(g_buf, (size_t)gotm, &type, &data, &dlen)) continue;
    if (type != tunnel::PACKET_DATA) continue;
    if (dlen == (uint16_t)c.payload.size() &&
        memcmp(data, c.payload.data(), dlen) == 0)
      c.echo_ok++;
    else
      c.echo_bad++;
  }
}

// ---- reg: 小波次注册并逐波计时(看"加 500 条会话"的增量成本怎么随表大小涨) ----
static void run_reg(size_t count) {
  uint64_t t0 = now_ms();
  std::vector<Ctx> v;
  v.reserve(count);
  for (size_t i = 0; i < count; i++) {
    Ctx c; c.fd = make_sock(&c.gw); v.push_back(c);
  }
  uint64_t t1 = now_ms();
  const size_t W = 500;  // 每波并发数,别一次灌爆网关收包缓冲
  size_t cum = 0;
  for (size_t lo = 0; lo < count; lo += W) {
    size_t hi = std::min(count, lo + W);
    uint64_t tb = now_ms();
    size_t done = register_chunk(v, lo, hi, tb + g_deadline_ms);
    uint64_t te = now_ms();
    cum += done;
    double wsec = (double)(te - tb) / 1000.0;
    if (wsec > 0.0)
      printf("  wave cum=%zu (+%zu)  %.0f ms  -> %.0f sess/s\n",
             cum, done, (double)(te - tb), (double)done / wsec);
    if (done < hi - lo) {
      printf("  wave FAILED at cum=%zu (got %zu/%zu)\n", cum, done, hi - lo);
      break;
    }
  }
  uint64_t t2 = now_ms();

  std::unordered_set<uint32_t> convs;
  for (auto &c : v) if (c.reg_ok) convs.insert(c.conv);
  double sec = (double)(t2 - t1) / 1000.0;
  printf("reg count=%zu  open_socks=%.0fms  registered=%zu/%zu  unique_conv=%zu\n",
         count, (double)(t1 - t0), cum, count, convs.size());
  if (sec > 0.0)
    printf("reg total=%.0f sess/s  (%.3fs)\n", (double)cum / sec, sec);
  for (auto &c : v) close(c.fd);
}

// ---- relay: 并发 echo 全链路 ----
static void run_relay(size_t sessions, uint64_t each, size_t plen,
                      const char *route, size_t cap) {
  if (cap == 0) cap = 64;
  if (plen > 1400) plen = 1400;  // 限单 KCP 段内,免得分段
  uint64_t t0 = now_ms();
  std::vector<Ctx> v;
  v.reserve(sessions);
  for (size_t i = 0; i < sessions; i++) {
    Ctx c;
    c.fd = make_sock(&c.gw);
    c.payload.assign(plen, (uint8_t)('A' + (i % 26)));
    if (plen >= 2) { c.payload[0] = (uint8_t)i; c.payload[1] = (uint8_t)(i >> 8); }
    v.push_back(c);
  }
  // relay 的会话也分波注册,避免一次突发丢 REGISTER
  size_t reg_ok = 0;
  const size_t RW = 500;
  for (size_t lo = 0; lo < sessions; lo += RW) {
    size_t hi = std::min(sessions, lo + RW);
    reg_ok += register_chunk(v, lo, hi, now_ms() + g_deadline_ms);
  }
  double reg_sec = (double)(now_ms() - t0) / 1000.0;
  printf("relay sessions=%zu each=%llu payload=%zu route=%s cap=%zu\n",
         sessions, (unsigned long long)each, plen, route, cap);
  printf("registered=%zu/%zu  (%.2fs)\n", reg_ok, sessions, reg_sec);
  if (reg_ok != sessions) { for (auto &c : v) close(c.fd); return; }

  int ep = epoll_create1(0);
  for (auto &c : v) {
    c.kcp = ikcp_create(c.conv, &c);
    ikcp_setoutput(c.kcp, out_cb);
    ikcp_nodelay(c.kcp, 1, 10, 2, 1);
    ikcp_wndsize(c.kcp, (int)cap, (int)cap);
    c.total = each;
    epoll_event ev{}; ev.events = EPOLLIN; ev.data.ptr = &c;
    epoll_ctl(ep, EPOLL_CTL_ADD, c.fd, &ev);
  }

  // 先发 CONNECT(route <= 256B, DATA 段 <= 3+1400B, 共用 sendbuf)
  uint8_t sendbuf[tunnel::DATA_HEAD_LEN + 1400];
  for (auto &c : v) {
    size_t sz = tunnel::pack_connect(sendbuf, route, (uint8_t)strlen(route));
    ikcp_send(c.kcp, (char *)sendbuf, (int)sz);
    ikcp_update(c.kcp, now32()); ikcp_flush(c.kcp);
  }
  epoll_event events[512];

  // 诊断: CONNECT 突发后等一段再开 DATA 洪泛, 模拟真实客户端"先握后发"。
  // 默认 0(与旧行为一致); CONNECT_DELAY_MS 覆盖。避免 CONNECT 与 DATA 在初始
  // 突发里互相挤压、sn=0 被丢后重传又被 DATA 洪泛淹死的伪影。
  if (const char *e = getenv("CONNECT_DELAY_MS")) {
    uint64_t d = strtoull(e, nullptr, 10);
    if (d > 0) {
      // 等待期间也要喂 KCP(update+flush), 让 CONNECT 的 ACK 能发回来
      uint64_t end = now_ms() + d;
      while (now_ms() < end) {
        uint32_t kn2 = now32();
        for (auto &c : v) { ikcp_update(c.kcp, kn2); ikcp_flush(c.kcp); }
        int r2 = epoll_wait(ep, events, 512, 2);
        if (r2 > 0)
          for (int j = 0; j < r2; j++)
            drain_ctx(*static_cast<Ctx *>(events[j].data.ptr));
      }
    }
  }

  uint64_t t_start = now_ms();
  uint64_t last_prog = t_start;
  uint64_t prev_delivered = 0;
  uint64_t last_hb = t_start;
  bool ok_done = false;

  while (!ok_done) {
    // KCP 是定时器驱动: 每轮必须对所有会话 update(发 ACK/超时重传/推进窗口),
    // 否则全发完的会话会因不再 update 而停止回 ACK, 把网关 snd_wnd 卡死形成死锁。
    uint32_t kn = now32();
    for (auto &c : v) {
      if (c.sent < c.total) {
        // 注意: 本 kcp 的 ikcp_send 成功时返回入队字节数(正数), 仅 <0 是失败
        while (c.sent < c.total && (c.sent - c.echo_ok) < cap) {
          size_t sz = tunnel::pack_data(sendbuf, c.payload.data(),
                                        (uint16_t)c.payload.size());
          int rc = ikcp_send(c.kcp, (char *)sendbuf, (int)sz);
          if (rc < 0) break;  // 仅当真正失败(如分片超窗)才停, 等腾窗
          c.sent++;
        }
      }
      ikcp_update(c.kcp, kn);
      ikcp_flush(c.kcp);
    }

    int r = epoll_wait(ep, events, 512, 2);
    if (r > 0)
      for (int j = 0; j < r; j++)
        drain_ctx(*static_cast<Ctx *>(events[j].data.ptr));

    uint64_t nowt = now_ms();
    uint64_t deli = 0, sent_sum = 0;
    for (auto &c : v) { deli += c.echo_ok; sent_sum += c.sent; }
    if (deli != prev_delivered) { prev_delivered = deli; last_prog = nowt; }
    if (nowt - last_hb >= 2000) {
      last_hb = nowt;
      printf("  hb t=%llus sent=%llu delivered=%llu inwin=%lld | v[0].s=%llu e=%llu tot=%llu | v[1].s=%llu e=%llu\n",
             (unsigned long long)(nowt - t_start) / 1000,
             (unsigned long long)sent_sum, (unsigned long long)deli,
             (long long)(sent_sum - deli),
             (unsigned long long)v[0].sent, (unsigned long long)v[0].echo_ok,
             (unsigned long long)v[0].total,
             (unsigned long long)v[1].sent, (unsigned long long)v[1].echo_ok);
    }

    uint64_t rem = sessions * each - deli;
    if (rem == 0) { ok_done = true; }
    else if (nowt - last_prog > 15000) {
      printf("STALL: no progress 15s, sent=%llu delivered=%llu/%llu\n",
             (unsigned long long)sent_sum, (unsigned long long)deli,
             (unsigned long long)(sessions * each));
      break;
    }
  }
  uint64_t t_end = now_ms();

  uint64_t deli = 0, bad = 0;
  for (auto &c : v) { deli += c.echo_ok; bad += c.echo_bad; }
  double sec = (double)(t_end - t_start) / 1000.0;
  uint64_t expected = sessions * each;
  printf("delivered=%llu/%llu  mismatch=%llu  (%.3fs)\n",
         (unsigned long long)deli, (unsigned long long)expected,
         (unsigned long long)bad, sec);
  if (sec > 0.0) {
    printf("pps=%.0f msg/s   loss=%.2f%%\n", (double)deli / sec,
           100.0 * (double)(expected - deli) / (double)expected);
    if (plen > 0)
      printf("throughput=%.1f MB/s (payload %zu B)\n",
             (double)deli * (double)plen / sec / 1e6, plen);
  }
  close(ep);
  for (auto &c : v) {
    if (c.kcp) ikcp_release(c.kcp);
    close(c.fd);
  }
}

static void usage() {
  printf("load_test reg <count>\n");
  printf("load_test relay <sessions> <each> [payload] [route] [cap]\n");
}

int main(int argc, char **argv) {
  if (argc < 3) { usage(); return 1; }
  if (const char *e = getenv("LOAD_DEADLINE_MS"))
    g_deadline_ms = strtoull(e, nullptr, 10);
  std::string mode = argv[1];
  if (mode == "reg") {
    run_reg((size_t)strtoull(argv[2], nullptr, 10));
  } else if (mode == "relay") {
    size_t sessions = (size_t)strtoull(argv[2], nullptr, 10);
    uint64_t each = strtoull(argv[3], nullptr, 10);
    size_t plen = argc > 4 ? (size_t)strtoull(argv[4], nullptr, 10) : 32;
    const char *route = argc > 5 ? argv[5] : "test.com";
    size_t cap = argc > 6 ? (size_t)strtoull(argv[6], nullptr, 10) : 64;
    run_relay(sessions, each, plen, route, cap);
  } else {
    usage(); return 1;
  }
  return 0;
}
