#!/bin/bash
D=$HOME/cpplinux/linux_server/netCode/game_acceleration_m
bash "$D/gw_restart.sh"
ulimit -n 200000
( cd "$D" && LOAD_DEADLINE_MS=30000 ./build/load_test reg 8000 > "$HOME/reg8k_run2.log" 2>&1 ) &
sleep 8
pkill -x load_test
sleep 1
echo "--- 旁路 reg150(表里应已挂约4096) ---"
LOAD_DEADLINE_MS=8000 "$D/build/load_test" reg 150
echo "--- reg8000 前半结果 ---"
head -12 "$HOME/reg8k_run2.log"
echo "--- gw REGISTER 总行数 ---"
grep -c REGISTER "$HOME/gw_load.log"
