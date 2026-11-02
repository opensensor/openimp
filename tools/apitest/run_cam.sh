#!/bin/bash
# run_cam.sh IP SOC [outdir]  -  device API run on ONE camera: apitest (all safe areas), area picture (former imgfx, statistics
# only, no picture leaves the camera), T40: area t40dev (former t40_devtest) - ONE binary apitest_<soc>.  Restores the camera (streamer restarted, /tmp clean).
#
#   build first:  tools/apitest/build.sh            (binaries in tools/apitest/build, or BIN=<dir>)
#   example:      tools/apitest/run_cam.sh <cam-ip> T41
#
# Rules it follows (brief RC2 1024 + addenda): cam lock held for the WHOLE run, STATUS "start" / "done, free",
# ssh keep-alive, nothing large in /tmp (binaries < 1 MB, output streamed to the host), refuses to run while
# audit/ab-t41/USER_IN_CELLAR exists, NO sound output (APITEST_AO=0: no AO call at all; the AEC/speaker cases
# are SKIP in apitest anyway), no flash, no reboot, no module load/unload, no light change.
#
# env: S (session dir, default below)  BIN  AREAS (default sys,fs,enc,osd,ivs,audio,isp,su)  RISKY=1 (imgfx
#      bypass/FPS cases)  NOIMG=1 (skip imgfx)  T40_MASK (t40_devtest phases, hex)  TAG (label, default agg47)
#      SENSOR I2C W H (override the table)  DRY=1 (print what would run)
#      IMGFX_FILTER=name (picture area: only the cases whose name contains it, e.g. exp-; the base cases always run)
S=${S:?set S=<scratch dir with audit/ and locks/>}
D=$(cd "$(dirname "$0")" && pwd)
BIN=${BIN:-$D/build}
IP=$1; SOC=$(echo "$2" | tr a-z A-Z); NN=${IP##*.}
TAG=${TAG:-agg47}
OUT=${3:-$S/audit/apitest-evidence/$(date +%m%d)-$SOC-$IP}
AREAS=${AREAS:-sys,fs,enc,osd,ivs,audio,isp,su,cases}
T40_MASK=${T40_MASK:-cdff}
[ "$AREAS" = cases ] || [ "$AREAS" = audio ] && NODEV=${NODEV:-1} NOIMG=${NOIMG:-1}      # a cases-only run touches nothing else
lc=$(echo "$SOC" | tr A-Z a-z)
export SSH_AUTH_SOCK=${SSH_AUTH_SOCK:-/run/user/1000/ssh-cam.sock}
SSH=${SSH_CMD:-"ssh -o ServerAliveInterval=3 -o ServerAliveCountMax=3 -o ConnectTimeout=8 -o BatchMode=yes root@$IP"}
[ -n "$IP" ] && [ -n "$SOC" ] || { sed -n 2,3p "$0"; exit 2; }

# sensor table (verified against the camera below when it can be read)
case "$SOC:$IP" in
  T10:*)   : ${SENSOR:=jxh42};  : ${I2C:=0x30}; : ${W:=1280}; : ${H:=720};;
  T20:*)   : ${SENSOR:=jxf23};  : ${I2C:=0x40}; : ${W:=1920}; : ${H:=1080};;
  T21:*)   : ${SENSOR:=jxf23};  : ${I2C:=0x40}; : ${W:=1920}; : ${H:=1080};;
  T23:*)   : ${SENSOR:=sc1a4t}; : ${I2C:=0x30}; : ${W:=1280}; : ${H:=720};;
  T31:*)   : ${SENSOR:=sc2336}; : ${I2C:=0x30}; : ${W:=1920}; : ${H:=1080};;
  T40:*)   : ${SENSOR:=sc830ai};: ${I2C:=0x30}; : ${W:=3840}; : ${H:=2160};;
  T41:*)   : ${SENSOR:=sc5336}; : ${I2C:=0x30}; : ${W:=2880}; : ${H:=1620};;     # sc5336 mode of the open driver (2592x1944 distorts the geometry cases)
  *) echo "unknown SoC $SOC"; exit 2;;
esac

STATUS=$S/locks/cam-$NN.STATUS
st() { echo "$(date '+%F %H:%M') apitest-evidence: $*" >> "$STATUS"; }
cam() { $SSH "$@"; }
[ -e "$S/audit/ab-t41/USER_IN_CELLAR" ] && { echo "USER_IN_CELLAR exists: no picture measurements now (apitest/imgfx read frames in memory)"; exit 3; }
[ -x "$BIN/apitest_$lc" ] || { echo "missing $BIN/apitest_$lc (run build.sh)"; exit 2; }
if [ -n "$DRY" ]; then echo "would run on $IP ($SOC) sensor $SENSOR $I2C ${W}x$H, areas $AREAS, out $OUT"; exit 0; fi

mkdir -p "$OUT"
exec 9> "$S/locks/cam-$NN.lock"
flock -w 3600 9 || { echo "cam lock cam-$NN busy for 1 h"; exit 4; }       # held for the WHOLE run (fd 9)
tail -n 3 "$STATUS" 2>/dev/null | grep -qiE "stuck|deadlock|hung" && { echo "STATUS reports the camera stuck: no run"; exit 5; }
STREAMER=
restore() {
  # kill our own test processes by PID only, remove the test files, start the streamer again
  cam 'for p in /tmp/at/*.pid; do [ -f $p ] && kill -9 $(cat $p) 2>/dev/null; done; rm -rf /tmp/at' > /dev/null 2>&1
  if [ -n "$STREAMER" ]; then cam "$STREAMER start" > "$OUT/restore.txt" 2>&1; sleep 12
    cam 'ps | grep -c "[t]imps\|[p]rudynt\|[r]aptor\|[r]vd"' >> "$OUT/restore.txt" 2>&1; fi
  cam 'dmesg | grep -cE "Oops|unaligned access|Unable to handle|BUG:|Kernel panic"; uptime' > "$OUT/after.txt" 2>&1
}
fin() { restore; st "$SOC $IP ${NOTE:-planned test}: done, free (streamer restarted, /tmp/at removed, out $OUT)"; }
trap fin EXIT

NOTE="planned test"; [ "$AREAS" = audio ] && NOTE="planned test: AO/AEC silence only (muted, volume minimum, zero PCM frames)"
st "$SOC $IP $NOTE: start (cam-$NN.lock held for the whole run, ~10 min): apitest $AREAS + imgfx stats$([ $SOC = T40 ] && echo " + t40_devtest $T40_MASK"), streamer stopped then restarted, no audible output, no flash/reboot/modules"
cam 'uname -m; free | head -2; df /tmp | tail -1; ls /etc/init.d | grep -E "timps|prudynt|raptor|rvd"; for f in /proc/jz/sensor/*/name /proc/jz/sensor/name; do [ -r $f ] && echo "sensor $f: $(cat $f)"; done; md5sum /usr/lib/libimp.so 2>/dev/null; ls -l /usr/lib/libimp.so; cat /etc/os-release 2>/dev/null | head -4; cat /etc/thingino-build 2>/dev/null | head -3; lsmod | head -20; dmesg | grep -cE "Oops|unaligned access|Unable to handle|BUG:|Kernel panic"; echo preflight-ok' > "$OUT/before.txt" 2>&1; grep -q preflight-ok "$OUT/before.txt" || { echo "ssh failed"; exit 6; }
grep -qi "$SENSOR" "$OUT/before.txt" || echo "NOTE: sensor $SENSOR not seen in the camera's /proc/jz/sensor (table value used; override with SENSOR= I2C= W= H=)" | tee -a "$OUT/before.txt"
STREAMER=$(grep -oE "^S[0-9]+(timps|prudynt|raptor|rvd)[A-Za-z0-9_-]*" "$OUT/before.txt" | grep -v -E "motion|onvif|webui" | head -1)
[ -n "$STREAMER" ] && STREAMER=/etc/init.d/$STREAMER
echo "streamer: ${STREAMER:-none found}" | tee -a "$OUT/before.txt"
cam 'dmesg -c > /dev/null; [ -n "'"$STREAMER"'" ] && '"$STREAMER"' stop > /dev/null 2>&1; sleep 3; mkdir -p /tmp/at'
copy() { cat "$1" | cam "cat > /tmp/at/$2; chmod 755 /tmp/at/$2"; }
copy "$BIN/apitest_$lc" apitest

# run NAME CMD  -  output streamed to the host, remote PID recorded for the cleanup, host watchdog 20 min
run() { local name=$1; shift; echo "== $name: $*" >> "$OUT/run.log"
  timeout 1200 $SSH "cd /tmp/at; echo \$\$ > /tmp/at/$name.pid; exec env APITEST_AO=$([ "$AREAS" = audio ] && echo 1 || echo 0) $EXTRA_ENV $*" > "$OUT/$name.log" 2> "$OUT/$name.err"
  echo "   rc=$? lines=$(wc -l < "$OUT/$name.log")" >> "$OUT/run.log"; }
run apitest ./apitest --json /tmp/at/apitest.json "$SENSOR" "$I2C" "$W" "$H" "$AREAS"
cam "cat /tmp/at/apitest.json" > "$OUT/apitest.json" 2>/dev/null
if [ -z "$NOIMG" ] && [ "$SOC" != T40 ]; then
  [ -e "$S/audit/ab-t41/USER_IN_CELLAR" ] || run imgfx env IMGFX_STATS=1 IMGFX_SKIP_RISKY=$([ -n "$RISKY" ] && echo 0 || echo 1) ./apitest "$SENSOR" "$I2C" "$W" "$H" picture /tmp/at/img $IMGFX_FILTER
fi
# colour pass (issue 105): CCM/BCSH act on colour, a grey scene cannot show them.  Saturation 230 for the run, only the colour cases
# (ccm*, bypass-*): results2json takes this log as the second --imgfx file and lets a clear picture effect override the first pass.
# COLOR=0 skips it; the scene must be colourful (daylight or light.keller on, LIGHT.lock) - the chroma of the base frame is in the log.
if [ -z "$NOIMG" ] && [ "$SOC" != T40 ] && [ "${COLOR:-1}" = 1 ] && [ ! -e "$S/audit/ab-t41/USER_IN_CELLAR" ]; then
  for f in ccm bypass-; do
    run imgfx2-$f env IMGFX_STATS=1 IMGFX_SAT=230 IMGFX_SKIP_RISKY=1 ./apitest "$SENSOR" "$I2C" "$W" "$H" picture /tmp/at/img2 $f
    cat "$OUT/imgfx2-$f.log" >> "$OUT/imgfx2.log"
  done
fi
[ "$SOC" = T40 ] && [ -z "$NODEV" ] && run devtest ./apitest "$SENSOR" "$I2C" "$W" "$H" t40dev "$T40_MASK"
cam 'dmesg | tail -n 40' > "$OUT/dmesg-tail.txt" 2>&1

python3 "$D/results2json.py" --soc "$SOC" --ip "$IP" --image "$TAG" --date "$(date +%F)" --out "$OUT/result.json" \
  --apitest "$OUT/apitest.log" $([ -s "$OUT/apitest.json" ] && echo --report "$OUT/apitest.json") $([ -s "$OUT/imgfx.log" ] && echo --imgfx "$OUT/imgfx.log") $([ -s "$OUT/imgfx2.log" ] && echo --imgfx "$OUT/imgfx2.log") $([ -s "$OUT/devtest.log" ] && echo --devtest "$OUT/devtest.log")
grep -hE "^\[T\] summary|^\[E\]" "$OUT/apitest.log" | tail -3
echo "FAIL lines:"; grep -hE "^\[A\] .* FAIL" "$OUT/apitest.log" | head -40
