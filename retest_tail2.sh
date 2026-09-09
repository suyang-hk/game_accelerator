#!/bin/bash
# 尾损归因测试: 重建回退 wnd 的网关 + 从当前源码重启 8MB-rcvbuf 的 echo, 再测极值档
D=/home/suyang/cpplinux/linux_server/netCode/game_acceleration_m
ulimit -n 200000
echo "=== rebuild gateway (wnd reverted to default 32) ==="
cmake --build "$D/build" --target gateway 2>&1 | tail -2

echo "=== rebuild + restart echo (8MB rcvbuf, current source) ==="
g++ -O2 -o ~/fastecho ~/fastecho.cpp || { echo "echo build FAILED"; exit 1; }
pkill -x fastecho 2>/dev/null
sleep 0.4
nohup stdbuf -oL ~/fastecho 8080 > ~/fastecho.log 2>&1 &
sleep 0.6
ss -ulnp 2>/dev/null | grep ':8080' || echo "echo FAILED to bind"

run() {
  local sess=$1 each=$2 plen=$3 cap=$4 tag=$5
  bash "$D/gw_restart.sh" >/dev/null
  echo "=== $tag: relay $sess x $each plen=$plen cap=$cap ==="
  LOAD_DEADLINE_MS=120000 stdbuf -oL "$D/build/load_test" relay $sess $each $plen test.com $cap 2>&1 | grep -E "relay sessions|registered=|delivered|pps|throughput|STALL"
  echo ""
}
run 500 2000 1400 64 "曾尾部卡18(echo大缓冲后)"
run 1000 1000 100 64 "曾尾部卡112(echo大缓冲后)"
run 500 2000 100 64 "曾93%丢 回归确认"
