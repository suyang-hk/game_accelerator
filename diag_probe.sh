#!/bin/bash
D=/home/suyang/cpplinux/linux_server/netCode/game_acceleration_m
ulimit -n 200000
bash "$D/gw_restart.sh" >/dev/null
echo "=== relay 300 x 3000 plen=100 cap=32 (probe counters) ==="
( cd "$D" && LOAD_DEADLINE_MS=60000 stdbuf -oL ./build/load_test relay 300 3000 100 test.com 32 > ~/relay_probe.log 2>&1 ) &
LP=$!
for i in 1 2 3 4 5 6; do
  sleep 2
  echo "--- t=$((i*2))s ---"
  grep -E "probe|delivered" ~/gw_load.log | tail -1
  grep "hb t=" ~/relay_probe.log | tail -1
done
echo "=== 收尾 ==="
grep -E "delivered|pps|STALL|loss" ~/relay_probe.log | tail -2
grep -E "probe" ~/gw_load.log | tail -2
kill $LP 2>/dev/null
