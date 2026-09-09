#!/bin/bash
D=$HOME/cpplinux/linux_server/netCode/game_acceleration_m
bash "$D/gw_restart.sh"
ulimit -n 200000

# 大客户端: 期望注册到 ~4096 后卡住; 行缓冲输出便于看 wave
( cd "$D" && LOAD_DEADLINE_MS=40000 stdbuf -oL ./build/load_test reg 8000 > "$HOME/reg8k_c.log" 2>&1 ) &
BIG=$!
for i in 1 2 3 4 5 6; do
  sleep 1
  echo "t${i}s gw_pending=$(grep -c 'session pending' "$HOME/gw_load.log") alive=$(kill -0 $BIG 2>/dev/null && echo Y || echo N)"
done
echo "=== reg8000 wave 前几行 ==="
head -11 "$HOME/reg8k_c.log"

echo "=== 并发 reg300 (不 kill 大客户端) ==="
LOAD_DEADLINE_MS=10000 "$D/build/load_test" reg 300
echo "gw_pending after concurrent reg300 = $(grep -c 'session pending' "$HOME/gw_load.log")"
echo "=== reg8000 是否恢复进展? ==="
sleep 1
echo "gw_pending = $(grep -c 'session pending' "$HOME/gw_load.log")"
tail -2 "$HOME/reg8k_c.log"

echo "=== kill 大客户端后再 reg300 ==="
pkill -x load_test
sleep 1
LOAD_DEADLINE_MS=10000 "$D/build/load_test" reg 300
echo "gw_pending final = $(grep -c 'session pending' "$HOME/gw_load.log")"
