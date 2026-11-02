#!/usr/bin/env python3
import functools, builtins
print = functools.partial(builtins.print, flush=True)
"""qemu_run.py - run apitest cases inside our QEMU (rule: every new case runs in QEMU first, per SoC, before any camera).

  qemu_run.py PORT SOC SENSOR I2C W H BINARY|auto [case ...] [--outdir D]

Console driver for a QEMU guest booted with a root shell on tcp PORT (boot-t2x.sh / run-t4x.sh, see $S/qemu/README-*.md).  Pushes BINARY
(gzip+base64 over the console), stops the streamer, then per case: `APITEST_CASES=<case> apitest ... cases`, followed by a health check
(plain `fs` area: a fresh FrameSource EnableChn must work and frames must flow, dmesg without Oops).  A case that panics/reboots the guest or
breaks the health check is a BUG and stops the run.  Output: OUTDIR/<case>.log and a summary line per case
  QEMU <SOC> <case>: <ok|HEALTH-FAIL|CRASH> (apitest rc, FAIL lines)
What QEMU proves: return codes, set/get round trips, oops/crash, frame flow afterwards.  It cannot prove picture effects."""
import base64, gzip, os, re, socket, sys, time

a = sys.argv[1:]
outdir = "."
nopush = "--nopush" in a           # the binary is already in the guest (/tmp/at/apitest from an earlier push)
if nopush:
    a.remove("--nopush")
if "--outdir" in a:
    i = a.index("--outdir"); outdir = a[i + 1]; del a[i:i + 2]
port, soc, sensor, i2c, W, H, binary = a[:7]
cases = a[7:]
if binary == "auto":     # the per-SoC build of tools/apitest/build.sh
    binary = os.path.join(os.path.dirname(os.path.abspath(__file__)), "build", "apitest_" + soc.lower())
os.makedirs(outdir, exist_ok=True)
s = socket.create_connection(("127.0.0.1", int(port))); s.settimeout(0.5)


def rd(idle, maxt, until=None):
    buf = b""; last = t0 = time.time()
    while time.time() - last < idle and time.time() - t0 < maxt:
        try:
            d = s.recv(65536)
            if d:
                buf += d; last = time.time()
                if until and until in buf:
                    break
        except socket.timeout:
            pass
    return buf.decode(errors="replace")


def sh(cmd, maxt=300, idle=60):
    """run cmd, return (output, rc) ; rc None when the marker never came (hang/reboot)"""
    tag = "QR%d" % int(time.time() * 1000 % 1e9)
    s.sendall(("%s; echo %s_$?_END\n" % (cmd, tag)).encode())
    out = rd(idle, maxt, until=None)
    t0 = time.time()
    while not re.search(tag + r"_(\d+)_END", out) and time.time() - t0 < maxt:
        more = rd(idle if idle < 20 else 20, 20)
        out += more
        if re.search(r"Kernel panic|Restarting system|U-Boot", more):
            break
    m = re.search(tag + r"_(\d+)_END", out)
    return out, (int(m.group(1)) if m else None)


s.sendall(b"\x03\n"); rd(1, 3)
sh("stty -echo; mkdir -p /tmp/at; /etc/init.d/S95timps stop >/dev/null 2>&1; sleep 2; dmesg -c >/dev/null; free | head -2", 60, 5)
CH = int(os.environ.get("QR_CHUNK", "150"))      # characters per echo line (QR_CHUNK=900 pushes about 6x faster on a quiet host)
def push(path, dest, mode="755"):
    """gzip+base64 over the console into /tmp/at/<dest>"""
    data = base64.b64encode(gzip.compress(open(path, "rb").read(), 9)).decode()
    pace = float(os.environ.get("QR_PACE", "0.03"))
    for attempt in range(3):
        s.sendall(b": > /tmp/at/b.b64\n"); rd(0.3, 2)
        for i in range(0, len(data), CH):
            s.sendall(("echo %s >> /tmp/at/b.b64\n" % data[i:i + CH]).encode()); rd(pace, 1)
        o, _ = sh("wc -c < /tmp/at/b.b64", 900, 120)
        got = re.findall(r"^(\d+)\s*$", o, re.M)
        if got and int(got[-1]) == len(data) + len(data) // CH + (1 if len(data) % CH else 0):
            break
        print("push attempt %d: size %s, expected %d - retrying slower" % (attempt, got[-1:], len(data) + (len(data) + CH - 1) // CH))
        pace *= 3
    return sh("base64 -d /tmp/at/b.b64 | gunzip > /tmp/at/%s && chmod %s /tmp/at/%s && rm /tmp/at/b.b64; ls -l /tmp/at/%s" % (dest, mode, dest, dest), 120, 10)


# QR_LIB=<libimp.so>: push that library too and run the guest apitest against it (LD_LIBRARY_PATH=/tmp/at) instead of the image's libimp
ldp = ""
if os.environ.get("QR_LIB") and not nopush and not os.environ.get("QR_LIBKEEP"):   # QR_LIBKEEP=1: the lib from an earlier push is still in /tmp/at
    lo, _ = push(os.environ["QR_LIB"], "libimp.so")
    print("pushed lib:", lo.strip().splitlines()[-1:] if lo.strip() else lo)
    # QR_LIBEXTRA=<a.so,b.so>: further libraries next to it (the stock libimp needs the vendor libalog / libsysutils)
    for extra in filter(None, os.environ.get("QR_LIBEXTRA", "").split(",")):
        lo, _ = push(extra, os.path.basename(extra))
        print("pushed extra:", lo.strip().splitlines()[-1:] if lo.strip() else lo)
if os.environ.get("QR_LIB"):
    ldp = "LD_LIBRARY_PATH=/tmp/at "
    # the stock libimp does not list libalog as NEEDED (the vendor programs link it): preload the extras
    pre = [os.path.join("/tmp/at", os.path.basename(e)) for e in os.environ.get("QR_LIBEXTRA", "").split(",") if e]
    if pre:
        ldp += "LD_PRELOAD=%s " % ":".join(pre)
out, rc = push(binary, "apitest") if not (nopush or os.environ.get("QR_APPKEEP")) else sh("ls -l /tmp/at/apitest", 20, 5)    # QR_APPKEEP=1: keep the apitest of an earlier push, push the library only
print("pushed:", out.strip().splitlines()[-2:] if out.strip() else out)
res = []
for c in cases:
    envx = ""
    area = "cases"
    if c.startswith("area:"):        # area:audio -> the whole area (all [A] lines of IMP_ functions reported)
        area = c[5:]; c = area
    elif ":" in c:                   # case:ENVVAR  -> run with ENVVAR=1 (e.g. process:APITEST_PROCESS_BYPASS)
        c, envx = c.split(":", 1); envx = " ".join(v if "=" in v else v + "=1" for v in envx.split(":")) + " "   # case:VAR1:VAR2 -> VAR1=1 VAR2=1 (VAR=value keeps the value)
    cmd = "cd /tmp/at; " + ldp + envx + ("APITEST_AO=1 " if area == "audio" else "APITEST_AO=0 ") + "APITEST_QEMU=1 APITEST_CASES=%s ./apitest %s %s %s %s %s 2>&1 | grep -E '^\\[A\\] stalesnap|^\\[A\\] tisp_node_|^\\[A\\] sensorattr_t40|^\\[A\\] IMP_DMIC_|^\\[A\\] SU_|^\\[A\\] customalgo|^\\[A\\] libsysutils|^\\[A\\] dmic|^\\[A\\] IMP_(Alloc|Free|Sp_Alloc|Decoder_|ISPOSD_Alloc|OSD_Alloc|Phys_to|Virt_to|FlushCache|Pool|MemPool_|System_|IVS_|FrameSource_|Encoder_|ISP_Tuning|ISP_Get|ISP_Set|Log|LOG|ISP_[A-Z]|OSD_[A-Za-z_]*ISP[A-Za-z]*|A[IOED]).*(PASS|FAIL|N/A|SKIP)|^\\[E\\]|^\\[T\\] summary|^hskip: |^demask: |^pic ' | grep -v 'IMP_ISP_\\(Open\\|AddSensor\\|EnableSensor\\|DisableSensor\\|DelSensor\\|Close\\|EnableTuning\\|DisableTuning\\)'" % (c, sensor, i2c, W, H, area)
    out, rc = sh(cmd, 300, 90)
    open(os.path.join(outdir, c + ".log"), "w").write(out)
    if rc is None or re.search(r"Kernel panic|Oops|Unable to handle|Restarting system", out):
        res.append((c, "CRASH", out[-300:])); print("QEMU %s %s: CRASH (guest hung/rebooted/oopsed)" % (soc, c)); break
    hc, hrc = sh("cd /tmp/at; ./apitest %s %s %s %s fs 2>&1 | grep -cE 'EnableChn.*FAIL|GetFrame.*FAIL'; dmesg | grep -cE 'Oops|BUG:|Unable to handle|unaligned'; true" % (sensor, i2c, W, H), 200, 60)
    nums = re.findall(r"^(?:.*# )?(\d+)\s*$", re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", hc), re.M)
    healthy = hrc == 0 and nums[:2] == ["0", "0"]
    fails = [l for l in out.splitlines() if l.startswith("[A]") and " FAIL " in l]
    open(os.path.join(outdir, c + ".health"), "w").write(hc)
    st = "ok" if healthy else "HEALTH-FAIL"
    res.append((c, st, fails))
    print("QEMU %s %s: %s (health counts %s; %d FAIL lines)" % (soc, c, st, nums[:2], len(fails)))
    for l in fails:
        print("   ", l.strip()[:260])
    if not healthy:
        break
