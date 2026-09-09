#!/bin/bash
D=/home/suyang/cpplinux/linux_server/netCode/game_acceleration_m
ulimit -n 200000
: > ~/echo_rate.log
bash "$D/gw_restart.sh" >/dev/null
echo "=== relay 300 x 3000 plen=100 cap=32 ==="
( cd "$D" && LOAD_DEADLINE_MS=60000 stdbuf -oL ./build/load_test relay 300 3000 100 test.com 32 > ~/relay300.log 2>&1 ) &
LP=$!
# 采样 14 秒: 每 2s 打客户端进度 + echo 累计计数
for i in 1 2 3 4 5 6 7; do
  sleep 2
  echo "--- t=$((i*2))s ---"
  grep "hb t=" ~/relay300.log | tail -1
  tail -1 ~/echo_rate.log
done
echo "=== 收尾 ==="
grep -E "delivered|pps|STALL|loss" ~/relay300.log | tail -2
kill $LP 2>/dev/null
