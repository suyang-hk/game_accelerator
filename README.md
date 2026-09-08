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
