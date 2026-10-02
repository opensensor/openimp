# T31: IVS-Bewegungserkennung („move“ und „base move“) in OpenIMP

Stand: Branch `claude/t31-ivs-move`. Unter qemu gebaut und getestet, **noch nicht auf einer
Kamera gelaufen.** Abschnitt 6 beschreibt den Gerätetest für eine lokale Claude-Session.

Bisher waren die `IMP_IVS_*`-Funktionen für T31 nur Stubs: `PollingResult` hat nur
geschlafen, `GetResult` hat leere Ergebnisse geliefert. Jetzt rechnen beide
Vendor-Algorithmen wirklich, mit dem Verhalten der T31-libimp 1.1.6.

---

## 1. Dateien

| Datei | Inhalt |
|---|---|
| `src/t31/openimp_t31_ivs_abi.h` | Strukturen im Vendor-Layout, mit `_Static_assert` (0x450 `IMP_IVS_MoveParam`, 0x40 `IMP_IVS_BaseMoveParam`, 0x34 `IMPIVSInterface`, 0x30 `IMPFrameInfo`) |
| `src/t31/openimp_t31_ivs_move.c/.h` | die beiden Algorithmen, reines C ohne Abhängigkeiten |
| `src/t31/openimp_t31_ivs.c/.h` | IVS-Framework (`IMP_IVS_*`), Interfaces, Frame-Hook |
| `src/kernel_interface.c` | ruft den Frame-Hook pro Capture-Frame auf (nur T31) |
| `src/t31/openimp_t31_services.c` | alte Stubs nur noch für T21/T23/T30 |
| `tools/t31_ivs_compare.c` | Vergleich mit einer Stock-libimp auf synthetischen Frames |

Die generischen Header `include/imp/imp_ivs*.h` passen **nicht** zum T31-ABI
(`IMPFrameInfo` hat dort nur 8 Byte, `IMPRect` ist x/y/w/h). Sie werden für T31 nicht benutzt.

---

## 2. Verhalten

### 2.1 move (ein Ja/Nein pro ROI)

- Das Luma wird 2:1 dezimiert: das Pixel oben links aus jedem 2×2-Block.
- Ein Ring aus 4 Halbbildern. Verglichen wird immer Eingangsframe t mit Frame t−3.
- Binäre Differenz `|A−B| > 20`.
- 3×3-Erosion: ein Pixel bleibt nur, wenn alle 9 Nachbarn gesetzt sind. Am Bildrand wird
  repliziert.
- Pro ROI werden die übrigen Pixel gezählt. `retRoi[i] = count > T[sense]`.
- Schwellen T (Pixel in halber Auflösung):

  | sense | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
  |---|---|---|---|---|---|---|---|---|---|
  | T | 1365 | 455 | 151 | 50 | 16 | 8 | 4 | 2 | 1 |

- Beim Anlegen wird sense auf 0…4 geklemmt. Über `IMP_IVS_SetParam` sind 0…8 erlaubt.
- Die ersten 3 Frames liefern Ergebnisse mit lauter Nullen.
- Danach kommt ein Ergebnis alle `skipFrameCnt + 1` Frames. Das erste bei Frame
  `skipFrameCnt + 3`.
- ROI-Koordinaten: erst auf das Bild klemmen, dann halbieren. Die Ecken sind inklusiv.
- Vendor-Eigenheit, nachgebaut: Beginnt die Bounding-Box aller ROIs nicht in Zeile 0, ist die
  Erosion um eine Zeile nach unten versetzt. Ausgabezeile y ist dann um Bildzeile y+1
  zentriert. Bei einem vollen Raster (timps ohne Privacy-Maske) tritt das nicht auf.
- `SetParam` übernimmt nur `sense[]`, `roiRect[]` und `roiRectCnt`. `skipFrameCnt` und
  `frameInfo` bleiben. Der Ring und der Frame-Zähler bleiben erhalten.

### 2.2 base move (ein Byte pro 8×8-Block)

- Volle Auflösung. Frame n wird mit Frame n − `referenceNum` verglichen, alle
  `skipFrameCnt + 1` Frames.
- `T = |A−B|`, wenn `|A−B| ≥ thr`, sonst 0. thr nach sense: 30, 20, 15, 10 für 0…3, sonst 0.
- 3×3-Minimum (Rand repliziert), dann Summe pro 8×8-Block, **auf 8 Bit abgeschnitten**.
- Nur Breite und Höhe als Vielfache von 8 liefern Daten.
- Es wird **jeder Frame** ein Ergebnis veröffentlicht. `ret = 1` nur auf Detektionsframes.
  Bei `ret = 0` enthält `data` alten Inhalt. Konsumenten müssen `ret` prüfen.
- `sadMode` muss 0 sein, `skipFrameCnt ≥ 0`, `referenceNum ≥ 1`. Sonst scheitert
  `IMP_IVS_CreateChn`.
- `SetParam`: nur `sense` wirkt.

### 2.3 Framework

- Nur IVS-Gruppe 0, Kanäle 0…63.
- Pro Kanal ein Thread `ivs_chnN` und drei Semaphoren (Start = 0, Ende = 1, Ergebnis = 0).
- Jeder Frame des FrameSource-Kanals, der an Gruppe 0 gebunden ist, erreicht jeden
  empfangenden Kanal. Ist ein Kanal noch mit dem vorigen Frame beschäftigt, wird der neue Frame
  für ihn **verworfen**.
- `IMP_IVS_SetParam` kopiert nach `interface->param`. Die Änderung wirkt beim **nächsten
  angenommenen Frame**. `IMP_IVS_GetParam` liefert bis dahin noch den alten Stand.
- Ergebnisse liegen in einem Ring mit 6 Einträgen. `GetResult` liefert einen Zeiger hinein und
  prüft nicht, ob ein Ergebnis da ist. `ReleaseResult` tut nichts. Wer mehr als 6 Ergebnisse
  zurückliegt, liest überschriebene Daten (wie beim Vendor).
- `PollingResult(chn, 0)` prüft nur. Negativer Timeout (`IMP_IVS_DEFAULT_TIMEOUTMS`) = 10 s.
- `StopRecvPic` wartet bis 1 s auf einen laufenden Frame, dann `flushFrame`.
- `DestroyChn` stoppt, meldet ab, wartet auf wartende Aufrufer und beendet den Thread.

### 2.4 Frame-Hook

- In OpenIMP trägt `IMP_System_Bind` nur in eine Tabelle ein. Es gibt keine Observer für
  `DEV_ID_IVS`, und der Encoder holt Frames aus der Ready-Queue der FrameSource.
- Deshalb ruft `VBMKernelDequeue` für **jeden** Capture-Frame `openimp_t31_ivs_capture()` auf,
  **bevor** der Frame in die Ready-Queue kommt.
- Vorteile:
  - Das entspricht der Vendor-Stelle (`ivs_update` im FrameSource-Kontext, jeder Frame).
  - Der Puffer gehört in diesem Moment nur dem Capture-Thread. Der Encoder kann ihn weder
    freigeben noch OSD hineinmischen.
  - Kein Encoder-Lock ist beteiligt.
- Dort passiert nur die Kopie:
  - move: die 2:1-Dezimation, und nur für Frames, die ein späterer Vergleich liest.
    Ausnahme seit `claude/ivs-opt`: Wird der neue Frame später nicht mehr als Referenz
    gebraucht (skipFrameCnt 1 oder ≥ 3), läuft die Detektion gleich hier direkt auf dem
    Capture-Puffer (siehe 2.6).
  - base move: das Luma der Capture-Frames.
- Vor der Kopie wird der Luma-Bereich im Cache invalidiert (`DMA_RmemFlushCache(virt, size, 2)`),
  weil der ISP per DMA in gecachtes rmem schreibt.
- Das Rechnen läuft im Kanal-Thread.
- Die Quelle der Gruppe wird per `IMP_System_GetBindbyDest` rückwärts gesucht (normal
  FS → IVS direkt).

### 2.5 Kosten (qemu, nur als Verhältnis aussagekräftig)

Gemessen gegen eine einfache Referenz-Implementierung desselben Verhaltens, gleiche Eingaben:

| Fall | OpenIMP | Referenz | Faktor |
|---|---|---|---|
| move 640×360, skip 5, 52 ROIs, pro Frame | 51 µs | 237 µs | 4,6 |
| move 1920×1080, skip 5, 52 ROIs, pro Frame | 346 µs | 1,87 ms | 5,4 |
| move 1920×1080, eine Detektion | 1,45 ms | 8,4 ms | 5,8 |
| move 1920×1080, eine Dezimation | 0,41 ms | 0,97 ms | 2,4 |
| base move 640×360, Detektion jeder Frame | 1,3 ms | 9,7 ms | 7,5 |
| base move 1920×1080, Detektion jeder Frame | 9–14 ms | 77–83 ms | 6–9 |

- Der Vendor kopiert bei move in **jedem** Frame das volle Luma (2 MB bei 1080p). OpenIMP
  dezimiert bei skip 5 nur 2 von 6 Frames.
- Die echten Zeiten auf der Kamera misst der Gerätetest (`OPENIMP_T31_IVS_STATS=1`).

### 2.6 Beschleunigung (Branch `claude/ivs-opt`)

Anlass: T20 (Wyze, 1080p-Hauptstream) zeigte timps mit Bewegungserkennung bei ~8 % CPU,
ohne bei ~0,4 %. Die Ergebnisse bleiben bitgleich.

- **Erosion auf Abruf:** Ausgabezeile y ist das UND der erodierten Zeilen y+sh−1, y+sh,
  y+sh+1. Eine leere Zeile löscht also drei Ausgabezeilen. Die Zeilen werden erst bei
  Bedarf berechnet, die unterste zuerst. In einer ruhigen Szene wird nur jede dritte Zeile
  differenziert.
- **Detektion direkt auf dem Capture-Puffer:** Liest kein späterer Vergleich den neuen
  Frame (skipFrameCnt 1 oder ≥ 3), dezimiert `feed()` ihn nicht mehr komplett in den Ring.
  Es rechnet die Detektion sofort gegen das Referenzbild und dezimiert nur die Zeilen,
  die sie liest. `run()` vergleicht dann nur noch mit den Schwellen. Folge für die
  Statistik: `copy` (Capture-Thread) enthält bei diesen Frames die Detektion, `process` ist
  fast null.
- **Differenztest in Byte-Lanes:** |a − b| > 20 für vier Pixel pro Wort über Floor- und
  Ceiling-Mittelwert von a und 255 − b (≥ 138 bzw. ≤ 117). Bisher liefen zwei 16-Bit-Lanes.
- **Optional, nur auf Wunsch:** `OPENIMP_IVS_MOVE_INTERVAL=N` (2…1000). Ein move-Kanal
  sieht nur jeden N-ten Capture-Frame; `skipFrameCnt` zählt dann diese Frames. Ohne die
  Variable (oder mit 1) bleibt alles wie beim Vendor. base move und fremde Interfaces sind
  nicht betroffen.
- **Prüfung:** `make -C tests/t30 check` lässt `ivs_move_bench_t20 equiv` laufen. Das
  vergleicht den neuen Code Frame für Frame mit einer eingefrorenen Kopie des alten
  (`tests/t23/ivs_move_ref.c`): 10 Größen bis 1920×1080 (auch ungerade), skip 0…9,
  Gitter- und Zufalls-ROIs, Live-SetParam mit sense 0…8, fünf Szenen. `tests/t23` prüft
  zusätzlich den festen Digest `ca3d1021f2a30c82` von `ivs_move_digest`.
- **Host-Messung:** `make -C tests/t30 ivs-bench` (x86-64, gcc -O2, 4×4-ROI-Gitter,
  ns pro Eingangsframe für feed + run, Szenen: ruhig mit ±3 Rauschen, Nacht ±24, bewegtes
  Rechteck):

| Fall | vorher | nachher | Faktor |
|---|---|---|---|
| 1080p ruhig, skip 5 | 175 µs | 82 µs | 2,1 |
| 1080p ruhig, skip 0 | 744 µs | 417 µs | 1,8 |
| 1080p Bewegung, skip 5 | 184 µs | 118 µs | 1,6 |
| 1080p Nacht, skip 5 | 175 µs | 165 µs | 1,06 |
| 360p ruhig, skip 5 | 14,4 µs | 6,6 µs | 2,2 |
| 360p ruhig, skip 0 | 68 µs | 32 µs | 2,1 |
| 360p Bewegung, skip 5 | 14,9 µs | 9,6 µs | 1,6 |
| 360p Nacht, skip 5 | 14,5 µs | 13,8 µs | 1,05 |

- Nicht gemacht: MXU-SIMD (kein `-mmxu`/MXU-Code im Projekt, die Toolchain wird ohne
  gebaut). Arbeit ganz auslassen, wenn niemand pollt: timps pollt dauernd, also kein
  Gewinn, aber ein Verhaltensrisiko.

---

## 3. Abweichungen vom Vendor

1. **Zeile hinter dem Puffer (move).** Beginnt die ROI-Box unterhalb von Zeile 0 **und** reicht
   sie bis zur untersten Zeile, liest der Vendor eine Zeile hinter seinem Puffer (Heap-Müll).
   OpenIMP wiederholt stattdessen die letzte Zeile.
   - Betrifft nur ROIs in der untersten Zeile, nur in diesem Layout.
   - timps ohne Privacy-Maske ist nicht betroffen (Box beginnt in Zeile 0).
   - Im qemu-Vergleich: 49 von 2880 Frames dieses Layouts weichen ab, alle in einer unteren ROI.
2. **Ungültiges sense bei SetParam (move).** Der Vendor bricht mittendrin ab und lässt einen
   freigegebenen Filter zurück (Use-after-free). OpenIMP prüft vorher und behält den alten
   Stand.
3. **Frame-Größe.** Der Vendor kopiert `width × height` des Frames in einen Puffer nach
   `frameInfo`. Bei einem größeren Frame läuft der Heap über. OpenIMP verwirft Frames, deren
   Größe nicht zu `frameInfo` passt, und meldet das einmal im Log.
4. **Stride.** Luma wird zeilenweise mit dem Stride gelesen. In OpenIMP T31 ist er gleich der
   Breite (wie im JPEG- und OSD-Pfad). Der Vendor setzt das stillschweigend voraus.
5. **Fehler in `processAsync`.** Der Vendor beendet dann den Kanal-Thread, danach kommen nie
   wieder Ergebnisse. OpenIMP loggt einmal und macht weiter.
6. **Überflüssige Arbeit entfällt.** Frames, die kein Vergleich liest, werden nicht dezimiert.
   Das Ergebnis ist identisch.
7. **Kein Debug-Ballast.** base move schreibt kein `total_count` auf stdout und hängt keine
   Frames an `/tmp/mountdir/ivsbasemovesnap.nv12` an.
8. **IVS sieht Frames ohne OSD.** Der Hook liegt vor dem Encoder und damit vor dem OSD-Blending.
   Ob der Vendor IVS mit oder ohne OSD füttert, ist nicht geprüft. Ohne OSD gibt es keine
   Fehlalarme durch die Uhrzeit-Einblendung.
9. **Kleinigkeiten:**
   - Kanal 64 wird abgelehnt. Der Vendor nimmt ihn an, bedient ihn aber nie.
   - `DestroyChn` stoppt und meldet einen Kanal selbst ab, statt ihn hängen zu lassen.
   - `PollingResult` wartet auf der monotonen Uhr. Ein Sprung der Systemzeit (NTP beim Boot)
     verlängert oder verkürzt den Timeout nicht.
   - Ungerade Bildgrößen: ROI-Koordinaten werden zusätzlich auf das Halbbild geklemmt.
   - Von Streamern selbst gebaute Interfaces bekommen `preProcessSync` auf dem Capture-Frame
     und `processAsync` auf einer privaten NV12-Kopie. OpenIMP kann für sie keinen
     Capture-Puffer festhalten. `IMP_IVS_ReleaseData` tut deshalb nichts.

---

## 4. Tests unter qemu (erledigt)

- **Algorithmen gegen die Referenz**, bitgenau: 82 880 move-Frames (42 328 Ergebnisse) und
  70 560 base-move-Frames (22 365 Detektionen), 0 Abweichungen.
  - Größen 640×360, 320×240, 64×48, 66×50, 96×34, 128×72.
  - skipFrameCnt −1, 0, 1, 2, 3, 5, 9; referenceNum 1, 3, 5, 7; sense −1…4 (move per
    SetParam bis 8).
  - Bewegte Quadrate, Rauschen, Helligkeitssprünge, Flackern, statische Szene.
  - ROI-Sonderfälle: einzelne Pixel, Ränder, außerhalb des Bildes, leere und überlappende ROIs,
    52 ROIs, Box ab Zeile > 0.
  - Stride größer als die Breite, auch nicht durch 4 teilbar.
- **Gegen die Stock-libimp (Skalarpfad)** mit `tools/t31_ivs_compare.c`: 8591 von 8591
  move-Frames und 11 520 von 11 520 base-move-Frames identisch, plus die 49 Frames aus
  Abweichung 1.
- **Framework** (Semaphoren, Drop bei Belegt, SetParam beim nächsten Frame, falsche
  Frame-Größe, fremder FS-Kanal, fremde Interfaces, Timeouts, DestroyChn während
  PollingResult wartet): alles bestanden, unter qemu und nativ mit ThreadSanitizer und
  AddressSanitizer (Create/Start/Stop/Destroy in einer Schleife, während Frames fließen).

---

## 5. Offene Fragen, die nur die Kamera beantwortet

1. **SIMD = Skalar beim Vendor?** Auf der Kamera rechnet die Stock-libimp mit MXU2. Stimmen
   deren Ergebnisse mit dem Skalarpfad (und damit mit OpenIMP) überein? → Stufe 1.
2. **Cache:** Sieht die CPU nach dem Invalidieren das aktuelle ISP-Frame? Zeichen für ein
   Problem: bei Bewegung kaum Treffer oder Treffer ohne Bewegung.
3. **Stride:** Ist der NV12-Stride der T31-FrameSource bei allen genutzten Auflösungen gleich
   der Breite?
4. **Kosten:** Kopierzeit im Capture-Thread und Rechenzeit im IVS-Thread bei 1080p und 2K. Stört
   die Kopie die Bildrate des Hauptstreams?
5. **„Gefühl“:** Reagiert die Erkennung bei sense 0…4 wie mit der Stock-libimp?

---

## 6. Gerätetest (lokale Claude-Session mit SSH zur T31-Testkamera)

### 6.0 Regeln

1. Nur die eine Testkamera.
2. Nichts dauerhaft ändern:
   - nicht flashen;
   - nichts unter `/etc` oder im Overlay ändern, auch nicht über `POST /control` gegen die
     normale Konfiguration (timps speichert dann nach `/etc/timps.conf`);
   - neue Dateien nur nach `/tmp`;
   - die neue `libimp.so` nur per Bind-Mount einhängen. Ein Neustart stellt alles wieder her.
3. Vor jedem Stoppen des Streamers kurz Bescheid geben.
4. **Kamerabilder** (Snapshots, Aufnahmen) nur mit Einverständnis des Menschen weitergeben.
   Die Stufe 1 braucht keine Kamerabilder.
5. Alles melden, auch Fehlschläge, mit dem genauen Befehl davor.

### 6.1 Bauen (auf dem Rechner)

```sh
cd openimp && git switch claude/t31-ivs-move
TC=<thingino>/output/<branch>/<target>/host/bin/mipsel-linux
THINGINO_DIR=<thingino> TOOLCHAIN_PREFIX=$TC T31_OUTPUT_DIR=$PWD/build/t31 sh build-t31.sh
# Vergleichstool; dynamisch und mit derselben (uclibc-)Toolchain, weil es die
# Stock-libimp per dlopen lädt:
$TC-gcc -O2 -march=mips32r2 -rdynamic -Isrc/t31 -o build/t31/t31_ivs_compare \
    tools/t31_ivs_compare.c src/t31/openimp_t31_ivs_move.c -ldl
scp build/t31/libimp.so root@<kamera>:/tmp/libimp-ivs.so
scp build/t31/t31_ivs_compare root@<kamera>:/tmp/
```

Die Stock-libimp für den Vergleich: T31 SDK 1.1.6, uclibc
(`ingenic-lib/T31/lib/1.1.6/uclibc/4.7.2/libimp.so`, md5 `6ba99769…a818`). Nach
`/tmp/libimp-stock.so` kopieren. Ist sie nicht greifbar, den Menschen fragen oder Stufe 1
überspringen.

### 6.2 Stufe 1: Algorithmen gegen die Stock-libimp (ohne Streamer-Stopp)

Das Tool rechnet nur auf synthetischen Frames. Es braucht weder ISP noch Kamerabild und läuft
neben dem Streamer.

```sh
grep -i 'ASEs' /proc/cpuinfo            # steht mxu_v2 drin?
rm -f /tmp/closesimd
/tmp/t31_ivs_compare /tmp/libimp-stock.so 2>&1 | tee /tmp/ivs-compare-simd.txt
touch /tmp/closesimd                    # erzwingt den Vendor-Skalarpfad
/tmp/t31_ivs_compare /tmp/libimp-stock.so 2>&1 | tee /tmp/ivs-compare-scalar.txt
rm -f /tmp/closesimd                    # wichtig: sonst rechnet auch ein Stock-Streamer skalar
```

- Die erste Zeile nennt den Pfad (`MXU2 SIMD` oder `scalar`).
- **Erfolg:** in beiden Läufen `differ=0` bei move und base. `oob-differ` ist erlaubt
  (Abweichung 1).
- Abweichungen im SIMD-Lauf sind ein wichtiges Ergebnis, kein Abbruchgrund. Die ersten
  `MOVE DIFF`/`BASE DIFF`-Zeilen melden.
- Scheitert `dlopen` (fehlende Symbole, falsche libc), die Meldung zurückgeben.

### 6.3 Stufe 2: timps mit OpenIMP und Bewegungserkennung

```sh
cp /usr/lib/libimp.so /tmp/libimp-orig.so
mount --bind /tmp/libimp-ivs.so /usr/lib/libimp.so
cp /etc/timps.conf /tmp/timps-ivs.conf
```

In `/tmp/timps-ivs.conf` setzen (nur in der Kopie):

```
motion.enabled = 1
motion.monitor_stream = 0
motion.sensitivity = 128
motion.skip_frames = 5
```

Streamer von Hand starten, mit Statistik:

```sh
/etc/init.d/S95timps stop
OPENIMP_T31_IVS_STATS=1 /usr/bin/timpsd -c /tmp/timps-ivs.conf > /tmp/run-ivs.log 2>&1 &
sleep 30
grep -E '\[IVS\]|motion' /tmp/run-ivs.log | tail -20
logread 2>/dev/null | grep -E 'IVS|motion' | tail -20
```

Prüfen, dass die Variable ankommt:
`tr '\0' '\n' < /proc/$(pidof timpsd)/environ | grep OPENIMP`

Messungen:

1. **Start:** Meldet timps `motion detection started (…)`? Kein `CreateChn failed`, kein
   `does not match the configured` von `[IVS]`.
2. **Statistik** (alle 10 s eine Zeile pro Kanal):
   `[IVS] chn0: <n> frames, <d> dropped busy, <r> results; copy avg … max …; process avg … max …`
   - `frames` ≈ Bildrate × 10.
   - `results` ≈ `frames / (skip_frames + 1)` minus Drops.
   - `copy` = Zeit im Capture-Thread, `process` = Rechenzeit im IVS-Thread.
3. **Ruhige Szene** (niemand im Bild, 2 Minuten):
   `curl -s http://127.0.0.1:8880/control` (Token nach Bedarf), im Objekt `motion`: `active[]`
   bleibt 0, `last_ms` ändert sich nicht oder selten. Kein `stalled: 1`.
4. **Gehtest:** Mit Einverständnis des Menschen durchs Bild gehen (oder eine Hand vor die
   Linse). `active[]` zeigt die Zellen, `last_ms` wird aktuell. Live mitlesen:
   `curl -N "http://127.0.0.1:8880/events?stream=motion"`.
5. **Empfindlichkeit live:** Weil timps mit `-c /tmp/timps-ivs.conf` läuft, speichert
   `POST /control` nur in die Kopie:
   `curl -s -X POST http://127.0.0.1:8880/control -d '{"motion":{"sensitivity":255}}'`,
   dann `0`. Im Log: `motion sensitivity updated live to …`. Erwartung: bei 255 (sense 4)
   deutlich mehr Treffer als bei 0 (sense 0).
6. **Last und Bildrate:** `top -b -n 3 | head -20` mit `motion.enabled = 1` und mit `0`
   (zweiter Lauf). Bildrate des Hauptstreams aus `/control` (`stats`) in beiden Läufen. Einen
   RTSP-Client 2 Minuten verbinden, auf Ruckler achten.
7. **Nacht/IR** (optional): im Dunkeln mit IR ruhige Szene 2 Minuten. Viele Fehlalarme melden.
8. **Stop/Start:** `curl … -d '{"motion":{"enabled":0}}'`, dann `1`. Kein Absturz, danach wieder
   Ergebnisse (Statistikzeile).

**Abbruchkriterium:** Absturz, eingefrorener Stream oder ständig `stalled: 1`. Dann
`logread`, `dmesg` und `/tmp/run-ivs.log` sichern und melden.

prudynt statt timps: dieselben Schritte. Die Konfiguration nach `/tmp` kopieren, die
Bewegungserkennung dort einschalten und den Konfigpfad beim Start übergeben (Option mit
`prudynt --help` prüfen). Ergebnisse stehen dort im Log.

### 6.4 Stufe 3 (optional): Vergleich mit dem Stock-Stack

Nur mit einer zweiten T31-Kamera mit Original-Stack (Stock-Kernelmodule und Stock-libimp),
gleiche Szene, gleiche Einstellungen. Die Stock-libimp läuft nicht auf dem offenen Stack.

- Gehtest wie in 6.3, Punkt 4, einmal ohne und einmal mit `touch /tmp/closesimd` vor dem
  Streamer-Start (danach `rm -f /tmp/closesimd`).
- Vergleichen: Welche Zellen lösen aus, wie schnell, wie viele Fehlalarme bei ruhiger Szene.
- Erwartung: gleiches Verhalten, weil die Algorithmen bitgleich sind. Unterschiede kommen dann
  von Eingangsbild, Bildrate oder OSD.

### 6.5 Aufräumen

```sh
kill $(pidof timpsd); umount /usr/lib/libimp.so; /etc/init.d/S95timps start
rm -f /tmp/closesimd
```

Oder die Kamera neu starten. Danach prüfen, dass der Streamer normal läuft.

### 6.6 Was zurückmelden

- Kopf: Modell, SoC (`/proc/cpuinfo`, auch die Zeile `ASEs implemented`), thingino-Stand,
  Kernel, Streamer und Version, Commit (`git rev-parse --short HEAD`).
- Stufe 1: beide Ausgaben von `t31_ivs_compare` vollständig.
- Stufe 2:
  - die `[IVS]`-Statistikzeilen (mindestens 1 Minute) für Hauptstream 1080p oder 2K;
  - `top`-Werte mit und ohne Bewegungserkennung, Bildrate beider Läufe;
  - Ergebnis von ruhiger Szene, Gehtest, Empfindlichkeit, Stop/Start;
  - Logauszüge bei Fehlern.
- Stufe 3, falls gemacht: Beobachtungen im Vergleich.
- Keine Kamerabilder ohne Einverständnis.

## 7. T23 (T23 SDK 1.3.0)

Seit Branch `claude/t23-stub-fixes` baut `build-t23.sh` dieselben Dateien
(`openimp_t31_ivs.c`, `openimp_t31_ivs_move.c`) mit `PLATFORM_T23`; die IVS-Stubs in
`openimp_t31_services.c` gelten nur noch für T21/T30.

- **Vendor-Abgleich (Disassembly von `libimp.a` 1.3.0 gegen T31 1.1.6):** `ivs.c`,
  `move_ivs.c`, `base_move_ivs.c`, `ivs_move.c`, `ivs_base_move.c`, `filter.c`, `sad.c` sind
  bis auf Struktur-Offsets identisch. Wo T31 einen MXU2-Zweig hat (`resize`,
  `MorphRowFilter`, `MorphColumnFilter`, `move_detect`, `Proceed`, `MergeBaseMove`), hat T23
  nur den skalaren Zweig und ein `assert` statt des SIMD-Codes; Konstanten
  (Schwellen 1365/455/151/50/16, 20, 30/20/15/10) und Tabellen sind gleich. OpenIMP bildet
  den skalaren T31-Pfad bitgenau nach, also auch T23.
- **ABI-Unterschied:** nur `IMPFrameInfo` (0x38 statt 0x30 Byte: `direct_phyAddr` bei 0x20,
  `timeStamp` bei 0x28, `timeStamp_ivdc` bei 0x30). Daraus folgt `IMP_IVS_MoveParam` 0x458
  (roiRect 0x110, roiRectCnt 0x450), `IMP_IVS_BaseMoveParam` 0x48 und das öffentliche
  `IMP_IVS_BaseMoveOutput.timeStamp` bei 0x10. `openimp_t31_ivs_abi.h` prüft das mit
  `_Static_assert`.
- **Frame-Hook und Idle-Drain:** wie T31 (`VBMKernelDequeue` → `openimp_t31_ivs_capture`,
  Rückgabe der Puffer an den Treiber, solange niemand `GetFrame` aufruft, und
  `VBMRecycleIdleFrames` nach leerem `select`), jetzt auch für `PLATFORM_T23`.
- **Hosttests:** `make -C tests/t23 check` vergleicht die Algorithmen mit T31- und T23-Layout
  (gleicher Digest, 3708 move-Ergebnisse, 5120 base-move-Detektionen) und fährt das
  Framework mit T23-Capture-Records von Group/Chn/Bind bis Poll/Get/Release durch.
- **Gerätevergleich:** `tools/t31_ivs_compare.c` mit `-DPLATFORM_T23` bauen und gegen
  `/opt/openimp-t23/libimp.so` (die Stock-libimp des Helix-Workers) laufen lassen.
