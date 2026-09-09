#!/bin/bash
# 重启两个回声服: 输出到 /dev/null(否则每包一行 printf 会在压测下卡死单线程回声服)
B=$HOME/cpplinux/linux_server/netCode/game_acceleration_m/build/udp_echo_server
pkill -x udp_echo_server 2>/dev/null
sleep 0.3
nohup "$B" 8080 >/dev/null 2>&1 &
nohup "$B" 8800 >/dev/null 2>&1 &
sleep 0.6
ss -ulnp 2>/dev/null | grep -E ':8080|:8800' || echo "echo servers FAILED to start"
