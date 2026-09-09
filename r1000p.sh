#!/bin/bash
D=/home/suyang/cpplinux/linux_server/netCode/game_acceleration_m
ulimit -n 200000
bash "$D/gw_restart.sh" >/dev/null
echo "=== relay 1000 x 1000 plen=1400 cap=32 CONNECT_DELAY_MS=500 ==="
LOAD_DEADLINE_MS=90000 CONNECT_DELAY_MS=500 stdbuf -oL "$D/build/load_test" relay 1000 1000 1400 test.com 32 2>&1 | grep -E "relay sessions|registered=|delivered|pps|throughput|STALL"
echo "=== 网关 CONNECT opened ==="
grep -c 'CONNECT conv=' /home/suyang/gw_load.log
