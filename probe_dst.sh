#!/bin/bash
D=$HOME/cpplinux/linux_server/netCode/game_acceleration_m
g++ -O2 -o "$D/sendprobe" "$D/sendprobe.cpp"
bash "$D/gw_restart.sh"
ulimit -n 200000
echo "=== 基线各端口 ==="
for p in 8000 8080 9999; do "$D/sendprobe" min $p | head -1; done

( cd "$D" && LOAD_DEADLINE_MS=40000 stdbuf -oL ./build/load_test reg 8000 > "$HOME/reg8k_p.log" 2>&1 ) &
sleep 2
echo "卡死中: pending=$(grep -c 'session pending' "$HOME/gw_load.log")"
echo "=== 卡死中各端口 sendprobe min ==="
for p in 8000 8080 9999; do "$D/sendprobe" min $p | head -1; done
pkill -x load_test
sleep 1
echo "=== kill 后各端口 ==="
for p in 8000 8080 9999; do "$D/sendprobe" min $p | head -1; done
