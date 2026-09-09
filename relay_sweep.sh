#!/bin/bash
D=$HOME/cpplinux/linux_server/netCode/game_acceleration_m
ulimit -n 200000
run() {
  local sess=$1 each=$2 plen=$3 cap=$4
  bash "$D/gw_restart.sh" >/dev/null
  LOAD_DEADLINE_MS=25000 stdbuf -oL "$D/build/load_test" relay $sess $each $plen test.com $cap 2>&1 | grep -E 'delivered|registered'
  echo ""
}
echo "=== A: 20x1000 plen100 cap32 ==="; run 20 1000 100 32
echo "=== B: 50x1000 plen100 cap32 ==="; run 50 1000 100 32
echo "=== C: 100x1000 plen100 cap32 ==="; run 100 1000 100 32
echo "=== D: 100x200 plen100 cap32 ==="; run 100 200 100 32
echo "=== E: 200x200 plen100 cap32 ==="; run 200 200 100 32
echo "=== F: 50x500 plen1400 cap32 ==="; run 50 500 1400 32
