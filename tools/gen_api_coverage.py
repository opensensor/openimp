#!/usr/bin/env python3
"""API coverage per function: model, Markdown section and HTML parts (stdlib only).

Inputs (all committed):
  docs/api_evidence.json     evidence level per function and SoC (hand-maintained, machine-readable)
  docs/api_exports.json      exported IMP_* names of the OpenIMP libimp.so per SoC (nm of the builds; refresh
                             with --refresh-exports, T10 uses the T20 build)
  docs/status/features.yaml  hand-written part of the main matrix (symbols, texts, issue links); see tools/status_data.py
  docs/status/issues.json    cache of the public GitHub issues (open issue with label type:wip = construction sign)
  tools/export-check/vendor-T*.txt   vendor export lists per SoC (T32/T33: vendor libimp 2.2.0 / SDK 2.0.0)
                             main matrix symbols come from features.yaml (feature links: a function whose evidence is only
                             "audited" counts as device-tested while the linked main-matrix feature cell is supported)

Outputs: docs/FEATURE_MATRIX.md (generated head from docs/status + the section "API coverage per function";
written by this script and by tools/gen_feature_matrix_html.py) and the HTML parts used by
tools/gen_feature_matrix_html.py.  Consistency problems are printed as warnings (exit status 1 with --check).

Usage: gen_api_coverage.py [--write] [--check] [--refresh-exports [BUILD_DIR]] [--refresh-run OUT_DIR SOAK_LOG]
"""
import html
import json
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import status_data as sd
import release_gates as rg

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
MD = os.path.join(ROOT, "docs", "FEATURE_MATRIX.md")
EVID = os.path.join(ROOT, "docs", "api_evidence.json")
EXPO = os.path.join(ROOT, "docs", "api_exports.json")
RUN = os.path.join(ROOT, "docs", "api_run.json")
VEND = os.path.join(ROOT, "tools", "export-check")
HEAD = "API coverage per function"

AREAS = ["ISP tuning", "AE / AWB / AF", "Encoder", "FrameSource", "OSD", "IVS", "Audio", "System"]
AREA_TITLE = {"Encoder": "Encoder (and decoder)", "System": "System / sysutils / log"}
STREAMERS = {"p": "prudynt", "r": "raptor", "t": "timps"}
# code -> (css class, short markdown symbol, html label, description)
CELL = {
    "dev": ("s-ok", "✅", "✅", "works, device-tested"),
    "dev+": ("s-plus", "✅+", "✅+", "works, device-tested, better than the vendor"),
    "rt": ("s-ok", "✅rt", "✅ rt", "API round trip on device, no visible effect expected"),
    "no-op": ("s-ok", "✅○", "✅ no-op", "vendor no-op, OpenIMP matches that"),
    "nohw": ("s-ok", "✅?", "✅ not hw-verified", "counted as done by user decision, NOT verified on hardware (the lab board lacks the hardware; see the reference)"),
    "run": ("s-ok", "✅r", "✅ run", "works: called by the streamer on fleet cameras of this SoC that passed the fleet soak (not an individual device test; see the reference)"),
    "qemu": ("s-wip", "🔧+", "🔧 qemu", "API round trip in QEMU, no camera"),
    "host": ("s-wip", "🔧h", "🔧 host", "implemented, host tests only (no camera run yet)"),
    "aud": ("s-wip", "🔧a", "🔧 aud", "really connected per static audit, no device test tracked"),
    "cache": ("s-bug", "⚠c", "⚠ cache", "value only stored and read back, nothing applied"),
    "stub": ("s-bug", "⚠s", "⚠ stub", "returns 0 without any effect"),
    "err": ("s-miss", "❌e", "❌ err", "exported but fails or has a known defect"),
    "miss": ("s-miss", "❌", "❌ miss", "not exported by OpenIMP"),
    "n.a.": ("s-na", "·", "–", "function does not exist on this SoC (not in that SoC's vendor API; not a gap)"),
    "?": ("s-unk", "?", "?", "state not determined"),
    "nob": ("s-plan", "📋", "📋", "no OpenIMP build for this SoC yet (planned)"),
}
OK, WIPC, GAP, UNK = {"dev", "dev+", "run", "no-op", "rt", "nohw"}, {"host", "aud", "qemu"}, {"cache", "stub", "err", "miss"}, {"?"}
LEVEL = {"dev", "rt", "run", "nohw", "host", "aud", "qemu", "no-op", "cache", "stub", "err"}     # states that need an exported symbol
SEV = {"err": 0, "miss": 0, "cache": 1, "stub": 1, "?": 2}


def esc(s):
    return html.escape(str(s), quote=True)


def slugify(s):
    return re.sub(r"[^a-z0-9]+", "-", s.lower()).strip("-")


class Model:
    def __init__(self, md_text=None):
        self.ev = json.load(open(EVID, encoding="utf-8"))
        self.socs = self.ev["socs"]
        self.exports = json.load(open(EXPO, encoding="utf-8")) if os.path.exists(EXPO) else {}
        up = os.path.join(sd.SDIR, "untested.json")
        self.untested = json.load(open(up, encoding="utf-8")).get("socs", {}) if os.path.exists(up) else {}
        self.sdata = sd.load_features()
        self.issues = sd.load_issues()
        self.wip = sd.wip_items(self.sdata, self.issues)
        self.cell_issues = sd.cell_issue_links(self.sdata, self.issues)
        self.func_issues = sd.func_issue_links(self.sdata, self.issues)
        self.vendor = {}
        for s in self.socs:
            p = os.path.join(VEND, "vendor-%s.txt" % s)
            self.vendor[s] = {re.sub(r"^IMP_", "", x.strip()) for x in open(p) if x.strip()} if os.path.exists(p) else set()
        self.run = json.load(open(RUN, encoding="utf-8")) if os.path.exists(RUN) else {"socs": {}}
        md_text = sd.render_head(self.sdata, sd.load_history()) if md_text is None else md_text
        self.matrix = self._matrix_syms(md_text)
        self.warnings = []
        self.funcs = []
        self._build()

    # ------------------------------------------------------------ data
    @staticmethod
    def _matrix_syms(md):
        """{feature number: {soc: symbol}} from the main matrix table."""
        res, socs, titles = {}, None, {}
        for ln in md.split("\n"):
            if not ln.startswith("|"):
                continue
            r = [c.strip() for c in ln.strip().strip("|").split("|")]
            if r[:2] == ["#", "Feature"]:
                socs = r[2:]
            elif socs and re.fullmatch(r"\d+", r[0] or "x"):
                m = re.match(r"^\[(.*)\]\(#.*\)$", r[1])
                titles[int(r[0])] = m.group(1) if m else r[1]
                res[int(r[0])] = {s: v.replace("️", "") for s, v in zip(socs, r[2:])}
            elif socs and r[0] == "" and len(r) > 1 and r[1].startswith("**"):
                pass
        res["_titles"] = titles
        return res

    @staticmethod
    def expand(n):
        m = re.match(r"^(.*?)Get/Set(.*)$", n)
        return [m.group(1) + "Get" + m.group(2), m.group(1) + "Set" + m.group(2)] if m else [n]

    def _wild(self, n):
        return re.compile("^(" + "|".join(re.escape(p.strip().rstrip("*")) for p in n.split(" / ")) + ")")

    def weight(self, f, soc):
        if f["cells"].get(soc) == "n.a.":
            return 0
        if "*" in f["name"]:
            rx = self._wild(f["name"])
            return max(1, sum(1 for x in self.vendor[soc] if rx.match(x)))
        return 2 if "Get/Set" in f["name"] else 1

    def _build(self):
        W = self.warnings
        links = {}
        for lk in self.ev.get("feature_links", []):
            sym = self.matrix.get(lk["feature"], {}).get(lk["soc"], "")
            if lk["feature"] not in self.matrix:
                W.append("feature link: feature #%s does not exist" % lk["feature"])
            for fn in lk["functions"]:
                if fn not in self.ev["functions"]:
                    W.append("feature link: unknown function %s" % fn)
                links.setdefault((fn, lk["soc"]), []).append((lk["feature"], sym))
        wipf, wipfeat = {}, {}
        titles = self.matrix["_titles"]
        for w in self.wip:
            if w.get("function"):
                if w["function"] not in self.ev["functions"]:
                    W.append("status data: wip item for an unknown function %s" % w["function"])
                wipf.setdefault((w["function"], w["soc"]), []).append(w)
            elif w.get("feature") is not None:
                if w["feature"] not in titles:
                    W.append("status data: wip item for an unknown feature %s" % w["feature"])
                wipfeat.setdefault((w["feature"], w["soc"]), []).append(w)
            else:
                W.append("matrix_wip.json: item without function or feature: %s" % w)
            if w["soc"] not in self.socs:
                W.append("status data: wip item for an unknown SoC %s" % w["soc"])
        self.wipfeat = wipfeat
        covered = set()
        for name, e in self.ev["functions"].items():
            f = {"name": name, "area": e["area"], "raw": dict(e["cells"]), "ref": dict(e.get("ref", {})),
                 "used": e.get("used", {}), "note": e.get("note", ""), "better": e.get("better", []), "auto": e.get("auto", False), "alias": e.get("alias", [])}
            f["cells"] = {}
            f["wip"] = {}
            if "*" not in name:
                covered |= set(self.expand(name))
            for s in self.socs:
                c = e["cells"][s]
                rs = self.run["socs"].get(s)
                if rs and c in ("host", "aud", "?") and "*" not in name and not name.startswith("SU_") or (
                        rs and c in ("host", "aud", "?") and name.startswith("SU_")):
                    ex = self.expand(name)
                    if all(x in rs["calls"] for x in ex):
                        c = "run"
                        f["ref"][s] = "called by %s on the fleet %s (%s), soak %s; not an individual device test" % (
                            self.run["streamer"], ", ".join(rs["clean"]), s, self.run["soak"]["image"])
                if c == "aud":
                    for feat, sym in links.get((name, s), []):
                        if sym in ("✅", "✅+"):
                            c = "dev"
                            f["ref"][s] = "device-tested through main-matrix feature #%d (%s)" % (feat, titles.get(feat, ""))
                            break
                if c == "dev" and s in f["better"]:
                    c = "dev+"
                f["cells"][s] = c
                ws = wipf.get((name, s))
                if ws:
                    f["wip"][s] = ws
            f["w"] = {s: self.weight(f, s) for s in self.socs}
            self.funcs.append(f)
            # consistency with the built libraries (nm snapshot)
            if "*" not in name and not name.startswith("SU_"):
                for s in self.socs:
                    if s not in self.exports:
                        continue
                    exp = set(self.exports[s])
                    have = [x in exp for x in self.expand(name) + f["alias"]]
                    c = e["cells"][s]
                    if c == "miss" and all(have):
                        W.append("%s %s: evidence says miss but the function is exported" % (s, name))
                    if c in LEVEL and not any(have):
                        W.append("%s %s: evidence says %s but the function is not exported" % (s, name, c))
        names = set(self.ev["functions"])
        rx = [self._wild(n) for n in names if "*" in n]
        untracked = set(self.ev.get("vendor_untracked", []))
        for s in self.socs:
            for x in sorted(self.vendor[s]):
                if x in covered or x in untracked or any(r.match(x) for r in rx):
                    continue
                W.append("%s: vendor function %s has no row in api_evidence.json (run --sync-vendor)" % (s, x))
        self.funcs.sort(key=lambda f: (AREAS.index(f["area"]), f["name"].lower()))

    # ------------------------------------------------------------ queries
    def users(self, f, soc):
        return f["used"].get(soc, [])

    @staticmethod
    def nstream(us):
        return len({u[0] for u in us})

    def area_funcs(self, area):
        return [f for f in self.funcs if f["area"] == area]

    def stat(self, funcs, soc):
        """weights: total, ok, wip, gap, unk, nob, number of construction-sign functions"""
        r = dict(rt=0, total=0, ok=0, wip=0, gap=0, unk=0, nob=0, ws=0)
        for f in funcs:
            w = f["w"][soc]
            if not w:
                continue
            c = f["cells"][soc]
            r["total"] += w
            if c in OK:
                r["ok"] += w
                if c == "rt":
                    r["rt"] += w
            elif c in WIPC:
                r["wip"] += w
            elif c in GAP:
                r["gap"] += w
            elif c == "nob":
                r["nob"] += w
            else:
                r["unk"] += w
            if soc in f["wip"]:
                r["ws"] += w
        return r

    def summary_text(self, st):
        """'14/14 works' style: works/total and the other classes, zeros included"""
        if st["total"] == 0:
            return ""
        if st["nob"]:
            return "0/%d (no OpenIMP build yet)" % st["total"]
        t = "%d/%d ✅ · %d 🔧 · %d gaps · %d ?" % (st["ok"], st["total"], st["wip"], st["gap"], st["unk"])
        if st["ws"]:
            t += " · %d 🚧" % st["ws"]
        return t

    def priorities(self):
        """per SoC: (streamer-used gaps, gaps nobody uses, no-build functions streamers call, streamer-used unknown, unknown unused)"""
        res = {}
        for s in self.socs:
            used, unused, nobu, uunk, unk = [], [], [], [], []
            for f in self.funcs:
                c = f["cells"][s]
                us = self.users(f, s)
                if c == "nob":
                    if us:
                        nobu.append(f)
                elif c in GAP:
                    (used if us else unused).append(f)
                elif c in UNK:
                    (uunk if us else unk).append(f)
            key = lambda f: (-self.nstream(self.users(f, s)), SEV.get(f["cells"][s], 3), f["name"].lower())
            for l in (used, nobu, uunk):
                l.sort(key=key)
            unused.sort(key=lambda f: (SEV.get(f["cells"][s], 3), f["name"].lower()))
            res[s] = (used, unused, nobu, uunk, unk)
        return res

    ACTIONS = [("implement", "implement"), ("enotsup", "documented ENOTSUP"), ("n.a.", "not applicable")]

    def action(self, f, s):
        """suggested action for a backlog item: evidence 'action' override, else derived from the cell and the note"""
        a = self.ev["functions"][f["name"]].get("action", {}).get(s)
        if a:
            return a
        n = f["note"]
        c = f["cells"][s]
        if re.search(r"cannot link it either|declared in the SDK header, exported by (?:none|no vendor)", n):
            return "n.a."
        if c in ("err", "stub") and re.search(r"ENOTSUP|EOPNOTSUPP|not supported", n):
            return "enotsup"
        return "implement"

    def backlog(self):
        """per SoC: {area: {action: [functions]}} of missing / partial / unknown functions that no streamer calls"""
        res = {}
        for s in self.socs:
            d = {}
            for f in self.funcs:
                c = f["cells"][s]
                if c == "n.a." or self.users(f, s) or not (c in GAP or c in UNK or c == "nob"):
                    continue
                d.setdefault(f["area"], {}).setdefault(self.action(f, s), []).append(f)
            res[s] = d
        return res

    def backlog_count(self, d):
        return sum(len(l) for a in d.values() for l in a.values())

    # ------------------------------------------------------------ markdown
    def cellmd(self, f, s):
        c = f["cells"][s]
        t = CELL[c][1]
        return t + ("🚧" if s in f["wip"] else "")

    @staticmethod
    def ulabel(us):
        return " ".join(us) if us else "–"

    def md(self):
        socs = self.socs
        o = []
        o.append("## " + HEAD)
        o.append("")
        o.append("<!-- GENERATED by tools/gen_api_coverage.py from docs/api_evidence.json, docs/api_exports.json, "
                 "docs/status/issues.json and tools/export-check/vendor-*.txt. Do not edit this section by hand: edit the "
                 "data files and run python3 tools/gen_feature_matrix_html.py -->")
        o.append("")
        o.append("As of: %s. One row per vendor IMP/SU function (a Get/Set row counts as two functions, a wildcard row as the "
                 "vendor names it covers). Only functions that exist in a SoC's vendor API count for that SoC; a function that "
                 "is not in that SoC's vendor API is shown as `·` and is never a gap. Evidence lives in "
                 "`docs/api_evidence.json` (level per function and SoC, reference, streamer use); the exports of the OpenIMP "
                 "builds in `docs/api_exports.json` (nm of `libimp.so`) are cross-checked on every run." % self.ev["asof"])
        o.append("")
        o.append("### Coverage summary")
        o.append("")
        o.append("Per area and SoC: functions that work (device-tested or vendor no-op) / functions that exist in the SoC's "
                 "vendor API; in brackets the functions that are implemented but not device-tested (🔧), the gaps "
                 "(cache-only, stub, error, missing), functions of unknown state (?) and work in progress (🚧).")
        o.append("")
        for a in AREAS + ["all"]:
            fs = self.funcs if a == "all" else self.area_funcs(a)
            parts = []
            for s in socs:
                t = self.summary_text(self.stat(fs, s))
                if t:
                    parts.append("%s %s" % (s, t))
            o.append("- **%s:** %s" % ("All areas" if a == "all" else a, " · ".join(parts)))
        o.append("")
        o.append("### Priorities for streamers")
        o.append("")
        o.append("Gaps (error, missing, stub, cache-only) in functions that at least one streamer "
                 "(p prudynt, r raptor, t timps; † = source only, no binary checked) calls, sorted by the number of streamers; "
                 "functions of unknown state (?) that a streamer calls are listed as a separate tier (audit pending). "
                 "Gaps in functions no streamer calls follow as a count (lower priority). T32/T33 have no OpenIMP build yet: their line "
                 "counts the vendor functions the streamers call and that the first build has to provide.")
        o.append("")
        pr = self.priorities()
        for s in socs:
            used, unused, nobu, uunk, unk = pr[s]
            if nobu:
                top = ", ".join("`%s` (%s)" % (f["name"], self.ulabel(self.users(f, s))) for f in nobu[:12])
                o.append("- **%s:** no OpenIMP build yet; %d vendor functions are called by a streamer. Most used: %s" % (
                    s, len(nobu), top))
                continue
            top = ", ".join("`%s` (%s; %s)" % (f["name"], f["cells"][s], self.ulabel(self.users(f, s))) for f in used)
            ut = ""
            if uunk:
                ut = "; %d streamer-used functions are of unknown state (audit pending), e.g. %s" % (
                    len(uunk), ", ".join("`%s`" % f["name"] for f in uunk[:6]))
            o.append("- **%s:** %d streamer-used gap%s%s; %d further gaps no streamer calls%s" % (
                s, len(used), "" if len(used) == 1 else "s", (": " + top) if used else "", len(unused), ut))
        o.append("")
        o.append("### Long-term backlog")
        o.append("")
        o.append("Completeness list, separate from the streamer priorities: every vendor function of the SoC that is missing, "
                 "partial (cache-only, stub, error) or of unknown state (?) and that **no streamer calls today** (for T32/T33: "
                 "every vendor function no streamer calls, as there is no build yet). Grouped by area with the suggested action: "
                 "implement, documented ENOTSUP (the open driver answers like a vendor stack without the feature) or not applicable "
                 "(declared in a header but exported by no vendor library). Default action derived from the evidence note; "
                 "override per function with `action` in `docs/api_evidence.json`.")
        o.append("")
        bl = self.backlog()
        for s in socs:
            d = bl[s]
            o.append("<details><summary><b>%s</b>: %d functions</summary>" % (s, self.backlog_count(d)))
            o.append("")
            for a in AREAS:
                if a not in d:
                    continue
                parts = []
                for key, lab in self.ACTIONS:
                    if key in d[a]:
                        parts.append("%s (%d): %s" % (lab, len(d[a][key]), ", ".join("`%s`" % f["name"] for f in d[a][key])))
                o.append("- **%s:** %s" % (AREA_TITLE.get(a, a), "; ".join(parts)))
            o.append("")
            o.append("</details>")
            o.append("")
        o.append("### Legend")
        o.append("")
        for c in ("dev", "dev+", "rt", "run", "no-op", "nohw", "host", "aud", "qemu", "cache", "stub", "err", "miss", "n.a.", "?", "nob"):
            o.append("- `%s` %s" % (CELL[c][1], CELL[c][3]))
        o.append("- `🚧` appended to a symbol: work in progress, from the public GitHub issues labelled `type:wip` (`docs/status/issues.json`)")
        o.append("")
        for a in AREAS:
            fs = self.area_funcs(a)
            ngap = sum(1 for f in fs if any(f["cells"][s] in GAP | UNK for s in socs))
            o.append("### " + AREA_TITLE.get(a, a))
            o.append("")
            o.append("%d rows (%d with a gap or unknown state in at least one SoC). Columns: %s." % (len(fs), ngap, ", ".join(socs)))
            o.append("")
            o.append("| Vendor function | " + " | ".join(socs) + " | Used by |")
            o.append("|---|" + "---|" * len(socs) + "---|")
            for f in fs:
                us = sorted({u for s in socs for u in self.users(f, s)})
                o.append("| `%s` | %s | %s |" % (f["name"], " | ".join(self.cellmd(f, s) for s in socs), self.ulabel(us)))
            o.append("")
            notes = [f for f in fs if f["note"] or f["ref"]]
            if notes:
                o.append("<details><summary>Notes and references (%d)</summary>" % len(notes))
                o.append("")
                for f in notes:
                    t = f["note"]
                    for s, r in f["ref"].items():
                        t += (" " if t else "") + "%s: %s." % (s, r.rstrip("."))
                    o.append("- `%s`: %s" % (f["name"], t.replace("\n", " ")))
                o.append("")
                o.append("</details>")
                o.append("")
        o.append("### History of the function audit")
        o.append("")
        for h in self.ev.get("history", []):
            o.append(h)
            o.append("")
        o.append("### Notes")
        o.append("")
        for n in self.ev.get("general_notes", []):
            o.append("- " + n)
        o.append("")
        return "\n".join(o)

    # ------------------------------------------------------------ html
    WHY_ICON = {"nocase": ("∅", "no test case yet"), "unclear": ("≈", "test ran, result unclear"), "notrun": ("↷", "not run on this SoC")}

    def why(self, f, soc):
        """(code, text) for an implemented, not device-tested cell (host/aud): why is it untested."""
        u = self.untested.get(soc)
        if not u:
            return ("notrun", "not run on this SoC (no device run of the apitest/imgfx tests on %s yet: no camera or not scheduled)" % soc)
        names = self.expand(f["name"]) if "*" not in f["name"] else [f["name"]]
        hits = [u["funcs"][n] for n in [f["name"]] + names if n in u["funcs"]]
        if not hits:
            return ("nocase", "no test case yet (apitest, imgfx and devtest have no case for this function)")
        v, det = sorted(hits, key=lambda x: {"PART": 0, "SKIP": 1, "NA": 2}.get(x[0], 3))[0]
        if v == "PART":
            return ("unclear", "test ran, result unclear (PART: %s)" % (det or "no detail"))
        return ("notrun", "not run on this SoC (%s%s)" % ("skipped" if v == "SKIP" else "not applicable in the test", ": " + det if det else ""))

    VENDOR_SRC = {"T32": "vendor libimp 2.2.0 / SDK 2.0.0", "T33": "vendor libimp 2.2.0 / SDK 2.0.0"}

    def cell_text(self, f, soc):
        """Lines explaining one API cell: state, why untested, evidence, issue (the note is added by the page script)."""
        c = f["cells"][soc]
        if c == "n.a.":
            return ["%s: function does not exist on this SoC" % soc,
                    "Not in the %s vendor API (%s; tools/export-check/vendor-%s.txt)" % (
                        soc, self.VENDOR_SRC.get(soc, "union of the vendor libimp versions held"), soc)], None
        L = ["%s: %s" % (soc, CELL[c][3])]
        wy = None
        if c in WIPC:
            wy = self.why(f, soc)
            L.append("Untested: " + wy[1])
        if f["ref"].get(soc):
            L.append("Evidence: " + f["ref"][soc])
        for i in self.func_issues.get((f["name"], soc), []):
            L.append("Issue #%d (%s): %s" % (i["number"], i.get("state", ""), i.get("title", "")))
        for w in f["wip"].get(soc, []):
            L.append("Work in progress: %s (since %s)" % (w.get("branch", ""), w.get("since", "")))
        return L, wy

    def _cellhtml(self, f, s):
        c = f["cells"][s]
        cls, _, lab, desc = CELL[c]
        L, wy = self.cell_text(f, s)
        sign = " 🚧" if f["wip"].get(s) else ""
        icon = ""
        attr = ""
        if wy:
            ic, t = self.WHY_ICON[wy[0]]
            icon = ' <span class="why" title="%s">%s</span>' % (esc(t), ic)
            attr = ' data-why="%s"' % wy[0]
        il = self.func_issues.get((f["name"], s), [])
        if il:
            attr += ' data-issue="%d|%s"' % (il[0]["number"], esc(il[0].get("url", "")))
        return '<td class="c %s" title="%s" data-code="%s" data-soc="%s"%s tabindex="0">%s%s%s</td>' % (
            cls, esc("\n".join(L)), c, s, attr, esc(lab), icon, sign)

    def html_summary(self):
        socs = self.socs
        rows = []
        for a in AREAS + ["all"]:
            fs = self.funcs if a == "all" else self.area_funcs(a)
            tds = []
            for s in socs:
                st = self.stat(fs, s)
                if not st["total"]:
                    tds.append('<td class="c s-na"></td>')
                    continue
                if st["nob"]:
                    cls, big, small = "s-plan", "0/%d" % st["total"], "no build"
                else:
                    big = "%d/%d" % (st["ok"], st["total"])
                    def fl(n, sym, state):
                        return ('<span class="flt" role="button" tabindex="0" data-area="%s" data-soc="%s" data-state="%s" title="show these rows">%d%s</span>'
                                % ("" if a == "all" else slugify(a), s, state, n, sym)) if n else "%d%s" % (n, sym)
                    small = "%s %s %s %s" % (fl(st["ok"], "✅", "works"), fl(st["wip"], "🔧", "untested"), fl(st["gap"], "⚠", "gap"), fl(st["unk"], "?", "unknown")) + (" %d🚧" % st["ws"] if st["ws"] else "")
                    if st["ok"] == st["total"]:
                        cls = "s-ok"
                    elif st["gap"] + st["unk"] == 0:
                        cls = "s-wip"
                    elif st["gap"] > 0.25 * st["total"]:
                        cls = "s-miss"
                    else:
                        cls = "s-bug"
                tip = "%s %s: %d work (device-tested, API round trip or vendor no-op; %d of them round trip only), %d implemented not device-tested, %d gaps, %d unknown, of %d vendor functions" % (
                    s, a, st["ok"], st["rt"], st["wip"], st["gap"], st["unk"], st["total"])
                tds.append('<td class="c %s" title="%s"><b>%s</b><br><small>%s</small></td>' % (cls, esc(tip), big, small))
            rows.append('<tr%s><td>%s</td>%s</tr>' % (' class="sep"' if a == "all" else "",
                        "<b>All areas</b>" if a == "all" else '<a href="#api-%s">%s</a>' % (slugify(a), esc(a)), "".join(tds)))
        th = "".join("<th>%s</th>" % s for s in socs)
        return ('<div class="wrap"><table class="mi apisum"><thead><tr><th>Area</th>%s</tr></thead><tbody>%s</tbody></table></div>'
                % (th, "".join(rows)))

    def html_priorities(self):
        pr = self.priorities()
        out = []
        for s in self.socs:
            used, unused, nobu, uunk, unk = pr[s]

            def li(f, st=True):
                w = " &#128679;" if s in f["wip"] else ""
                return '<li><span class="%s sym">%s</span> <code>%s</code>%s <span class="small">%s</span></li>' % (
                    CELL[f["cells"][s]][0], esc(CELL[f["cells"][s]][2]), esc(f["name"]), w,
                    esc(self.ulabel(self.users(f, s))) if st else "")
            if nobu:
                items = "".join('<li><code>%s</code> <span class="small">%s</span></li>' % (esc(f["name"]), esc(self.ulabel(self.users(f, s)))) for f in nobu[:40])
                out.append('<details class="prio"><summary><b>%s</b> <span class="s-plan sym">no OpenIMP build yet</span> %d vendor functions are called by a streamer '
                           '(the first build has to provide them)</summary><ul class="pl">%s</ul>%s</details>' % (
                               s, len(nobu), items, "<p class='small'>top 40 by number of streamers</p>" if len(nobu) > 40 else ""))
                continue
            head = ("<span class='s-miss sym'>%d streamer-used gap%s</span>" % (len(used), "" if len(used) == 1 else "s")) if used else \
                "<span class='s-ok sym'>no gap in a function a streamer calls</span>"
            if uunk:
                head += " <span class='s-unk sym'>%d streamer-used functions unknown</span>" % len(uunk)
            body = ("<ol class='pl'>%s</ol>" % "".join(li(f) for f in used)) if used else ""
            if uunk:
                body += "<details class='lowp'><summary>%d streamer-used functions of unknown state (audit pending)</summary><ul class='pl'>%s</ul></details>" % (
                    len(uunk), "".join(li(f) for f in uunk))
            if unused:
                body += "<details class='lowp'><summary>%d further gaps no streamer calls (lower priority)</summary><ul class='pl'>%s</ul></details>" % (
                    len(unused), "".join(li(f, False) for f in unused))
            out.append('<details class="prio"%s><summary><b>%s</b> %s</summary>%s</details>' % (" open" if used else "", s, head, body))
        return "".join(out)

    def html_backlog(self):
        bl = self.backlog()
        out = []
        for s in self.socs:
            d = bl[s]
            n = self.backlog_count(d)
            inner = ""
            for a in AREAS:
                if a not in d:
                    continue
                inner += "<h4>%s <span class='small'>(%d)</span></h4>" % (esc(AREA_TITLE.get(a, a)), sum(len(x) for x in d[a].values()))
                for key, lab in self.ACTIONS:
                    if key in d[a]:
                        inner += "<p class='small'><b>%s</b> (%d): %s</p>" % (esc(lab), len(d[a][key]), ", ".join(
                            "<code title='%s'>%s</code>" % (esc(CELL[f["cells"][s]][3]), esc(f["name"])) for f in d[a][key]))
            out.append('<details class="prio"><summary><b>%s</b> &mdash; %d functions no streamer calls today</summary>%s</details>' % (s, n, inner or "<p class='small'>none</p>"))
        return "".join(out)

    def html_top(self):
        return (rg.Gates(self).html() + '<section class="card" id="priorities"><h2>Priorities for streamers</h2>'
                '<p class="small">Gaps in functions that timps, prudynt or raptor call, per SoC, sorted by the number of streamers '
                '(p prudynt, r raptor, t timps; &dagger; source only). Below the table: how much of the vendor API each area covers.</p>'
                + self.html_priorities() +
                '</section><section class="card" id="backlog"><h2>Long-term backlog</h2>'
                '<p class="small">Separate from the streamer priorities: vendor functions that are missing, partial or of unknown state and '
                'that no streamer calls today, per SoC, grouped by area with the suggested action (implement / documented ENOTSUP / '
                'not applicable). For T32/T33 (no build yet) all vendor functions no streamer calls.</p>' + self.html_backlog() +
                '</section><section class="card"><h3>API coverage at a glance</h3>'
                '<p class="small">Works (device-tested or vendor no-op) / functions that exist in the SoC\'s vendor API; functions the '
                'vendor API does not have are not counted. Click an area for the full function table.</p>'
                + self.html_summary() + '</section>')

    def html_section(self):
        socs = self.socs
        o = ['<h2 id="api-coverage">%s</h2>' % HEAD]
        o.append('<p class="meta">As of %s. One row per vendor function; click a function name for its note and evidence. '
                 'Legend: %s. A construction sign (&#128679;) marks work in progress (open GitHub issue labelled type:wip, docs/status/issues.json).</p>' % (
                     esc(self.ev["asof"]), " &middot; ".join(
                         '<span class="sym %s">%s</span> %s' % (CELL[c][0], esc(CELL[c][2] or "&middot;"), esc(CELL[c][3]))
                         for c in ("dev", "dev+", "rt", "run", "no-op", "nohw", "host", "aud", "qemu", "cache", "stub", "err", "miss", "n.a.", "?", "nob"))))
        o.append('<div class="mxctl"><div class="fg"><span class="fl">Rows</span>'
                 '<label><input type="checkbox" id="apg"> only gaps / unknown</label> '
                 '<label><input type="checkbox" id="apu"> only streamer-used</label> '
                 '<input id="apq" type="search" placeholder="Filter by function name" aria-label="Filter by function name"></div>'
                 '<div class="fg"><span class="fl">State</span>%s</div>'
                 '<div class="fg"><span class="fl">SoC</span>%s</div>'
                 '<div class="fg"><span class="fl">Area</span>%s</div>'
                 '<div class="fg"><button type="button" class="chip" id="apreset">Reset filters</button> <span class="small" id="apn" aria-live="polite"></span>'
                 ' <span class="small">States combine with SoC and area: a row shows if one selected SoC (all if none) has a selected state. Icons: '
                 '&empty; no test case yet &middot; &asymp; test ran, result unclear &middot; &#8631; not run on this SoC.</span></div></div>'
                 % ("".join('<button type="button" class="chip" data-apst="%s" aria-pressed="false">%s</button>' % (k, l) for k, l in (
                        ("works", "works (dev / run / rt)"), ("untested", "implemented, not device-tested"), ("gap", "gap"),
                        ("unknown", "unknown"), ("na", "n.a."), ("nob", "no build"))),
                    "".join('<button type="button" class="chip" data-apsoc="%s" aria-pressed="false">%s</button>' % (x, x) for x in socs),
                    "".join('<button type="button" class="chip" data-aparea="%s" aria-pressed="false">%s</button>' % (slugify(a), esc(a)) for a in AREAS)))
        for a in AREAS:
            fs = self.area_funcs(a)
            ngap = sum(1 for f in fs if any(f["cells"][s] in GAP | UNK for s in socs))
            trs = []
            for f in fs:
                gap = any(f["cells"][s] in GAP | UNK for s in socs)
                us = sorted({u for s in socs for u in self.users(f, s)})
                det = []
                if f["note"]:
                    det.append("<p>%s</p>" % esc(f["note"]))
                for s in socs:
                    if f["ref"].get(s):
                        det.append("<p><b>%s:</b> %s</p>" % (s, esc(f["ref"][s])))
                    for i in self.func_issues.get((f["name"], s), []):
                        det.append('<p><b>%s:</b> issue <a href="%s">#%d</a> (%s): %s</p>' % (s, esc(i.get("url", "")), i["number"], esc(i.get("state", "")), esc(i.get("title", ""))))
                    for w in f["wip"].get(s, []):
                        det.append("<p><b>%s &#128679;</b> %s %s</p>" % (s, ('<a href="%s">%s</a>' % (esc(w["url"]), esc(w.get("branch", "")))) if w.get("url") else esc(w.get("branch", "")), esc("since " + w.get("since", "") + (": " + w["note"] if w.get("note") else ""))))
                per = [s + ": " + self.ulabel(self.users(f, s)) for s in socs if self.users(f, s)]
                if per:
                    det.append("<p class='small'>Used by (per SoC): %s</p>" % esc("; ".join(per)))
                cells = "".join(self._cellhtml(f, s) for s in socs)
                name = ('<button type="button" class="fname" data-fn>%s</button>' % esc(f["name"])) if det else esc(f["name"])
                trs.append('<tr data-gap="%d" data-used="%d" data-n="%s"%s><td>%s</td>%s<td class="c small">%s</td></tr>' % (
                    1 if gap else 0, 1 if us else 0, esc(f["name"].lower()),
                    (' data-note="%s"' % esc(f["note"])) if f["note"] else "", name, cells, esc(self.ulabel(us))))
                if det:
                    trs.append('<tr class="det" hidden><td colspan="%d">%s</td></tr>' % (len(socs) + 2, "".join(det)))
            th = "".join("<th>%s</th>" % s for s in socs)
            o.append('<details class="full" id="api-%s"><summary>%s &mdash; %d rows (%d with a gap or unknown state)</summary>'
                     '<div class="wrap"><table class="mi apit"><thead><tr><th>Vendor function</th>%s<th>Used by</th></tr></thead><tbody>%s</tbody></table></div></details>'
                     % (slugify(a), esc(AREA_TITLE.get(a, a)), len(fs), ngap, th, "".join(trs)))
        o.append('<details class="hist" id="api-history"><summary>History of the function audit and notes</summary>%s<ul>%s</ul></details>' % (
            "".join("<p class='small'>%s</p>" % esc_md(h) for h in self.ev.get("history", [])),
            "".join("<li>%s</li>" % esc_md(n) for n in self.ev.get("general_notes", []))))
        o.append("<script>%s</script>" % API_JS)
        return "\n".join(o)

    def api_json(self):
        """per area and SoC the function lists for the click panel of the main matrix"""
        res = {}
        for a in AREAS:
            res[a] = {}
            for s in self.socs:
                d = {"ok": [], "wip": [], "gap": [], "unk": [], "nob": 0, "ws": []}
                for f in self.area_funcs(a):
                    c = f["cells"][s]
                    if c == "n.a.":
                        continue
                    if c == "nob":
                        d["nob"] += 1
                        continue
                    k = "ok" if c in OK else "wip" if c in WIPC else "gap" if c in GAP else "unk"
                    d[k].append(f["name"] + ("" if k in ("ok", "wip", "unk") else " (" + c + ")") + ("" if not self.users(f, s) else " [" + self.ulabel(self.users(f, s)) + "]" if k in ("gap", "unk") else ""))
                    if s in f["wip"]:
                        d["ws"].append(f["name"])
                res[a][s] = d
        fa = self.ev.get("feature_areas", {})
        return {"areas": res, "fa": fa, "wip": {"%s/%s" % (k[0], k[1]): [{"b": w.get("branch", ""), "s": w.get("since", ""), "n": w.get("note", "")} for w in v]
                                                 for k, v in self.wipfeat.items()}}


def esc_md(s):
    s = esc(s)
    return re.sub(r"`([^`]+)`", r"<code>\1</code>", s)


API_JS = r"""(function(){
var t=document.getElementById('api-coverage');if(!t)return;
function $$(s,r){return [].slice.call((r||document).querySelectorAll(s))}
var ST={works:['dev','dev+','run','no-op','rt'],untested:['host','aud','qemu'],gap:['cache','stub','err','miss'],unknown:['?'],na:['n.a.'],nob:['nob']};
var sel={st:{},soc:{},area:{}};
function any(o){return Object.keys(o).some(function(k){return o[k]})}
function ap(){var g=document.getElementById('apg').checked,u=document.getElementById('apu').checked,q=document.getElementById('apq').value.trim().toLowerCase();
 var socs=Object.keys(sel.soc).filter(function(k){return sel.soc[k]}),sts=Object.keys(sel.st).filter(function(k){return sel.st[k]}),n=0;
 $$('table.apit').forEach(function(tb){var det=tb.closest('details'),aid=det.id.replace('api-',''),ah=any(sel.area)&&!sel.area[aid],cnt=0;
  $$('tbody tr[data-n]',tb).forEach(function(tr){var ok=!ah;
   if(g&&tr.getAttribute('data-gap')!=='1')ok=false;if(u&&tr.getAttribute('data-used')!=='1')ok=false;
   if(q&&tr.getAttribute('data-n').indexOf(q)<0)ok=false;
   var cells=$$('td.c[data-soc]',tr);
   if(ok&&sts.length){ok=cells.some(function(c){return (!socs.length||socs.indexOf(c.getAttribute('data-soc'))>=0)&&sts.some(function(s){return ST[s].indexOf(c.getAttribute('data-code'))>=0})})}
   cells.forEach(function(c){var m=sts.length&&(!socs.length||socs.indexOf(c.getAttribute('data-soc'))>=0)&&sts.some(function(s){return ST[s].indexOf(c.getAttribute('data-code'))>=0});c.classList.toggle('dim',!!sts.length&&!m)});
   tr.hidden=!ok;var d=tr.nextElementSibling;while(d&&(d.classList.contains('det')||d.classList.contains('cdet'))){if(!ok)d.hidden=true;d=d.nextElementSibling}
   if(ok)cnt++});
  n+=cnt;det.hidden=ah;if(any(sel.area)&&!ah)det.open=true});
 document.getElementById('apn').textContent=(sts.length||socs.length||any(sel.area)||q||g||u)?n+' rows':''}
function chips(attr,key,dk){$$('['+attr+']').forEach(function(b){b.addEventListener('click',function(){var k=b.getAttribute(attr);sel[key][k]=!sel[key][k];b.setAttribute('aria-pressed',sel[key][k]?'true':'false');ap()})})}
chips('data-apst','st');chips('data-apsoc','soc');chips('data-aparea','area');
function setf(area,soc,state){sel={st:{},soc:{},area:{}};$$('.chip[data-apst],.chip[data-apsoc],.chip[data-aparea]').forEach(function(b){b.setAttribute('aria-pressed','false')});
 function on(attr,key,k){if(!k)return;sel[key][k]=true;var b=document.querySelector('['+attr+'="'+k+'"]');if(b)b.setAttribute('aria-pressed','true')}
 on('data-apst','st',state);on('data-apsoc','soc',soc);on('data-aparea','area',area);ap();
 var d=document.getElementById(area?'api-'+area:'api-isp-tuning');var c=document.getElementById('apq');(c.closest('.mxctl')||c).scrollIntoView({block:'start'})}
document.addEventListener('click',function(e){var f=e.target.closest('.flt');if(f)setf(f.getAttribute('data-area'),f.getAttribute('data-soc'),f.getAttribute('data-state'))});
document.addEventListener('keydown',function(e){if(e.key==='Enter'||e.key===' '){var f=e.target.closest&&e.target.closest('.flt');if(f){e.preventDefault();setf(f.getAttribute('data-area'),f.getAttribute('data-soc'),f.getAttribute('data-state'))}}});
document.getElementById('apreset').addEventListener('click',function(){setf('','','');document.getElementById('apq').value='';document.getElementById('apg').checked=false;document.getElementById('apu').checked=false;ap()});
['apg','apu','apq'].forEach(function(i){document.getElementById(i).addEventListener('input',ap)});
function esc(s){return String(s).replace(/[&<>"]/g,function(c){return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]})}
$$('table.apit').forEach(function(tb){
 tb.addEventListener('mouseover',function(e){var c=e.target.closest('td.c[data-code]');if(!c||c.getAttribute('data-n'))return;c.setAttribute('data-n','1');
  var n=c.parentNode.getAttribute('data-note');if(n)c.title+='\nNote: '+n});
 tb.addEventListener('click',function(e){var b=e.target.closest('[data-fn]');
  if(b){var d=b.closest('tr').nextElementSibling;if(d&&d.classList.contains('det'))d.hidden=!d.hidden;return}
  var c=e.target.closest('td.c[data-code]');if(!c)return;var tr=c.parentNode,nx=tr.nextElementSibling;
  while(nx&&nx.classList.contains('det')){nx=nx.nextElementSibling}
  if(nx&&nx.classList.contains('cdet')){var same=nx.getAttribute('data-soc')===c.getAttribute('data-soc');nx.remove();if(same)return}
  var lines=c.title.split('\n'),n=tr.getAttribute('data-note');if(n&&lines.join().indexOf('Note: ')<0)lines.push('Note: '+n);
  var h=lines.map(function(l){return '<div>'+esc(l)+'</div>'}).join(''),iss=c.getAttribute('data-issue');
  if(iss){var p=iss.split('|');h+='<div><a href="'+esc(p[1])+'">Issue #'+esc(p[0])+'</a></div>'}
  var r=document.createElement('tr');r.className='cdet';r.setAttribute('data-soc',c.getAttribute('data-soc'));
  r.innerHTML='<td colspan="'+tr.children.length+'">'+h+'</td>';tr.parentNode.insertBefore(r,nx)})});
})();
"""

API_CSS = r"""
details.prio{border:1px solid var(--line);border-radius:8px;margin:4px 0;padding:4px 10px;background:var(--card)}
details.prio>summary{cursor:pointer;font-size:.88rem}ul.pl,ol.pl{margin:.3rem 0;padding-left:1.4rem;font-size:.82rem;columns:2 22rem}
ul.pl li,ol.pl li{break-inside:avoid;margin:1px 0}details.lowp{margin:.3rem 0;font-size:.82rem}
.sym.s-ok,.sym.s-miss,.sym.s-wip,.sym.s-bug,.sym.s-plus,.sym.s-plan,.sym.s-unk,.sym.s-na{padding:0 6px;border-radius:5px}
table.apisum td.c{min-width:56px;line-height:1.15}table.apisum tr.sep td{border-top:2px solid var(--line)}
table.apit td.c{white-space:nowrap;font-size:.74rem;padding:3px 4px;text-align:center}table.apit tr.det td{background:var(--head);font-size:.8rem}
table.apit tr.det p{margin:.2rem 0}#apq{font:inherit;font-size:.85rem;padding:3px 8px;border:1px solid var(--line);border-radius:8px;background:var(--bg);color:var(--fg)}
.apx{border-top:1px solid var(--line);margin-top:8px;padding-top:6px}.apx ul{margin:.2rem 0;padding-left:1.2rem;font-size:.8rem;columns:2 16rem}
.wipmark{font-size:.8em;margin-left:3px}
td.c.s-na{opacity:.45}td.c.dim{opacity:.18}td.c[data-code]{cursor:pointer}td.c .why{font-size:.8em;opacity:.8;cursor:help}
tr.cdet td{background:var(--head);font-size:.8rem;padding:6px 10px;text-align:left;white-space:normal}tr.cdet div{margin:.1rem 0}
.flt{cursor:pointer;border-bottom:1px dotted var(--muted);white-space:nowrap}.flt:hover,.flt:focus-visible{background:var(--head);outline:none}
"""


# ------------------------------------------------------------------ md update / cli
def update_md(md_path=MD, write=True):
    """Regenerate docs/FEATURE_MATRIX.md: head from docs/status (features.yaml, history.md) + the API section."""
    head = sd.render_head(sd.load_features(), sd.load_history())
    m = Model(head)
    k = head.find("\n## Summary")
    gates = rg.Gates(m).md()
    head = head[:k + 1] + gates + "\n" + head[k + 1:] if k >= 0 else head
    out = head + m.md()
    old = open(md_path, encoding="utf-8").read() if os.path.exists(md_path) else ""
    changed = out != old
    if changed and write:
        open(md_path, "w", encoding="utf-8").write(out)
    return m, changed


def refresh_exports(build_dir):
    res = {}
    for s in json.load(open(EVID))["socs"]:
        lib = os.path.join(build_dir, s.lower(), "libimp.so")
        if s == "T10":
            lib = os.path.join(build_dir, "t20", "libimp.so")
        if not os.path.exists(lib):
            continue
        out = subprocess.run(["nm", "-D", "--defined-only", lib], capture_output=True, text=True, check=True).stdout
        res[s] = sorted({re.sub(r"^IMP_", "", l.split()[2]) for l in out.splitlines()
                         if len(l.split()) == 3 and l.split()[1] in "TWDBR" and l.split()[2].startswith("IMP_")})
    json.dump(res, open(EXPO, "w"), indent=0)
    print("wrote %s (%s)" % (EXPO, ", ".join("%s %d" % (k, len(v)) for k, v in res.items())))


FLEET_SOC = (("t10", "T10"), ("t20", "T20"), ("t21", "T21"), ("t23", "T23"), ("t30", "T30"), ("t31", "T31"), ("t40", "T40"), ("t41", "T41"))


def refresh_run(out_dir, soak_log, nm="nm"):
    """docs/api_run.json: IMP_/SU_ names the timpsd of every fleet camera imports (nm -D --undefined-only), per SoC,
    with the soak verdict of each camera (clean = 0 oops, 0 encoder-dead, every snapshot 200, timpsd always running)."""
    stat = {}
    for ln in open(soak_log):
        m = re.match(r"(\S+ \S+) \.(\d+) up=(\d+) pid=(\S+) snap=(\d+) oops=(\d+) encdead=(\d+)", ln)
        if not m:
            continue
        d = stat.setdefault(m.group(2), {"n": 0, "bad": 0, "oops": 0, "enc": 0})
        d["n"] += 1
        d["bad"] += (m.group(4) == "none" or m.group(5) != "200")
        d["oops"] = max(d["oops"], int(m.group(6)))
        d["enc"] = max(d["enc"], int(m.group(7)))
    res = {"streamer": "timps", "soak": {"image": "agg46", "log": "audit/logs1025/soak-agg46.log (main session scratch)",
                                         "window": "2026-10-25 11:38 - 2026-10-26 10:00", "samples": max(d["n"] for d in stat.values())},
           "cameras": [], "socs": {}}
    for d in sorted(os.listdir(out_dir)):
        m = re.search(r"agg46-192\.168\.10\.(\d+)$", d)
        exe = os.path.join(out_dir, d, "target", "usr", "bin", "timpsd")
        if not m or not os.path.exists(exe):
            continue
        ip = m.group(1)
        soc = next(v for k, v in FLEET_SOC if "_%s" % k in d.split("-agg46")[0] or "_%sx" % k in d or "_%sn" % k in d)
        out = subprocess.run([nm, "-D", "--undefined-only", exe], capture_output=True, text=True, check=True).stdout
        calls = {re.sub(r"^IMP_", "", x.split()[-1]) for x in out.splitlines() if re.match(r"^\s*(?:w|U)?\s*(?:IMP_|SU_)", x) or x.split()[-1:] and x.split()[-1].startswith(("IMP_", "SU_"))}
        st = stat.get(ip, {"n": 0, "bad": 0, "oops": 0, "enc": 0})
        why = []
        if st["oops"]:
            why.append("%d oops" % st["oops"])
        if st["enc"]:
            why.append("encoder dead (%d)" % st["enc"])
        if st["bad"]:
            why.append("%d samples without a streamer or snapshot" % st["bad"])
        if not st["n"]:
            why.append("not in the soak log")
        res["cameras"].append({"ip": "." + ip, "soc": soc, "dir": d, "calls": len(calls), "clean": not why, "issues": why})
        r = res["socs"].setdefault(soc, {"cameras": [], "clean": [], "calls": set()})
        r["cameras"].append("." + ip)
        if not why:
            r["clean"].append("." + ip)
            r["calls"] |= calls
    for soc, r in list(res["socs"].items()):
        r["calls"] = sorted(r["calls"])
        if not r["clean"]:
            del res["socs"][soc]
    json.dump(res, open(RUN, "w"), indent=1)
    print("wrote %s: %s" % (RUN, ", ".join("%s %d calls from %s" % (k, len(v["calls"]), "+".join(v["clean"])) for k, v in res["socs"].items())))
    print("cameras not clean: %s" % "; ".join("%s %s (%s)" % (c["ip"], c["soc"], ", ".join(c["issues"])) for c in res["cameras"] if not c["clean"]))


def print_counts(m):
    print("API coverage: %d rows; works/total per SoC (device-tested or vendor no-op / vendor functions):" % len(m.funcs))
    for s in m.socs:
        st = m.stat(m.funcs, s)
        print("  %s: %d/%d works (%d of them rt = API round trip only), %d implemented not device-tested, %d gaps, %d unknown%s%s" % (
            s, st["ok"], st["total"], st["rt"], st["wip"], st["gap"], st["unk"],
            ", %d no-build" % st["nob"] if st["nob"] else "", ", %d in progress" % st["ws"] if st["ws"] else ""))


def main():
    a = sys.argv[1:]
    if "--refresh-exports" in a:
        k = a.index("--refresh-exports")
        refresh_exports(a[k + 1] if len(a) > k + 1 else os.path.join(ROOT, "build"))
    if "--refresh-run" in a:
        k = a.index("--refresh-run")
        refresh_run(a[k + 1], a[k + 2])
    m, changed = update_md(write="--check" not in a)
    for w in m.warnings:
        sys.stderr.write("warning: %s\n" % w)
    print_counts(m)
    if "--check" in a:
        if changed:
            sys.stderr.write("warning: FEATURE_MATRIX.md API section is out of date (run without --check)\n")
        sys.exit(1 if (changed or m.warnings) else 0)
    print("%s %s" % (MD, "updated" if changed else "unchanged"))


if __name__ == "__main__":
    main()
