#!/usr/bin/env python3
"""Release gates RC3 and Release 1: model, Markdown section and HTML card (stdlib; the rules are in docs/status/release.yaml).

RC3        green only if (a) all streamer-function cells work, (b) no open issue has severity:critical or severity:high,
           (c) no open issue is in the milestone RC3 (rules: rc3.* in docs/status/release.yaml; evaluate_rc3()).
           Per gate SoC: how many of the vendor functions that a streamer calls work (API matrix cell dev, dev+, run, rt, no-op),
           the missing ones with their issue links.  Source: the API model of tools/gen_api_coverage.py (docs/api_evidence.json,
           "Used by" = streamer call sets, docs/status/issues.json for the issue links).
Release 1  per gate SoC: stable days of the release image (best camera of the SoC) against the threshold, the reset events.
           Source: docs/status/soak.json (written on the host by tools/status_soak.py).

Used by gen_api_coverage.update_md (Markdown) and gen_feature_matrix_html (HTML); not run on its own.
"""
import html
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import status_data as sd

REPO_URL = "https://github.com/" + sd.REPO


def esc(s):
    return html.escape(str(s), quote=True)


def bar(n, total, width=20):
    k = int(round(width * n / total)) if total else 0
    return "█" * k + "░" * (width - k)


def rule(rel, gate, key):
    g = next(g for g in rel["gates"] if g["id"] == gate)
    for r in g["rules"]:
        if key in r:
            return r[key]
    return None


def rc3_blockers(issues, severities, milestone):
    """Open issues that block RC3 -> {"critical": [...], "high": [...], "milestone": [...]} (issue dicts, sorted by number).
    `issues` is {number: dict} as sd.load_issues() returns it; an issue can be listed in several groups."""
    res = {"critical": [], "high": [], "milestone": []}
    for n in sorted(issues):
        i = dict(issues[n], number=n)
        if i.get("state") != "open":
            continue
        labs = i.get("labels", [])
        if severities[0] in labs:
            res["critical"].append(i)
        if len(severities) > 1 and severities[1] in labs:
            res["high"].append(i)
        if milestone and i.get("milestone") == milestone:
            res["milestone"].append(i)
    return res


def evaluate_rc3(cells_ok, issues, rel):
    """RC3 verdict: dict(ok, cells=bool a, severity=bool b, milestone=bool c, blockers=rc3_blockers()).
    GREEN only if all three parts hold."""
    sev = rule(rel, "rc3", "block_severity")
    ms = rule(rel, "rc3", "block_milestone")
    bl = rc3_blockers(issues, sev, ms)
    a, b, c = bool(cells_ok), not (bl["critical"] or bl["high"]), not bl["milestone"]
    return dict(ok=a and b and c, cells=a, severity=b, milestone=c, blockers=bl)


class Gates:
    def __init__(self, model):
        self.m = model
        self.rel = sd.load_release()
        self.socs = self.rel["socs"]
        self.works = set(next(g for g in self.rel["gates"] if g["id"] == "rc3")["works_cells"])
        self.soak = json.load(open(sd.SOAK, encoding="utf-8")) if os.path.exists(sd.SOAK) else None
        r1 = "release1"
        self.min_days = rule(self.rel, r1, "min_stable_days")
        self.min_cams = rule(self.rel, r1, "min_cameras_per_soc")
        self.max_reb = rule(self.rel, r1, "max_unplanned_reboots")
        self.max_oops = rule(self.rel, r1, "max_oops")
        self._rc3()
        self._r1()

    # ------------------------------------------------------------ RC3
    def _rc3(self):
        m = self.m
        self.rc3 = {}
        for s in self.socs:
            tot = ok = 0
            miss = []
            for f in m.funcs:
                us = m.users(f, s)
                w = f["w"][s]
                if not us or not w:
                    continue
                tot += w
                if f["cells"][s] in self.works:
                    ok += w
                else:
                    miss.append(f)
            miss.sort(key=lambda f: (-m.nstream(m.users(f, s)), f["name"].lower()))
            self.rc3[s] = dict(total=tot, ok=ok, missing=miss)
        self.rc3_cells_ok = all(v["ok"] == v["total"] for v in self.rc3.values())
        self.rc3_eval = evaluate_rc3(self.rc3_cells_ok, self.m.issues, self.rel)
        self.rc3_ok = self.rc3_eval["ok"]

    @staticmethod
    def sev_of(i):
        v = [l.split(":", 1)[1] for l in i.get("labels", []) if l.startswith("severity:")]
        return "severity " + v[0] if v else "no severity"

    def issues_of(self, f, s):
        return self.m.func_issues.get((f["name"], s), [])

    @staticmethod
    def ilink(i):
        t = "#%d" % i["number"]
        if i.get("state") == "closed":
            t += " (closed)"
        return "[%s](%s)" % (t, i.get("url", REPO_URL + "/issues/%d" % i["number"]))

    # ------------------------------------------------------------ Release 1
    def _r1(self):
        self.r1 = {}
        g = next(g for g in self.rel["gates"] if g["id"] == "release1")
        self.r1_tag, self.r1_image = g.get("rc3_tag"), g.get("release_image")
        # counting starts only after RC3 is published, and only on the image built from the RC3 tag
        self.r1_started = bool(self.r1_tag and self.r1_image and self.soak and self.soak.get("image") == self.r1_image)
        cams = self.soak["cameras"] if self.r1_started else []
        for s in self.socs:
            cs = [c for c in cams if c["soc"] == s]
            best = max([c["stable_days"] for c in cs], default=0.0)
            nok = sum(1 for c in cs if c["stable_days"] >= self.min_days)
            events = []
            for c in cs:
                for e in c["events"]:
                    if e["kind"] != "flash":
                        events.append((c["camera"], e))
            self.r1[s] = dict(cams=cs, best=best, nok=nok, events=sorted(events, key=lambda x: x[1]["time"]),
                              ok=nok >= self.min_cams and best >= self.min_days)
        self.r1_ok = bool(self.soak) and self.rc3_ok and all(v["ok"] for v in self.r1.values())

    # ------------------------------------------------------------ helpers
    def totals(self):
        t = sum(v["total"] for v in self.rc3.values())
        o = sum(v["ok"] for v in self.rc3.values())
        return o, t

    @staticmethod
    def verdict(ok):
        return "GREEN" if ok else "RED"

    def r1_events_text(self, s):
        out = []
        for cam, e in self.r1[s]["events"]:
            kind = {"reboot": "reboot", "oops": "oops"}.get(e["kind"], e["kind"])
            out.append("%s %s %s %s: %s" % (e["time"], cam, "planned" if e["planned"] else "UNPLANNED", kind, e["reason"]))
        return out

    # ------------------------------------------------------------ markdown
    def md(self):
        o = ["## Release gates", "",
             "<!-- GENERATED by tools/release_gates.py from docs/status/release.yaml, docs/api_evidence.json, docs/status/issues.json "
             "and docs/status/soak.json. Do not edit by hand. -->", "",
             "Rules (machine-readable: `docs/status/release.yaml`). **RC3** is green only if (a) every vendor function that a streamer "
             "(timps, prudynt, raptor) calls works on %s (API matrix cell dev, dev+, run, rt or vendor no-op), (b) no open issue has the "
             "label severity:critical or severity:high and (c) no open issue is in the milestone RC3. **Release 1:** RC3 is green and the "
             "release image ran at least %d days on the fleet with at least %d camera per SoC, %d unplanned reboots and %d oops; a "
             "new image or any reset restarts the counter; planned flashes and tests count as planned only when the camera STATUS "
             "log marks them so." % (", ".join(self.socs), self.min_days, self.min_cams, self.max_reb, self.max_oops), "",
             "Project board status of an open issue (https://github.com/users/Lu-Fi/projects/1): Todo, In progress, **Monitoring** "
             "(diagnostics are running, waiting for the next event of the kind), **Waiting** (blocked from outside, for example on "
             "test hardware or a measurement setup; the issue says what it waits for, such as a light box A/B), Review, Done. "
             "Monitoring and Waiting issues are still open: they keep counting for the gates above.", ""]
        wo, wt = self.totals()
        e, bl = self.rc3_eval, self.rc3_eval["blockers"]
        nbl = len({i["number"] for g in bl.values() for i in g})
        o.append("- **RC3: %s %s** (%s)" % ("🟢" if self.rc3_ok else "🔴", self.verdict(self.rc3_ok),
                 "all three parts hold" if self.rc3_ok else "%d blocking issue%s%s" % (
                     nbl, "" if nbl == 1 else "s", "" if e["cells"] else "; streamer cells missing")))
        o.append("  - (a) streamer functions: %s %d of %d cells work, %d missing" % ("🟢" if e["cells"] else "🔴", wo, wt, wt - wo))
        o.append("  - (b) no open issue with severity critical or high: %s %d critical, %d high" % (
            "🟢" if e["severity"] else "🔴", len(bl["critical"]), len(bl["high"])))
        o.append("  - (c) no open issue in milestone RC3: %s %d open" % ("🟢" if e["milestone"] else "🔴", len(bl["milestone"])))
        nsoc = sum(1 for v in self.r1.values() if v["ok"])
        asof = self.soak["asof"] if self.soak else "no soak data"
        o.append("- **Release 1: %s %s** (%d of %d SoCs reached %d stable days; image %s, soak as of %s)" % (
            "🟢" if self.r1_ok else "🔴", self.verdict(self.r1_ok), nsoc, len(self.socs), self.min_days,
            self.r1_image or "–", asof))
        if not self.r1_started:
            o.append("  - Not started: the %d-day counter starts when RC3 is published (tag%s) and runs only on the image built "
                     "from that tag." % (self.min_days, " " + self.r1_tag if self.r1_tag else " pending"))
        o += ["", "### RC3: blocking issues", ""]
        titles = (("critical", "Critical (severity:critical)"), ("high", "High (severity:high)"), ("milestone", "RC3 milestone"))
        if not any(bl.values()):
            o.append("- none")
        for key, title in titles:
            if not bl[key]:
                continue
            o.append("- **%s: %d**" % (title, len(bl[key])))
            for i in bl[key]:
                o.append("  - %s %s (%s; milestone %s)" % (self.ilink(i), i["title"].replace("|", "\\|"), self.sev_of(i), i.get("milestone") or "none"))
        o += ["", "### RC3: streamer functions that work", "",
              "Per SoC: vendor functions that at least one streamer calls (a Get/Set row counts two, a wildcard row its vendor names) "
              "and that work. The list below names the missing ones with their issues (milestone RC3).", "",
              "| SoC | works | progress | missing |", "|---|--:|---|--:|"]
        for s in self.socs:
            v = self.rc3[s]
            o.append("| %s | %d/%d | `%s` | %d |" % (s, v["ok"], v["total"], bar(v["ok"], v["total"]), v["total"] - v["ok"]))
        o.append("")
        for s in self.socs:
            v = self.rc3[s]
            if not v["missing"]:
                o.append("- **%s:** nothing missing" % s)
                continue
            items = []
            for f in v["missing"]:
                iss = self.issues_of(f, s)
                items.append("`%s` (%s; %s; %s)" % (f["name"], f["cells"][s], self.m.ulabel(self.m.users(f, s)),
                                                    ", ".join(self.ilink(i) for i in iss) if iss else "no issue yet"))
            o.append("- **%s** (%d missing, %d rows): %s" % (s, v["total"] - v["ok"], len(v["missing"]), ", ".join(items)))
        o += ["", "### Release 1: stable days on the release image", "",
              "Stable days = days since the later of: the image first ran on the camera, its last unplanned reboot, its last oops "
              "(the best camera of a SoC counts). Cameras are named by the last octet of their LAN address.", "",
              "| SoC | cameras | best stable days | progress | unplanned reboots | oops |", "|---|---|--:|---|--:|--:|"]
        for s in self.socs:
            v = self.r1[s]
            o.append("| %s | %s | %.1f/%d | `%s` | %d | %d |" % (
                s, " ".join(c["camera"] for c in v["cams"]) or "–", v["best"], self.min_days,
                bar(min(v["best"], self.min_days), self.min_days), sum(c["unplanned_reboots"] for c in v["cams"]),
                sum(c["oops"] for c in v["cams"])))
        o.append("")
        o.append("Reset events of the release image (flashes of the image itself are not listed):")
        o.append("")
        any_ev = False
        for s in self.socs:
            for t in self.r1_events_text(s):
                o.append("- %s: %s" % (s, t))
                any_ev = True
        if not any_ev:
            o.append("- none")
        o += ["", "Open items: milestones [RC3](%s/milestones) and Release 1; tracking issue [#107](%s/issues/107). The soak data is refreshed "
              "daily on the host with `python3 tools/status_soak.py` (see its header), then this page is regenerated." % (REPO_URL, REPO_URL), ""]
        return "\n".join(o)

    # ------------------------------------------------------------ html
    def _pb(self, n, total, cls="b-dev", gap="b-gap"):
        pct = 100.0 * n / total if total else 0
        return ('<div class="pb"><i class="%s" style="width:%.2f%%" title="%d of %d"></i><i class="%s" style="width:%.2f%%"></i></div>'
                % (cls, pct, n, total, gap, 100 - pct if total else 0))

    def html(self):
        def head(ok, label, sub):
            return ('<h3><span class="sym %s" style="padding:2px 10px;border-radius:5px">%s %s</span> %s</h3><p class="small">%s</p>'
                    % ("s-ok" if ok else "s-miss", "&#128994;" if ok else "&#128308;", self.verdict(ok), label, sub))
        wo, wt = self.totals()
        h = ['<section class="card" id="release-gates"><h2>Release gates</h2>'
             '<p class="small">Rules in <code>docs/status/release.yaml</code>. RC3: streamer functions work on every gate SoC, no open critical/high issue, none in milestone RC3.'
             ' Release 1: RC3 green and the release image ran %d days without a reset on at least %d camera per SoC '
             '(%d unplanned reboots, %d oops).</p>'
             '<p class="small">Board status of an open issue: Todo, In progress, <b>Monitoring</b> (diagnostics are running, waiting for the next '
             'event of the kind), <b>Waiting</b> (blocked from outside, e.g. on test hardware or a measurement setup; the issue says what it '
             'waits for, such as a light box A/B), Review, Done. Monitoring and Waiting issues stay open and still count for the gates.</p>'
             % (self.min_days, self.min_cams, self.max_reb, self.max_oops)]
        e, bl = self.rc3_eval, self.rc3_eval["blockers"]
        mk = lambda ok: "&#128994;" if ok else "&#128308;"
        h.append(head(self.rc3_ok, "RC3", "Green only if all three parts hold. Milestone "
                      '<a href="%s/milestones">RC3</a>.' % REPO_URL))
        h.append("<ul class='pl'><li>(a) streamer functions: %s %d of %d cells work, %d missing</li>"
                 "<li>(b) no open issue with severity critical or high: %s %d critical, %d high</li>"
                 "<li>(c) no open issue in milestone RC3: %s %d open</li></ul>" % (
                     mk(e["cells"]), wo, wt, wt - wo, mk(e["severity"]), len(bl["critical"]), len(bl["high"]),
                     mk(e["milestone"]), len(bl["milestone"])))
        for key, title in (("critical", "Critical"), ("high", "High"), ("milestone", "RC3 milestone")):
            if bl[key]:
                h.append("<details class='prio'%s><summary><b>%s</b> %d blocking</summary><ul class='pl'>%s</ul></details>" % (
                    " open" if key == "critical" else "", title, len(bl[key]), "".join(
                        '<li><a href="%s">#%d</a> %s <span class="small">%s &middot; milestone %s</span></li>' % (
                            esc(i.get("url", "")), i["number"], esc(i["title"]), esc(self.sev_of(i)), esc(i.get("milestone") or "none"))
                        for i in bl[key])))
        rows = []
        for s in self.socs:
            v = self.rc3[s]
            rows.append("<tr><td>%s</td><td class='c'>%d/%d</td><td>%s</td><td class='c'>%d</td></tr>" % (
                s, v["ok"], v["total"], self._pb(v["ok"], v["total"]), v["total"] - v["ok"]))
        h.append('<table class="mi"><thead><tr><th>SoC</th><th>streamer functions that work</th><th>progress</th><th>missing</th></tr></thead>'
                 "<tbody>%s</tbody></table>" % "".join(rows))
        for s in self.socs:
            v = self.rc3[s]
            if not v["missing"]:
                continue
            li = []
            for f in v["missing"]:
                iss = self.issues_of(f, s)
                il = ", ".join('<a href="%s">#%d</a>' % (esc(i.get("url", "")), i["number"]) for i in iss) if iss else "<i>no issue yet</i>"
                li.append('<li><span class="sym s-miss">%s</span> <code>%s</code> <span class="small">%s &middot; %s</span></li>' % (
                    esc(f["cells"][s]), esc(f["name"]), esc(self.m.ulabel(self.m.users(f, s))), il))
            h.append('<details class="prio"><summary><b>%s</b> %d missing (%d rows)</summary><ul class="pl">%s</ul></details>' % (
                s, v["total"] - v["ok"], len(v["missing"]), "".join(li)))
        asof = self.soak["asof"] if self.soak else "no soak data"
        h.append(head(self.r1_ok, "Release 1", ("Not started: the %d-day counter starts when RC3 is published and runs only on the image "
                      "built from the RC3 tag. " % self.min_days if not self.r1_started else "") +
                      "Image %s, soak as of %s. Stable days = since the later of: image first ran, last unplanned reboot, "
                      "last oops (best camera per SoC). Milestone Release 1." % (esc(self.r1_image or "-"), esc(asof))))
        rows = []
        for s in self.socs:
            v = self.r1[s]
            rows.append("<tr><td>%s</td><td>%s</td><td class='c'>%.1f/%d</td><td>%s</td><td class='c'>%d</td><td class='c'>%d</td></tr>" % (
                s, " ".join(c["camera"] for c in v["cams"]) or "&ndash;", v["best"], self.min_days,
                self._pb(min(v["best"], self.min_days) and int(round(min(v["best"], self.min_days) * 100)), self.min_days * 100),
                sum(c["unplanned_reboots"] for c in v["cams"]), sum(c["oops"] for c in v["cams"])))
        h.append('<table class="mi"><thead><tr><th>SoC</th><th>cameras</th><th>stable days</th><th>progress</th><th>unplanned reboots</th><th>oops</th></tr></thead>'
                 "<tbody>%s</tbody></table>" % "".join(rows))
        ev = [(s, t) for s in self.socs for t in self.r1_events_text(s)]
        h.append("<details class='prio'%s><summary><b>Reset events</b> of the release image (%d)</summary><ul class='pl'>%s</ul></details>" % (
            " open" if ev else "", len(ev), "".join("<li><b>%s</b> %s</li>" % (s, esc(t)) for s, t in ev) or "<li>none</li>"))
        h.append("</section>")
        return "".join(h)
