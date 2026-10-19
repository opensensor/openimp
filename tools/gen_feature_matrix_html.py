#!/usr/bin/env python3
"""Generate docs/feature-matrix.html from docs/FEATURE_MATRIX.md.

    python3 tools/gen_feature_matrix_html.py [in.md [out.html]]

Python 3 standard library only (no markdown module): the small subset of
Markdown used by FEATURE_MATRIX.md is parsed here (headings, paragraphs,
bullet/numbered lists, pipe tables, code fences, <details> blocks, inline
code/bold/italic/links).  The look (style block, legend, sticky table, light
and dark tokens) is the same as the former hand-made HTML.

Special handling:
  * "## Matrix" table + "### Matrix details" bullets are merged into one
    table (vendor column + one cell per SoC, class s-ok/s-plus/... from the
    symbol in the table); extra text of a detail section goes into a
    <details id="slug"> block below the table.
  * Function tables (first column "Vendor function") get cell classes from
    the cell codes (dev, host, aud, ...), data-gap rows and a "only rows with
    gaps" filter.
  * "Progress per SoC" gets a bar column computed from the counts.
"""
import html
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, "docs", "FEATURE_MATRIX.md")
DST = os.path.join(ROOT, "docs", "feature-matrix.html")

CSS = r"""
:root{--bg:#f6f7f9;--fg:#1c2128;--muted:#5b6573;--card:#fff;--line:#d9dee5;--head:#eef1f5;--accent:#2457c5;
--ok:#e3f4e8;--plus:#cdeed8;--bug:#fde8c8;--wip:#dfe8fb;--plan:#ebe4f7;--miss:#fbdada;--na:#eceff2;--unk:#f4f4f4;}
@media (prefers-color-scheme:dark){:root:not([data-theme="light"]){--bg:#14171c;--fg:#e6e9ee;--muted:#9aa4b2;--card:#1c2027;--line:#333a45;--head:#232932;--accent:#8fb2ff;
--ok:#1d3326;--plus:#18432b;--bug:#4a3818;--wip:#1f2d4d;--plan:#34294d;--miss:#4a2323;--na:#262b32;--unk:#22262c;}}
:root[data-theme="dark"]{--bg:#14171c;--fg:#e6e9ee;--muted:#9aa4b2;--card:#1c2027;--line:#333a45;--head:#232932;--accent:#8fb2ff;
--ok:#1d3326;--plus:#18432b;--bug:#4a3818;--wip:#1f2d4d;--plan:#34294d;--miss:#4a2323;--na:#262b32;--unk:#22262c;}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
main{max-width:none;margin:0 auto;padding:16px}
h1{font-size:1.45rem;margin:.2rem 0 .1rem}h2{font-size:1.1rem;margin:1.4rem 0 .5rem}
h3{font-size:1rem;margin:1.1rem 0 .4rem}h4{font-size:.92rem;margin:.9rem 0 .3rem;color:var(--accent)}
.meta{color:var(--muted);font-size:.85rem;margin:0 0 .8rem}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:12px 14px;margin:0 0 12px}
.card ul{margin:.3rem 0 0;padding-left:1.1rem}.card li{margin:.25rem 0}
.legend{display:flex;flex-wrap:wrap;gap:8px}
.legend span{display:inline-flex;align-items:center;gap:6px;border:1px solid var(--line);border-radius:8px;padding:3px 8px;font-size:.85rem}
.legend small{color:var(--muted)}
.wrap{overflow-x:auto;border:1px solid var(--line);border-radius:10px;background:var(--card);max-width:100%}
table{border-collapse:separate;border-spacing:0;width:100%;table-layout:fixed;font-size:.8rem}
th,td{padding:6px 8px;border-bottom:1px solid var(--line);border-right:1px solid var(--line);vertical-align:top;text-align:left}
thead th{position:sticky;top:0;background:var(--head);z-index:3}
th:first-child,td:first-child{position:sticky;left:0;z-index:2;background:var(--card);width:11%;font-weight:600}
thead th:first-child{z-index:4;background:var(--head)}
td.vend{color:var(--muted);font-size:.76rem}th:nth-child(2){width:10%}td{overflow-wrap:anywhere}@media (max-width:1100px){table{width:max-content;table-layout:auto}th:first-child,td:first-child{min-width:140px;max-width:140px}td.cellc,td[class^="s-"]{min-width:165px;max-width:200px}}

tr.grp td{background:var(--head);font-weight:700;color:var(--accent);position:sticky;left:0}
.sym{display:inline-block;margin-right:5px;font-weight:700;white-space:nowrap}
.s-ok{background:var(--ok)}.s-plus{background:var(--plus)}.s-bug{background:var(--bug)}.s-wip{background:var(--wip)}
.s-plan{background:var(--plan)}.s-miss{background:var(--miss)}.s-na{background:var(--na)}.s-unk{background:var(--unk)}
td:first-child a{color:inherit;text-decoration:none;border-bottom:1px dotted var(--muted)}
details{border-top:1px solid var(--line);padding:6px 0}details summary{cursor:pointer;font-weight:600}
details p{margin:.4rem 0 .2rem;color:var(--muted)}
.small{font-size:.82rem;color:var(--muted)}
th small{font-weight:400;color:var(--muted)}
table.mi{table-layout:auto;width:100%}table.mi th:nth-child(2){width:auto}table.mi td:first-child{width:auto;min-width:16rem;font-weight:400}table.mi td.nt{font-size:.74rem;color:var(--muted)}table.mi td.c{text-align:center;white-space:nowrap}.lg{display:flex;flex-wrap:wrap;gap:3px 10px;font-size:.74rem;color:var(--muted);margin:.35rem 0 .5rem}.lg b{color:var(--fg);padding:0 6px;border-radius:5px;border:1px solid var(--line);font-weight:600}.lg b.sy{background:var(--card)}.pb{display:flex;height:14px;min-width:240px;border:1px solid var(--line);border-radius:4px;overflow:hidden;background:var(--na)}.pb i{display:block;height:100%}.b-dev{background:#2e9e5b}.b-host{background:#4f7fd9}.b-q{background:#a9b1bd}.b-noop{background:#9b82d0}.b-gap{background:#d9534f}details.full{border:1px solid var(--line);border-radius:8px;padding:6px 10px;margin:.4rem 0 1rem;background:var(--card)}details.full>summary{font-weight:600}details.full label{font-size:.8rem;color:var(--muted);margin-left:1rem}
td.cellc{overflow-wrap:anywhere}pre{overflow-x:auto;background:var(--head);padding:8px;border-radius:6px;font-size:.8rem}
.card ol,details ol{padding-left:1.4rem}details li{margin:.2rem 0}
"""

# status symbol -> (css class, title)
SYM = {
    "✅": ("s-ok", "supported"),
    "✅+": ("s-plus", "improved beyond vendor"),
    "⚠": ("s-bug", "known defect / deviation"),
    "🔧": ("s-wip", "in progress"),
    "📋": ("s-plan", "planned"),
    "❌": ("s-miss", "missing"),
    "—": ("s-na", "hardware does not have it"),
    "?": ("s-unk", "unknown"),
}
SYM_RE = re.compile(r"^(✅\+|✅|⚠️?|🔧|📋|❌|—|\?)(?:\s+|$)")

# function-table cell code -> (class, long label)
CODE = {
    "dev": ("s-ok", "done (dev)"),
    "host": ("s-wip", "done (host)"),
    "aud": ("s-unk", "done (?)"),
    "no-op": ("s-plan", "vendor no-op"),
    "cache": ("s-bug", "cache-only"),
    "stub": ("s-bug", "stub"),
    "err": ("s-miss", "error"),
    "miss": ("s-miss", "missing"),
    "n.a.": ("s-na", "n.a."),
    "?": ("s-unk", "?"),
}
GAP_CODES = {"cache", "stub", "err", "miss"}


def norm_sym(s):
    return s.replace("️", "")


def esc(s):
    return html.escape(s, quote=True)


SEEN_IDS = set()


def uid(t):
    """unique anchor id for a heading (second 'Legend' becomes legend-1)"""
    b = slug(t)
    i, c = 0, b
    while c in SEEN_IDS:
        i += 1
        c = "%s-%d" % (b, i)
    SEEN_IDS.add(c)
    return c


def slug(t):
    """GitHub style heading anchor."""
    t = re.sub(r"[`*]", "", t).strip().lower()
    t = re.sub(r"[^\w\- ]", "", t)
    return t.replace(" ", "-")


# ---------------------------------------------------------------- inline
def inline(s):
    codes = []

    def keep(m):
        codes.append("<code>%s</code>" % esc(m.group(2)))
        return "\x00%d\x00" % (len(codes) - 1)

    s = re.sub(r"(`+)(.+?)\1", keep, s)
    s = esc(s)
    s = re.sub(r"\[([^\]]+)\]\(([^)\s]+)\)",
               lambda m: '<a href="%s">%s</a>' % (m.group(2), m.group(1)), s)
    s = re.sub(r"\*\*(.+?)\*\*", r"<b>\1</b>", s)
    s = re.sub(r"(?<![\w*])\*(?![\s*])([^*\n]+?)(?<![\s*])\*(?![\w*])", r"<i>\1</i>", s)
    return re.sub(r"\x00(\d+)\x00", lambda m: codes[int(m.group(1))], s)


# ---------------------------------------------------------------- block parse
def parse_blocks(lines):
    """-> list of (type, data).  Types: h(level,text) p(text) ul(items) ol(items)
    table(rows,aligns) code(text) rawopen(summary) rawclose"""
    out = []
    i, n = 0, len(lines)
    while i < n:
        ln = lines[i]
        if not ln.strip():
            i += 1
            continue
        m = re.match(r"^(#{1,6})\s+(.*)$", ln)
        if m:
            out.append(("h", (len(m.group(1)), m.group(2).strip())))
            i += 1
            continue
        if ln.startswith("```"):
            j = i + 1
            buf = []
            while j < n and not lines[j].startswith("```"):
                buf.append(lines[j])
                j += 1
            out.append(("code", "\n".join(buf)))
            i = j + 1
            continue
        m = re.match(r"^<details[^>]*>(?:<summary>(.*?)</summary>)?\s*$", ln)
        if m:
            out.append(("rawopen", m.group(1)))
            i += 1
            continue
        if ln.strip() == "</details>":
            out.append(("rawclose", None))
            i += 1
            continue
        if ln.startswith("|"):
            rows = []
            while i < n and lines[i].startswith("|"):
                rows.append(split_row(lines[i]))
                i += 1
            aligns = []
            if len(rows) > 1 and all(re.fullmatch(r":?-+:?", c.strip()) for c in rows[1]):
                aligns = ["r" if c.strip().endswith(":") else "" for c in rows[1]]
                del rows[1]
            out.append(("table", (rows, aligns)))
            continue
        if re.match(r"^\s*(?:[-*]\s+|\d+\.\s+)", ln):
            items = []
            ordered = bool(re.match(r"^\s*\d+\.\s", ln))
            while i < n:
                ln = lines[i]
                mm = re.match(r"^(\s*)(?:([-*])|(\d+)\.)\s+(.*)$", ln)
                if mm:
                    items.append([len(mm.group(1)), mm.group(3), mm.group(4)])
                    i += 1
                elif ln.strip() and ln.startswith(" ") and items:
                    items[-1][2] += " " + ln.strip()
                    i += 1
                elif not ln.strip() and i + 1 < n and re.match(r"^\s*(?:[-*]\s+|\d+\.\s+)", lines[i + 1]) \
                        and bool(re.match(r"^\s*\d+\.", lines[i + 1])) == ordered:
                    i += 1
                else:
                    break
            out.append(("ol" if ordered else "ul", items))
            continue
        buf = []
        while i < n and lines[i].strip() and not re.match(
                r"^(#{1,6}\s|```|\||<details|</details>|\s*[-*]\s+|\s*\d+\.\s+)", lines[i]):
            buf.append(lines[i].strip())
            i += 1
        if not buf:  # safety: never loop
            buf = [lines[i].strip()]
            i += 1
        out.append(("p", " ".join(buf)))
    return out


def split_row(ln):
    ln = ln.strip()
    if ln.startswith("|"):
        ln = ln[1:]
    if ln.endswith("|") and not ln.endswith("\\|"):
        ln = ln[:-1]
    cells, cur, k = [], "", 0
    while k < len(ln):
        c = ln[k]
        if c == "\\" and k + 1 < len(ln) and ln[k + 1] == "|":
            cur += "|"
            k += 2
            continue
        if c == "|":
            cells.append(cur.strip())
            cur = ""
        else:
            cur += c
        k += 1
    cells.append(cur.strip())
    return cells


# ---------------------------------------------------------------- renderers
def render_list(kind, items):
    """items: [indent, num, text]; nested by indent."""
    out = []
    stack = []  # indents

    def open_(ind, it):
        out.append("<%s>" % kind)
        stack.append(ind)

    for ind, num, text in items:
        if not stack:
            open_(ind, text)
        elif ind > stack[-1]:
            out.append("<%s>" % kind)
            stack.append(ind)
        else:
            out.append("</li>")
            while len(stack) > 1 and ind < stack[-1]:
                out.append("</%s></li>" % kind)
                stack.pop()
        val = ' value="%s"' % num if (kind == "ol" and num) else ""
        out.append("<li%s>%s" % (val, inline(text)))
    out.append("</li>")
    while len(stack) > 1:
        out.append("</%s></li>" % kind)
        stack.pop()
    out.append("</%s>" % kind)
    return "".join(out)


def legend_item(text):
    """bullet of a legend -> <span class=..>"""
    m = SYM_RE.match(text)
    if m:
        sym = norm_sym(m.group(1))
        cls = SYM[sym][0]
    else:
        m2 = re.match(r"^\*\*(.+?)\*\*", text)
        key = m2.group(1) if m2 else ""
        cls = CODE.get(key, ("s-na",))[0]
    mm = re.match(r"^(\*\*.+?\*\*|\S+)(.*)$", text)
    head, rest = mm.group(1), mm.group(2)
    if ": " in rest and not rest.lstrip().startswith("("):
        a, b = rest.split(": ", 1)
        return '<span class="%s">%s%s <small>%s</small></span>' % (
            cls, "<b>%s</b>" % inline(head) if not head.startswith("**") else inline(head),
            inline(a), inline(b))
    return '<span class="%s">%s%s</span>' % (cls, inline(head), inline(rest))


def table_wrap(inner):
    return '<div class="wrap">%s</div>' % inner


def render_generic_table(rows, aligns):
    head, body = rows[0], rows[1:]
    h = "".join("<th>%s</th>" % inline(c) for c in head)
    b = []
    for r in body:
        tds = []
        for k, c in enumerate(r):
            cl = ' class="c"' if k < len(aligns) and aligns[k] == "r" else ""
            tds.append("<td%s>%s</td>" % (cl, inline(c)))
        b.append("<tr>%s</tr>" % "".join(tds))
    return table_wrap('<table class="mi"><thead><tr>%s</tr></thead><tbody>%s</tbody></table>'
                      % (h, "".join(b)))


def num(x):
    try:
        return float(x.replace("*", "").replace("%", "").strip())
    except ValueError:
        return None


def render_progress(rows, aligns):
    head, body = rows[0], rows[1:]
    idx = {h.replace("*", "").strip(): k for k, h in enumerate(head)}
    out = ['<div class="lg"><span><b class="b-dev" style="color:#fff">done (dev)</b></span>'
           '<span><b class="b-host" style="color:#fff">done (host)</b></span>'
           '<span><b class="b-q" style="color:#fff">done (?) / ?</b></span>'
           '<span><b class="b-noop" style="color:#fff">vendor no-op</b></span>'
           '<span><b class="b-gap" style="color:#fff">gaps</b></span></div>']
    th = "".join("<th>%s</th>" % inline(c) for c in [head[0], "progress"] + head[1:])
    trs = []
    for r in body:
        g = lambda k: num(r[idx[k]]) or 0
        fns = g("fns") or 1
        parts = [("b-dev", "done (dev)", g("dev")), ("b-host", "done (host)", g("host")),
                 ("b-q", "done (?) / ?", g("aud") + g("?")),
                 ("b-noop", "vendor no-op", g("no-op")), ("b-gap", "gaps", g("gaps"))]
        bar = "".join('<i class="%s" style="width:%.3f%%" title="%s: %d"></i>'
                      % (c, 100.0 * v / fns, t, v) for c, t, v in parts if v)
        tds = ["<td>%s</td>" % inline(r[0]), '<td><div class="pb">%s</div></td>' % bar]
        for k, c in enumerate(r[1:], 1):
            tds.append('<td class="c">%s</td>' % inline(c))
        trs.append("<tr>%s</tr>" % "".join(tds))
    out.append(table_wrap('<table class="mi"><thead><tr>%s</tr></thead><tbody>%s</tbody></table>'
                          % (th, "".join(trs))))
    return "".join(out)


def render_func_table(rows, aligns):
    head, body = rows[0], rows[1:]
    th = "".join("<th>%s</th>" % inline(c) for c in head)
    # SoC columns = header cells that look like Tnn
    soc = [k for k, c in enumerate(head) if re.fullmatch(r"T\d+", c)]
    trs = []
    for r in body:
        if len(r) > len(head) and "" in r[1:]:   # tolerate a stray '||' in the source
            sys.stderr.write("warning: extra empty cell in row %s\n" % r[0])
            r = r[:]
            r.remove("")
        gap = any(r[k] in GAP_CODES for k in soc if k < len(r))
        tds = []
        for k, c in enumerate(r):
            if k == 0:
                tds.append("<td>%s</td>" % inline(c))
            elif k in soc:
                mc = re.match(r"^(.*?)((?:\s*\[\d+\])*)$", c)
                code, refs = mc.group(1), mc.group(2).strip()
                cls, lab = CODE.get(code, ("s-unk", code))
                tds.append('<td class="c %s" data-code="%s">%s%s</td>' % (
                    cls, esc(code), esc(lab), " <small>%s</small>" % esc(refs) if refs else ""))
            elif k < len(head) and head[k] == "Used by":
                tds.append('<td class="c" style="white-space:normal">%s</td>' % inline(c))
            else:
                tds.append('<td class="nt">%s</td>' % inline(c))
        trs.append('<tr data-gap="%d">%s</tr>' % (1 if gap else 0, "".join(tds)))
    return (len(trs), sum(1 for t in trs if 'data-gap="1"' in t),
            table_wrap('<table class="mi"><thead><tr>%s</tr></thead><tbody>%s</tbody></table>'
                       % (th, "".join(trs))))


FILTER = ("<label><input type=\"checkbox\" onchange=\"var on=this.checked;"
          "this.closest('details').querySelectorAll('tr[data-gap=&quot;0&quot;]')"
          ".forEach(function(e){e.style.display=on?'none':''})\"> only rows with gaps</label>")


def render_streamer_gaps(items):
    """bullets '**T41**: 19 (timps 0, prudynt 3, raptor 18): functions' -> table"""
    rows = []
    for _, _, t in items:
        m = re.match(r"^\*\*(T\d+)\*\*:\s*(\d+)\s*\(timps (\d+), prudynt (\d+), raptor (\d+)\)(?::\s*(.*))?$", t)
        if m:
            fn = m.group(6) or ""
        else:
            m = re.match(r"^\*\*(T\d+)\*\*:\s*(\d+)\s*\((.*)\)\s*$", t)
            if not m:
                return None
            rows.append((m.group(1), m.group(2), "", "", "", m.group(3)))
            continue
        rows.append((m.group(1), m.group(2), m.group(3), m.group(4), m.group(5), fn))
    trs = []
    for soc, tot, a, b, c, fn in rows:
        trs.append('<tr><td>%s</td><td class="c"><b>%s</b></td><td class="c">%s</td><td class="c">%s</td>'
                   '<td class="c">%s</td><td class="nt">%s</td></tr>'
                   % (soc, tot, a, b, c, inline(fn)))
    return table_wrap('<table class="mi"><thead><tr><th>SoC</th><th>gaps used by at least one streamer</th>'
                      '<th>timps</th><th>prudynt</th><th>raptor</th><th>functions (state; streamers)</th>'
                      '</tr></thead><tbody>%s</tbody></table>' % "".join(trs))


# ---------------------------------------------------------------- matrix
SOCS = ["T10", "T20", "T21", "T23", "T30", "T31", "T40", "T41"]


def parse_details(blocks):
    """'### Matrix details' blocks -> {num: dict(title, vendor, cells, extra_cells, extra_blocks)}"""
    res, groups = {}, []
    cur = None
    for typ, d in blocks:
        if typ == "h" and d[0] == 4:
            groups.append(d[1])
            cur = None
        elif typ == "h" and d[0] == 5:
            m = re.match(r"^(\d+)\.\s+(.*)$", d[1])
            cur = {"title": d[1], "vendor": "", "cells": {}, "extra_cells": {}, "extra": [],
                   "group": groups[-1] if groups else ""}
            res[int(m.group(1)) if m else len(res) + 1000] = cur
        elif cur is not None:
            if typ == "ul":
                for _, _, t in d:
                    m = re.match(r"^\*\*(.+?):\*\*\s*(.*)$", t)
                    if not m:
                        cur["extra"].append(("ul", [[0, None, t]]))
                        continue
                    lab, txt = m.group(1), m.group(2)
                    if lab == "Vendor stack":
                        cur["vendor"] = txt
                    elif lab in SOCS:
                        cur["cells"][lab] = txt
                    elif lab.split(" ")[0] in SOCS:
                        cur["extra_cells"].setdefault(lab.split(" ")[0], []).append((lab, txt))
                    else:
                        cur["extra"].append(("ul", [[0, None, t]]))
            else:
                cur["extra"].append((typ, d))
        elif typ not in ("h",):
            pass
    return res, groups


def render_matrix(rows, details, groups_info):
    head = rows[0]
    socs = head[2:]
    th = "<th>Feature</th><th>Vendor stack</th>" + "".join("<th>%s</th>" % esc(s) for s in socs)
    trs = []
    mism = []
    for r in rows[1:]:
        if not r[0].strip() and r[1].startswith("**"):
            trs.append('<tr class="grp"><td colspan="%d">%s</td></tr>'
                       % (len(head) - 1, inline(r[1].strip("*"))))
            continue
        n = int(r[0])
        m = re.match(r"^\[(.*)\]\(#(.*)\)$", r[1])
        title, anchor = (m.group(1), m.group(2)) if m else (r[1], "")
        d = details.get(n, {"vendor": "", "cells": {}, "extra_cells": {}})
        if not d.get("extra"):
            anchor = "r-%d" % n   # no detail block: link to the row itself
        tds = ['<td><a href="#%s">%s</a></td>' % (esc(anchor), inline(title)),
               '<td class="vend">%s</td>' % inline(d["vendor"])]
        for soc, sym in zip(socs, r[2:]):
            sym = norm_sym(sym.strip())
            cls, ttl = SYM.get(sym, ("s-unk", "unknown"))
            txt = d["cells"].get(soc, "")
            mm = SYM_RE.match(txt)
            if mm:
                if norm_sym(mm.group(1)) != sym:
                    mism.append((n, soc, sym, mm.group(1)))
                txt = txt[mm.end():]
            body = '<span class="sym" title="%s">%s</span><span class="txt">%s</span>' % (
                ttl, esc(sym), inline(txt))
            for lab, t in d["extra_cells"].get(soc, []):
                t = SYM_RE.sub("", t, count=1)
                body += '<br><span class="txt"><b>%s:</b> %s</span>' % (inline(lab), inline(t))
            tds.append('<td class="cellc %s" data-sym="%s">%s</td>' % (cls, esc(sym), body))
        trs.append('<tr id="r-%d">%s</tr>' % (n, "".join(tds)))
    for m in mism:
        sys.stderr.write("warning: row %d %s: table symbol %s, detail symbol %s\n" % m)
    return table_wrap("<table><thead><tr>%s</tr></thead><tbody>%s</tbody></table>"
                      % (th, "".join(trs)))


# ---------------------------------------------------------------- main render
def render(md):
    blocks = parse_blocks(md.split("\n"))
    out = []
    # split the matrix-details range off
    mi = next(k for k, (t, d) in enumerate(blocks) if t == "h" and d[1] == "Matrix details")
    mj = next((k for k in range(mi + 1, len(blocks))
               if blocks[k][0] == "h" and blocks[k][1][0] <= 3), len(blocks))
    details, _ = parse_details(blocks[mi + 1:mj])
    det_intro = [b for b in blocks[mi + 1:mj] if b[0] == "p"][:1]

    section = ""      # current h2
    sub = ""          # current h3
    in_card = False
    in_legend = False
    in_full = False
    full_gaps = None

    def close_card():
        nonlocal in_card
        if in_card:
            out.append("</div>")
            in_card = False

    k = 0
    while k < len(blocks):
        typ, d = blocks[k]
        k += 1
        if k - 1 == mi:   # matrix details: render extras only
            close_card()
            out.append("<h3>Matrix details</h3>")
            out.append('<p class="meta">Per feature: the vendor-stack behaviour and the full per-SoC cell '
                       'text (chronological test log, newest results win) are in the table above; '
                       'additional text per feature follows here.</p>')
            grp = ""
            for n in sorted(details):
                dd = details[n]
                if not dd["extra"]:
                    continue
                if dd["group"] != grp:
                    grp = dd["group"]
                    out.append("<h4>%s</h4>" % inline(grp))
                inner = "".join(render_block(t, x) for t, x in dd["extra"])
                out.append('<details id="%s"><summary>%s</summary>%s</details>'
                           % (slug(dd["title"]), inline(dd["title"]), inner))
            k = mj
            continue
        if typ == "h":
            lvl, text = d
            close_card()
            in_legend = False
            if lvl == 1:
                out.append("<h1>%s</h1>" % inline(text))
            elif lvl == 2:
                section = text
                sub = ""
                out.append('<h2 id="%s">%s</h2>' % (uid(text), inline(text)))
                if text in ("Summary",):
                    out.append('<div class="card">')
                    in_card = True
                if text == "Legend":
                    in_legend = True
            elif lvl == 3:
                sub = text
                out.append('<h3 id="%s">%s</h3>' % (uid(text), inline(text)))
                if text == "Notes":
                    out.append('<div class="card">')
                    in_card = True
                if text == "Legend":
                    in_legend = True
            else:
                out.append("<h4>%s</h4>" % inline(text))
            continue
        if typ == "rawopen":
            out.append('<details class="full"><summary>%s</summary>%s' % (inline(d or ""), FILTER))
            in_full = True
            continue
        if typ == "rawclose":
            out.append("</details>")
            in_full = False
            continue
        if typ == "table":
            rows, aligns = d
            if section == "Matrix" and rows[0][:2] == ["#", "Feature"]:
                out.append(render_matrix(rows, details, None))
            elif rows[0][0] == "Vendor function":
                cnt, gaps, h = render_func_table(rows, aligns)
                out.append(h)
            elif rows[0][0] == "SoC" and "fns" in rows[0]:
                out.append(render_progress(rows, aligns))
            else:
                out.append(render_generic_table(rows, aligns))
            continue
        if typ == "code":
            if sub == "Progress per SoC":
                continue   # ASCII bars: replaced by the bar column of the table
            out.append("<pre>%s</pre>" % esc(d))
            continue
        if typ in ("ul", "ol"):
            if in_legend:
                cls = "card legend" if not in_card else "legend"
                out.append('<div class="%s">%s</div>' % (cls, "".join(legend_item(t) for _, _, t in d)))
            elif sub.startswith("Streamer-relevant gaps") and render_streamer_gaps(d):
                out.append(render_streamer_gaps(d))
            else:
                out.append(render_list(typ, d))
            continue
        # paragraph
        if in_card:
            out.append('<p class="small" style="margin:.6rem 0 0">%s</p>' % inline(d))
        elif in_full:
            out.append('<p class="small">%s</p>' % inline(d))
        else:
            out.append('<p class="meta">%s</p>' % inline(d))
    close_card()
    return "\n".join(out)


def render_block(typ, d):
    if typ == "p":
        return "<p>%s</p>" % inline(d)
    if typ in ("ul", "ol"):
        return render_list(typ, d)
    if typ == "code":
        return "<pre>%s</pre>" % esc(d)
    if typ == "table":
        return render_generic_table(*d)
    if typ == "h":
        return "<h4>%s</h4>" % inline(d[1])
    return ""


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else SRC
    dst = sys.argv[2] if len(sys.argv) > 2 else DST
    md = open(src, encoding="utf-8").read()
    md = re.sub(r"<!--.*?-->", "", md, flags=re.S)
    body = render(md)
    title = re.search(r"^#\s+(.*)$", md, re.M).group(1)
    page = ('<!doctype html>\n<html lang="en"><head><meta charset="utf-8">\n'
            '<meta name="viewport" content="width=device-width,initial-scale=1">\n'
            "<title>%s</title>\n<style>%s</style></head><body><main>\n%s\n"
            "<p class=\"small\">Generated from FEATURE_MATRIX.md by tools/gen_feature_matrix_html.py; "
            "do not edit by hand.</p></main></body></html>\n" % (esc(title), CSS, body))
    open(dst, "w", encoding="utf-8").write(page)
    print("wrote %s (%d bytes)" % (dst, len(page.encode())))


if __name__ == "__main__":
    main()
