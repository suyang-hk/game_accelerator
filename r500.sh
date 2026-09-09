#!/bin/bash
D=/home/suyang/cpplinux/linux_server/netCode/game_acceleration_m
ulimit -n 200000
bash "$D/gw_restart.sh" >/dev/null
echo "=== relay 500 x 2000 plen=100 cap=64 ==="
LOAD_DEADLINE_MS=60000 stdbuf -oL "$D/build/load_test" relay 500 2000 100 test.com 64 2>&1 | grep -E "relay|registered|hb t=2|hb t=4|hb t=6|delivered|pps|throughput|STALL"
