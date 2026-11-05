#!/usr/bin/env python3
"""Generate the API reference pages of the OpenIMP wiki (ISP, FrameSource, Encoder, OSD, IVS, Audio, DMIC, System).

Inputs (all in this repository):
  include/imp/*.h, include/openimp/openimp_tuning.h   signatures (the API itself)
  docs/api_evidence.json                              per function and SoC: status cell, "better than vendor", streamers
  docs/api_exports.json                               symbols exported by libimp per SoC
  docs/api_reference/<module>.yaml                    hand-written descriptions (the single place for prose)
  docs/OPENIMP_BEYOND_VENDOR.md                       (or a wiki checkout's Beyond-the-Vendor.md) for "see also" links

Output (into --out DIR, normally a wiki checkout):
  API-Reference.md  API-ISP.md  API-FrameSource.md  API-Encoder.md  API-OSD.md  API-IVS.md  API-Audio.md
  API-DMIC.md  API-System.md

Usage:
  python3 tools/gen_api_wiki.py --out ../openimp.wiki      write the pages
  python3 tools/gen_api_wiki.py --check                    list exported functions without yaml entry (warning only, exit 0)
  python3 tools/gen_api_wiki.py --check --strict           the same, exit 1 when something is missing

yaml format (docs/api_reference/<module>.yaml):
  title: ...
  intro: |  markdown
  groups: [{id: x, title: ..., intro: ...}]     # page order
  functions:
    ISP_Tuning_Get/SetContrast:     # evidence row name; Get/Set pairs are one entry
      group: x
      summary: one or two sentences, own words
      params: {name: text}           # optional
      returns: text                  # optional, default "0 on success, -1 on error."
      notes: text                    # optional: typical call order, ranges, pitfalls
      diff: text                     # optional: how OpenIMP differs from the vendor library
      soc: text                      # optional: SoC-specific marker, overrides the automatic one
      decl: text                     # optional: C declaration(s) when the function is not in an OpenIMP header
      unverified: true               # optional: description not yet checked against src/ (page shows a note)
      see_also: [ISP_Tuning_GetBrightness]
Descriptions are written in our own words; the vendor documentation is only read to understand semantics.
"""
import argparse
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOCS = ["T10", "T20", "T21", "T23", "T30", "T31", "T40", "T41"]
MODULES = [  # key, page, yaml, headers, title, export prefixes
    ("ISP", "API-ISP", "isp.yaml", ["imp_isp.h"], "ISP (including ISP tuning)", ["ISP_"]),
    ("FrameSource", "API-FrameSource", "framesource.yaml", ["imp_framesource.h"], "FrameSource", ["FrameSource_"]),
    ("Encoder", "API-Encoder", "encoder.yaml", ["imp_encoder.h"], "Encoder", ["Encoder_"]),
    ("OSD", "API-OSD", "osd.yaml", ["imp_osd.h"], "OSD", ["OSD_", "ISPOSD_"]),
    ("IVS", "API-IVS", "ivs.yaml", ["imp_ivs.h", "imp_ivs_move.h", "imp_ivs_base_move.h"], "IVS", ["IVS_"]),
    ("Audio", "API-Audio", "audio.yaml", ["imp_audio.h"], "Audio (AI, AO, AENC, ADEC)", ["AI_", "AO_", "AENC_", "ADEC_", "Audio_"]),
    ("DMIC", "API-DMIC", "dmic.yaml", ["imp_dmic.h"], "DMIC", ["DMIC_"]),
    ("System", "API-System", "system.yaml", ["imp_system.h"], "System", ["System_", "MemPool_", "Log_", "Decoder_", "SU_"]),
]
EXTRA_HEADERS = ["openimp/openimp_tuning.h"]
MATRIX_URL = "https://lu-fi.github.io/openimp/feature-matrix.html"
STREAMERS = {"p": "prudynt", "r": "raptor", "t": "timps"}
# same symbols as the "API coverage" tables of the feature matrix (tools/gen_api_coverage.py)
CELL = {
    "dev": ("✅", "works, device-tested"),
    "dev+": ("✅+", "works, device-tested, better than the vendor"),
    "rt": ("✅rt", "API round trip on device, no visible effect expected"),
    "run": ("✅r", "works: called by the streamer on fleet cameras of this SoC that passed the fleet soak"),
    "no-op": ("✅○", "vendor no-op, OpenIMP matches that"),
    "nohw": ("✅?", "counted as done by user decision, not verified on hardware (the lab board lacks the hardware)"),
    "qemu": ("🔧+", "API round trip in QEMU, no camera"),
    "host": ("🔧h", "implemented, host tests only (no camera run yet)"),
    "aud": ("🔧a", "really connected per static audit, no device test tracked"),
    "cache": ("⚠c", "value only stored and read back, nothing applied"),
    "stub": ("⚠s", "returns 0 without any effect"),
    "err": ("❌e", "exported but fails or has a known defect"),
    "miss": ("❌", "not exported by OpenIMP"),
    "n.a.": ("·", "function does not exist on this SoC (not in that SoC's vendor API; not a gap)"),
    "?": ("?", "state not determined"),
    "nob": ("📋", "no OpenIMP build for this SoC yet (planned)"),
}
DEFAULT_RET = "0 on success, non-zero on error (mostly -1; T40 and T41 return their own error codes for some failures)."


def load_json(name):
    with open(os.path.join(ROOT, "docs", name)) as f:
        return json.load(f)


def norm(n):
    """exported symbol -> base name: second-sensor suffix and multi-camera infix removed"""
    return re.sub(r"_Sec$", "", n).replace("MultiCamera_", "")


def pair_key(n):
    m = re.match(r"^(.*?_)(Get|Set)(.*)$", n)
    return m.group(1) + "Get/Set" + m.group(3) if m else None


def row_key(n, rows, alias, known=()):
    """exported symbol -> row key (yaml entry name, else evidence row name)"""
    b = norm(n)
    g0 = pair_key(b)
    if b in known:
        return b
    if g0 and g0 in known:
        return g0
    if b in rows:
        return b
    g = pair_key(b)
    if g and g in rows:
        return g
    for a, c in (("_Get", "_Set"), ("_Set", "_Get")):     # Get-only export whose row is named after the Set
        if a in b:
            o = b.replace(a, c, 1)
            if o in rows:
                return o
    if b in alias:
        return alias[b]
    return b


def expand(key):
    """row key -> list of API names (without IMP_)"""
    m = re.match(r"^(.*?_)Get/Set(.*)$", key)
    if m:
        return [m.group(1) + "Get" + m.group(2), m.group(1) + "Set" + m.group(2)]
    return [key]


def read_decls():
    out = {}
    files = ["imp/" + h for m in MODULES for h in m[3]] + EXTRA_HEADERS
    for f in files:
        p = os.path.join(ROOT, "include", f)
        if not os.path.exists(p):
            continue
        t = open(p).read()
        t = re.sub(r"/\*.*?\*/", "", t, flags=re.S)
        t = re.sub(r"//.*", "", t)
        for m in re.finditer(r"(?m)^[ \t]*([A-Za-z_][\w \t\*]*?\b(IMP_\w+)\s*\([^;{}]*?\))\s*;", t, flags=re.S):
            out.setdefault(m.group(2)[4:], " ".join(m.group(1).split()) + ";")
    return out


def load_yaml(path):
    try:
        import yaml
    except ImportError:
        sys.exit("gen_api_wiki: needs PyYAML (python3-yaml / pip install pyyaml)")
    if not os.path.exists(path):
        return {}
    with open(path) as f:
        return yaml.safe_load(f)


def slug(h):
    return re.sub(r"[^\w\- ]", "", h.lower()).strip().replace(" ", "-")


def beyond_index(path):
    """function name -> [(heading, anchor)] from the Beyond-the-Vendor document"""
    if not path or not os.path.exists(path):
        return {}
    idx, head = {}, None
    for ln in open(path, encoding="utf-8"):
        m = re.match(r"^#{2,4}\s+(.*)", ln)
        if m:
            head = m.group(1).strip()
            continue
        if head:
            for n in set(re.findall(r"IMP_(?:ISP|FrameSource|Encoder|OSD|IVS|AI|AO|AENC|ADEC|DMIC|System|Log)_\w+", ln)):
                idx.setdefault(n[4:], set()).add(head)
    return {k: sorted((h, slug(h)) for h in v) for k, v in idx.items()}


class Data:
    def __init__(self, beyond_path):
        ev = load_json("api_evidence.json")
        self.asof = ev.get("asof", "")
        self.rows = ev["functions"]
        self.alias = {a: k for k, v in self.rows.items() for a in v.get("alias", [])}
        self.exports = load_json("api_exports.json")
        self.decls = read_decls()
        self.beyond = beyond_index(beyond_path)
        self.mods = []
        self.known = set()
        for key, page, yf, _hdr, title, _pfx in MODULES:
            y = load_yaml(os.path.join(ROOT, "docs", "api_reference", yf)) or {}
            self.mods.append((key, page, title, y, _pfx))
            self.known |= set(y.get("functions") or {})

    def row_for(self, key):
        """evidence row of a yaml entry; a Get/Set entry whose evidence is split into two rows uses the Set row"""
        if key in self.rows:
            return self.rows[key]
        for n in reversed(expand(key)):
            if n in self.rows:
                return self.rows[n]
        for rk, row in self.rows.items():     # wildcard rows such as "DMIC_*" or "AENC_* / ADEC_*"
            if "*" in rk and any(p.strip().endswith("_*") and key.startswith(p.strip()[:-1]) for p in rk.split(" / ")):
                return row
        return None

    def exported_keys(self, prefixes):
        keys = {}
        for _soc, names in self.exports.items():
            for n in names:
                if n.startswith(tuple(prefixes)):
                    keys.setdefault(row_key(n, self.rows, self.alias, self.known), set()).add(n)
        return keys

    def missing(self):
        out = {}
        for key, _p, _t, y, pfx in self.mods:
            have = set((y.get("functions") or {}))
            out[key] = sorted(k for k in self.exported_keys(pfx) if k not in have)
        return out


def cell_sym(row, soc):
    c = row["cells"].get(soc, "?")
    if c == "dev" and soc in row.get("better", []):
        c = "dev+"
    return CELL.get(c, ("?", ""))[0]


def shorten_ref(r, limit=260):
    """evidence reference text; a very long one is cut at the last ';' or ',' before the limit"""
    r = " ".join(r.split())
    if len(r) <= limit:
        return r
    cut = max(r.rfind("; ", 0, limit), r.rfind(", ", 0, limit))
    return r[:cut if cut > limit // 2 else limit].rstrip(" ,;") + " ..."


def cname(key):
    """C name of a yaml entry heading: IMP_<key>, except for SU_ functions"""
    return key if key.startswith("SU_") else "IMP_" + key


def render_function(d, key, ent, expo):
    row = d.row_for(key)
    names = sorted(set(expand(key)) | {norm(n) for n in expo if norm(n) not in d.alias and not n.endswith("_Sec")})
    if ent.get("decl"):
        names = []
    o = ["### " + cname(key), "", "```c"]
    if ent.get("decl"):
        o += ent["decl"].strip().splitlines()
    else:
        for n in names:
            o.append(d.decls.get(n, "/* IMP_%s: exported by libimp; declared in the vendor SDK header, not in the OpenIMP include directory */" % n))
    o += ["```", ""]
    if ent.get("summary"):
        o += [ent["summary"].strip(), ""]
    if ent.get("params"):
        o += ["**Parameters**", ""]
        for k, v in ent["params"].items():
            o.append("- `%s`: %s" % (k, str(v).strip()))
        o.append("")
    o += ["**Returns:** " + str(ent.get("returns", DEFAULT_RET)).strip(), ""]
    if ent.get("notes"):
        o += [ent["notes"].strip(), ""]
    if ent.get("unverified"):
        o += ["Details not yet verified against the implementation.", ""]
    soc = ent.get("soc")
    if not soc and row:
        have = [s for s in SOCS if row["cells"].get(s) not in ("n.a.", "nob", None)]
        na = [s for s in SOCS if row["cells"].get(s) == "n.a."]
        if have and na:
            soc = ("only in the vendor API of " + ", ".join(have)) if len(have) <= 4 else \
                  ("not in the vendor API of " + ", ".join(na))
    if soc:
        o += ["**SoC:** " + str(soc).strip(), ""]
    if row:
        o += ["| " + " | ".join(SOCS) + " |", "|" + "---|" * len(SOCS),
              "| " + " | ".join(cell_sym(row, s) for s in SOCS) + " |", ""]
        refs = [(s, row["ref"][s]) for s in SOCS if row.get("ref", {}).get(s)]
        if refs:
            o += ["Tested (tool, date, camera, what was checked):", ""]
            o += ["- **%s**: %s" % (s, shorten_ref(r)) for s, r in refs]
            o.append("")
        us = sorted({STREAMERS[u[0]] for s in SOCS for u in row.get("used", {}).get(s, []) if u and u[0] in STREAMERS})
        if us:
            o += ["Used by: " + ", ".join(us) + ".", ""]
    if any(n.endswith("_Sec") or "MultiCamera_" in n for n in expo):
        o += ["Multi-sensor variants (T23): `_Sec` suffix (second sensor) and `ISP_MultiCamera_` prefix.", ""]
    diff = []
    if ent.get("diff"):
        diff.append(ent["diff"].strip())
    if row and row.get("better"):
        diff.append("Better than the vendor on " + ", ".join(row["better"]) + ".")
    bl = []
    for n in names:
        for hb in d.beyond.get(n, []):
            if hb not in bl:
                bl.append(hb)
    if diff or bl:
        o.append("**Difference from the vendor library:** " + " ".join(diff))
        if bl:
            o.append("See [Beyond the Vendor](Beyond-the-Vendor): " + ", ".join(
                "[%s](Beyond-the-Vendor#%s)" % (h.replace("|", "/"), a) for h, a in bl[:3]) + ".")
        o.append("")
    if ent.get("see_also"):
        o += ["See also: " + ", ".join("[`%s`](#%s)" % (cname(s), slug(cname(s))) for s in ent["see_also"]), ""]
    return o


def legend():
    o = ["Status symbols are those of the [feature matrix](%s) (API coverage tables):" % MATRIX_URL, ""]
    for c in ("dev", "dev+", "rt", "run", "no-op", "nohw", "host", "aud", "qemu", "cache", "stub", "err", "miss", "n.a.", "?", "nob"):
        o.append("- `%s` %s" % (CELL[c][0], CELL[c][1]))
    return o + [""]


def render_page(d, mod):
    key, _page, title, y, pfx = mod
    funcs = y.get("functions") or {}
    expo = d.exported_keys(pfx)
    allkeys = set(funcs) | set(expo)
    groups = list(y.get("groups") or [])
    gids = {g["id"] for g in groups}
    if any(funcs.get(k, {}).get("group") not in gids for k in allkeys):
        groups.append({"id": "_other", "title": "Other exported functions",
                       "intro": "Exported by libimp; no description written yet."})
    o = ["# API reference: " + title, "", y.get("intro", "").strip(), "",
         "Status columns: %s. As of %s. Part of the [API reference](API-Reference)." % (" ".join(SOCS), d.asof), "",
         "## Contents", ""]
    bygroup = {}
    for k in allkeys:
        g = funcs.get(k, {}).get("group")
        bygroup.setdefault(g if g in gids else "_other", []).append(k)
    for g in groups:
        if bygroup.get(g["id"]):
            o.append("- [%s](#%s) (%d)" % (g["title"], slug(g["title"]), len(bygroup[g["id"]])))
    o.append("")
    o += legend()
    order = {k: i for i, k in enumerate(funcs)}
    for g in groups:
        ks = bygroup.get(g["id"], [])
        if not ks:
            continue
        o += ["## " + g["title"], ""]
        if g.get("intro"):
            o += [g["intro"].strip(), ""]
        for k in sorted(ks, key=lambda k: (order.get(k, 10**6), k)):
            o += render_function(d, k, funcs.get(k, {}), expo.get(k, set()))
    return "\n".join(o).rstrip() + "\n", len(allkeys), sum(1 for k in allkeys if k in funcs)


def render_index(d, counts):
    o = ["# API reference", "",
         "Reference of the public OpenIMP API for people who build streamers and tools on top of it. The functions have the "
         "signatures of the vendor SDK; each entry says what the call does in OpenIMP, which values it accepts, what it returns, "
         "how it is usually combined with other calls, and on which SoCs it has been tested.", "",
         "| Module | Entries | Described |", "|---|---|---|"]
    for key, page, title, _y, _p in d.mods:
        n, h = counts[key]
        o.append("| [%s](%s) | %d | %d |" % (title, page, n, h))
    o += ["", "One entry covers a Get/Set pair. Entries that are marked \"not yet verified\" are described from the function name and a quick look at the implementation; they are being checked one by one.", ""]
    o += legend()
    o.append("The pages are generated from the OpenIMP sources (`tools/gen_api_wiki.py`); the descriptions live in "
             "`docs/api_reference/*.yaml`. Differences from the vendor library that go beyond a single call are collected in "
             "[Beyond the Vendor](Beyond-the-Vendor).")
    return "\n".join(o) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", help="directory (wiki checkout) to write the pages into")
    ap.add_argument("--check", action="store_true", help="report exported functions without yaml entry")
    ap.add_argument("--strict", action="store_true", help="with --check: exit 1 when entries are missing")
    ap.add_argument("--beyond", help="Beyond-the-Vendor markdown (default: <out>/Beyond-the-Vendor.md, else docs/OPENIMP_BEYOND_VENDOR.md)")
    a = ap.parse_args()
    bp = a.beyond
    if not bp:
        bp = os.path.join(a.out, "Beyond-the-Vendor.md") if a.out and os.path.exists(os.path.join(a.out, "Beyond-the-Vendor.md")) \
            else os.path.join(ROOT, "docs", "OPENIMP_BEYOND_VENDOR.md")
    d = Data(bp)
    if a.check or not a.out:
        miss = d.missing()
        tot = 0
        for m, ks in miss.items():
            tot += len(ks)
            if ks:
                print("warning: API reference %s: %d exported functions without yaml entry in docs/api_reference/: %s%s" % (
                    m, len(ks), ", ".join(ks[:8]), " ..." if len(ks) > 8 else ""))
        if not tot:
            print("API reference: every exported function of the covered modules (%s) has a yaml entry" % ", ".join(m[0] for m in d.mods))
        if not a.out:
            return 1 if (tot and a.strict) else 0
    counts = {}
    os.makedirs(a.out, exist_ok=True)
    for mod in d.mods:
        text, n, h = render_page(d, mod)
        counts[mod[0]] = (n, h)
        with open(os.path.join(a.out, mod[1] + ".md"), "w", encoding="utf-8") as f:
            f.write(text)
    with open(os.path.join(a.out, "API-Reference.md"), "w", encoding="utf-8") as f:
        f.write(render_index(d, counts))
    for k, (n, h) in counts.items():
        print("%s: %d entries, %d described" % (k, n, h))
    return 0


if __name__ == "__main__":
    sys.exit(main())
