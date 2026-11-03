#!/bin/bash
# run_swap.sh IP SOC KO LIB OUTDIR  -  apitest on ONE camera with a RUNTIME swap of the open ISP kernel module and/or libimp
# (no flash, no reboot, no vendor ISP ko): the streamer is stopped, KO (optional, "-" = keep) replaces the loaded tx_isp_* module
# via rmmod/insmod with the arguments of /etc/modules.d, LIB (optional, "-" = keep) is used through LD_LIBRARY_PATH; apitest runs
# with the given areas/cases; the stock module is loaded again and the streamer restarted afterwards (trap, also on errors).
#
#   AREAS=cases CASES=isposd tools/apitest/run_swap.sh <cam-ip> T41 path/tx-isp-t41.ko path/libimp.so OUT
#
# env: AREAS (default sys,fs,enc,osd,ivs,audio,isp,su,cases)  CASES (APITEST_CASES)  TOPIC (STATUS tag)  BIN (apitest dir, default ./build)
#      PUSHF="local[:name] ..." (extra files into /tmp/rsw)  PULLF="name ..." (files of /tmp/rsw copied to OUT after the run)
#      SENSOR I2C W H (override the per-SoC table of run_cam.sh)  NOIMG=1 (default here; set NOIMG=0 for the picture area)
# libimp-only (KO = "-"): the kernel modules are never touched, also not in the restore (the Eufy T40 panics/hangs on rmmod).
# Rules: cam lock held for the whole run, STATUS "planned test" start/done lines, nothing large in /tmp (ko < 1 MB on T41 -> check
# 'free'), no AO call (APITEST_AO=0), never the vendor ISP ko.
S=${S:?set S=<scratch dir with audit/ and locks/>}
D=$(cd "$(dirname "$0")" && pwd)
BIN=${BIN:-$D/build}
IP=$1; SOC=$(echo "$2" | tr a-z A-Z); KO=$3; LIB=$4; OUT=$5; NN=${IP##*.}
[ -n "$OUT" ] || { sed -n 2,12p "$0"; exit 2; }
lc=$(echo "$SOC" | tr A-Z a-z)
AREAS=${AREAS:-sys,fs,enc,osd,ivs,audio,isp,su,cases}
case "$SOC" in
  T10) : ${SENSOR:=jxh42};  : ${I2C:=0x30}; : ${W:=1280}; : ${H:=720};;
  T20) : ${SENSOR:=jxf23};  : ${I2C:=0x40}; : ${W:=1920}; : ${H:=1080};;
  T21) : ${SENSOR:=jxf23};  : ${I2C:=0x40}; : ${W:=1920}; : ${H:=1080};;
  T23) : ${SENSOR:=sc1a4t}; : ${I2C:=0x30}; : ${W:=1280}; : ${H:=720};;
  T31) : ${SENSOR:=sc2336}; : ${I2C:=0x30}; : ${W:=1920}; : ${H:=1080};;
  T40) : ${SENSOR:=sc830ai};: ${I2C:=0x30}; : ${W:=3840}; : ${H:=2160};;
  T41) : ${SENSOR:=sc5336}; : ${I2C:=0x30}; : ${W:=2880}; : ${H:=1620};;
  *) echo "unknown SoC"; exit 2;;
esac
export SSH_AUTH_SOCK=${SSH_AUTH_SOCK:-/run/user/1000/ssh-cam.sock}
SSH="ssh -o BatchMode=yes -o ConnectTimeout=8 -o ServerAliveInterval=3 -o ServerAliveCountMax=3 root@$IP"
STATUS=$S/locks/cam-$NN.STATUS
st() { echo "$(date '+%F %H:%M') ${TOPIC:-rc3-gaps}: $SOC $IP planned test: $*" >> "$STATUS"; }
[ -e "$S/audit/ab-t41/USER_IN_CELLAR" ] && { echo "USER_IN_CELLAR"; exit 3; }
mkdir -p "$OUT"
exec 9> "$S/locks/cam-$NN.lock"; flock -w 3600 9 || { echo "cam lock busy"; exit 4; }
tail -n 3 "$STATUS" | grep -qiE "stuck|deadlock|hung" && { echo "STATUS says stuck"; exit 5; }

$SSH 'cat /etc/modules.d/20-isp /etc/modules.d/30-sensor* 2>/dev/null; ls /etc/init.d | grep -E "timps|prudynt|raptor"; uname -r; lsmod | grep -E "tx_isp|sensor_"' > "$OUT/before.txt" 2>&1
ISPLINE=$(grep -E "^tx[-_]isp" "$OUT/before.txt" | head -1); ISPMOD=${ISPLINE%% *}; ISPARGS=${ISPLINE#"$ISPMOD"}; [ "$ISPARGS" = "$ISPLINE" ] && ISPARGS=
SENS=$(grep -oE "^sensor_[a-z0-9_]*" "$OUT/before.txt" | awk '!seen[$0]++' | tr "\n" " " | sed "s/ *$//")      # ALL loaded sensor modules (the Eufy T40 has two: the ISP module cannot be removed while one is loaded)
# the sensor modules' own arguments from /etc/modules.d (e.g. sensor_resolution=300 on the T30X, shvflip=1 on T10/T21):
# a bare modprobe would bring the sensor up in another mode (T30X: 2304x1536 instead of 2048x1536)
SENSLOAD=$(for m in $SENS; do a=$(grep -E "^$m( |$)" "$OUT/before.txt" | grep -vE "^$m +[0-9]+ +[0-9]" | head -1 | cut -s -d' ' -f2-); printf 'modprobe %s %s || echo FAIL-sensor; ' "$m" "$a"; done)
STREAMER=$(grep -oE "^S[0-9]+(timps|prudynt|raptor)[A-Za-z0-9_-]*" "$OUT/before.txt" | grep -v -E "motion|onvif|webui" | head -1)
KR=$(grep -E "^[0-9]+\.[0-9]+\.[0-9]+" "$OUT/before.txt" | head -1)
STOCK=$($SSH "find /lib/modules /usr/lib/modules -name '${ISPMOD//_/-}.ko' 2>/dev/null | head -1")
echo "isp=$ISPMOD args=[$ISPARGS] sensor=$SENS streamer=$STREAMER stock=$STOCK" | tee "$OUT/run.log"
[ -n "$ISPMOD" ] && [ -n "$SENS" ] && [ -n "$STREAMER" ] && [ -n "$STOCK" ] || { echo "cannot determine the module setup"; exit 6; }
MD5=$($SSH "md5sum $STOCK")

st "start (cam-$NN.lock held, ~10 min): streamer stopped$([ "$KO" != - ] && echo ", runtime swap of $ISPMOD ($(basename "$KO"))")$([ "$LIB" != - ] && echo ", test libimp via LD_LIBRARY_PATH"), apitest $AREAS $CASES, stock module + streamer restored; no flash, no reboot, no vendor ko, no sound"
restore() {
  $SSH "killall apitest 2>/dev/null; sleep 1; [ \"$KO\" != - ] && lsmod | grep -q ^$ISPMOD && { for m in $SENS; do rmmod \$m; done; rmmod $ISPMOD || echo FAIL-rmmod-ours; insmod $STOCK $ISPARGS || echo FAIL-insmod-stock; $SENSLOAD; }; rm -rf /tmp/rsw; /etc/init.d/$STREAMER start >/dev/null 2>&1; sleep 12; echo streamer=\$(ps | grep -c '[t]impsd\|[p]rudynt\|[r]aptor'); lsmod | grep -E 'tx_isp|sensor_'; md5sum $STOCK; dmesg | grep -ciE 'oops|BUG:|panic|unable to handle'" > "$OUT/restore.txt" 2>&1
  grep -q "$(echo "$MD5" | cut -d' ' -f1)" "$OUT/restore.txt" && st "done, free (stock module md5 ok, streamer restarted, /tmp/rsw removed, out $OUT)" || st "done, free BUT restore needs a check: see $OUT/restore.txt"
}
trap restore EXIT
$SSH 'mkdir -p /tmp/rsw; free | head -2; df /tmp | tail -1' >> "$OUT/run.log" 2>&1
cat "$BIN/apitest_$lc" | $SSH 'cat > /tmp/rsw/apitest; chmod 755 /tmp/rsw/apitest'
[ "$KO" != - ] && cat "$KO" | $SSH 'cat > /tmp/rsw/test.ko'
[ "$LIB" != - ] && cat "$LIB" | $SSH 'cat > /tmp/rsw/libimp.so'
# stable ISP day/night + ircut before the streamer is stopped (audit/T10_DARK_PICTURE_1102.md: the boot-day window after a streamer start gives dark pictures)
$SSH 'last=; same=0; n=0; while [ $n -lt 300 ] && [ $same -lt 20 ]; do cur="$(grep -h -m1 Runing /proc/jz/isp/isp_info /proc/jz/isp/isp-m0 2>/dev/null)|$(cat /tmp/ircutmode.txt 2>/dev/null)"; if [ "$cur" = "$last" ]; then same=$((same+1)); else same=0; last="$cur"; fi; n=$((n+1)); sleep 2; done; echo "precheck stable=$same/20 after $((n*2))s: $last"' >> "$OUT/precheck.txt" 2>&1
$SSH "/etc/init.d/$STREAMER stop >/dev/null 2>&1; sleep 3; dmesg -c >/dev/null"
if [ "$KO" != - ]; then
  $SSH "for m in $SENS; do rmmod \$m || echo FAIL-rmmod-sensor; done; rmmod $ISPMOD || echo FAIL-rmmod-stock; insmod /tmp/rsw/test.ko $ISPARGS || echo FAIL-insmod-ours; $SENSLOAD; sleep 1; lsmod | grep -E 'tx_isp|sensor_'; echo osd_commits=\$(cat /sys/module/${ISPMOD}/parameters/osd_commits 2>/dev/null)" >> "$OUT/run.log" 2>&1
  grep -q FAIL- "$OUT/run.log" && { echo "swap failed, see $OUT/run.log"; exit 7; }
fi
for f in $PUSHF; do cat "${f%%:*}" | $SSH "cat > /tmp/rsw/$(basename "${f##*:}")"; done
LD=; [ "$LIB" != - ] && LD="LD_LIBRARY_PATH=/tmp/rsw"
# SAMPLE=1: memory sampler on the camera every 20 s (ends when /tmp/rsw is removed by the restore) -> PULLF=mem.txt
[ -n "$SAMPLE" ] && $SSH -n 'nohup sh -c "while [ -d /tmp/rsw ]; do echo t=\$(date +%s); grep -E \"MemFree|^Cached|Slab\" /proc/meminfo; cat /proc/jz/rmem /proc/jz/vbm 2>/dev/null | head -20; sleep 20; done" > /tmp/rsw/mem.txt 2>&1 &' >/dev/null 2>&1
timeout ${TMO:-1500} $SSH "cd /tmp/rsw; $LD APITEST_AO=0 ${EXTRA_ENV} ${CASES:+APITEST_CASES=$CASES} ./apitest --json /tmp/rsw/apitest.json $SENSOR $I2C $W $H $AREAS" > "$OUT/apitest.log" 2> "$OUT/apitest.err"
echo "apitest rc=$?" >> "$OUT/run.log"
$SSH 'cat /tmp/rsw/apitest.json' > "$OUT/apitest.json" 2>/dev/null
for f in $PULLF; do $SSH "cat /tmp/rsw/$f" > "$OUT/$f" 2>/dev/null; done
$SSH 'dmesg | tail -n 40; echo ---; dmesg | grep -E "guard|Oops|BUG:|Unable to handle" | head -20' > "$OUT/dmesg-tail.txt" 2>&1
python3 "$D/results2json.py" --soc "$SOC" --ip "$IP" --image "rc3gaps-swap" --date "$(date +%F)" --out "$OUT/result.json" --apitest "$OUT/apitest.log" $([ -s "$OUT/apitest.json" ] && echo --report "$OUT/apitest.json") > "$OUT/r2j.txt" 2>&1
grep -hE "^\[T\] summary|^\[E\]" "$OUT/apitest.log" | tail -3
echo "FAIL lines:"; grep -hE "^\[A\] .* FAIL" "$OUT/apitest.log" | cut -c1-260
