#!/bin/bash
D=$HOME/cpplinux/linux_server/netCode/game_acceleration_m
bash "$D/gw_restart.sh"
ulimit -n 200000
( cd "$D" && LOAD_DEADLINE_MS=40000 stdbuf -oL ./build/load_test reg 8000 > "$HOME/reg8k_d.log" 2>&1 ) &
sleep 2
echo "网关进程数: $(pgrep -x gateway | wc -l)"
snmp() { grep '^Udp:' /proc/net/snmp; }
udprow() { grep -E ':1F40 ' /proc/net/udp; }
echo "=== 卡死时基线 ==="
snmp; echo "--- gw socket (/proc/net/udp): sl loc rem st tx rx ... drops ---"; udprow
echo "pending=$(grep -c 'session pending' "$HOME/gw_load.log")"
echo "=== 启动并发 reg300 (5s), 期间看计数器增量 ==="
A=$(snmp | awk '{print $2,$4,$5,$7,$8}')   # InDatagrams NoPorts InErrors RcvbufErrors SndbufErrors
B=$(udprow)
LOAD_DEADLINE_MS=5000 "$D/build/load_test" reg 300 > "$HOME/reg300d.log" 2>&1 &
R=$!
sleep 3
C=$(snmp | awk '{print $2,$4,$5,$7,$8}')
E=$(udprow)
echo "snmp 增量 (InDg NoPorts InErr RcvbufErr SndbufErr):"
echo "  before: $A"; echo "  after : $C"
echo "  gw socket before: $B"; echo "  gw socket after : $E"
echo "pending now=$(grep -c 'session pending' "$HOME/gw_load.log")"
echo "=== 等 reg300 结束 ==="
wait $R; tail -2 "$HOME/reg300d.log"
pkill -x load_test 2>/dev/null; sleep 1
