#!/bin/bash
D=$HOME/cpplinux/linux_server/netCode/game_acceleration_m
bash "$D/gw_restart.sh"
ulimit -n 200000
echo "=== 基线(空表) sendprobe ==="
"$D/sendprobe"

( cd "$D" && LOAD_DEADLINE_MS=40000 stdbuf -oL ./build/load_test reg 8000 > "$HOME/reg8k_s.log" 2>&1 ) &
sleep 2
echo "卡死中: pending=$(grep -c 'session pending' "$HOME/gw_load.log")"
echo "=== 卡死期间 sendprobe (独立的第三个进程, 新socket) ==="
"$D/sendprobe"

echo "=== 再试 sendprobe 连发50次看 errno 分布 ==="
"$D/sendprobe" | sort | uniq -c | head

pkill -x load_test
sleep 1
echo "=== kill 大客户端后 sendprobe ==="
"$D/sendprobe"
