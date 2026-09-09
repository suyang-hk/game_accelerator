#!/bin/bash
L=/home/suyang/gw_load.log
echo "CONNECT opened:    $(grep -c 'CONNECT conv=' "$L")"
echo "before-CONNECT:    $(grep -c 'before CONNECT, dropped' "$L")"
echo "already-open ign:  $(grep -c 'already open, ignored' "$L")"
echo "REGISTER pending:  $(grep -c 'session pending' "$L")"
echo "REGISTER re-ans:   $(grep -c 're-answered conv' "$L")"
echo "--- 抽样 before-CONNECT ---"
grep 'before CONNECT, dropped' "$L" | head -4
echo "--- 抽样 CONNECT opened ---"
grep 'CONNECT conv=' "$L" | head -4
