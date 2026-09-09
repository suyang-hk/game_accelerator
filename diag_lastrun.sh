#!/bin/bash
L=/home/suyang/gw_load.log
echo "=== 最后 3 条 probe 计数 ==="
grep 'probe' "$L" | tail -3
echo "=== CONNECT opened 计数 ==="
grep -c 'CONNECT conv=' "$L"
echo "=== REGISTER pending ==="
grep -c 'session pending' "$L"
echo "=== before-CONNECT 丢弃 ==="
grep -c 'before CONNECT, dropped' "$L"
echo "=== 池耗尽/截断/异常 ==="
grep -cE 'pool|exceeds|truncated' "$L"
echo "=== 异常日志抽样 ==="
grep -vE 'REGISTER|CONNECT conv=|probe|KVStore' "$L" | grep -v '^$' | tail -8
