# Übergabe: OpenIMP OSD — gleichmäßige Strichdicke für Diagonalen

Stand: 2026-10-03 — **fertig implementiert, host-getestet, 3 lokale Commits — NICHT gepusht.**
**Ein Punkt ist bewusst offen: auf dem Gerät ist nichts geprüft** (Abschnitt 6).

Diese Fassung enthält die Nachbesserungen aus dem Review: `lw = 1` ist jetzt byte-gleich zum
Stock (Endpunkt p1 ausgenommen), der vollständige Winkelsweep ist als fester Host-Test drin,
und die Aussagen zur Schrittzahl und zu den Kappen sind korrigiert (Abschnitt 4.2).

Branch `claude/osd-thick-lines`, Worktree `<path>/openimp-osdthick`.

---

## 1. Auftrag (Original, wörtlich)

> Aufgabe (OpenIMP, nur Host-Arbeit, keine Kamera): OSD-Linien mit gleichmäßiger Dicke.
> Repo: <path>/openimp. Basis: origin/claude/openimp-all-10 (4c1ffe1). Lege einen eigenen
> Worktree an und einen neuen Branch claude/osd-thick-lines. Lokal committen, NICHT pushen.
>
> Anforderungen: gleichmäßige Strichdicke für Linien mit Winkel ≠ 0°/90° (Bresenham mit
> Perpendicular-Span, oder Murphy's thick line), nur Integer-Arithmetik, keine Floats, kein
> malloc im Zeichenpfad; Clipping und NV12-Y/UV-Behandlung beibehalten (UV halbe Auflösung);
> der Dry-Pass (`cv.dry = 1`) muss weiterhin alle tatsächlich geschriebenen Zeilen abdecken;
> Host-Tests in `tests/t31` erweitern (`make -C tests/t31 check`): senkrechte Dicke von
> 45°/30°/60°-Linien bei lw = 1,2,4,8 innerhalb ±1 Pixel des Ziels, horizontale/vertikale Linien
> und Rechtecke byte-identisch zum alten Ergebnis, keine Schreibzugriffe außerhalb des Frames,
> Dry-Pass-Zeilen ⊇ geschriebene Zeilen; `tests/t30 check`, `tests/t23 check` müssen grün
> bleiben.
>
> Deliverable: kurzer Bericht mit Commit-Hash, geänderten Dateien, Testausgabe und einem
> Host-Mikrobenchmark (alt vs. neu, 1080p-Diagonale lw=4).
> „Keine Änderungen außerhalb des Branches, keine Kamera, kein Push."

Alle Regeln eingehalten: nur lokal committet, nichts gepusht, kein anderer Branch/Worktree
angefasst, kein Kamerazugriff, keine Floats und kein `malloc` im Zeichenpfad.

---

## 2. Ergebnis auf einen Blick

| | |
|---|---|
| Branch | `claude/osd-thick-lines` |
| Basis | `origin/claude/openimp-all-10` = `4c1ffe1` |
| Commits | **3 Commits vor der Basis, nicht gepusht**: `cb272dd` (Implementierung, nach Review nachgezogen) + zwei Doku-Commits (diese Datei, `T31_OSD_RE.md` §3.6) — exakte SHAs: `git --no-pager log --oneline 4c1ffe1..HEAD` |
| Diff | 7 Dateien, **+1.258 / −132** |
| Host-Tests | `tests/t31` (inkl. `osd_draw_test: ok`), `tests/t30`, `tests/t23` — alle exit 0 |
| Benchmark (Host, 1080p-Diagonale, lw 4) | alt **53,9 µs / 10.908 Y-Bytes**, neu **23,4 µs / 9.591 Y-Bytes** = **0,43×**; Dry+Draw 69,0 → 34,6 µs |
| Gemessene Dicke (senkrecht) | lw 1/2/4/8 bei 45°: 0,71 / 2,12 / **4,24** / **7,78** px (alt: 0,71 / 2,12 / 4,95 / 10,61); Winkelsweep 0…180° lw 2…8: max. Abweichung **0,463 px** |
| Byte-Identität | Achsenlinien, Rechtecke **und `lw = 1`** (jede Richtung) |
| Betroffene Targets | T31, T20, T21, T30 (geteilter Header; T23 kompiliert den OSD-Block aus, T40/T41 kompilieren die Datei nicht) |
| Offen | **keine Geräteprüfung** der Diagonale und des Flush-Bands (Abschnitt 6) |

---

## 3. Commits und Dateien

`git --no-pager log --oneline 4c1ffe1..HEAD` (3 Commits, exakte SHAs dort):

```
docs: note the OSD diagonal stroke deviation in T31_OSD_RE.md
    docs/T31_OSD_RE.md               +10

docs: handover notes for the OSD thick-line change
    HANDOVER_OIMP_OSDTHICK.md        +282 (neu)

cb272dd osd: uniform stroke thickness for diagonal lines
    src/t31/openimp_t31_osd_draw.h   +205 / −74
    tests/t31/osd_draw_test.c        +500 / −55
    tests/t31/osd_draw_old.h         +120 (neu)
    tests/t31/osd_thick_bench.c      +125 (neu)
    tests/t31/Makefile               +16 / −3
```

(Achtung: `git commit --amend`/Rebase ändert SHAs — die SHAs in diesem Dokument sind Hinweise,
maßgeblich ist `git --no-pager log --oneline 4c1ffe1..HEAD`.)

- **`src/t31/openimp_t31_osd_draw.h`** — der gemeinsame, header-only CPU-Zeichner für
  `OSD_REG_LINE`, `OSD_REG_RECT`, `OSD_REG_BITMAP` auf NV12. Hardware-frei, host-testbar.
  Einziger Produktions-Consumer: `src/t31/openimp_t31_services.c` (Zeile 25).
- **`tests/t31/osd_draw_old.h`** (neu) — **eingefrorene Kopie** des Stock-Zeichners zum
  Basis-Commit (`osd_draw_line_old`, `osd_draw_rect_old`, `osd_fill_new_old`). Dient als
  Byte-Identitäts-Referenz, als Vorher-Spalte der Dickentabelle und als Benchmark-Gegner.
- **`tests/t31/osd_draw_test.c`** — `lw = 1`-Identitätstest, Winkel-Sweep, Achsen-Identitätstest,
  Dickentabelle, Diagonal-Clip- und Dry-Test. Der alte `test_line_reference()` (der die
  Stock-Treppengeometrie festschrieb) ist ersetzt. Der Test linkt `-lm` (`cos/sin/lround` nur
  zum Erzeugen der Endpunkte; der Zeichner selbst bleibt float-frei).
- **`tests/t31/osd_thick_bench.c`** (neu) + `make -C tests/t31 bench` — Host-Mikrobenchmark.

---

## 4. Was technisch gemacht wurde

### 4.1 Das Problem in einer Formel

Der Stock-Drawer zeichnet eine Linie als Folge von `lw × lw`-Stempeln, einen pro Schritt der
dominanten Achse. Bei x-dominant deckt ein Schritt `lw + (lw − 1)·tan(θ)` Zeilen ab, quer zur
Linie also `lw·cos θ + (lw − 1)·sin θ`. Das ist bei 45° genau `2·lw − 1` Zeilen
= `(2·lw − 1)/√2 ≈ 1,41·lw` Pixel. Die Linie ist damit **nie** `lw` breit, sondern immer
breiter — bis Faktor 1,41 bei großem `lw`.

### 4.2 Die neue Bande

Für Diagonalen (`dx ≠ 0` **und** `dy ≠ 0`) läuft `osd_draw_diagonal()` die dominante Achse
entlang und füllt pro Schritt **einen** Lauf entlang der Nebenachse:

```
t = round(lw · hypot / major)          /* Lauf-Länge in Pixeln */
r0 = c − t/2;  r1 = r0 + t − 1
```

`hypot` kommt aus `osd_isqrt64(((uint64_t)adx*adx + ady*ady) << 20)` (Hypotenuse in
1/1024-Einheiten), der Rundungsterm ist `(lw*hq + (major << 9)) / (major << 10)`; `t` wird auf
`[1, OSD_DRAW_MAX_LW]` geklemmt. Auf die Liniennormale projiziert ist die Bande damit `lw` breit
— innerhalb einer halben Pixellänge, und das ist bei einem ganzzahligen Lauf das Beste, was
möglich ist (gemessener Worst Case im Winkelsweep: **0,463 px**). Die Nebenachse wird als
`floor(slope · s)` gerechnet und auf den Endpunktbereich geklemmt, beide Enden werden um `lw/2`
verlängert.

**Zwei Einschränkungen, ehrlich:**
- Die Kappen sind **nicht** senkrecht, sondern Parallelogramm-Ecken (wie bei den Stock-Stempeln):
  der Überhang läuft entlang der dominanten Achse, auf die Normale projiziert also höchstens
  `lw/2 + 0,5` Pixel. Die Bande ist exakt `lw` breit, nicht die Endfläche.
- Die **Schrittzahl ist nur innen gleich** der des Stock-Drawers. Durch den Überhang kommen an
  beiden Enden bis zu `lw/2` Schritte dazu, zusammen also bis zu `lw + 1`. Die Aussage „gleiche
  Schrittzahl" gilt für die Innen-Schritte — und genau die misst der Test.

**Eigenschaften, die der Auftrag verlangt:**
- nur Integer: kein `float`/`double` irgendwo; die einzige Division ist die Skalierung von `t`
  (einmal pro Linie, nicht pro Pixel),
- kein `malloc`, kein statischer Zustand, keine Allokation,
- ein Durchlauf, ein Lauf pro Schritt, jedes Pixel der Bande genau einmal,
- Clipping inline, NV12-Semantik unverändert (`osd_fill_col()` schreibt Y pro Zeile und das
  UV-Paar bei `(x & ~1)` in Chroma-Zeile `row >> 1`),
- `dry` respektiert: `osd_fill_col()` / `osd_fill_box()` aktualisieren `ymin`/`ymax` und
  schreiben bei `c->dry` keinen einzigen Byte.

### 4.3 Achsenlinien und `lw = 1` bleiben byte-gleich

`adx == 0 || ady == 0` behält **exakt den Stock-Pfad**: `adx < 2*lw` → eine gefüllte Box um die
Mitten-x, sonst die Vereinigung der Stempel an `pmin..pmax`. Damit sind horizontale und
vertikale Linien — und damit alle Rechtecke, die aus vier solchen bestehen — **byte-identisch**
zum alten Ergebnis.

`lw == 1` ebenfalls: ein einzelnes Pixel hat keine Breite zu verteilen, und der Überhang der
Bande würde am Endpunkt p1 einen Schritt zufügen. Deshalb gilt für `lw == 1` weiter die
Stock-Dispatch (`|dx| < 2*lw` ⟺ `|dx| ≤ 1` → Box) und in `osd_draw_diagonal()` die
Stock-Schrittgrenze **p0 inklusive, p1 exklusive**. Ein einpixeliger Strich ist damit in jeder
Richtung byte-identisch. Nur echte Diagonalen mit `lw ≥ 2` nehmen den neuen Pfad.

Der alte Helfer `osd_fill_new()` (inkrementelle Stempel-Vereinigung, nur vom alten Pfad genutzt)
wurde dadurch toter Code und ist entfernt; seine eingefrorene Kopie lebt im Test.

### 4.4 Bewusste Verhaltensänderung

`|dx| < 2*lw` wurde als Dispatch-Bedingung für den Box-Pfad **für `lw ≥ 2`** fallengelassen (er
greift dort nur noch bei `adx == 0`); für `lw == 1` bleibt er erhalten, weil er dort byte-gleich
ist und nichts kostet. Eine **kurze Diagonale** mit `lw ≥ 2` war vorher eine Box und ist jetzt
eine Bande. Das ist gewollt und deckt sich mit dem Auftragsziel.

---

## 5. Verifikation (Host)

### 5.1 Testläufe

```
make -C tests/t31 check   # osd_draw_test: ok   (exit 0, inkl. t31-p2-jpeg-source-test)
                          # angle sweep 0..180 deg, lw 2..8: worst |width - lw| = 0.463 px
make -C tests/t30 check   # System Init/Exit tests passed      (exit 0)
make -C tests/t23 check   # T23 JPEG user tables: all checks passed   (exit 0)
```

Neu im OSD-Test:

| Test | Prüft |
|---|---|
| `test_axis_identity` | 600 deterministische LCG-Fälle, je Fall ein Rechteck **plus** eine Linie (gerade Indizes horizontal, ungerade vertikal, damit Clamp und `offPos` sie nicht in eine Diagonale drehen): Koordinaten −16…79 / −16…63, `offPos` −4…4, `lw ∈ {1,2,3,4,7,9,100,0xffffffff}`; `memcmp` gegen den eingefrorenen Stock-Drawer **und** gleiches `ymin/ymax`-Band |
| `test_lw1_identity` | 15 feste Fälle (Ecken, `|dx| = 1/2` → Box-Pfad, 45°, weit außerhalb, `INT_MIN/INT_MAX`) × 3 `offPos` plus 400 Zufallsdiagonalen (LCG, mit `dx ≠ 0` **und** `dy ≠ 0`): `lw = 1` muss `memcmp`-gleich zum Stock-Drawer sein, inklusive Band |
| `test_diagonal_thickness` | 45°/30°/60° bei lw 1/2/4/8: senkrechte Dicke innerhalb ±1 px, genau ein zusammenhängender Lauf pro Schritt, gleiche Innen-Schrittanzahl wie alt, kein Pixel weiter als `lw/2 + 1,5` von der Linie, Fläche/Länge = Breite |
| `test_angle_sweep` | **0…180° in 1°-Schritten × lw 2…8** (1.267 Läufe, Segment 200 px in der Mitte eines 512×512-Frames): jeder Innen-Schritt genau ein Lauf, kein Loch, senkrechte Breite in `lw ± 0,5`; Worst Case wird ausgegeben (**0,463 px**) |
| `test_diagonal_clip_dry` | 14 Segmente (negative Koordinaten, alle vier Richtungen, `INT_MIN`/`INT_MAX`, `lw = 0xffffffff`): Randbytes und Zeilen-Padding unberührt, Dry-Pass schreibt nichts, und **jede Zeile mit geschriebenen Pixeln liegt in `[ymin, ymax]` des Dry-Passes** |

### 5.2 Dickentabelle (senkrecht, nur Innen-Schritte)

```
  45 deg lw 1: new 0.71   old  0.71
  45 deg lw 2: new 2.12   old  2.12
  45 deg lw 4: new 4.24   old  4.95
  45 deg lw 8: new 7.78   old 10.61
  30 deg lw 1: new 0.87   old  0.87
  30 deg lw 2: new 1.73   old  1.73..2.60 (mean 2.23)
  30 deg lw 4: new 4.33   old  4.33..5.20 (mean 4.96)
  30 deg lw 8: new 7.80   old 10.40..11.26 (mean 10.43)
  60 deg lw 1: new 0.87   old  0.87
  60 deg lw 2: new 1.73   old  1.73..2.60 (mean 2.23)
  60 deg lw 4: new 4.33   old  4.33..5.19 (mean 4.96)
  60 deg lw 8: new 7.79   old 10.39..11.26 (mean 10.43)
```

Bei `lw = 1` ist die Geometrie identisch: `2·lw − 1 = 1` Zeile ist bei 45° nun einmal
`1/√2 ≈ 0,71 px`; da ist nichts zu verbessern. Für `lw = 1` zeichnet OpenIMP weiter den
Stock-Pfad, das ist per `memcmp` gegen den eingefrorenen Stock-Drawer abgesichert
(`test_lw1_identity`); nur der Endpunkt p1 bleibt wie im Stock ausgespart.

### 5.3 Mikrobenchmark (Host, `make -C tests/t31 bench`)

1920×1080, Diagonale `(0,0)–(1919,1079)`, `lw = 4`, 200 Iterationen pro Lauf:

```
  old staircase  one pass :     53.9 us    10908 Y bytes  1080 rows
  new band       one pass :     23.4 us     9591 Y bytes  1080 rows
  old staircase  dry+draw :     69.0 us
  new band       dry+draw :     34.6 us
  ratio band/staircase    :     0.43 one pass, 0.50 dry+draw
```

Die neue Bande ist also rund **2,3× schneller** als der Stock-Stempel und schreibt **12 %**
weniger Y-Bytes. Zahlen sind Host-`-O2` ohne Target-Flags: Größenordnung, keine Gerätezahl.
Kontrolllauf: 53,2 / 23,7 µs, Dry+Draw 68,9 / 31,8 µs — die Streuung liegt bei rund ±10 %.

### 5.4 Produktions-TU

```
cc -fsyntax-only -std=gnu99 -O2 -Wall -Wextra -Isrc -Iinclude src/t31/openimp_t31_services.c
# clean
```

(Mit `-std=c99` scheitert es an einem vorbestehenden, unabhängigen `O_CLOEXEC`-Problem der
Datei, nicht an dieser Änderung.)

---

## 6. ⚠️ Offener Punkt: Geräteprüfung fehlt, und die Abweichung ist bewusst

**Wie beauftragt wurde ausschließlich auf dem Host gearbeitet.** Der Pfad
`openimp_t31_osd_apply()` (Dry-Pass → Flush → Zeichnen → Flush) ist nur host-getestet. Beim
ersten Gerätelauf ist zu prüfen:

1. Eine Diagonale ist sichtbar **durchgehend, gleich dick und ohne Lücke** (bei lw ≥ 2).
2. Der Flush-Band deckt die Bande noch vollständig ab (kein abgeschnittener Rand oben/unten).
3. Beim Winkelwechsel wirkt der Strich nicht mehr „dicker", sondern konstant.
4. Eine Linie mit `lw = 1` sieht aus wie vorher (byte-gleich, nur host-verifiziert).

**Es gibt dafür noch keinen Gerätetest:** `docs/T31_OSD_IPU_TEST.md` deckt ausschließlich
Bitmap- und Cover-Ebenen ab; für LINE/RECT-Geometrie existiert dort kein Fall. Ein solcher
müsste neu angelegt werden.

**Die sichtbare Änderung ist gewollt, aber eine Abweichung vom Stock:** Diagonalen werden
**dünner** gezeichnet als bisher. Bestehende OSD-Layouts mit Diagonalen sehen danach anders
aus. Wenn „byte-gleich zum Stock-OSD" ein hartes Projektkriterium ist, braucht dieser Commit
eine ausdrückliche Abnahme. Achsenlinien, Rechtecke und `lw = 1` bleiben unverändert
(byte-gleich).

**Ehrliche Korrektur einer früheren Annahme:** Die verbreitete Vermutung, der Stock-Strich sei
bei 45° um √2 zu dünn, ist **falsch** — er ist zu **breit** (Faktor bis 1,41, siehe 4.1). Die
Messungen in 5.2 sind der Beleg. Diese Korrektur steht auch als Kommentar im Header und im Test.

---

## 7. Grenzen des Nachweises

- Gemessen wird die Breite über Innen-Schritte (mindestens `lw + 4` Pixel vom Ende), nicht an
  den Kappen. Die Kappen sind Parallelogramm-Ecken und bis `lw/2 + 0,5` px über die Normale
  hinaus — bewusst so, siehe 4.2.
- Der Winkelsweep ist jetzt vollständig (0…180° in 1°-Schritten für `lw 2…8`), aber die
  Dickenprüfung gilt nur für die Innen-Schritte; die Endpunkt-Geometrie ist nicht 1:1 gegen den
  Stock geprüft (nur `lw = 1` ist überall byte-gleich).
- `lw = 0` wird nur über den Guard abgedeckt (`lw <= 0` → sofortiges Return), nicht getestet.
- Der Benchmark ist Host-`-O2` ohne Target-Flags; er belegt die Größenordnung, nicht die
  Gerätelaufzeit.
- Byte-Identität ist gegen den **eingefrorenen** Stock-Drawer aus `4c1ffe1` geprüft, nicht
  gegen Geräteaufnahmen der Stock-`libimp`.

---

## 8. Regeln (Zustand)

- **Kein Push.** Der Branch ist lokal, `git status` sauber.
- **Kein anderer Branch/Worktree** angefasst (`git worktree list` unverändert).
- **Keine Kamera** benutzt.
- Änderungen ausschließlich auf `claude/osd-thick-lines`.
