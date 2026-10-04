# T31-Testergebnisse, Branch `claude/t31-all`

Testlauf am 2026-09-30 nach [`docs/T31_TEST_PLAN.md`](../../T31_TEST_PLAN.md), ausgeführt von
einer lokalen Claude-Code-Session per SSH. Die Kamera ist im Bericht anonymisiert (`cam-A`,
`192.0.2.x`), die Dateien unter `test-results/` ebenso.

## Kopf

| | |
|---|---|
| Kamera | WUUK Y0510, Profil `wuuk_y0510_t31x_sc4336p_ssv6158`, Sensor SC4336P (2560×1440) |
| SoC | T31X (`/proc/cpuinfo`: `system type: Swan`, `cpu model: Ingenic Xburst V0.0 FPU V0.0`, `isa: mips32r1`, **`ASEs implemented:` leer**, also kein MXU2) |
| Speicher | `mem=78M@0x0 rmem=50M@0x4e00000` |
| thingino | Branch `ciao` (`BUILD_ID="ciao+64ae16c, 2026-09-28 14:03:37 UTC"`), Buildroot 2026.08, uClibc, GCC 16 |
| Kernel | `3.10.14__isvp_swan_1.0__ #2 PREEMPT Mon Sep 28 15:58:36 CEST 2026` |
| ISP/AVPU | **Original-Stack**: `tx-isp-t31.ko` (Author „Ingenic xhshen"), `avpu.ko` (Allegro), `sensor_sc4336p_t31.ko` |
| libimp im Flash | **Original-libimp** T31 SDK 1.1.6, uclibc/5.4.0 (md5 `97ca48de99f9039bd9550ba40c0ae7eb`) |
| Streamer | timps v1.9.28 (Init-Skript `S95timps`, `/usr/bin/timpsd -c /etc/timps.conf`) |
| Main/Sub | H.264 1920×1080 bzw. 640×360, 15 fps, 1200/384 kbit/s, Snapshot aus JPEG-Kanal auf Gruppe 0 |
| Getesteter Commit | `claude/t31-all` = `3f501a2` (mit rmem-Fix; erster C-Lauf mit `014edbc`, Vergleich in C zusätzlich `main` = `9cab219`) |
| Toolchain | thingino-Toolchain des Kameraprofils (`mipsel-linux-gcc` 16.1.0) |
| Szene | dunkler Innenraum, Nachtmodus mit IR, statisch |

### Abweichung vom Testplan: Original-Stack statt offener Stack

Der Testplan setzt eine Kamera mit thingino `aperto` und offenem Stack (open-tx-isp + OpenIMP)
voraus. Die Testkamera läuft mit dem **Original-Stack** (Stock-tx-isp, Original-libimp). Nach
Rücksprache mit dem Menschen wurde trotzdem getestet:

- A1 und A2 sind davon unabhängig (brauchen nur `/dev/avpu` und `/dev/ipu`).
- Ab B läuft OpenIMP per Bind-Mount auf den **Stock-Kernelmodulen**. Laut README unterstützt
  OpenIMP diese Treiber. Die Referenz in C ist deshalb die **Original-libimp** (statt
  Original-OpenIMP) und zusätzlich OpenIMP `main`.
- Alle manuellen timps-Starts laufen mit einer Kopie der Konfiguration unter `/tmp/timps.conf`,
  weil timps Einstellungen (z. B. `image.running_mode` beim Tag/Nacht-Wechsel) zurückschreibt.
  Nach `/etc` wurde nichts geschrieben.
- Ein thingino-Image mit offenem Stack für dieses Profil wurde lokal gebaut (siehe Anhang),
  aber nicht geflasht.

## Übersicht

| Test | Status | Kernaussage |
|---|---|---|
| A1 HW-JPEG-Tool | bestanden | gültige JPEGs mit Hardware-Headern, 7,85 ms für 1080p, DRI/Overflow wie erwartet |
| A2 IPU-OSD-Tool | bestanden | alle Ebenen korrekt; Abdeckfarbe: IPU übernimmt Y ohne +16 |
| B Bauen/Einhängen | bestanden | alle 126 von timps benötigten Symbole vorhanden |
| C Regression | bestanden mit `3f501a2` | `014edbc` (und `main`) scheiterten am rmem-Leck; Fix hält 25 Zyklen, No-Reuse reproduziert den alten Fehler |
| S1 Dauerlauf 2 h | bestanden | 108002 Frames je Stream, 690/690 Snapshots, alle AVC-Zähler 0; kein Hänger, Recovery also nicht ausgelöst |
| S2 Snapshot-Last | bestanden mit Anmerkung | keine Aussetzer; OpenIMP-Zeitstempel unruhiger als Original, mit HW-JPEG am stärksten |
| I Stufe 1 | bestanden | move und base `differ=0` (Skalarpfad; CPU ohne MXU2); Vergleichstool hatte ein Speicherleck (behoben) |
| I Stufe 2 | bestanden mit Befund | mit Client: 0 Fehlalarme, PTZ-Bewegung erkannt, live-Empfindlichkeit, Stop/Start ok; **ohne Client liefert die Erkennung nichts (stalled)** |
| G OSD | bestanden | OSD in RTSP und Snapshot sichtbar; timpsd 7,9 % statt 13,0 % (Original) |
| E/H JPEG | bestanden (H teilweise) | HW-JPEG: 18,5 % statt 66,9 % CPU, 0,28 s pro Snapshot, Bilder korrekt |
| D Companion-Stage | bestanden | ohne Companion-Stage kein Nachteil |
| F, S3, S4 | übersprungen | keine T20; Zeit |
| open-tx-isp per insmod | nicht möglich | `rmmod tx_isp_t31` (Stock) → Kernel-Oops |
| **Befund Beenden** | offen | 5 von ~40 Stopps: `FS(n)-tick` dreht im Kernel, timpsd bleibt Zombie, nur Neustart hilft |

---

## A1. Hardware-JPEG-Testtool – **bestanden**

Gebaut mit `mipsel-linux-gcc -O2 -march=mips32r2 -static -Wall -Wextra` (thingino-Toolchain),
timps dafür gestoppt. Dateien: [`a1/`](a1/).

```sh
/tmp/t31_hwjpeg_probe -v -e /tmp/ep1.bin 2>&1 | tee /tmp/probe-1080p.txt
/tmp/t31_hwjpeg_probe -W 640 -H 360 -o /tmp/hwjpeg-360.jpg 2>&1 | tee /tmp/probe-360p.txt
```

| Lauf | Status `0x8430` | Länge | `0x8438` | HW-Zeit | Ergebnis |
|---|---|---|---|---|---|
| 1920×1080 | `0x0004039e` | 52012 | 0 | 7,85 ms | SOI + EOI, JFIF-Header von der Hardware |
| 640×360 | `0x000075a8` | 7703 | 0 | 0,91 ms | SOI + EOI |
| `-c 0x31` (Bit 8 aus), 1080p und 360p | wie oben | 52012 / 7703 | 0 | 7,85 / 0,91 ms | **bitgleich** zum Lauf mit Bit 8 |
| `-W 640 -H 360 -c 0x00080131` (DRI 8) | `0x000075a8` | 8203 | 0 | 0,92 ms | `FF DD` vorhanden, **114 RST-Marker** `D0…D7` (920 MCUs / 8 = 115 Intervalle, passt) |
| `-W 640 -H 360 -s 4096` | `0x000075a8` | 7703 | **`0x2`** | 0,91 ms | Overflow-Bit wie erwartet; Tool meldet `implausible length 7703` |
| `-s 65536` (1080p) | wie 1080p | 52012 | 0 | 7,86 ms | **kein** Overflow: das Testbild ist nur 52 KB groß, 64 KB reichen |

- `0x85f8` ist vor und nach dem Start immer `0x00000000`.
- Erste Bytes: `ff d8 ff e0 00 10 4a 46 49 46 00 01 01 00 00 01` (Hexdump in
  [`a1/probe-1080p.txt`](a1/probe-1080p.txt)).
- Bildprüfung: Graustufen, Schachbrett und die acht Farbbalken (grau, gelb, cyan, grün,
  magenta, rot, blau, grau) stimmen mit dem Generator überein, keine U/V-Vertauschung.
  Luma gegen das Soll-Muster ([`a1/luma-check.txt`](a1/luma-check.txt), direkt als YCbCr
  dekodiert): 54,7 dB (1080p), 55,6 dB (360p), Schachbrett-Zeilen verlustfrei.
- `ep1.bin` liegt bei ([`a1/ep1.bin`](a1/ep1.bin), 0x790 Bytes).

**Befund: DMA-Speicher über `/dev/avpu`.** Nach dem ersten 1080p-Lauf schlugen weitere
1080p-Läufe fehl:

```
/tmp/t31_hwjpeg_probe -v -s 32768 -o /tmp/overflow-32768.jpg
GET_DMA_MMAP(3133440) failed: Cannot allocate memory
```

dmesg: `avpu avpu.0: Can't alloc DMA buffer`. `/dev/avpu` holt den Puffer per
`dma_alloc_coherent` aus dem 78-MB-Kernel-RAM (nicht aus rmem). Ein 3-MB-Puffer braucht einen
freien 4-MB-Block (Order 10); `/proc/buddyinfo` zeigte keinen mehr, auch nicht nach
`drop_caches`. Deshalb liefen die Zusatztests mit 640×360. Für den Hardware-JPEG-Pfad in
OpenIMP heißt das: Quell- und Stream-Puffer sollten nicht über das AVPU-Coherent-Fallback
kommen.

## A2. IPU-OSD-Testtool – **bestanden** (mit Hinweis zur Abdeckfarbe)

```sh
/tmp/t31_ipu_osd_probe -v -n 200 2>&1 | tee /tmp/ipu-probe.txt
python3 tools/ipu_osd_analyze.py test-results/a2
```

Dateien: [`a2/`](a2/) (`ipu-probe.txt`, `ipu-dmesg.txt`, `ipu_osd_analyze.txt`, beide PNGs,
Rohdaten).

- `IOCTL_IPU_START ok, 379 us`, keine IPU-Fehler in dmesg.
- Alle vier Ebenen an der richtigen Stelle, **außerhalb der Kästen 0 Pixel verändert**.
- Bitmap: Bänder in der Reihenfolge rot, grün, blau, weiß, schwarz; Alpha blendet von links
  nach rechts ein; Kanal 1 halb so deckend wie Kanal 0. Rot deckend, blau halbtransparent.
- Luma-Modell: bestes `kernel-analog+16` mit `+128>>8` (mittlerer Fehler 0,10); globales Alpha
  128: mittlerer Fehler 0,05.
- Zeiten (`-n 200`):
  - 4 Ebenen in einem Durchlauf: **352,2 µs**
  - ein Durchlauf pro Ebene: **375,1 µs** pro Frame (4 Durchläufe)
  - `IOCTL_IPU_BUF_FLUSH_CACHE` über 356352 Bytes: **80,1 µs** (das Tool spült 348 KB, nicht 1 MB)

**Hinweis Abdeckfarbe:** Die Hardware übernimmt Y aus dem Farbwort **unverändert**:

| Fläche | Farbwort (`bak`) | gemessen | analyze erwartet (Stock) |
|---|---|---|---|
| rot, a=255 | `0xff425af0` (Y=0x42=66) | Y 66,0, U 90, V 240 | Y=82 |
| blau, a=128 | `0x8019f06e` (Y=0x19=25) | Y 66,5 (Hintergrund 108,5), U 184, V 119 | Y=41 |

Blau passt exakt zu `(25+108,5)/2`, `(240+128)/2`, `(110+128)/2`. Das Tool schreibt Y ohne
+16 (66 statt 82). Soll die Abdeckfarbe der Stock-Ausgabe entsprechen, muss das Farbwort
Y inklusive +16 tragen; die IPU addiert nichts.

## B. `libimp.so` bauen und einhängen – **bestanden**

```sh
TC=<thingino>/output/ciao/wuuk_y0510_t31x_sc4336p_ssv6158-3.10.14-uclibc-192.0.2.x/host/bin/mipsel-linux
THINGINO_DIR=<thingino> TOOLCHAIN_PREFIX=$TC T31_OUTPUT_DIR=$PWD/build/t31 sh build-t31.sh
# Kamera:
cp /usr/lib/libimp.so /tmp/libimp-orig.so
mount --bind /tmp/libimp-t31all.so /usr/lib/libimp.so
```

- Build ohne Fehler (nur Warnungen, [`b/build-t31all.log`](b/build-t31all.log)),
  `libimp.so` 485228 Bytes, md5 auf der Kamera `b8035149d22f40270ee51b452eb0ec31`.
- Alle **126** libimp-Symbole, die timpsd importiert, exportiert OpenIMP (`nm -D` gegen die
  Original-libimp).
- timps startet mit OpenIMP auf dem Stock-Kernel: ISP, Framesource, H.264, JPEG, OSD-Aufrufe
  laufen an (`/proc/<pid>/maps` zeigt die eingehängte Datei).

## C. Regression ohne Schalter – erster Lauf **fehlgeschlagen** (rmem-Leck), mit rmem-Fix `3f501a2` **bestanden**

Der erste Lauf mit `014edbc` scheiterte am rmem-Leck (unten). Der Fix
(`a511238 dma: free reserved-arena (rmem) allocations instead of leaking them`, gemergt als
`3f501a2`) wurde danach getestet, siehe [C mit rmem-Fix](#c-mit-rmem-fix-3f501a2). Alle Tests
ab S laufen mit `3f501a2`.

### Ablauf (erster Lauf, `014edbc`)

```sh
/etc/init.d/S95timps stop
mount --bind /tmp/libimp-t31all.so /usr/lib/libimp.so
/usr/bin/timpsd -c /tmp/timps.conf > /tmp/run-c.log 2>&1 &
# PC: ffmpeg -rtsp_transport tcp -i rtsp://…/ch0 bzw. ch1 -t 600 -c copy …, Snapshot alle 10 s
```

### Referenz: Original-libimp (5 min, gleicher Ablauf)

| | Main | Sub |
|---|---|---|
| Frames / fps | 4442 / 14,81 | 4445 / 14,81 |
| Lücken > 0,5 s | 0 | 0 |
| Dekodierfehler | 0 | 0 |

Snapshots 30/30 HTTP 200, Ø 230 KB, Ø 0,37 s. timpsd 11–15 % CPU, System ~78 % idle
([`c/top-stock.txt`](c/top-stock.txt), [`c/snaps-stock.txt`](c/snaps-stock.txt)).

### Mit `claude/t31-all`

- Startet, H.264 Main/Sub und JPEG laufen an (`chn0: first encoded frame … key=1`), die ersten
  Snapshots kommen, OSD-Aufrufe ohne Fehlermeldung (`OSD stream 0: 4 overlay(s)`).
- Nach dem ersten Abbau der Framesource (der Main-Client ging) lässt sie sich nicht wieder
  aktivieren:

```
08:07:29.667 [INF] HAL_ING      framesource 0 disabled (idle)
[VBM] CreatePool: chn=0, 1920x1080 … 2 frames, size=3133440
[VBM] CreatePool: allocation failed
08:07:32.653 [ERR] HAL_ING      framesource 0: EnableChn failed (attempt 1)
08:07:37.654 [ERR] HAL_ING      jpeg chn3: encoder dead after 10 consecutive misses …
08:08:30 … jpeg chn3: 5 consecutive forced-recovery cycles never produced a frame - giving up …
```

- Snapshots danach HTTP 503 (Ergebnis: 1× 200, 7× 503, 3× Timeout; [`c/snaps-c.txt`](c/snaps-c.txt)).
- Log: [`c/run-c-t31all.log`](c/run-c-t31all.log) (Pro-Frame-Zeilen `GetFrame`/`DQBUF` entfernt),
  dmesg: [`c/dmesg-c-t31all.txt`](c/dmesg-c-t31all.txt).

### Ursache: rmem wird nie freigegeben

OpenIMP legt auf dieser Kamera alle DMA-Puffer über `/dev/rmem` mit einem Bump-Allocator an
(`src/dma_alloc.c`, `dma_free_buffer()`: *„RMEM bump allocations are not individually freed
(no-op)"*). Jeder Aus/An-Zyklus der Framesource legt die zwei 3-MB-Framepuffer neu an. Die
physischen Adressen steigen nur:

```
0x55f1000 0x58ee000 | 0x5beb000 0x5ee8000 | 0x61e5000 0x64e2000 | 0x67df000 0x6adc000 |
0x6dd9000 0x70d6000 | 0x73d3000 0x76d0000 | (Sub) 0x7d7c000 0x7dd2400 | danach: allocation failed
```

rmem endet bei `0x8000000`. timps baut die Framesource ab, sobald der letzte Client geht, und
schaltet sie beim Start für die Tag/Nacht-Umschaltung mehrmals kurz ein (4 Zyklen in den
ersten 30 s). Nach 5–7 Zyklen ist rmem voll.

### Gegenprobe mit `main` (`9cab219`)

Gleicher Ablauf für beide Bibliotheken: timps starten, 45 s warten, dann 6× Main 10 s abrufen
und trennen ([`c/cyc-main-summary.txt`](c/cyc-main-summary.txt),
[`c/cyc-t31all-summary.txt`](c/cyc-t31all-summary.txt)).

| | `main` | `claude/t31-all` |
|---|---|---|
| Framesource-Enables bis zum ersten Fehler | 5 | 5 |
| `allocation failed` schon beim 1. Client | 14 | 14 |
| Ende | `chn0: 5 consecutive forced-recovery cycles never produced a frame … exiting` | identisch |

Beide verhalten sich **identisch**. Die Stelle in `dma_alloc.c` ist auf `main`,
`claude/t31-re` und `claude/t31-all` gleich. Das ist ein bestehender OpenIMP-Fehler, keine
Regression des Branches. Er trifft jeden Streamer, der Kanäle zur Laufzeit abbaut und neu
anlegt.

**Vorschlag:** Den rmem-Arena-Allocator mit Freigabe versehen (Freiliste oder Buddy), oder
VBM-Pools pro Kanal beim ersten Anlegen behalten und bei `DisableChn`/`EnableChn`
wiederverwenden.

### Zwischenzeitlicher Workaround (nur alter Stand)

Vor dem Fix lief S1 kurz (08:30–08:45) mit dem alten Stand und dauerhaften RTSP-Clients, damit
die Framesource nicht abgebaut wird: 86/86 Snapshots, alle AVC-Zähler 0, Sub konstant 15 fps,
Main 7 von 90 10-s-Intervallen mit nur 77–139 statt ~150 Frames. Dieser Lauf wurde für den Fix
abgebrochen und zählt nicht als S-Ergebnis.

### C mit rmem-Fix (`3f501a2`)

Bibliothek neu gebaut (md5 auf der Kamera `d1d9d43ca6e65be727c3b31c6594f50f`), wie in B
eingehängt, timps ohne Variablen und **ohne** dauerhaften Client gestartet.

**FrameSource-Zyklen** ([`c/cycles-c2.txt`](c/cycles-c2.txt), Log [`c/run-c2.log`](c/run-c2.log)):
nach 45 s Start (4 Tag/Nacht-Zyklen) 25-mal Main 8 s per RTSP abrufen und trennen, jeweils
danach 2 s Sub abrufen, 5 s Pause (timps baut die Framesource dann ab).

| | Ergebnis |
|---|---|
| Framesource-Enables Main / Sub | **30 / 25** |
| `allocation failed` | 0 |
| `rmem out of memory` | 0 |
| `[ERR]`/`[WRN]` im Log | 0 |
| ffmpeg-Fehler Main (ohne dts-Hinweise) | 0 in allen 25 Zyklen |
| Sub-Frames in 2 s | 31–32 in jedem Zyklus |
| timps-PID | durchgehend dieselbe |

**Bildfehler nach einem Zyklus:** Von jedem Zyklus wurden der 1. und 16. Frame nach dem
Neuverbinden gesichert (nur lokal, Kamerabild) und angesehen: alle 25 ersten Frames vollständig,
ohne Blockmüll oder alte Bildinhalte. Numerisch: größte mittlere Abweichung eines
16×16-Blocks vom Median aller 50 Frames **7,7** Graustufen, also Rauschniveau
([`c/blockcheck-c2.txt`](c/blockcheck-c2.txt)). Kein Hinweis auf Hardware, die in freigegebene
Puffer schreibt.

**Gegenprobe `OPENIMP_RMEM_NO_REUSE=1`** ([`c/cycles-c2-noreuse.txt`](c/cycles-c2-noreuse.txt),
[`c/tail-c2-noreuse.txt`](c/tail-c2-noreuse.txt)): Der alte Fehler kommt wie erwartet zurück,
nach 6 Enables:

```
[DMA] rmem out of memory: requested 706560, used 51888128 of 52428800, largest free block 540672, 26 allocations
… chn1: 5 consecutive forced-recovery cycles never produced a frame … exiting
```

Die neue Meldung benennt die Ursache jetzt direkt.

**Offen:** Im ersten Zyklus des No-Reuse-Laufs (und im ersten C-Lauf) meldete ffmpeg
`number of reference frames (0+2) exceeds max (1; probably corrupt input)` beim Einstieg in den
Main-Stream ([`c/ffmpeg-noreuse-cycle1.txt`](c/ffmpeg-noreuse-cycle1.txt)). In den 25 Zyklen
mit Wiederverwendung trat es nicht auf. Zusammenhang mit rmem unwahrscheinlich; eher der
Einstieg mitten in einer GOP bzw. die Doppel-IDR-Stelle.

**10-Minuten-Lauf** (RTSP Main+Sub live mit ffmpeg dekodiert, Snapshot alle 10 s, kein
dauerhafter Client vorab; [`c/rtsp-summary-c3.txt`](c/rtsp-summary-c3.txt),
[`c/snaps-c3.txt`](c/snaps-c3.txt), [`c/top-c3.txt`](c/top-c3.txt), [`c/run-c3.log`](c/run-c3.log)):

| | Original-libimp (5 min) | OpenIMP `3f501a2` (10 min) |
|---|---|---|
| Main / Sub fps | 14,81 / 14,81 | 15,0 / 15,0 (je 9001 Frames) |
| 10-s-Intervalle mit < 140 Frames | – | 0 (außer dem angeschnittenen letzten) |
| Dekodierfehler | 0 | 0 (nur `non monotonically increasing dts`, siehe Jitter) |
| Snapshots | 30/30, Ø 230 KB, 0,37 s | 58/58, Ø 208 KB, 0,44 s |
| timpsd CPU (`top -d 5`) | 11–15 % | 5–7 % ohne Snapshot im Fenster, 45 % mit (Software-JPEG) |
| `[ERR]`/`[WRN]` | – | 0 |
| OSD | sichtbar | unsichtbar (erwartet), OSD-Aufrufe ohne Fehler |

Damit ist C mit dem rmem-Fix **bestanden**.

### Weitere Beobachtungen in C

- Die erste Main-Aufnahme (`ffmpeg … -c copy run.mkv`) brach nach 5 s ab:
  `number of reference frames (0+2) exceeds max (1; probably corrupt input), discarding one`,
  dann `Can't write packet with unknown timestamp` ([`c/ffmpeg-rec-main-t31all.txt`](c/ffmpeg-rec-main-t31all.txt)).
  Mit der Original-libimp trat das nicht auf. Ob das die bekannte Doppel-IDR-Stelle
  (`idr_pic_id = 0`) ist, ließ sich nicht klären.
- Zeitstempel-Jitter: Die Frame-Abstände im Sub-Stream sind mit OpenIMP unregelmäßiger
  (neben 67/68 ms auch 34, 55, 88 ms; vereinzelt doppelte Zeitstempel), mit der Original-libimp
  fast nur 67/68 ms. ffmpeg meldet deshalb regelmäßig `non monotonically increasing dts`.
- Mit OpenIMP schreibt der Stock-tx-isp bei jedem Abbau der Framesource
  `Streaming off, will not wait for buffers,chan index 0` mit Stack-Dump in dmesg (aufrufender
  Thread jeweils `FS(0)-tick` von OpenIMP). Offenbar wartet der Thread noch in DQBUF, wenn
  STREAMOFF kommt. Folgen hatte das keine.

## S. H.264-Stabilität

### S1. Dauerlauf Main + Sub – **bestanden** (2 h, ohne Hänger)

`claude/t31-all` `3f501a2`, keine Variablen, 09:17–11:17. Main und Sub dauerhaft per RTSP
(ffmpeg dekodiert live nach `null`, Fortschritt alle 10 s), dazu alle 10 s ein Snapshot;
Nachtbild, 15 fps. Dateien: [`s/`](s/).

| | Main | Sub |
|---|---|---|
| Frames in 2 h | 108002 (15,0 fps) | 108002 (15,0 fps) |
| Verbindungsabbrüche | 0 | 0 |
| 10-s-Intervalle mit 0 Frames | 0 | 0 |
| 10-s-Intervalle < 140 Frames | 3 (133, 136, 132) + angeschnittenes Ende | 3 (136, 138, 133) + Ende |
| ffmpeg-Dekodierfehler | 0 | 0 |
| `non monotonically increasing dts` (Jitter, s. C) | 3673 | 3117 |

Die drei Einbrüche lagen bei Main und Sub im selben Intervall (z. B. ~09:42:45), ohne
Logmeldung von timps oder libimp; eher WLAN/Netz als Encoder.

- Snapshots: **690/690** HTTP 200, Ø 0,44 s, max. 0,82 s ([`s/s1-snaps.txt`](s/s1-snaps.txt)).
- Zähler (syslog, [`s/s1-syslog.txt`](s/s1-syslog.txt)): `completion timeout` 0,
  `without size writeback` 0, `completion found without IRQ` 0, `keeps returning EINTR` 0,
  `size written back but core still running` 0, `picture larger than the stream buffer` 0,
  `[ERR]`/`[WRN]` 0.
- timpsd-CPU aus `top` alle 10 min ([`s/s1-mon.txt`](s/s1-mon.txt)): 0–13 % ohne Snapshot im
  Messmoment, 36–80 % mit (Software-JPEG). Freier RAM sank von ~6 auf ~3,6 MB, Cache stieg
  entsprechend; `/tmp` konstant.
- Da kein Hänger auftrat, wurde der Recovery-Pfad (Core-Reset, IDR nach ≤ 2 s) nicht
  ausgelöst und ist damit nicht belegt.
- Vergleichslauf mit alter Bibliothek: Der kurze Lauf mit `014edbc` + Workaround (08:30–08:45,
  [`c/`](c/) Abschnitt „Zwischenzeitlicher Workaround") zeigte auf Main 7 von 90 Intervallen
  mit 77–139 Frames; mit `3f501a2` über 2 h nur die 3 gemeinsamen Netz-Einbrüche.

## I. IVS-Bewegungserkennung

### Stufe 1: Algorithmen gegen die Original-libimp – **bestanden** (nach Fix des Vergleichstools)

Stock-libimp: T31 SDK 1.1.6 uclibc/4.7.2 (md5 `6ba997693eb23c0d5e6a5fc14b56a818`) als
`/tmp/libimp-stock.so`. Die CPU meldet **kein** `mxu_v2` (`ASEs implemented:` leer), die
Original-libimp rechnet deshalb in beiden Läufen skalar.

```sh
rm -f /tmp/closesimd; /tmp/t31_ivs_compare /tmp/libimp-stock.so 2>&1 | tee /tmp/ivs-compare-simd.txt
touch /tmp/closesimd;  /tmp/t31_ivs_compare /tmp/libimp-stock.so 2>&1 | tee /tmp/ivs-compare-scalar.txt
rm -f /tmp/closesimd
```

Ergebnis, beide Läufe identisch ([`i/ivs-compare-simd.txt`](i/ivs-compare-simd.txt),
[`i/ivs-compare-scalar.txt`](i/ivs-compare-scalar.txt)):

```
stock path: scalar
move: frames same=8591 differ=0 oob-differ=49 (results 3708)
base: frames same=11520 differ=0 (detections 5120)
rc=0
```

Der MXU2-SIMD-Pfad der Original-libimp ließ sich auf dieser CPU nicht prüfen.

**Befund: Das Vergleichstool gab die Original-Handles nie frei.** Der erste Lauf
(11:19, neben OpenIMP-timps mit ~3,6 MB freiem RAM) endete mit einem **Neustart der Kamera**
(Boot-Meldung `CPU0 RESET ERROR PC:801A4008` = `arch_local_irq_restore`, also vermutlich
Watchdog nach Aushungern). Die Wiederholung neben Original-timps zeigte die Ursache: Der
Prozess wuchs linear um ~1,5 MB/s bis ~50 MB RSS, dann beendete ihn der OOM-Killer
(`Killed process … (t31_ivs_compare) total-vm:52868kB, anon-rss:50828kB`) nach dem move-Teil
([`i/ivs-compare-unpatched-memory.txt`](i/ivs-compare-unpatched-memory.txt),
[`i/ivs-compare-unpatched-oom.txt`](i/ivs-compare-unpatched-oom.txt)). `compare_move()` und
`compare_base()` legen je Konfiguration ein Handle mit `imp_alloc_move`/`imp_alloc_base_move`
an und geben es nie frei (Kommentar im Code: „its destructor is internal"). Die Original-libimp
exportiert aber `imp_free_move` und `imp_free_base_move` (ein Argument, das Handle; per
Disassembly geprüft). Fix als eigener Commit auf diesem Ergebnis-Branch:
`tools: t31_ivs_compare frees the stock move/base-move handles`. Danach bleibt der Prozess bei
~1–1,5 MB RSS.

### Stufe 2: timps mit Bewegungserkennung – **bestanden mit Befund** (ohne Client keine Ergebnisse)

Konfiguration: Kopie `/tmp/timps-ivs.conf` mit `motion.enabled = 1`, `monitor_stream = 0`,
`sensitivity = 128`, `skip_frames = 5`. **`motion.on_motion` in der Kopie auf `""` gesetzt**,
weil der Hook der Kamera Bilder an externe Dienste schickt. `/etc/timps.conf` unverändert.
Start: `OI_CONF=/tmp/timps-ivs.conf … OPENIMP_T31_IVS_STATS=1 timpsd -c /tmp/timps-ivs.conf`.
Bewegung per PTZ statt Gehtest: `motors -d g -x 150 -y 0`, 4 s, zurück mit
`motors -d h -x 2630 -y 0` (Endposition geprüft: `2630,0`). Ablauf: [`i/ivs2-steps.txt`](i/ivs2-steps.txt).

**Befund, erster Lauf ohne RTSP-Client:** Die Erkennung lieferte nichts:

```
[MOTION] motion detection started (1920x1080 grid 5x5=25 cells, sense=2/4, max 52, hold=800ms)
[ERR] MOTION  no IVS move results for over 10000ms - cycling the move channel to recover   (alle 10 s)
/control: "stalled":1
```

Keine einzige `[IVS]`-Statistikzeile. Framesource 0 (von timps für die Erkennung gepinnt)
lieferte genau **2 Frames** (`DQBUF … OK` zweimal, `nrVBs=2`), danach blockierte DQBUF.
`GetFrame` 0-mal. Ohne Encoder-Abnehmer gibt der IVS-Pfad die beiden Puffer offenbar nicht an
den Treiber zurück. Bewegungserkennung ohne gleichzeitiges Streaming (der Normalfall bei
Aufnahme „nur bei Bewegung") funktioniert damit nicht. Log:
[`i/run-ivs-stalled.log`](i/run-ivs-stalled.log).

**Zweiter Lauf, Main+Sub per RTSP verbunden** ([`i/run-ivs2.log`](i/run-ivs2.log),
[`i/ivs-stats.txt`](i/ivs-stats.txt)):

| Punkt | Ergebnis |
|---|---|
| Start | `motion detection started (…)`, kein `CreateChn failed`, kein `does not match the configured` |
| Statistik (43 Zeilen à 10 s) | Frames 151 (Median, 125–152), Ergebnisse 25 (23–27) = Frames/6, **0 dropped busy**; copy Ø 1,9 ms (max 22 ms), process Ø 1,3 ms (max 22 ms) |
| Ruhige Szene, 120 s, Abfrage alle 0,5 s | **0** Proben mit aktiver Zelle, `stalled` 0, keine Fehlalarme (Nacht/IR) |
| PTZ-Schwenk, sensitivity 128 | erkannt (`motion detected` ~1 s nach Fahrtbeginn), 3 von 30 Proben aktiv |
| sensitivity 255 (`motion sensitivity updated live to 4/4`) | ruhig 30 s: 0; Schwenk: erkannt, 2/30 aktiv |
| sensitivity 0 (`… 0/4`) | ruhig 30 s: 0; Schwenk: erkannt, 3/30 aktiv |
| Stop/Start (`enabled` 0 → 1) | `motion detection stopped` / `started`, kein Absturz, danach wieder Statistik und Erkennung beim Schwenk |
| Bildrate Main/Sub während der Erkennung | 149–152 Frames je 10 s, 0 Aussetzer |
| timpsd-CPU (4 Proben à 5 s) | mit Erkennung Ø 10,8 %, ohne Ø 7,7 % (System idle 80 % bzw. 85 %) |

- Ein PTZ-Schwenk ändert das ganze Bild, deshalb löst er bei jeder Empfindlichkeit aus. Der
  erwartete Unterschied zwischen 255 und 0 ließ sich so nicht zeigen. Dafür bräuchte es eine
  kleine, schwache Bewegung (Gehtest).
- Aktive Zellen nur kurz nach Fahrtbeginn (2–3 Proben à 0,5 s bei `hold_ms=800`), während der
  übrigen Fahrt nicht; ob das dem Original entspricht (Vergleich Stufe 3), ist offen.
- Beim Stopp dieses Laufs trat der Zombie-Hänger (siehe Befund unten) mit `FS(1)-tick` auf.

## G. OSD im Streamer (`OPENIMP_T31_OSD=1`) – **bestanden**

`3f501a2`, timps mit `OPENIMP_T31_OSD=1`, OSD der Streamer-Konfiguration eingeschaltet
(4 Elemente je Stream: Datum/Uhrzeit, Kameraname, System-Uptime `T:HH:MM`, thingino-Logo als
Bitmap; keine Privatzone konfiguriert). Dateien: [`g/`](g/).

- Log: `[OSD] T31 IPU OSD backend requested (OPENIMP_T31_OSD=1)`, `OSD stream 0: 4 overlay(s)`,
  `OSD stream 1: 4 overlay(s)`; **kein** `backend disabled`, keine IPU-Meldung in dmesg.
- Sichtprüfung (Kamerabilder nur lokal angesehen, nicht committet):
  - Snapshot (JPEG, 1920×1080): alle vier Elemente an der erwarteten Stelle, Text sauber,
    Logo mit farbigem „ing" (Farbe und Alpha korrekt).
  - RTSP Main: Uhrzeit zählt weiter (zwei Frames 2 s auseinander zeigen `11:35:32` und
    `11:35:34`), kein Flackern, keine alte Uhrzeit.
  - RTSP Sub (640×360): OSD vorhanden, kleiner skaliert, Logo sichtbar.
  - Uptime-Element springt minütlich (`0:00:15` → `0:00:16`), wie im Format vorgesehen.
- 5 min mit Snapshot alle 10 s: 27/27 Snapshots, Ø 0,51 s, Main/Sub 15 fps, 0 Dekodierfehler.
- **CPU** (je 2,5 min, Main+Sub per RTSP, **ohne** Snapshots, `top -d 5`, 11 Proben):

| | timpsd Ø | System idle Ø |
|---|---|---|
| Original-libimp (OSD über Original) | **13,0 %** | 79,3 % |
| OpenIMP ohne Variable (OSD unsichtbar) | 7,8 % | 83,0 % |
| OpenIMP `OPENIMP_T31_OSD=1` | **7,9 %** | 84,8 % |

  Das IPU-OSD kostet messbar nichts und liegt deutlich unter der Original-libimp.

## Befund: timpsd bleibt beim Beenden als Zombie mit drehendem Kernel-Thread hängen

**5 von gut 40 Stopps** eines OpenIMP-timps endeten nicht vollständig (11:40 nach G, 11:48 nach
einem Kurzlauf ohne Variable, 12:30 nach dem HW-JPEG-Lauf, 12:37 nach dem ersten IVS-Lauf,
12:47 nach dem zweiten IVS-Lauf mit streamenden Clients). Jedes Mal:

- Der Hauptthread ist `Z` (Zombie, Elternprozess PID 1). Ein OpenIMP-Thread **`FS(0)-tick`**
  bzw. **`FS(1)-tick`** läuft weiter, zu 100 % im Kernel (`utime` ≈ 0, `stime` +~95 Ticks/s,
  System 100 % sys). `SIGKILL` wirkt nicht.
- Folge: `/run/timps.lock` gilt als belegt (`another timpsd instance already holds
  /run/timps.lock`), der nächste Start bleibt nach `audio in` stehen, der Bind-Mount lässt sich
  nicht lösen (`Device or resource busy`). **Nur ein Neustart hilft.**
- Die dmesg-Meldungen `Streaming off, will not wait for buffers,chan index 0` des Stock-tx-isp
  kommen immer von `Comm: FS(0)-tick`; Call-Trace `SyS_ioctl` → `vfs_ioctl` → tx-isp
  (`0xc0233070`, `0xc023f2e4`). Der Tick-Thread ruft also DQBUF auf einem Kanal auf, der
  gestoppt ist oder gerade gestoppt wird. Beim Prozessende dreht der Stock-Treiber in diesem
  ioctl.
- Belege: [`g/fs-tick-spin-evidence.txt`](g/fs-tick-spin-evidence.txt),
  [`g/fs-tick-spin-evidence-2.txt`](g/fs-tick-spin-evidence-2.txt),
  [`g/fs-tick-spin-evidence-3.txt`](g/fs-tick-spin-evidence-3.txt),
  [`i/stuck-after-ivs.txt`](i/stuck-after-ivs.txt).
- Gezielte Reproduktion mit kurzen Läufen (20–40 s) gelang **28-mal nicht**
  ([`g/stop-cycles.txt`](g/stop-cycles.txt)): Stopp während des Streamens, im Leerlauf,
  0,5–3 s nach dem Trennen des letzten Clients, mit Ausgabe über eine Pipe, mit doppeltem
  SIGTERM. Die Treffer kamen nach Läufen von 1–9 min, mit und ohne Clients, mit einem schon
  lange laufenden Tick-Thread (TID aus der Startphase) und mit einem gerade neu angelegten.
  Ein einfaches Muster ist nicht erkennbar.
- Mit der Original-libimp trat das in keinem Stopp auf.
- Vorschlag: Tick-Thread vor `STREAMOFF`/`DisableChn` und vor dem Prozessende stoppen und
  joinen; kein DQBUF auf einem Kanal, der nicht im Streaming ist. Ob open-tx-isp dasselbe
  zeigt, ist offen (siehe unten, open-tx-isp ließ sich nicht laden).

## E/H. JPEG im Streamer – **bestanden** (E Lauf C/D; H nur eingeschränkt)

`3f501a2` enthält die Änderungen aus `claude/t31-perf` (schnellere Software-JPEG, HW-JPEG liest
direkt aus rmem). E und H fallen deshalb zusammen. Je 3 min, Main+Sub per RTSP, auf der Kamera
eine Snapshot-Schleife (`curl`, 1 s Pause zwischen den Abrufen), `top -d 5`. Dateien: [`e/`](e/).

| | Original-libimp | OpenIMP Software-JPEG (Lauf C) | OpenIMP `OPENIMP_T31_HW_JPEG=1` (Lauf D) |
|---|---|---|---|
| Snapshots | 129/129 | 112/112 | 128/128 |
| Snapshot-Zeit Ø / max | 0,29 / 0,55 s | 0,46 / 1,00 s | **0,28 / 0,54 s** |
| JPEG-Größe Ø | 290 KB | 283 KB | 289 KB |
| timpsd-CPU Ø | 19,4 % | **66,9 %** | **18,5 %** |
| System idle Ø | 64,7 % | 17,6 % | 65,0 % |
| Main/Sub fps, Dekodierfehler | 15 / 15, 0 | 15 / 15, 0 | 15 / 15, 0 |

**Log Lauf D** ([`e/hwjpeg-log-lines.txt`](e/hwjpeg-log-lines.txt)):

```
[Codec] HWJPEG: T31 hardware JPEG requested (OPENIMP_T31_HW_JPEG=1)
[Codec] AVPU: T31 companion stage disabled
[Codec] HWJPEG: no AVC IRQ waiter yet, software until one runs
[Codec] HWJPEG: ready, ep1=0x00df0000
[Codec] HWJPEG: 1920x1080 q75 -> 294175 bytes [#1]
…
[Codec] HWJPEG: 1920x1080 q75 -> 295808 bytes [#800]
```

Kein `disabled`, kein `no IRQ 4`, kein `stream overflow`. Über 800 Hardware-JPEGs in 3 min.
Der A1-Befund zum AVPU-Coherent-Speicher traf hier nicht zu (`ep1` liegt in rmem).

**Bildqualität Lauf D:** Snapshot lokal angesehen (Kamerabild, nicht committet): vollständig,
keine Streifen, kein Blockmüll, keine alten Bildinhalte, Helligkeit und Schärfe wie beim
Software-JPEG. `OPENIMP_T31_HW_JPEG_SRC_COHERENT=1` war nicht nötig.

**H, Punkt 1 (bitgleiche Software-JPEG gegenüber `claude/t31-re`):** Nicht geprüft. Mit
Live-Bildern lässt sich das nicht bitgenau vergleichen, dafür wäre ein Vergleich auf einem
festen Quellbild nötig.

## S2. Snapshot-Last (F6) – **bestanden mit Anmerkung**

`ffprobe -show_frames … -read_intervals %+30` am Main-Stream während der Läufe aus E, dazu ein
Lauf ohne Snapshots:

| | Frames in 30 s | Intervall Median | p99 | max | Intervalle > 100 ms |
|---|---|---|---|---|---|
| OpenIMP ohne Snapshots | 449 | 67 ms | 89 ms | 101 ms | 4 |
| OpenIMP Software-JPEG, 1 Snapshot/s | 449 | 67 ms | 102 ms | 149 ms | 33 |
| OpenIMP HW-JPEG, 1 Snapshot/s | 447 | 67 ms | 135 ms | 164 ms | 93 |
| Original-libimp, 1 Snapshot/s | 442 | 67 ms | 69 ms | 71 ms | 0 |

- Keine Aussetzer: Unter Software-JPEG-Last bleibt die größte Lücke bei 149 ms, Frames gehen
  nicht verloren. F6 (Software-JPEG blockiert H.264 nicht) ist damit erfüllt.
- Die Zeitstempel sind mit OpenIMP grundsätzlich unruhiger als mit der Original-libimp (siehe C,
  Jitter), unter Last mehr. Überraschend: Mit **HW-JPEG** ist der Jitter größer als mit
  Software-JPEG (93 statt 33 Intervalle > 100 ms). Vermutlich warten H.264 und JPEG auf denselben
  AVPU-IRQ-Waiter bzw. Core-Lock; nicht weiter untersucht.

## D. Companion-Stage A/B – **bestanden** (B mindestens so gut wie A)

| Lauf | Variable | Dauer | Main/Sub | Dekodierfehler | Snapshots |
|---|---|---|---|---|---|
| A (= C mit rmem-Fix) | keine (`[Codec] AVPU: T31 companion stage enabled`) | 10 min | je 9001 Frames, 15 fps | 0 | 58/58 |
| B | `OPENIMP_T31_COMPANION_STAGE=0` (`… companion stage disabled`) | 5 min | je 4501 Frames, 15 fps | 0 | 27/27, Ø 0,44 s |

In B keine Timeouts, keine `stall`-Meldungen, keine Intervalle unter 149 Frames/10 s (außer
dem angeschnittenen Ende). S2-Messung in B (Snapshot alle 10 s): Median 67 ms, max. 138 ms.
H.264 hängt also nicht von der Companion-Stage ab. Dateien: [`d/`](d/). Mitschnitte als
Videodatei wurden nicht gespeichert (live dekodiert), Bild in B nicht gesondert angesehen.

## F. `SetIntegrationTime` auf T20 – **übersprungen**

Keine T20-Testkamera mit OpenIMP im Auftrag.

## S3/S4 – **übersprungen**

- S3 (Substream zur Laufzeit neu anlegen): timps legt Encoder-Kanäle nur per `restart video`
  neu an, das startet alle Kanäle neu. Nicht gemacht.
- S4 (`OPENIMP_T31_AVC_LEGACY=1`, 1 h): aus Zeitgründen nicht gemacht. Da S1 keinen einzigen
  Hänger hatte, wäre der A/B-Vergleich für F1/F2 ohnehin wenig aussagekräftig.

## Zusatz: open-tx-isp per insmod – **nicht möglich**

Auf Wunsch des Menschen sollte open-tx-isp ohne Flashen getestet werden: timps stoppen,
Stock-`sensor_sc4336p_t31` und `tx_isp_t31` entladen, die Module aus dem Open-Stack-Build
(Anhang) aus `/tmp` laden. `avpu.ko` ist in beiden Builds bitgleich und bleibt geladen, der
Kernel ist gleich konfiguriert (vermagic identisch).

```sh
rmmod sensor_sc4336p_t31   # rc=0
rmmod tx_isp_t31           # rc=139, Segmentation fault
```

Kernel-Oops in `class_unregister` (`epc 801dbea4`), aufgerufen aus der Exit-Routine des
Stock-`tx_isp_t31` (`ra c0247b60`). Das Modul bleibt als `Unloading` stehen, ein anderes Modul
gleichen Namens lässt sich nicht laden. Danach Neustart. Belege:
[`open-tx-isp/rmmod-oops.txt`](open-tx-isp/rmmod-oops.txt).

Der Stock-ISP-Treiber lässt sich also nicht entladen. open-tx-isp auf dieser Kamera zu testen
braucht eine dauerhafte Änderung (Module beim Boot nicht laden: `/etc/modules.d`, Overlay oder
U-Boot-Env) oder das Flashen des Open-Stack-Images. Beides schließen die Regeln des Tests
aus.

## Anhang: Open-Stack-Image für dieses Profil (gebaut, nicht geflasht)

In einem separaten Worktree des thingino-Checkouts (Branch `ciao`, `cf463ffc3`, mit derselben
lokalen `ingenic-sdk`-Änderung wie das laufende Image) wurde per Fragment
`BR2_PACKAGE_THINGINO_ISP_OPEN=y` gesetzt. Build 11:45 min, erfolgreich.

- open-tx-isp `e92166b985606613f2395831bac65413c7542877` (opensensor/open-tx-isp, `main`),
  `tx-isp-t31.ko` meldet `author=Matt Davis`, `description=TX-ISP Camera Driver`.
- OpenIMP im Image: opensensor/openimp `main` `9cab2192` (**nicht** dieser Branch).
- Gegenüber dem Stock-Image kommen nur `openimp-tuningd`, `S30openimp-tuning` und
  `openimp-tuning.conf` dazu. Der Tuning-Daemon startet nur mit dem raptor-v4l2-Backend,
  mit timps also nicht.

## Sonstiges (alle Abweichungen und Zwischenfälle)

- 07:51: `timpsd --version` zur Versionsermittlung startete kurz eine zweite Instanz; sie
  beendete sich wegen `/run/timps.lock` sofort, der laufende Streamer blieb unberührt.
- A1: `dmesg -c` geleert und `echo 3 > /proc/sys/vm/drop_caches` ausgeführt (flüchtig).
- 11:20: Kamera-Neustart durch das Vergleichstool (siehe I, Stufe 1).
- 11:47, 11:52, 12:31, 12:38, 12:49: Neustarts wegen des Zombie-Hängers (siehe Befund).
- 12:56: Neustart nach dem Oops beim `rmmod tx_isp_t31`.
- Mein Test-Harness startete timpsd zeitweise über eine Pipe (Log-Filter). Dabei puffert
  timps' stdout, das Datei-Log ist dann unvollständig; ausgewertet wurde in diesen Fällen
  syslog (`logread`). Die Pipe war nicht die Ursache des Hängers (4 Gegenproben sauber).
- Die ersten beiden `ffmpeg`-Mitschnitte in C mit `-c copy` nach Matroska brachen an
  fehlenden Zeitstempeln ab; danach wurde live nach `null` dekodiert.
- Kamerabilder wurden nur lokal angesehen und nicht committet.
- Nichts unter `/etc`, im Overlay oder im Flash geändert; Dateien nur unter `/tmp`.
