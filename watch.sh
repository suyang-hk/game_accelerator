#!/bin/bash
# 采样网关 CPU% 共 N 秒
p=$(pgrep -x gateway)
for i in $(seq 1 "$1"); do
  cpu=$(ps -o %cpu= -p "$p" 2>/dev/null | tr -d ' ')
  echo "t${i}s cpu=${cpu}%"
  sleep 1
done
