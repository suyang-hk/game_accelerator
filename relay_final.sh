#!/bin/bash
D=/home/suyang/cpplinux/linux_server/netCode/game_acceleration_m
ulimit -n 200000
run() {
  local sess=$1 each=$2 plen=$3 cap=$4
  bash "$D/gw_restart.sh" >/dev/null
  echo "=== relay $sess x $each plen=$plen cap=$cap ==="
  LOAD_DEADLINE_MS=90000 stdbuf -oL "$D/build/load_test" relay $sess $each $plen test.com $cap 2>&1 | grep -E "relay sessions|registered=|delivered|pps|throughput|STALL"
  echo ""
}
run 500 2000 100 64
run 1000 1000 100 64
run 200 5000 1400 32
run 500 2000 1400 64
run 1000 1000 1400 32
