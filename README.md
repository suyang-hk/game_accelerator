# UDP 游戏加速网关(kcp + conv 分发)

客户端(`udp_client`)和游戏服之间垫一层网关:客户端只连网关,
网关按注册分配的 conv 把数据经 KCP 可靠地转发到真正的游戏服。
路由名 -> 服务器地址 存在 `kv_store` 里,网关 CONNECT 时去查。

```
udp_client  --KCP-->  gateway  --UDP-->  game server(echo)
                        |
                        +--TCP查询 route:xxx-->  kv_store(:9096)
```

## 两个子工程

| 目录         | 是什么                                   | 构建       |
|--------------|------------------------------------------|------------|
| `kv_store/`  | 键值存储(TCP 9096),存 route 映射         | `make`     |
| 根目录        | 网关 + 客户端 + 回声测试服(cmake)        | `cmake`    |

kv_store 的 `.o`/可执行产物已 gitignore;根目录产物在 `build/`(已忽略)。

## 构建与运行

### 1. kv_store(先起,网关要连它)

```sh
cd kv_store
make              # 产出 ./kv_store, 监听 0.0.0.0:9096
./kv_store &       # 或另行常驻
```

存路由(键 `route:<名字>` = `<ip:port>`;TCP 明文、空格分词、回复无换行):

```sh
python3 -c "import socket as s; c=s.create_connection(('127.0.0.1',9096)); \
c.sendall(b'SET route:test.com 127.0.0.1:8080'); print(c.recv(256)); c.close()"
```

### 2. 网关 + 客户端 + 回声服

```sh
cmake -S . -B build
cmake --build build     # 产出 build/gateway, build/udp_client, build/udp_echo_server

./build/gateway &        # 监听 UDP 8000,连 kv_store 9096
./build/udp_echo_server 8080 &   # 测试目标服(echo)
```

### 3. 跑客户端

```sh
./build/udp_client test.com hello      # 发一条
./build/udp_client test.com            # 交互模式,输一行发一行,quit/Ctrl-D 退出
```

网关日志会依次出现 REGISTER -> KVStore resolved route -> CONNECT -> DATA -> reply。
会话 30s 无流量自动回收(环境变量 `GATEWAY_IDLE_MS` 可改,便于测试)。

## 布局

```
common/    net_utils(时间/地址/socket) + tunnel_protocol(隧道报文格式)
gateway/   main / gateway(中继) / session / session_manager / kcp_session /
           event_loop / kv_client(查 kv_store) / mem_pool
client/    main + tunnel_client(注册->KCP->收发)
test_server/ udp_echo_server
third_party/kcp   KCP 静态库
kv_store/  键值存储(make 独立构建)
```

## 压测与性能优化(load_test 实测)

中继腿用 `build/load_test` 压测(多路会话并发,经网关到回声服往返,回包逐条校验字节):

```sh
cmake --build build            # 额外产出 build/load_test
./build/load_test relay <会话数> <每会话条数> <payload字节> test.com <在途上限cap>
```

`cap` 是每路会话的应用层在途窗口(echo 回来才发下一条),取值 1~64。结束打印
`delivered / mismatch / pps / 吞吐`;单点不回包超 15s 判定 STALL。环境变量:
`CONNECT_DELAY_MS` 让 CONNECT 波与 DATA 洪泛错开(模拟真实"先握后发"客户端)、
`LOAD_DEADLINE_MS` 总限时、`GATEWAY_IDLE_MS` 空闲回收超时。

### 压测暴露的真缺陷 + 修复

会话一多(>~200)中继整窗卡死,100 万条曾丢九成。根因在**所有客户端共用的那一个
隧道 socket(:8000)默认 ~208KB 收包缓冲**:百路 CONNECT/DATA 齐射一冲就溢出,被丢的
CONNECT 段此刻已被网关 KCP `input+flush` 先 ACK,永不重传 → 该会话从 0 号段起整窗死锁。

- `bind_tunnel_socket` 给 :8000 设 `SO_RCVBUF=8MB`(内核按 `rmem_max` 封顶到 4MB),
  让突发被吸收而不丢包;
- 两处热路径逐包 printf(DATA 转发 / 回包)注释掉——逐条打印会拖垮高负载吞吐。

### 修复后实测(2026-09,单线程网关,回声服 127.0.0.1:8080)

| 配置 | 结果 | 速率 |
|---|---|---|
| 100 × 5000 条 × 100B cap64 | 500k/500k,0 丢 | 109,697 msg/s,11 MB/s |
| 500 × 2000 × 100B cap64(修前 93% 丢) | 1M/1M,**0 丢** | 97,295 msg/s,9.7 MB/s |
| 200 × 5000 × 1400B cap32 | 1M/1M,0 丢 | 28,565 msg/s,40 MB/s |
| 500 × 2000 × 1400B cap64 | 999,982/1M,0.002% 尾丢 | 18,802 msg/s,26.3 MB/s |
| 1000 × 1000 × 100B cap64 | 999,888/1M,0.01% 尾丢 | 受 stall 拖累(裸跑曾 79.6k msg/s) |

所有 run `mismatch=0`:送达内容逐字节正确。单线程中继上限 ≈ 11 万 msg/s(小包)/ 40 MB/s(大包)。

### 残留尾丢的归因(网关侧三组对照实验钉死,非网关缺陷)

末尾少量消息偶发永久卡死,run-to-run 抖动大(同配置从 0 到 ~1.6% 都见过)。成因:
**网关→游戏服这段是原始 UDP、无重传、fire-and-forget**。客户端把消息 ACK 给网关后即交
出手;500~1000 路 × cap 在途 = 数万包在起爆瞬间砸向游戏服一个 socket,溢出丢一包,该消息
echo 永不回来、这路窗口永久卡一格。已实测排除两项怀疑:`ikcp_wndsize` 调 1024 反而把尾丢
放大到 ~1.5%(故保持默认 32 不调,自带回包侧背压);回声服本就 8MB rcvbuf。要做到零丢需把
回包腿可靠化(网关侧缓冲重传 / KCP 到游戏服),或靠真实游戏应用层重传 + 服务端大缓冲兜底。

### 当前架构的容量边界

- 注册:~1.6 万~3.7 万 会话/s,与注册表大小无关(现为 O(N) 幂等线性扫描,海量时才显 O(N²))。
- 并发:500~1000 活跃会话实测跑满且内容零错;空闲容量 1.2 万会话 0% CPU(5ms tick 空转)。
  压测后会话被 30s idle 正常回收,无泄漏。
- **百万连接当前架构做不到**:每会话 5ms 全表 `ikcp_update` 是 O(N)/tick(1M 会话 × 200 次/s
  = 2 亿次/s 起步)→ 需 timer-wheel/每会话定时;注册 O(N²) → 哈希;单线程 ~11 万 msg/s →
  多核/SO_REUSEPORT 分片;回包腿无重传 → 可靠化。方向详见下一节。

## 与真实加速器的差距 / 后期演进方向(规划,当前未实现)

现状定位一句话:**已跑通的是"转发面单跳原型"** —— 客户端走私有 KCP/conv 隧道到网关,
网关查 kv_store 得一个目标地址再 UDP 转发到游戏服。真实游戏加速器还差三层:
**接入透明化、节点多跳、控制面分离**。以下每条都是方向性改进方法,均未实现。

### 1. 接入透明化

> 现状:要加速必须先改写客户端、走你的私有协议,现成游戏不认识你。真加速是
> **透明接管**现成应用的 TCP/UDP 流量,游戏无感知。

- **本地收口代理(先证明协议可替换)**
  - 本机起一个 UDP/TCP 收口 socket,顶替"自研客户端"那半边:收到普通 UDP 后
    封装进隧道(REGISTER -> CONNECT -> DATA)发给网关。
  - 卡点:UDP 不带目标域名,一个收口端口不知道走哪个 route。解法选一:
    - **端口即 route**:一个游戏配一个收口端口,端口号映射到 route 名;
    - 本地维护"目标 ip:port -> 真实游戏服/route"映射,按原始目标分流。
- **iptables TPROXY 透明接管(接近真实,别人游戏直接可用)**
  - TPROXY 拿原始目标地址(mangle 表 + policy routing + `IP_TRANSPARENT`;
    UDP 用 `IP_RECVORIGDSTADDR` + recvmsg)。
  - TCP 用 REDIRECT 时可用 `getsockopt(SO_ORIGINAL_DST)`;UDP 没有等价物,
    透明场景主用 TPROXY。
  - 拿到"原始目标 -> route 解析"后送隧道;用 ip rule / socket mark 区分
    "要加速 / 不加速"的流量。
- **TUN 虚拟网卡**
  - Linux tun / Windows wintun / Android VPNService。
  - 应用流量进 TUN,用户态程序封装进隧道:UDP 直接封装即可;代价是要 root / 管理员权限。
- **游戏识别 + 服务器 IP 库**
  - 目标是"识别游戏 -> 自动把它所有服务器网段拉进接管范围"。
  - kv_store 正好当这个库:游戏名 / 域名 / 网段 -> route。数据靠抓包、官方列表。

### 2. 节点与多跳 

> 现状:一个 gateway 同时当接入点+出口点,route 写死一个地址;没有候选节点集,
> 没有健康/冗余,无法查上游高速节点

- **同一份程序按角色部署**
  - gateway 加启动角色:`--role=access`(收用户 KCP)/ `--role=exit`(连真实游戏服)
    / 纯 relay。
  - 转发逻辑不变,变的只是**下一跳是谁**:接入点把原来的"连游戏服"换成
    "连出口节点"(同样走 KCP/UDP 隧道),出口节点再还原原生 UDP 打游戏服。
    链路:**客户端 -> 接入点 -> 出口节点 -> 游戏服**,逐跳复用已跑通的转发。
- **节点注册表(扩 kv_store 的用法)**
  - 键模型:`node:<id> -> {ip:port, 区域, 角色, 状态}`;route 不再指单个地址,
    而指向"某区域的一组出口候选"。
  - 例:`route:test.com -> region=jp`,查询时先拿区域,再列该区域存活出口。
- **心跳 + 健康检查(最小容灾)**
  - 每节点周期写心跳(kv_store 里放 last_seen / 状态字段),选路只挑
    "心跳没过期"的节点,过期即剔除。
  - 更主动:接入点定期对候选出口发探测(KCP ping 或复用 DATA 往返),
    按 RTT/丢包换节点。
- **route 解析变两段 + 本地缓存**
  - 旧:查一次 route 得 1 个地址就 open_target。
  - 新:route -> 区域 -> 存活候选列表 ->(静态取首 or 测速选优)-> 连出口;
    网关本地缓存并带过期,不必每个 CONNECT 都查一次。
- **部署前提(现实约束)**
  - 接入点要有公网可达地址;出口点要能被接入点连到。NAT/家庭宽带后面的机器
    得放云上,或做 NAT 穿透(打洞)。

### 3. 转发面 / 控制面分离

> 现状:网关自己查 kv_store 决定去哪,无鉴权、无策略、无下发。

- **拆出独立控制面服务**
  - 控制面管决策:账号、策略、路由选择、配置下发;kv_store 退成它的存储。
  - 转发面(gateway)只执行:CONNECT 时问控制面"这次走哪个出口",不自查 kv_store。
    原 kv_client -> 变成"问控制面的客户端"。
- **接口与下发模型**
  - 控制面对外:`GET path?route=..&src_region=..` -> `{exit: jp-1, ip:port, ttl}`。
  - 决策带 TTL,转发面缓存到过期再问 -> 少一次往返;节点挂了靠心跳让它自然过期,
    比每个 CONNECT 都实时查更稳。
- **智能选路(控制面 + 接入点分工)**
  - 控制面按 目标区域 / 源区域 / 节点实时质量(心跳带 RTT·丢包·负载)给候选;
  - 接入点(或用户端)在候选内本地测速挑最优:集中决策给全局视野,本地测速追实时。
- **鉴权与计费(商业化门槛)**
  - 接入前 token/账号校验(握手时带),控制面验套餐与时长再放行;会话建/销上报计量。
- **收益**:转发面只转不决策 -> 加节点、改策略、容灾、灰度都不碰线上转发;
  决策集中 -> 能全局负载均衡。这是主流加速器的架构。
