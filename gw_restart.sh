#!/bin/bash
# 重启网关: 清空会话表, GATEWAY_QUIET=1, 行缓冲日志到 ~/gw_load.log
pkill -x gateway 2>/dev/null
sleep 0.4
cd ~/cpplinux/linux_server/netCode/game_acceleration_m || exit 1
GATEWAY_QUIET=1 nohup stdbuf -oL ./build/gateway > ~/gw_load.log 2>&1 &
sleep 0.8
if ss -ulnp 2>/dev/null | grep -q ':8000'; then echo "gateway up"; else echo "gateway FAILED"; fi
