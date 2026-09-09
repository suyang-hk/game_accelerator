#!/bin/bash
D=/home/suyang/cpplinux/linux_server/netCode/game_acceleration_m
ulimit -n 200000
bash "$D/gw_restart.sh" >/dev/null
( cd "$D" && LOAD_DEADLINE_MS=60000 stdbuf -oL ./build/load_test relay 500 2000 100 test.com 64 > ~/relay500.log 2>&1 ) &
sleep 5
echo "=== t5s 冻结点采样 ==="
grep -E "hb|delivered|STALL" ~/relay500.log | head -3
snmp() { awk '/^Udp:/{print "InDg="$2" InErr="$4" RcvbufErr="$7}' /proc/net/snmp; }
gwp=$(pgrep -x gateway); ep=$(pgrep -x fastecho); lp=$(pgrep -x load_test)
pstat() { top -b -n1 -p "$1" 2>/dev/null | tail -1 | awk '{print $9"%"}'; }
echo "gw  : pid=$gwp cpu=$(pstat $gwp) wchan=$(cat /proc/$gwp/wchan 2>/dev/null)"
echo "echo: pid=$ep cpu=$(pstat $ep) wchan=$(cat /proc/$ep/wchan 2>/dev/null)"
echo "load: pid=$lp cpu=$(pstat $lp) wchan=$(cat /proc/$lp/wchan 2>/dev/null)"
echo "snmp A: $(snmp)"
sleep 1
echo "snmp B: $(snmp)"
echo "=== :8000(gw收) 与回声/目标socket 队列 ==="
awk 'NR>1{print $2, "rx="strtonum("0x"$5), "tx="strtonum("0x"$6)}' /proc/net/udp 2>/dev/null | sort -t= -k2 -rn | head -8
echo "=== sendprobe :9999 环回是否 EAGAIN ==="
"$D/sendprobe" min 9999 | head -2
kill $(pgrep -x load_test) 2>/dev/null
