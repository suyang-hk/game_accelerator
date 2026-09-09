#!/bin/bash
D=$HOME/cpplinux/linux_server/netCode/game_acceleration_m
g++ -O2 -o "$D/sendprobe" "$D/sendprobe.cpp"
bash "$D/gw_restart.sh"
ulimit -n 200000
echo "=== 相关 sysctl / 基线内存 ==="
echo "udp_mem: $(cat /proc/sys/net/ipv4/udp_mem)"
echo "rmem_max: $(cat /proc/sys/net/core/rmem_max) wmem_max: $(cat /proc/sys/net/core/wmem_max)"
echo "sockstat(UDP行): $(grep '^Udp:' /proc/net/sockstat)"

( cd "$D" && LOAD_DEADLINE_MS=40000 stdbuf -oL ./build/load_test reg 8000 > "$HOME/reg8k_m.log" 2>&1 ) &
sleep 2
echo "=== 卡死中 ==="
echo "pending=$(grep -c 'session pending' "$HOME/gw_load.log")"
echo "sockstat(UDP行): $(grep '^Udp:' /proc/net/sockstat)"
echo "udp_mem: $(cat /proc/sys/net/ipv4/udp_mem)"
echo "MemAvailable: $(grep MemAvailable /proc/meminfo)"
echo "reg8000 RSS: $(ps -o rss= -p $(pgrep -x load_test) | awk '{printf "%.0f MB", $1/1024}')"
echo "gw RSS: $(ps -o rss= -p $(pgrep -x gateway) | awk '{printf "%.0f MB", $1/1024}')"
echo "=== 无缓冲设置的 sendprobe min ==="
"$D/sendprobe" min
echo "=== 带128KB缓冲的 sendprobe ==="
"$D/sendprobe"
pkill -x load_test
sleep 1
echo "=== kill 后 sockstat ==="
echo "sockstat(UDP行): $(grep '^Udp:' /proc/net/sockstat)"
