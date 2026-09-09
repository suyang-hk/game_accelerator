#!/bin/bash
D=/home/suyang/cpplinux/linux_server/netCode/game_acceleration_m
ulimit -n 200000
echo "=== 基线: relay 100 x 5000 plen=100 cap=64 (还原后产品原状网关) ==="
LOAD_DEADLINE_MS=60000 stdbuf -oL "$D/build/load_test" relay 100 5000 100 test.com 64 2>&1 | grep -E "relay sessions|registered=|delivered|pps|throughput|STALL"
