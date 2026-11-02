#!/usr/bin/env python3
"""Daily soak summary per camera: docs/status/soak.json (stdlib + PyYAML via status_data).

Runs on the HOST (not in CI): it reads the fleet soak sampler logs and the camera STATUS files, which live outside the repository.

  sampler logs   <audit>/logs*/soak-<image>.log   one line per camera and sample:
                 "<date> <time> .N up=S pid=P snap=C oops=N encdead=E rss=.. mem=.. dn=.. [motor=..]" or "<date> <time> .N OFFLINE"
  STATUS files   <locks>/cam-N.STATUS             "<date> <hh:mm> <who>: <text>"; read: "full OTA" (image flash, planned by
                 definition, starts a new image counter), "reboot" lines (recovery reboot, ...). A line containing the word
                 "planned" (not "unplanned") marks the event as planned.

Rules come from docs/status/release.yaml (fleet camera -> SoC, planned marker). The file is rebuilt completely from the logs on every
run (idempotent), so a daily cron or a manual run by the coordinator is enough:

  python3 tools/status_soak.py                     # write docs/status/soak.json, then: python3 tools/gen_feature_matrix_html.py
  python3 tools/status_soak.py --audit DIR --locks DIR --image agg47 --out FILE

Per camera: SoC, image, first seen on the image, uptime, unplanned reboots, oops, last reset (time, kind, reason), stable_since
(the later of: first seen on the image, last unplanned reboot, last oops) and the list of reset events. Reboots are detected two ways:
STATUS lines, and a boot time (sample time minus uptime) that no STATUS event explains (reported as an unplanned reboot "no STATUS entry").
"""
import argparse
import glob
import json
import os
import re
import sys
import datetime as dt

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import status_data as sd

S = os.environ.get("LUFI_SCRATCH") or sys.exit("set LUFI_SCRATCH=<scratch dir with audit/ and locks/>")
OUT = os.path.join(sd.SDIR, "soak.json")
FMT = "%Y-%m-%d %H:%M:%S"
BOOT_TOL = 150          # s: boot times closer than this are the same boot
EXPLAIN_TOL = 600       # s: a STATUS event within this distance of a boot explains the boot


def ts(s):
    return dt.datetime.strptime(s, FMT)


def fmt(t):
    return t.strftime("%Y-%m-%d %H:%M")


def is_planned(text, marker="planned"):
    return re.search(r"(?<!un)\b%s\b" % re.escape(marker), text, re.I) is not None


def parse_status(path, marker):
    """-> list of events {t, kind: flash|reboot|up, image, planned, text}"""
    ev = []
    for ln in open(path, encoding="utf-8", errors="replace"):
        m = re.match(r"^(\d{4}-\d\d-\d\d \d\d:\d\d) (\S+?):? (.*)$", ln.strip())
        if not m:
            continue
        t = ts(m.group(1) + ":00")
        who, text = m.group(2).rstrip(":"), m.group(3)
        mo = re.search(r"(\w+) full OTA", text)
        if mo:
            ev.append(dict(t=t, kind="flash", image=mo.group(1), planned=True, text=text))
            continue
        mu = re.search(r"(\w+) up \(up=", text)
        if mu:
            ev.append(dict(t=t, kind="up", image=mu.group(1), planned=True, text=text))
            continue
        if re.match(r"(?:coordinator|main)", who) and re.search(r"\b(recovery )?reboot\b(?!ed)", text, re.I) \
                or re.search(r"\bREBOOTED unplanned\b", text):
            ev.append(dict(t=t, kind="reboot", planned=is_planned(text, marker), text=text))
    return ev


def parse_log(path):
    """-> {cam: [(t, up|None, oops|None)]}; OFFLINE gives (t, None, None)"""
    res = {}
    for ln in open(path, encoding="utf-8", errors="replace"):
        m = re.match(r"^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d) (\.\d+) (.*)$", ln.strip())
        if not m:
            continue
        t, cam, rest = ts(m.group(1)), m.group(2), m.group(3)
        if rest.startswith("OFFLINE"):
            res.setdefault(cam, []).append((t, None, None))
            continue
        up = re.search(r"\bup=(\d+)", rest)
        oo = re.search(r"\boops=(\d+)", rest)
        if up:
            res.setdefault(cam, []).append((t, int(up.group(1)), int(oo.group(1)) if oo else 0))
    for v in res.values():
        v.sort(key=lambda x: x[0])
    return res


def build(audit, locks, image=None, rel=None):
    rel = rel or sd.load_release()
    marker = next(r["planned_marker"] for g in rel["gates"] for r in g["rules"] if "planned_marker" in r)
    logs = sorted(glob.glob(os.path.join(audit, "logs*", "soak-*.log")))
    byimg = {}
    for p in logs:
        im = re.match(r"soak-(.+)\.log$", os.path.basename(p)).group(1)
        byimg.setdefault(im, []).append(p)
    if image is None:
        image = max(byimg, key=lambda i: max(os.path.getmtime(p) for p in byimg[i]))
    samples = {}
    for p in byimg.get(image, []):
        for cam, v in parse_log(p).items():
            samples.setdefault(cam, []).extend(v)
    last_t = max((v[-1][0] for v in samples.values()), default=None)
    cams, days = [], {}
    for cam, soc in sorted(rel["fleet"].items(), key=lambda kv: int(kv[0][1:])):
        sp = os.path.join(locks, "cam-%s.STATUS" % cam[1:])
        sev = parse_status(sp, marker) if os.path.exists(sp) else []
        flashes = [e for e in sev if e["kind"] == "flash" and e["image"] == image]
        ups = [e for e in sev if e["kind"] == "up" and e["image"] == image]
        sm = samples.get(cam, [])
        up_s = [x for x in sm if x[1] is not None]
        first_sample = sm[0][0] if sm else None
        # first seen on the image: the "up" line after the flash, else the first sample (boot time)
        if ups:
            first = min(e["t"] for e in ups)
        elif up_s:
            first = up_s[0][0] - dt.timedelta(seconds=up_s[0][1])
        else:
            first = None
        events = []
        if flashes:
            events.append(dict(time=fmt(min(e["t"] for e in flashes)), kind="flash", planned=True,
                               reason="image %s flashed (full OTA)" % image))
        win0 = min(e["t"] for e in flashes) if flashes else (first or dt.datetime.min)
        for e in sev:
            if e["kind"] == "reboot" and e["t"] >= win0:
                events.append(dict(time=fmt(e["t"]), kind="reboot", planned=e["planned"], reason=e["text"][:140]))
        # sampler: boots that no STATUS event explains; oops increases
        known = [e["t"] for e in sev if e["t"] >= win0]
        boots, oops_ev = [], []
        prev_o, prev_boot = None, None
        for t, up, oo in up_s:
            b = t - dt.timedelta(seconds=up)
            if prev_boot is None or abs((b - prev_boot).total_seconds()) > BOOT_TOL:
                boots.append((b, t))
                prev_o = None
            if prev_o is None:
                if oo:
                    oops_ev.append((t, oo))
            elif oo > prev_o:
                oops_ev.append((t, oo - prev_o))
            prev_o, prev_boot = oo, b
        for k, (b, seen) in enumerate(boots):
            if any(abs((b - k2).total_seconds()) <= EXPLAIN_TOL for k2 in known):
                continue
            if k == 0 and first is not None and abs((b - first).total_seconds()) <= EXPLAIN_TOL:
                continue
            events.append(dict(time=fmt(b), kind="reboot", planned=False, reason="no STATUS entry (boot time derived from uptime, seen %s)" % fmt(seen)))
        for t, n in oops_ev:
            events.append(dict(time=fmt(t), kind="oops", planned=False, count=n, reason="kernel oops counter rose by %d (dmesg)" % n))
        events.sort(key=lambda e: e["time"])
        unplanned = [e for e in events if e["kind"] == "reboot" and not e["planned"]]
        oops_n = sum(e.get("count", 1) for e in events if e["kind"] == "oops")
        resets = [e for e in events if e["kind"] == "flash" or (not e["planned"] and e["kind"] in ("reboot", "oops"))]
        stable_since = max([first] + [ts(e["time"] + ":00") for e in resets]) if first else None
        online = bool(sm) and sm[-1][1] is not None
        seen_t = max((x[0] for x in up_s), default=None)
        uptime = up_s[-1][1] if up_s and online else None
        stable_days = round((last_t - stable_since).total_seconds() / 86400.0, 2) if (stable_since and last_t and online) else 0.0
        last_reset = resets[-1] if resets else None
        cams.append(dict(
            camera=cam, soc=soc, image=image, first_seen=fmt(first) if first else None,
            last_sample=fmt(seen_t) if seen_t else None, online=online, uptime_s=uptime,
            unplanned_reboots=len(unplanned), oops=oops_n,
            last_reset=dict(time=last_reset["time"], kind=last_reset["kind"], planned=last_reset["planned"], reason=last_reset["reason"]) if last_reset else None,
            stable_since=fmt(stable_since) if stable_since else None, stable_days=max(stable_days, 0.0),
            events=events))
        for t, up, oo in sm:
            d = days.setdefault(t.strftime("%Y-%m-%d"), {}).setdefault(cam, dict(samples=0, offline=0, max_uptime_s=0, max_oops=0))
            d["samples"] += 1
            if up is None:
                d["offline"] += 1
            else:
                d["max_uptime_s"] = max(d["max_uptime_s"], up)
                d["max_oops"] = max(d["max_oops"], oo or 0)
    return dict(
        _comment="Daily soak summary per fleet camera. Written by tools/status_soak.py on the host from the soak sampler logs and the "
                 "camera STATUS files (not in CI); read by tools/release_gates.py. Do not edit by hand. Rules: docs/status/release.yaml.",
        image=image, asof=fmt(last_t) if last_t else None,
        sources=dict(sampler_logs=[os.path.relpath(p, audit) for p in byimg.get(image, [])], status="cam-N.STATUS (coordinator log)"),
        cameras=cams, days={d: days[d] for d in sorted(days)})


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--audit", default=os.path.join(S, "audit"))
    ap.add_argument("--locks", default=os.path.join(S, "locks"))
    ap.add_argument("--image", help="image tag of the sampler log (default: the newest log)")
    ap.add_argument("--out", default=OUT)
    a = ap.parse_args()
    d = build(a.audit, a.locks, a.image)
    with open(a.out, "w", encoding="utf-8") as f:
        json.dump(d, f, indent=1, ensure_ascii=False)
        f.write("\n")
    print("wrote %s: image %s, as of %s, %d cameras, unplanned reboots %d, oops %d" % (
        a.out, d["image"], d["asof"], len(d["cameras"]), sum(c["unplanned_reboots"] for c in d["cameras"]), sum(c["oops"] for c in d["cameras"])))


if __name__ == "__main__":
    main()
