#!/bin/bash
D=$HOME/cpplinux/linux_server/netCode/game_acceleration_m
bash "$D/gw_restart.sh"
ulimit -n 200000
( cd "$D" && LOAD_DEADLINE_MS=40000 stdbuf -oL ./build/load_test reg 8000 > "$HOME/reg8k_w.log" 2>&1 ) &
BIG=$!
sleep 4
p=$(pgrep -x gateway)
echo "t4s gw_pending=$(grep -c 'session pending' "$HOME/gw_load.log")"
# 采样函数: 两次读 /proc/PID/stat 的 utime+stime, 差得 1s 内 CPU%
cpu_samp() {
  local p=$1
  local a1 a2 b1 b2 c1 c2 s1 s2 d1 d2 pct
  a1=$(awk '{print $14+$15}' /proc/$p/stat); b1=$(awk '{print $14}' /proc/$p/stat); c1=$(awk '{print $15}' /proc/$p/stat)
  sleep 1
  a2=$(awk '{print $14+$15}' /proc/$p/stat); b2=$(awk '{print $14}' /proc/$p/stat); c2=$(awk '{print $15}' /proc/$p/stat)
  pct=$(awk -v d1=$((a2-a1)) 'BEGIN{printf "%.0f", d1/10}')
  echo "cpu=${pct}% (utime+stime/10ms ticks in 1s) [sys_ticks=$((c2-c1))]"
}
for i in 1 2 3 4 5; do
  st=$(awk '{print $3}' /proc/$p/stat 2>/dev/null)
  wchan=$(cat /proc/$p/wchan 2>/dev/null)
  printf "sample%d state=%s wchan=%-14s " $i "$st" "$wchan"
  cpu_samp $p
done
echo "--- 网关 client socket (Recv-Q 满说明没被排空) ---"
ss -ulnp | grep ':8000'
echo "--- 网关 fd 数 / 会话 conv 上界 ---"
ls /proc/$p/fd | wc -l
grep -o 'conv=[0-9]*' "$HOME/gw_load.log" | grep -o '[0-9]*' | sort -n | tail -1
echo "--- reg8000 是否还在跑/已自己退出 ---"
kill -0 $BIG 2>/dev/null && echo "reg8000 alive" || echo "reg8000 GONE"
tail -3 "$HOME/reg8k_w.log"
pkill -x load_test 2>/dev/null
