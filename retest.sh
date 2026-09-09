#!/bin/bash
D=/home/suyang/cpplinux/linux_server/netCode/game_acceleration_m
ulimit -n 200000
run() {
  local sess=$1 each=$2 plen=$3 cap=$4 tag=$5
  bash "$D/gw_restart.sh" >/dev/null
  echo "=== $tag: relay $sess x $each plen=$plen cap=$cap ==="
  LOAD_DEADLINE_MS=90000 stdbuf -oL "$D/build/load_test" relay $sess $each $plen test.com $cap 2>&1 | grep -E "relay sessions|registered=|delivered|pps|throughput|STALL"
  echo ""
}
run 100 5000 100 64 "基线(曾干净)"
run 500 2000 100 64 "曾93%丢"
run 1000 1000 100 64 "曾需探针rcvbuf"
run 200 5000 1400 32 "大包曾需探针"
run 500 2000 1400 64 "大包500会话"
