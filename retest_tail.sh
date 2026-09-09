#!/bin/bash
D=/home/suyang/cpplinux/linux_server/netCode/game_acceleration_m
ulimit -n 200000
cmake --build "$D/build" --target gateway 2>&1 | tail -3
run() {
  local sess=$1 each=$2 plen=$3 cap=$4 tag=$5
  bash "$D/gw_restart.sh" >/dev/null
  echo "=== $tag: relay $sess x $each plen=$plen cap=$cap (wndsize=1024) ==="
  LOAD_DEADLINE_MS=120000 stdbuf -oL "$D/build/load_test" relay $sess $each $plen test.com $cap 2>&1 | grep -E "relay sessions|registered=|delivered|pps|throughput|STALL"
  echo ""
}
# 尾部死锁档各跑 2 遍看是否消除/偶发
run 1000 1000 100 64 "曾尾部卡112"
run 1000 1000 100 64 "曾尾部卡112(2)"
run 500 2000 1400 64 "曾尾部卡18"
run 500 2000 1400 64 "曾尾部卡18(2)"
