#!/bin/bash
D=$HOME/cpplinux/linux_server/netCode/game_acceleration_m
bash "$D/gw_restart.sh"
ulimit -n 200000
gwp=$(pgrep -x gateway)
cpu1s() {
  local a1 a2; a1=$(awk '{print $14+$15}' /proc/$1/stat); sleep 1
  a2=$(awk '{print $14+$15}' /proc/$1/stat); awk -v d=$((a2-a1)) 'BEGIN{printf "%.0f%%", d/10}'
}
TOT=0
for i in $(seq 1 12); do
  out=$(LOAD_DEADLINE_MS=20000 "$D/build/load_test" reg 1000)
  reg=$(echo "$out" | grep '^reg ' )
  TOT=$((TOT+1000))
  cpu=$(cpu1s $gwp)
  pend=$(grep -c 'session pending' "$HOME/gw_load.log")
  echo "batch#$i 目标总表~$TOT | $reg | gw_pending=$pend gw_cpu=$cpu"
done
pkill -x load_test 2>/dev/null
