#!/bin/bash
D=$HOME/cpplinux/linux_server/netCode/game_acceleration_m
bash "$D/gw_restart.sh" >/dev/null
ulimit -n 200000
( cd "$D" && LOAD_DEADLINE_MS=30000 stdbuf -oL ./build/load_test relay 100 5000 100 test.com 64 > "$HOME/relay_diag.log" 2>&1 ) &
sleep 6
echo "=== t6s (应已冻结) ==="
tail -3 "$HOME/relay_diag.log"
gwp=$(pgrep -x gateway); lts=$(pgrep -x load_test)
ep=$(pgrep -x udp_echo_server | head -1)
snmp() { grep '^Udp:' /proc/net/snmp | awk '{print "InDg="$2" InErr="$4" RcvbufErr="$7}'; }
echo "gw : cpu=$(top -b -n1 -p $gwp | tail -1 | awk '{print $9"%"}') wchan=$(cat /proc/$gwp/wchan)"
echo "lts: cpu=$(top -b -n1 -p $lts | tail -1 | awk '{print $9"%"}') wchan=$(cat /proc/$lts/wchan)"
echo "echo: cpu=$(top -b -n1 -p $ep | tail -1 | awk '{print $9"%"}') wchan=$(cat /proc/$ep/wchan)"
echo "snmp 前: $(snmp)"
echo "=== sendprobe :9999 (loopback 是否 EAGAIN) ==="
"$D/sendprobe" min 9999 | head -2
sleep 2
echo "snmp 后: $(snmp)"
echo "=== gw socket 队列 ==="
ss -ulnp | grep ':8000'
pkill -x load_test
