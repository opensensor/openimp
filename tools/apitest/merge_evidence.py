#!/usr/bin/env python3
"""Merge device-run results (results2json.py) into docs/api_evidence.json.
RT -> cell rt (API round trip on device, only over aud/host/?/run/miss); PASS -> cell dev (dev+/no-op/err-free cells keep their symbol; a miss cell of an exported function and an err cell written by this tool from an earlier FAIL of the same tag become dev), FAIL -> err, PART/SKIP/NA -> unchanged (listed).
The reference text goes to the per-SoC "ref" of the function row.  Overrides (JSON): {"ignore": {SOC: [key..]},
"fail": {SOC: {key: detail}}, "pass_only": [SOC..]} (pass_only: take only PASS results of that file, e.g. a starved devtest run).
Usage: merge_evidence.py [--tag agg47] [--dry] SOC=result.json[:pass_only] ... [--overrides f.json]"""
import json, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
EVID = os.path.join(HERE, "..", "..", "docs", "api_evidence.json")
args = sys.argv[1:]
tag, dry, ov = "agg47", False, {}
files = []
while args:
    a = args.pop(0)
    if a == "--tag": tag = args.pop(0)
    elif a == "--dry": dry = True
    elif a == "--overrides": ov = json.load(open(args.pop(0)))
    else: files.append(a)
ev = json.load(open(EVID, encoding="utf-8"))
F = ev["functions"]
EXPO = json.load(open(os.path.join(HERE, "..", "..", "docs", "api_exports.json")))
import re


def exported(soc, key):
    m = re.match(r"^(.*?)Get/Set(.*)$", key)
    if key == "ISP_Tuning_Get/SetHVFLIP": m = None; key = None       # vendor spelling: GetHVFlip / SetHVFLIP
    if key is None:
        return all(n in EXPO.get(soc, []) for n in ("ISP_Tuning_GetHVFlip", "ISP_Tuning_SetHVFLIP"))
    names = [m.group(1) + "Get" + m.group(2), m.group(1) + "Set" + m.group(2)] if m else [key]
    return all(n in EXPO.get(soc, []) for n in names) or "*" in key or key.startswith(("SU_", "Log_", "Audio_"))
report = {"dev": [], "err": [], "kept": [], "skipped": []}
for spec in files:
    soc, rest = spec.split("=", 1)
    path, _, mode = rest.partition(":")
    qemu = mode == "qemu"          # SOC=result.json:qemu - SoC without a camera: PASS/RT from a QEMU run set the cell to "qemu"
    r = json.load(open(path))
    ign = set(ov.get("ignore", {}).get(soc, []))
    ref = "apitest %s %s %s" % (tag, r.get("date", ""), "." + r.get("ip", "").rsplit(".", 1)[-1] if r.get("ip") else "")  # public docs: no full LAN addresses
    for k, v in sorted(r["funcs"].items()):
        if k in ign or k not in F:
            continue
        cells = F[k]["cells"]
        cur = cells.get(soc)
        if not exported(soc, k) and v["v"] in ("PASS", "FAIL", "RT"):
            report["skipped"].append((soc, k, v["v"], "not in docs/api_exports.json (export list older than the flashed libimp)")); continue
        if cur in ("n.a.", "nob", None):
            report["skipped"].append((soc, k, v["v"], "cell is %s" % cur)); continue
        if qemu:
            if v["v"] in ("PASS", "RT") and cur in ("aud", "host", "?", "run", "miss"):
                cells[soc] = "qemu"; report["dev"].append((soc, k, cur))
                F[k].setdefault("ref", {})[soc] = "apitest %s %s QEMU (no camera)" % (tag, r.get("date", ""))
            else:
                report["kept"].append((soc, k, v["v"], cur))
            continue
        if v["v"] == "PASS":
            if cur in ("aud", "host", "?", "run", "qemu"):    # qemu: a device PASS replaces the QEMU-only evidence
                cells[soc] = "dev"; report["dev"].append((soc, k, cur))
            elif cur == "miss":
                # exported by the refreshed docs/api_exports.json (checked above) and PASS on the device: the gap is closed
                cells[soc] = "dev"; report["dev"].append((soc, k, cur))
            elif cur == "err" and F[k].get("ref", {}).get(soc, "").startswith("FAIL apitest "):
                # the err cell was written by this tool from an earlier FAIL (any image tag: the fix may sit in a later build): a later PASS (re-run after a fix or a
                # corrected test expectation) clears it; hand-recorded err cells (other ref text) stay
                cells[soc] = "dev"; report["dev"].append((soc, k, cur))
            elif cur in ("cache", "stub", "err"):
                report["kept"].append((soc, k, "PASS", cur)); print("KEPT-GAP", soc, k, cur, "(PASS, but the cell records a known defect/stub: review)")
            if cur in ("aud", "host", "?", "run", "qemu", "dev", "dev+", "no-op", "miss") or cells[soc] == "dev": F[k].setdefault("ref", {})[soc] = ref
        elif v["v"] == "RT":
            if cur in ("aud", "host", "?", "run", "miss", "qemu") or (cur == "err" and F[k].get("ref", {}).get(soc, "").startswith("FAIL apitest ")):
                cells[soc] = "rt"; report["dev"].append((soc, k, cur))
                F[k].setdefault("ref", {})[soc] = ref + " (API round trip, no visible effect expected)"
            else:
                report["kept"].append((soc, k, "RT", cur))
        elif v["v"] == "FAIL" and mode != "pass_only":
            if cur != "miss":
                cells[soc] = "err"; report["err"].append((soc, k, v["detail"]))
                F[k].setdefault("ref", {})[soc] = "FAIL %s: %s" % (ref, v["detail"][:300])
        else:
            report["kept"].append((soc, k, v["v"], cur))
    for k, det in ov.get("fail", {}).get(soc, {}).items():
        if k in F and F[k]["cells"].get(soc) not in ("n.a.", "nob", None, "miss"):
            F[k]["cells"][soc] = "err"; report["err"].append((soc, k, det))
            F[k].setdefault("ref", {})[soc] = "FAIL %s: %s" % (ref, det)
if not dry:
    json.dump(ev, open(EVID, "w", encoding="utf-8"), indent=1, ensure_ascii=False)
    open(EVID, "a").write("\n")
for k in ("dev", "err"):
    print(k, len(report[k]))
json.dump(report, open(os.environ.get("MERGE_REPORT", "/dev/stdout") if False else "/dev/null", "w"))
import collections
c = collections.Counter((s, "->dev") for s, _, _ in report["dev"]); c.update((s, "->err") for s, _, _ in report["err"])
print(sorted(c.items()))
for s, k, d in report["err"]: print("ERR", s, k, "|", d[:140])
for s, k, v, why in report["skipped"]:
    if "export" in why: print("NOEXPORT", s, k, v)
