#!/bin/bash
D=$HOME/cpplinux/linux_server/netCode/game_acceleration_m
bash "$D/gw_restart.sh"
ulimit -n 200000
( cd "$D" && LOAD_DEADLINE_MS=40000 stdbuf -oL ./build/load_test reg 8000 > "$HOME/reg8k_f.log" 2>&1 ) &
sleep 2
gwp=$(pgrep -x gateway)
lts=$(pgrep -x load_test)
echo "gwp=$gwp lts=$lts"

# CPU% via 1s tick delta on /proc/stat utime+stime
cpu1s() {
  local p=$1
  local a1 a2; a1=$(awk '{print $14+$15}' /proc/$p/stat); sleep 1
  a2=$(awk '{print $14+$15}' /proc/$p/stat); awk -v d=$((a2-a1)) 'BEGIN{printf "%.0f%%", d/10}'
}
sample() {
  echo "--- $1 ---"
  echo "gw  : state=$(awk '{print $3}' /proc/$gwp/stat) wchan=$(cat /proc/$gwp/wchan) cpu=$(cpu1s $gwp) rq=$(ss -ulnp | grep ':8000' | awk '{print $2}') pending=$(grep -c 'session pending' "$HOME/gw_load.log")"
  echo "lts : state=$(awk '{print $3}' /proc/$lts/stat) wchan=$(cat /proc/$lts/wchan) cpu=$(cpu1s $lts)"
  echo "reg8k 日志尾: $(tail -1 "$HOME/reg8k_f.log")"
}
sample "t=2s (预期已卡 4096)"
# 并发小客户端, 观察它在网关缓冲满/空两种情况下的命运
LOAD_DEADLINE_MS=5000 "$D/build/load_test" reg 300 > "$HOME/reg300c.log" 2>&1 &
R300=$!
sleep 2
sample "并发 reg300 进行中"
wait $R300
echo "reg300 结果: $(tail -2 "$HOME/reg300c.log" | tr '\n' ' ')"
sample "并发 reg300 结束后"
pkill -x load_test
sleep 1
sample "kill 大客户端后(对照)"
