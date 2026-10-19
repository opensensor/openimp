#!/bin/sh
# control socket + tpctl: a process that loaded the library with OPENIMP_TEST_PATTERN_CTL answers set/get
B=$1; S=$B/tp_ctl.sock
rm -f $S
OPENIMP_TEST_PATTERN_CTL=$S OPENIMP_TEST_PATTERN=1 $B/tp_hold 3 &
P=$!
sleep 1
OPENIMP_TEST_PATTERN_CTL=$S $B/tpctl get | grep -q "mode=1" || { kill $P; echo "ctl get failed"; exit 1; }
OPENIMP_TEST_PATTERN_CTL=$S $B/tpctl set 3 obj_size=32 obj_cell=2,3 logo=1 | grep -q "mode=3.*obj_size=32.*obj_cell=2,3" || { kill $P; echo "ctl set failed"; exit 1; }
OPENIMP_TEST_PATTERN_CTL=$S $B/tpctl set 9 >/dev/null && { kill $P; echo "ctl range check failed"; exit 1; }
OPENIMP_TEST_PATTERN_CTL=$S $B/tpctl set 0 | grep -q "mode=0" || { kill $P; echo "ctl off failed"; exit 1; }
kill $P; rm -f $S
echo "ctl_test: ok"
