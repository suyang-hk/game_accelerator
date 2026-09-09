#!/bin/bash
D=/home/suyang/cpplinux/linux_server/netCode/game_acceleration_m
ulimit -n 200000
run() {
  local sess=$1 each=$2 plen=$3 cap=$4
  bash "$D/gw_restart.sh" >/dev/null
  echo "=== relay $sess x $each plen=$plen cap=$cap (in-flight<=$((sess*cap))) ==="
  LOAD_DEADLINE_MS=60000 stdbuf -oL "$D/build/load_test" relay $sess $each $plen test.com $cap 2>&1 | grep -E "delivered|pps|throughput|STALL|loss"
  echo ""
}
# 控制 aggregate in-flight 扫网关可持续容量 + 找拥塞崩溃点
run 500 2000 100 8
run 500 2000 100 16
run 300 3000 100 32
run 1000 2000 100 16
run 200 5000 100 64
