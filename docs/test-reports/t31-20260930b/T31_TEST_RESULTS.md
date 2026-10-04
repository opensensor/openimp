# T31-Testergebnisse, Runde 2: OpenIMP `claude/t31-all` (`dfbd90f`) und open-tx-isp `claude/t31-isp-all`

Testlauf am 2026-09-30 nach der Übergabe aus der Cloud-Sitzung. Ausgeführt von einer lokalen
Claude-Code-Session per SSH. Kamera anonymisiert (`cam-A`, `192.168.1.x`). Runde 1 (Original-
Stack, OpenIMP `3f501a2`) steht auf dem Branch `claude/t31-test-results-20260930`.

## Kopf

| | |
|---|---|
| Kamera | WUUK Y0510, Profil `wuuk_y0510_t31x_sc4336p_ssv6158`, Sensor SC4336P (2560×1440) |
| SoC | T31X, `isa: mips32r1`, `ASEs implemented:` leer (kein MXU2) |
| Speicher | `mem=78M@0x0 rmem=50M@0x4e00000` |
| thingino | `ciao` `cf463ffc3` mit `BR2_PACKAGE_THINGINO_ISP_OPEN=y`, dazu die drei T31-Patches `0004`–`0006` von open-tx-isp aus `aperto`; per komplettem OTA (`make ota`) geflasht, mit Freigabe des Menschen |
| Kernel | `3.10.14__isvp_swan_1.0__` |
| ISP-Modul geflasht („alt") | open-tx-isp `e92166b` + thingino-Patches `0001`–`0006` |
| ISP-Modul Test („neu") | open-tx-isp `claude/t31-isp-all` `97cccfa2` + thingino-Patches `0001`–`0006`, md5 `03b40e861c0724309e098a1c7cbfcf96` |
| libimp geflasht | OpenIMP `3f501a2` (Runde 1) |
| libimp Test | OpenIMP `claude/t31-all` `dfbd90f`, md5 `8b48664e0c0252c99625d18340f9a274`, per Bind-Mount |
| Streamer | timps v1.9.28, für die Tests von Hand mit einer Kopie der Konfiguration in `/tmp` |
| Main/Sub | H.264 1920×1080 / 640×360, 15 fps |

Regeln wie im Testplan: nur diese Kamera, Dateien nur in `/tmp`, Bind-Mount, keine Kamerabilder
im Repo (nur lokal angesehen, mit Einverständnis).

## Übersicht

| Bereich | Ergebnis |
|---|---|
| Phase 1 C (FrameSource-Zyklen, rmem-Fix) | **bestanden**; No-Reuse reproduziert den alten Fehler |
| Phase 1 S1 (78 min, 15 fps) | **bestanden** |
| HW-JPEG als Standard (`claude/t31-hwjpeg-default`) | **bestanden**; ~17 % statt ~70 % CPU bei 1 Snapshot/s, kein Jitter-Nachteil auf open-tx-isp |
| FrameSource-Stopp (`claude/t31-fs-stop-order`) | **bestanden**; behebt einen Deadlock (bedingungsloses `pthread_cancel`), der mit dem neuen Modul nach 6 Zyklen auftrat |
| G OSD über IPU (auch im HW-JPEG) | **bestanden** |
| I Bewegung mit Client | **bestanden**; ohne Client mit `dfbd90f` kein Ergebnis → **behoben** in `claude/t31-ivs-idle` |
| Stopp dauert 4 s + `hard exit` bei aktivem Client | Ursache Audio-`NOBLOCK` → **behoben** in `claude/t31-ai-noblock` (Stopp 1,1 s, AAC ab Start) |
| Phase 2 T1–T7 (neues Modul) | **bestanden** (T2 mit drop 0 und mit drop 6) |
| Phase 2 Latenz | neues Modul: max. 660 µs bei Tag/Nacht (alt 1313 µs), 0 Sekunden > 1 ms |
| Phase 2 Tuning | bestanden bis auf 5.6 (nicht aussagekräftig); `it_max` und `image.ae_it_max_us` wirken jetzt; Zurücksetzen auf 0 fehlt in timps |
| Phase 2 rmmod (L1) | alt und `97cccfa2`: **Oops** → **behoben** in open-tx-isp `claude/t31-isp-fixes` (3 Lade-Zyklen sauber) |
| C + S mit beiden neuen Teilen | **bestanden** (mit Stopp-Fix); Abschlusslauf 60 min @ 25 fps mit allen Korrekturen **bestanden** |
| Ungetestet (Branches vorhanden) | AU-Prüfung/H.264-Doppelbild, leise Logs + schneller Idle-Stopp, Zeitstempel auf dem Stock-Treiber, `fw_ota.sh`-Fix (thingino, lokal) |

### Branches aus dieser Runde

OpenIMP (`Lu-Fi/openimp`, alle auf `dfbd90f`):
`claude/t31-hwjpeg-default` (5087c02), `claude/t31-fs-stop-order` (6ce3114), `claude/t31-ivs-idle` (7c5ba1b), `claude/t31-ai-noblock` (0a2331a), `claude/t31-all-fixes` (4653f0a, Merge der vier), `claude/t31-quiet-logs` (auf all-fixes, ungetestet), `claude/t31-fs-timestamps` (auf all-fixes, ungetestet), `claude/t31-avc-au-check` (ungetestet).

open-tx-isp (`Lu-Fi/open-tx-isp`, auf `claude/t31-isp-all` 97cccfa2): `claude/t31-isp-rmmod-fix` (c7c2e7d2), `claude/t31-isp-csi-mutex` (0e1c5773), `claude/t31-isp-fixes` (6caeb75e, Merge, geräte-getestet).

---

# Phase 1: neue libimp, geflashtes open-tx-isp

## C. Regression mit FrameSource-Zyklen – **bestanden**

Dateien: [`p1-c/`](p1-c/).

### 25 FrameSource-Zyklen

Wie in Runde 1: timps ohne Variablen starten, 45 s warten, dann 25-mal Main 8 s per RTSP
abrufen und trennen, jeweils danach 2 s Sub, 5 s Pause
([`p1-c/cycles-25.txt`](p1-c/cycles-25.txt), Log [`p1-c/run-cycles-25.log`](p1-c/run-cycles-25.log)).

| | Ergebnis |
|---|---|
| FrameSource-Enables Main / Sub | **26 / 25** (in Zyklus 19–23 hielt ein WebRTC-Client der WebUI Main 140 s lang offen, deshalb dort keine Zyklen) |
| `allocation failed` / `rmem out of memory` | 0 / 0 |
| ffmpeg-Fehler Main | 0 in allen Zyklen |
| timps-PID | durchgehend dieselbe |
| Bildprüfung | 1. und 16. Frame nach jedem Neuverbinden lokal angesehen: vollständige Bilder, kein Blockmüll. Die numerischen Abweichungen in Zyklus 2–4 sind das Einregeln der Belichtung nach dem Start, ab Zyklus 19 eine echte Szenenänderung (PTZ-Position und offene Tür). |

**Befund, altes Modul:** Einmal fror der Main-Stream nach einem Tag/Nacht-Wechsel ein und
erholte sich erst durch die Recovery von timps:

```
13:46:08.423 [INF] HAL_ING      control image.running_mode=1
13:46:16.383 [INF] HAL_ING      control image.running_mode=1        (re-assert, Stream läuft)
13:46:17.003 [WRN] HAL_ING      chn0: PollingStream idle (rc=-1, miss#1) - encoder emits no frames
13:46:21.507 [ERR] HAL_ING      chn0: encoder dead after 10 consecutive misses - forcing a framesource disable/enable cycle to recover (recovery attempt 1/5)
```

Danach lief der Stream wieder (Recovery-Versuch 1 reichte). Das ist das Einfrieren nach einem
Tag/Nacht-Wechsel, das `claude/t31-isp-all` beheben soll; Vergleich in Phase 2 (T2, L5).

### Gegenprobe `OPENIMP_RMEM_NO_REUSE=1` – **bestanden**

[`p1-c/cycles-noreuse.txt`](p1-c/cycles-noreuse.txt), [`p1-c/run-noreuse.log`](p1-c/run-noreuse.log):
Der alte Fehler kommt zurück, nach 6 Enables:

```
[DMA] rmem out of memory: requested 6266880, used 49426432 of 52428800, largest free block 3002368, 20 allocations
13:57:05.622 [ERR] HAL_ING      framesource 0: EnableChn failed (attempt 1)
…
13:57:31.313 [INF] MAIN         teardown complete - exiting
```

timps beendet sich sauber (kein Zombie wie in Runde 1 auf dem Stock-Treiber).

Nebenbei: Die Kamera war danach ~40 min von meinem Rechner aus nicht erreichbar, lief aber
durch (Uptime unverändert, DHCP-Verlängerungen im syslog). Das war ein Netzproblem zwischen
Rechner und Kamera, kein Kamerafehler.

### 10-Minuten-Lauf

Main+Sub per RTSP live dekodiert, Snapshot alle 10 s, `top`
([`p1-c/c3-summary.txt`](p1-c/c3-summary.txt), [`p1-c/run-c3.log`](p1-c/run-c3.log)):

| | Ergebnis |
|---|---|
| Main / Sub | 8891 / 8890 Frames in 600 s (15 fps), kein Intervall < 140 außer dem angeschnittenen Ende |
| Dekodierfehler | 0 |
| Snapshots | 56/56, Ø 358 KB, Ø 0,53 s |
| S2-Messung (30 s Frame-Zeitstempel Main) | Median 67 ms, p99 73 ms, **max 80 ms**, 0 Intervalle > 100 ms |
| `[ERR]`/`[WRN]` | 1× `jpeg chn3: PollingStream idle (miss#1)`, ohne Folgen |
| timpsd-CPU | 13–32 % (Software-JPEG-Snapshots im Messfenster) |

Die Frame-Abstände sind auf open-tx-isp deutlich gleichmäßiger als in Runde 1 auf dem
Stock-Treiber (dort max. 101 ms ohne Snapshot-Last).

## Code-Review: HW-JPEG als Standard? (Review durch eine zweite Claude-Instanz, nur lesend, Basis `dfbd90f`)

Anlass: Mit Software-JPEG liegt timpsd bei Snapshot-Last deutlich über der Original-libimp
(Runde 1, E: 66,9 % gegen 19,4 %), mit `OPENIMP_T31_HW_JPEG=1` gleichauf (18,5 %).

Empfehlung: **Standard mit Bedingungen.** Vorher:

1. *(mittel)* `src/t40/codec-t40.c:7859-7869`: Beim Nachwachsen des Stream-Puffers nach einem
   Overflow wird der alte kohärente Puffer vor der neuen Allokation freigegeben. Scheitert die
   ~2,2-MB-Allokation aus Kernel-Lowmem (Fragmentierung, vgl. A1 in Runde 1), schaltet
   `disable("DMA allocation failed")` (`:7961`) den Pfad dauerhaft ab. Fix: erst neu allokieren,
   bei Fehler alten Puffer behalten; besser Stream/EP1 aus rmem wie der AVC-Pfad.
2. *(mittel)* Start-Race: HW-JPEG prüft nur, ob der IRQ-Waiter läuft (`:7935`), die globale
   AVC-Init passiert erst im ersten AVC-Process (`:9335-9341`). Ein früher JPEG-Job kann drei
   Timeouts sammeln und den Pfad dauerhaft abschalten (`:8035`). Fix: vor der AVC-Init den Frame
   in Software kodieren, nicht abschalten.
3. *(Testbedingung)* HW-JPEG schaltet die Companion-Stage immer ab (`:8114-8122`). Vor dem Umschalten
   Test C mit Zyklen und 10-min-Lauf mit `OPENIMP_T31_HW_JPEG=1`.

Minimaler Umbau: `codec-t40.c:7784-7788` und `openimp_p2_encoder.c:231-235` gleichlautend auf
„an, außer `OPENIMP_T31_HW_JPEG=0`" umstellen; Doku von opt-in auf opt-out.

Weitere Punkte: Alle übrigen Abschaltgründe sind dauerhaft, aber ohne Flattern; `disable` sollte
als Fehler geloggt werden. Der größere Zeitstempel-Jitter mit HW-JPEG kommt daher, dass jeder
JPEG-Job (~10–15 ms inkl. 3-MB-Kopie) den gemeinsamen Core-Lock hält; für 1 Snapshot/s unkritisch,
für MJPEG spürbar. EP1 und Stream-Puffer kommen aus `/dev/avpu` (Kernel-Lowmem), nicht aus rmem.

**Zombie-Hänger aus Runde 1:** kein Bezug zu HW-JPEG. `IMP_FrameSource_DisableChn` macht
STREAMOFF vor dem Join des Tick-Threads (`src/framesource/framesource_tseries.c:1993-2005`); die
innere Schleife ruft DQBUF ohne `running`-Prüfung erneut auf (`:1019-1046`). Im Stock-tx-isp
dreht dieses DQBUF nach STREAMOFF im Kernel. Commit `70c42bc` (fd vor Pool-Freigabe schließen)
behebt das nicht und war im getesteten Stand schon enthalten. Vorschlag: auf T31 zuerst den
Thread kooperativ stoppen und joinen, dann STREAMOFF. Auf open-tx-isp trat der Hänger nicht auf.

## S1. H.264-Dauerlauf – **bestanden** (78 min, ohne Hänger)

`dfbd90f` auf dem geflashten open-tx-isp, keine Variablen, 14:57–16:15. Main und Sub dauerhaft per
RTSP live dekodiert, Snapshot alle 10 s. Gekürzt wegen einer Pause des Menschen (Abfahrt), sonst
2 h. Das Rohlog wurde auf der Kamera minütlich gefiltert und geleert (die Pro-Frame-Debugzeilen
von OpenIMP ergeben sonst ~25 MB/h in `/tmp`). Dateien: [`p1-s/`](p1-s/).

| | Main | Sub |
|---|---|---|
| Frames in 78 min | 69397 (15,0 fps) | 69393 (15,0 fps) |
| Verbindungsabbrüche / 10-s-Intervalle mit 0 Frames | 0 / 0 | 0 / 0 |
| 10-s-Intervalle < 140 Frames | 2 (135, 139) | 0 |
| Dekodierfehler | 0 | 0 |

- Snapshots **444/444** HTTP 200, Ø 0,49 s, max. 0,89 s.
- Zähler (syslog und gefiltertes Log): `completion timeout`, `without size writeback`,
  `completion found without IRQ`, `keeps returning EINTR`, `size written back but core still
  running`, `picture larger than the stream buffer`, `[ERR]`, `[WRN]`: **alle 0**.
- Speicher (`top`, belegt inkl. Cache): 36,2 MB nach dem Start, danach konstant 39,8–40,3 MB.
  `/tmp` konstant bei 3 %. timpsd-CPU aus den 10-min-Proben: 0–33 % je nach Snapshot im Fenster.
- Kein Hänger, der Recovery-Pfad wurde also nicht ausgelöst.
- Nach dem Lauf Stopp in 0,3 s, Bind-Mount gelöst, `S95timps` normal gestartet.

## Neue Korrekturen aus dieser Runde (lokale Branches, von Agenten erstellt, auf dem Gerät geprüft)

Alle auf Basis `dfbd90f`, jeweils eigener Branch, jede Änderung als eigener Commit.

| Branch | Inhalt | Gerätetest |
|---|---|---|
| `claude/t31-hwjpeg-default` (`5087c02`) | HW-JPEG: Stream-Puffer wachsen ohne dauerhaftes Abschalten; kein JPEG-Job vor der AVC-Init; Abschalten als Fehler geloggt; **HW-JPEG standardmäßig an**, `OPENIMP_T31_HW_JPEG=0` schaltet zurück | bestanden (unten) |
| `claude/t31-fs-stop-order` (`6ce3114`, enthält die HW-JPEG-Commits) | FrameSource-Stopp: STREAMOFF → Worker joinen, `pthread_cancel` nur nach 1 s als letzter Ausweg; STREAMOFF aller Kanäle im Destruktor/Fatal-Handler; einmalige Log-Zeile, welcher ISP-Treiber läuft. Enthält auch die zurückgenommenen Zwischenstände `1f07faa`/`b77e0cf`/`2ead380`/`1deebd7` (Diagnose) | bestanden (unten) |
| `claude/t31-ivs-idle` (`7c5ba1b`) | IVS ohne Stream-Client: nicht abgeholte Frames gehen nach 1 s an den Treiber zurück | bestanden (unten) |
| `claude/t31-ai-noblock` (`0a2331a`) | Audio: `IMP_AI_GetFrame(NOBLOCK)`/`PollingFrame(timeout)` wie in der Original-libimp (Record-Thread + FIFO) | bestanden (unten) |

### HW-JPEG als Standard (`5087c02`)

Code-Review durch eine zweite Instanz (Abschnitt oben), Umsetzung als 4 Commits.

| Test (open-tx-isp, geflashtes Modul) | Ergebnis |
|---|---|
| 12 FrameSource-Zyklen + Snapshot alle 5 s | 29 Enables, 0 Dekodierfehler, 41/41 Snapshots, `HWJPEG: ready` |
| 10-min-Lauf (Snapshot alle 10 s) | Main 8890 Frames (15 fps), 57/57 Snapshots Ø 0,23 s, timpsd ~9,6 % CPU, Main-Frame-Abstände max. 84 ms. Einmal beim Start `chn1: encoder dead` mit Recovery (Sub bekam 2× `503`, danach 10 min sauber); in 11 weiteren gezielten Starts (6 einzeln, 5 Main+Sub parallel) nicht wieder aufgetreten |
| CPU/Jitter bei 1 Snapshot/s, 3 min | HW: timpsd ~17 %, Snapshot Ø 0,21 s, Frame-Abstand max. 92 ms, 0 > 100 ms. `OPENIMP_T31_HW_JPEG=0`: ~70 %, 0,42 s, max. 104 ms. Auf open-tx-isp erhöht HW-JPEG den Jitter nicht mehr (anders als in Runde 1 auf dem Stock-Treiber). |
| Opt-out | `HWJPEG: T31 hardware JPEG off (OPENIMP_T31_HW_JPEG=0), software encoder` |

Dateien: [`hwjpeg/`](hwjpeg/), [`fs-stop/hwj*-cycles.txt`](fs-stop/).

### FrameSource-Stopp-Reihenfolge

Ausgangspunkt: der Zombie-Hänger aus Runde 1 (Stock-Treiber). Der erste Ansatz (`1f07faa`: Worker vor STREAMOFF stoppen) lief auf open-tx-isp bei ~25 % der Abschaltungen in den 1-s-Timeout; die Diagnose (`1deebd7`) zeigte `worker in step dqbuf (dq ioctl) for 1353–3075 ms … exited after STREAMOFF`: **beide Treiber ignorieren `O_NONBLOCK` in DQBUF**, STREAMOFF ist der einzige Wecker. `b77e0cf` (select vor jedem DQBUF) half nicht und fiel mit einer neuen H.264-Warnung beim Stream-Einstieg zusammen, wurde zurückgenommen.

Endgültiger Entwurf (`92d00db`, `6ce3114`): STREAMOFF → join, Cancel nur als letzter Ausweg nach 1 s; beim Prozessende STREAMOFF auf alle Kanäle. Begründung aus dem HLIL des Stock-Moduls: dessen DQBUF dreht nur in einer `-ERESTARTSYS`-Schleife, wenn ein Signal (auch `pthread_cancel`/SIGKILL) einen Thread bei **noch laufendem** Stream trifft.

| Test | Ergebnis |
|---|---|
| 12 Zyklen + Snapshots (geflashtes Modul) | 0 Timeouts in 38 Abschaltungen, 0 Dekodierfehler, 41/41 Snapshots, Log `T31 ISP driver: open tx-isp` |
| 8 Stopps bei laufendem Stream, 3× `kill -9` | kein Zombie, Neustart danach normal |
| **20 Zyklen auf dem neuen Modul** | 48 Enables, 0 Fehler, 69/69 Snapshots (siehe Deadlock-Befund unten) |

Auf dem Stock-Treiber (Ursprung des Zombie-Befunds) kann ich das nicht mehr prüfen, weil die Kamera jetzt open-tx-isp hat.

**Befund Deadlock mit `dfbd90f` auf dem neuen Modul:** Im C-Zyklustest mit neuem Modul und `dfbd90f` blieb timps nach 6 Zyklen komplett stehen (`/control` antwortet nicht, RTSP-Verbindungen bleiben in der Listen-Queue, alle Threads schlafen, 0 % CPU). Letzte Ausgabe `[KernelIF] Stream stopped` von Kanal 1, danach nichts ([`fs-stop/run-p2-c2-deadlock.log`](fs-stop/run-p2-c2-deadlock.log)). Das passt zum bedingungslosen `pthread_cancel` in `dfbd90f`, das den Worker im VBM-Mutex abbricht. Mit `claude/t31-fs-stop-order` liefen danach 20 Zyklen fehlerfrei. `kill -9` löste den Hänger, kein Neustart nötig.

### G (OSD) mit HW-JPEG – bestanden

`OPENIMP_T31_OSD=1` mit `5087c02`: OSD (Datum/Uhrzeit, Name, Uptime, Logo) auch im Hardware-JPEG-Snapshot sichtbar (lokal angesehen), 15/15 Snapshots Ø 0,23 s, timpsd ~9,6 % CPU mit OSD + HW-JPEG + Snapshot alle 10 s, 0 Fehler. [`p1-g/`](p1-g/).

### I (Bewegung) auf open-tx-isp

- **Stufe 2 mit Client – bestanden** wie in Runde 1: 120 s ruhige Szene 0 Fehlalarme, jeder PTZ-Schwenk erkannt, Empfindlichkeit live übernommen, Stop/Start ok, CPU mit/ohne Erkennung ~10 %/~6 %, IVS-Statistik 149 Frames/10 s, 0 verworfen. PTZ zurück auf Ausgangsposition. [`p1-i/`](p1-i/).
- **Ohne Client – mit `dfbd90f` weiterhin kein Ergebnis** (auch auf open-tx-isp): 2 Frames geliefert, dann `no IVS move results … cycling`, `stalled:1`. Ursache (Analyse): gefüllte VBM-Ready-Queue wird nur vom Encoder (`PollingStream`) geleert; ohne Encoder-Abnehmer kehren die zwei Puffer nie zum Treiber zurück.
- **Mit `claude/t31-ivs-idle` – bestanden:** ohne Client `[IVS] chn0: 149 frames, 0 dropped busy, 25 results`, 0 Stillstandsmeldungen, `stalled:0`, PTZ-Schwenk erkannt; mit Main+Sub-Client unverändert (890 Frames/60 s, IVS-Statistik gleich, 0 Fehler). [`p2-ivs-idle/`](p2-ivs-idle/).

### Befund: timps braucht 4 s zum Beenden, wenn gerade ein Client verbunden wurde – behoben mit `claude/t31-ai-noblock`

Auf allen OpenIMP-Ständen (auch `3f501a2`/`dfbd90f`) endete jeder Stopp bei laufendem Client nach ~4 s mit `timpsd: shutdown alarm fired - hard exit` (20/20 in T1). Zeitstempel-Mitschnitt über `/dev/kmsg` ([`p2/stopdiag-dmesg.txt`](p2/stopdiag-dmesg.txt)): nach `framesource 0 disabled` 3,9 s Stille, der Codec-Abbau wird nie erreicht; mit dem neuen Modul kein `MSCA channel still busy`, also kein Kernel-Timeout. Ursache (Analyse, am Gerät bestätigt): timps leert beim Übergang idle→aktiv den Audiopuffer mit `IMP_AI_GetFrame(NOBLOCK)` bis 512 Frames; OpenIMP ignorierte `NOBLOCK` und blockierte pro Frame 40 ms → ~20 s (Log `audio resume: flushed 512 stale AI frame(s)` genau ~20 s nach dem Connect), Audio fehlt in dieser Zeit (`no AAC within warmup -> video-only mp4`), und ein Stopp in diesem Fenster wartet auf den Audio-Thread.

Mit `claude/t31-ai-noblock`: 4/4 Stopps (5 s bzw. 12 s nach Client-Connect) in 1,08–1,11 s mit `teardown complete`, kein `hard exit`, `flushed 6 stale AI frame(s)`, keine `no AAC`-Warnung, fMP4 enthält **H.264 und AAC** ab Beginn. Empfehlung zusätzlich für timps: die Drain-Schleife zeitlich begrenzen (robust gegen beide libimp-Semantiken).

---

# Phase 2: neues open-tx-isp-Modul (`claude/t31-isp-all` `97cccfa2` + thingino-Patches)

## Modultausch per insmod – nicht möglich, deshalb geflasht

Wie in der Anleitung (Streamer stoppen, `rmmod sensor_sc4336p_t31`, `rmmod tx_isp_t31`): Das **geflashte alte Modul** (opensensor `e92166b` + Patches) stürzt beim `rmmod` ab – `Unable to handle kernel paging request at virtual address 00000000, epc 8037cbf0 (__mutex_lock_slowpath)`, Modul bleibt `Unloading` ([`p2/rmmod-old-oops.txt`](p2/rmmod-old-oops.txt)). Ein erster Versuch mit `panic_on_oops=1` führte zum Neustart ohne Spuren; der zweite ohne lieferte den Oops. Mit Freigabe des Menschen wurde deshalb ein Image mit dem neuen Modul als installiertem Modul gebaut und per komplettem `make ota` geflasht (md5 des Moduls im Image `bbbfbbdd581448c667e7e61e47b80ad4`). Nach dem Flash änderte sich der SSH-Host-Key (neue Datenpartition), mit Freigabe ersetzt.

libimp für alle Phase-2-Tests: `dfbd90f` per Bind-Mount (wenn nicht anders angegeben). timps mit Konfigurationskopie `/tmp/timps-test.conf`, Stream-Abruf lokal über `https://127.0.0.1:8880/stream.mp4?chn=0` (fMP4). Hinweis: Die INFO-Marker der Anleitung (`stopped channel`, `ISP core quiet after`, `already open …`, `Day/night mode updated`) erscheinen mit `print_level=1` und thingino-Patch 0006 nicht in dmesg; ausgewertet wurde über Bytezahlen, Register und FAIL-Marker.

## Lebenszyklus T1–T7

| Test | Ergebnis |
|---|---|
| T1 Start/Stopp ×20 | **bestanden**: jede Runde 222–342 KB in 5 s, danach `0x13309804`/`0x13309808` = 0, keine FAIL-Treffer. Jeder Stopp ~5 s mit `hard exit` → Audio-Befund oben (mit `claude/t31-ai-noblock` behoben) |
| T2 Tag/Nacht ×20 bei laufendem Stream | **bestanden**: alle kurzen Abrufe mit Daten, Dauer-Client lief durch (52,8 MB), kein `encoder dead`. (Mit `isp_day_night_switch_drop_frame_num=0`; der Parameter ist nur beim `insmod` setzbar.) Zum Vergleich Phase 1 mit altem Modul: einmal 4,5 s Einfrieren nach einem Wechsel |
| T3 `kill -9` ×10 | **bestanden**: `0x13309804`=0 nach jedem Kill, kein Zombie, Neustart liefert 378–660 KB/5 s |
| T4 30 Snapshots parallel zum Stream | **bestanden**: 30/30 `ffd8`, Ø 195 KB, Stream-Client 120 s durchgehend (24,9 MB) |
| T5 zweites `open()` ×5 | **bestanden**: Datenrate mit/ohne gleich (4,36/4,24 MB in 30 s), keine Lücke |
| T6 20 Tag/Nacht-Kicks ohne Client | **bestanden**: danach sofort Daten (300 KB/5 s), keine FAIL-Treffer |
| T7 Speicher | Slab 5196 → 5472 KB über alle Tests, MemFree unverändert; kein stetiges Wachstum |

Dateien: [`p2/T*.txt`](p2/).

## Latenz alt gegen neu (gleiche libimp `dfbd90f`, Main-Stream aktiv)

| | altes Modul (geflasht, `e92166b`+Patches) | neues Modul (`97cccfa2`+Patches) |
|---|---|---|
| L2 Grundlast 60 s, max | 463 µs | 697 µs |
| L2 24 Tag/Nacht-Wechsel in 120 s, max | **1313 µs** | **660 µs** |
| Sekunden mit max > 1 ms | 2 | **0** |
| Aufwachvorgänge 500–999 µs | 194 | 198 |
| L4 `isp-m0` / `isp-w02` pro s | 115,9 / 29,6 | 121,1 / 29,6 |
| L4 `ctxt` pro s | 2736 | **2618** (−118) |

Die Tag/Nacht-Spitzen über 1 ms verschwinden mit dem neuen Modul. Dateien: [`p2/lat-*.txt`](p2/), [`p2/L4-*.txt`](p2/).

## Tuning-Tests (T31_ISP_TUNING_TEST.md, Abschnitt 5)

Ausgangswerte mit dem alten Modul: [`p2/tune-baseline-alt.txt`](p2/tune-baseline-alt.txt) (dort meldete `expr` noch `it_max=0`). Tests mit dem neuen Modul, timps läuft: [`p2/tuning-neu*.txt`](p2/).

| Test | Ergebnis |
|---|---|
| 5.1 Logging | bestanden (keine `Set control`-Zeilen) |
| 5.2 B7/B15 Zonen | bestanden: AF-Zonen nur `00`, AE-Zonen echte, sich ändernde Werte, kein `a5`-Muster, 0x8000045 20× `00` |
| 5.3 B2/B3 it_max | **bestanden**: `it_max=2246` (alt: 0), Kappe 1123 wirkt sofort (`it` → 1123), 0 → `EINVAL`, Rücksetzen ok. SetAeMin nur gelesen |
| 5.4 B14 / 5.5 B12 | bestanden (`EPERM` bzw. 2× `EINVAL`) |
| 5.6 Anti-Flicker nach FPS | nicht aussagekräftig: erwartete `T31_DEFLICK`-Zeile erscheint mit `print_level=1` nicht |
| 5.7 B9 ModuleControl | bestanden (Register unverändert) |
| 5.8 B4 Schärfe | **bestanden** (mit 1 s Pause nach jedem Setzen): 128/200/200/128/60/60/128 → identische Register für gleiche Werte, zurück auf Ausgangswert |
| 5.9 B5 Sinter | **bestanden**: 200 wirkt sofort, idempotent, zurück auf Ausgangswert |
| 5.10 B6 Defog | **bestanden**: 200 → 200, 200+128 → 128 |
| 5.11 B8/B17 | bestanden: Gewichte unverändert zurückgeschrieben, Gewicht 9 → `EPERM`, Histogramm gefüllt, ungültiger Zeiger → `EFAULT`, kein Oops. *Testtool-Fehler:* `parse_list` kopiert die Liste in `char tmp[1024]` und schnitt 225 Werte auf 205 ab; lokal auf 8192 vergrößert |
| 5.12 B11 Gamma | **bestanden**: gleiche LUT → Register unverändert, `lut[1]`+0x10 → sofort `0x00090000` → `0x000A0000`, zurück ok |
| 5.13 B10 CCM | bestanden (Register = gesetzte Matrix, auch nach 3 s; Attribut Byte 0 = 1; zurück auf Auto). Gesetzt wurde die aktuelle Matrix, nicht eine abweichende |
| 5.14 B13 AeAttr | **bestanden**: manuelle IT 1123 übernommen und gehalten, again 2×, zurück auf Auto |
| timps `image.ae_it_max_us` | **wirkt jetzt**: `image.ae_it_max_us=10000 -> capped AE at 454 lines; GetExpr now reports max=454 lines (9988us)`; `expr` bestätigt `it_max=454`. Zurücksetzen auf 0 hob die Kappe nicht auf (blieb 454) – für timps/libimp notiert |

## rmmod des neuen Moduls (L1) – nicht bestanden, Fix in Arbeit

- **`97cccfa2` (claude/t31-isp-all):** `rmmod tx_isp_t31` stürzt genauso ab wie das alte Modul (`__mutex_lock_slowpath`, NULL). addr2line: `tx_isp_exit` → `tx_isp_vic_remove` → `tx_isp_vic_stop` ([`p2/rmmod-new-oops.txt`](p2/rmmod-new-oops.txt)). Der Commit „cancel the day/night work on module exit" behebt diesen Absturz nicht. Neustart nötig.
- **Fix-Branch open-tx-isp `claude/t31-isp-rmmod-fix` (von einem Agenten, 2 Commits):** Ursachen laut Analyse: `vic_frame_end_lock` nie `mutex_init`, `kfree(vic_dev)` vor dessen Remove (Use-after-free + Doppel-free), `ourISPdev` im Core-Remove freigegeben. Zusätzlich `claude/t31-isp-csi-mutex` (3 Commits: CSI-Mutex lag über OEM-Feldern und war nie initialisiert, VIC-Frame-End-Completions und Custom-AE-Completion nie initialisiert; `BUILD_BUG_ON`-Layoutprüfung).
- **Gerätetest des geflashten Stands `claude/t31-isp-csi-mutex` `0e1c5773`** (Modul-md5 `8c9b353f5e460e16896f362fe348eb7a`): Das Modul läuft normal (Boot, Stream). `rmmod sensor` ok, `rmmod tx_isp_t31` führt zum **Neustart**. Live-Mitschnitt über `/proc/kmsg` ([`p2/kmsg-rmmod.txt`](p2/kmsg-rmmod.txt)): `Trying to free nonexistent resource <13310000-1331ffff>` und `<133e0000-133effff>`, direkt danach Oops in einem fremden Pfad (`kworker` → `call_usermodehelper` → `do_execve` → `kernel_read` → `__kmalloc`, BadVA 0x40) – beschädigte Slab-Freiliste, also noch ein falsches/doppeltes `kfree` im Entladepfad. Rückmeldung an den Agenten ist erfolgt.
- Folge: L1 und T2/L5 mit `isp_day_night_switch_drop_frame_num=6` konnten nicht getestet werden (der Parameter ist nur beim `insmod` setzbar).
- Nebenbei beim Flashen: Ein OTA-Versuch scheiterte mit `scp: /sbin/sysupgrade-stage2: No such file or directory`, weil `fw_ota.sh` `/overlay/usr` im laufenden Betrieb löscht und overlayfs danach keine Dateien unter `/usr` anlegen kann (hier gab es `/overlay/usr`); nach einem Neustart klappte das OTA. Für `thingino-firmware/scripts/fw_ota.sh` notiert.

## Test C + S mit beiden neuen Teilen

Neues Modul (`97cccfa2`) + libimp:

| libimp | Test | Ergebnis |
|---|---|---|
| `dfbd90f` | C, 12 Zyklen | **Deadlock nach 6 Zyklen** (siehe Befund oben, `pthread_cancel` im VBM-Mutex) |
| `claude/t31-fs-stop-order` | C, 20 Zyklen + Snapshots | **bestanden**: 48 Enables, 0 Fehler, 69/69 Snapshots |
| `claude/t31-fs-stop-order` | S, 30 min, 15 fps | **bestanden**: Main/Sub je 26669 Frames, 0 Dekodierfehler, 175/175 Snapshots Ø 0,23 s (HW-JPEG), alle AVC-Zähler 0, 0 ERR/WRN. [`p2-s/`](p2-s/) |


### Nachtrag: rmmod-Fix bestanden (open-tx-isp `claude/t31-isp-fixes` `6caeb75e`)

Zweiter Anlauf des Agenten (`c7c2e7d2`): Die Slab-Beschädigung kam von einem **doppelten `kfree` der Pad-Arrays** in `tx_isp_subdev_deinit` (die benannten Felder `outpads`/`inpads` überlappen die OEM-Rohslots +0xcc/+0xd0 mit vertauschten Rollen; bei VIN und CSI wurde dasselbe Array zweimal freigegeben). Zusätzlich: Abmelde-Reihenfolge (Core zuletzt, sonst hängt `__release_resource` die Kind-Regionen aus → „nonexistent resource"), fehlendes `misc_deregister` der vier `/dev/framechanN` (Use-after-free), doppeltes `cdev_del`. Integrations-Branch `claude/t31-isp-fixes` = rmmod-Fix + CSI-Mutex-Fix, als Image geflasht (Modul-md5 `4d5d49785f289b325b2518d88648d62d`).

| Test | Ergebnis |
|---|---|
| L1: 3× `rmmod sensor` + `rmmod tx_isp_t31` + `insmod` + `modprobe` | **bestanden**: alle rc=0, kein Oops (Kernel-Meldungen live über `/proc/kmsg` mitgeschnitten), danach keine `isp-m0`/`isp-w02` in `/proc/interrupts` und keine ISP-Regionen in `/proc/iomem`, Neuladen jedes Mal ok, MemFree 43132 → 43084 KB |
| T2/L5 mit `isp_day_night_switch_drop_frame_num=6` (per insmod), 20 Wechsel bei laufendem Stream | **bestanden**: jeder kurze Abruf mit Daten, Dauer-Client durchgehend (16,5 MB), nur einzelne `PollingStream idle (miss#1)`, kein `encoder dead` |

Dateien: [`p2/kmsg-L1-fixes.txt`](p2/kmsg-L1-fixes.txt), [`p2/L5-drop6.txt`](p2/L5-drop6.txt). Die ersten beiden Anläufe (`97cccfa2` und `claude/t31-isp-csi-mutex` mit dem ersten rmmod-Fix) stehen oben.

Nachtrag OTA: Der Fehler in `fw_ota.sh` tritt nicht bei jedem Flash auf. Beim letzten Flash klappte der erste Versuch trotz vorhandenem `/overlay/usr`. `/overlay/usr` stammt aus der Datenpartition des Images (Sounddateien unter `/usr/share/sounds`). Ein Fix liegt im thingino-Repo auf dem lokalen Branch `claude/fw-ota-overlay-fix` (Helfer nach `/tmp` statt `/sbin`, kein Löschen im Overlay), noch ungetestet.

## Abschlusslauf: alle OpenIMP-Korrekturen, neues Modul, 25 fps – bestanden

libimp `claude/t31-all-fixes` (`4653f0a`, md5 `b2ab4fee6923f8da59ae87cbd739b9bc`) = HW-JPEG-Standard + FrameSource-Stopp + IVS ohne Client + Audio-NOBLOCK; Modul `97cccfa2`+Patches; Konfigurationskopie mit `video0.fps = video1.fps = sensor.fps = 25` (`/etc` unverändert, dort bewusst 15 fps). 60 min, Main+Sub live, Snapshot alle 10 s. Dateien: [`final-25fps/`](final-25fps/).

| | Ergebnis |
|---|---|
| Frames Main / Sub | 88800 / 88824 (≈ 25 fps), kein 10-s-Intervall unter 233, 0 Verbindungsabbrüche |
| Dekodierfehler | 0 / 0 |
| Snapshots | 351/351 (HW-JPEG), Ø 0,22 s |
| Zähler | `completion timeout` 0, `without size writeback` 0, `EINTR` 0, `[ERR]` 0, `[WRN]` 1, Stopp-Timeout 0 |
| Start | `audio resume: flushed 6 stale AI frame(s)`, `HWJPEG: ready`, `T31 ISP driver: open tx-isp` |
| Speicher | belegt 33,7 → 37,4–38,0 MB (Cache), stabil |

## Nachtests in der Nacht (neues Modul `claude/t31-isp-fixes`, geflasht)

### `claude/t31-avc-au-check` – bestanden (auf `claude/t31-all-fixes` gemergt)

Allein (Basis `dfbd90f`) nicht testbar: Mit dem neuen Modul hing timps beim ersten Stopp des Sub-Streams im bekannten `pthread_cancel`-Deadlock (behoben in `claude/t31-fs-stop-order`) und musste mit `kill -9` beendet werden. Deshalb neuer Branch **`claude/t31-all-fixes-au`** (`5b8b451` = `claude/t31-all-fixes` + AU-Check; Konflikt in `src/t40/openimp_p2_encoder.c` von einem Agenten aufgelöst, beide Seiten behalten; libimp md5 `938e213dafc5e09a3d8262cd305b8555`).

Befehl: `cycle2.sh allau-c /tmp/libimp-allau.so 12`, danach dreimal `ffmpeg -loglevel verbose -rtsp_transport tcp -t 3 -i rtsp://…/ch0 -map 0:v -c copy -bsf:v trace_headers -f null -`.

| | Ergebnis |
|---|---|
| 12 Verbindungszyklen Main/Sub | 12/12, je 2 JPEGs Main, Sub 30 Frames/2 s, 0 Dekodierfehler, kein `allocation failed`/`rmem out of memory`, dieselbe PID |
| VCL-NALs pro AU (3 × 45 AUs) | immer genau 1 |
| `exceeds max`, Fehler im Trace | 0 |
| `idr_pic_id` | wechselt (1, dann 0) |
| `malformed AU` im timps-Log | 0 (Gate hat nichts verworfen) |

Dateien: [`au-check/`](au-check/). Die frühere ffmpeg-Meldung `(0+2) exceeds max (1)` trat in diesem Lauf nicht auf; die Diagnose bleibt im Code, um sie bei einem Wiederauftreten einzuordnen.

### `claude/t31-fs-timestamps` (`5e3b064`) – bestanden

libimp md5 `d7b29289631300ce35b314a7760e71ce`, 5 min Main+Sub live, Snapshots. Log: `[KernelIF] frame timestamps use CLOCK_MONOTONIC_RAW`. PTS-Abstände Sub (443 Frames): Median 0,067 s, p99 0,072 s, max 0,100 s, keine doppelten oder rückwärts laufenden PTS. Main/Sub 4446/4445 Frames, 0 Dekodierfehler, 27/27 Snapshots. Dateien: [`timestamps/`](timestamps/).

### Nachtlauf: `claude/t31-quiet-logs`, 25 fps, 6 h 40 min – bestanden

libimp `claude/t31-quiet-logs` (`6431d97`, md5 `be8d27034b00dda14b7dc1bcf5f5b465` = `claude/t31-all-fixes` + leisere Logs + schneller Tuning-Stopp), Modul `claude/t31-isp-fixes`, Konfigurationskopie mit 25 fps. 00:15–06:56, Main+Sub live dekodiert, Snapshot alle 10 s (bis 06:57), `top`/`free` alle 5 min. Dateien: [`night-25fps/`](night-25fps/).

| | Ergebnis |
|---|---|
| Frames Main / Sub | 592053 / 592111 (≈ 24,7 fps), kein 10-s-Intervall unter 227, 1 Verbindung je Stream über 24000 s, 0 Dekodierfehler |
| Snapshots | 2350/2350 (HTTP 200, JPEG), Ø 0,22 s; erster Snapshot 1,5 s, danach während Live-Clients verbunden max 0,34 s; ohne Live-Client (06:56–06:57, Kaltstart der FrameSource): 1,7 s |
| timps | dieselbe PID über die ganze Zeit, `[ERR]` 0, `[WRN]` 9 (alle `jpeg chn3: PollingStream idle (miss#1)`, siehe unten) |
| Logumfang | 25 KB in 6,7 h (gemessen alle 10 min; das Kürzen bei > 1 MB griff nie) |
| Speicher (`free` used) | nach der ersten Stunde 19,2–19,8 MB, kein Trend; RSS timpsd 4,1–4,7 MB |
| CPU timpsd | Median 7,6 % (5-min-Stichproben) |
| Stopp danach | 0,2 s (`stopped (waited 2x100ms)`) |

Nebenbefund: Die letzten Snapshots (06:56–06:57) fielen nach dem Ende der Live-Clients an. timps schaltet die FrameSource dafür jeweils ein und nach ~3,6 s wieder aus; der Snapshot braucht dann 1,7 s statt 0,2 s, und timps meldet bei jedem Kaltstart einmal `jpeg chn3: PollingStream idle (miss#1)`, weil die FrameSource erst 0,5 s läuft. Das ist kosmetisch und betrifft timps, nicht die libimp; timps könnte `miss#1` direkt nach dem Einschalten unterdrücken.

## Sammel-Branch `claude/t31-integration` – bestanden

`aedfced` = `claude/t31-all-fixes-au` + `claude/t31-quiet-logs` + `claude/t31-fs-timestamps` + zwei kleine Korrekturen (Zeitstempel-Folge startet neu, wenn die P0-Zeitbasis zurückspringt; Zusammenfassung zu synthetischen Zeitstempeln nur bei neuen Fällen). Merges von einem Agenten, ohne Konflikte; libimp md5 `d991d14150e555bd7827e86b14ea8da5`; Modul `claude/t31-isp-fixes` (geflasht).

| Test | Ergebnis |
|---|---|
| `cycle2.sh integ-c /tmp/libimp-integ.so 12` | 12/12, 0 Dekodierfehler, kein `allocation failed`/`rmem out of memory`, dieselbe PID |
| `trace_headers` (3 × 3 s Main) | je AU genau 1 VCL-NAL, `idr_pic_id` 1/0 wechselnd, 0 Fehler |
| Zeitstempel | `[KernelIF] frame timestamps use CLOCK_MONOTONIC_RAW` |
| `phase.sh integ-s 1200 10` (25 fps, Main+Sub live, Snapshots) | Main/Sub 29597/29609 Frames, kein 10-s-Intervall unter 234, 0 Fehler; 116/116 Snapshots Ø 0,22 s; Sub-PTS Median 0,040 s, max 0,047 s; `[ERR]` 0; Log 10 KB; Stopp 0,9 s |

Dateien: [`integration/`](integration/). Branch gepusht.

## open-tx-isp: DQBUF mit O_NONBLOCK und poll (`claude/t31-isp-dqbuf-nonblock`) – bestanden

Mit Zustimmung per `insmod` aus `/tmp` getestet (nicht geflasht), danach Neustart auf das geflashte Modul. Testprogramm `fc_nonblock_test` (von einem Agenten, statisch gegen die libimp-Objekte des Images gelinkt; steuert `/dev/framechan0` direkt). Ausgaben: [`isp-nonblock/fc_nonblock_test.txt`](isp-nonblock/fc_nonblock_test.txt).

- **Befund im bisherigen Modul (`6caeb75e`):** DQBUF ignoriert O_NONBLOCK (blockiert bis zum nächsten Frame), und **poll/select melden immer POLLIN**, auch ohne Frame und nach STREAMOFF. Ursache: `tx_isp_core.c` hatte eine eigene `static frame_channel_fops` ohne `.poll`; das `extern` bei der Registrierung der misc-Geräte band an diese Kopie, die vollständige fops in `tx_isp_module.c` war toter Code. Der libimp-Worker wartete deshalb nie wirklich in `select`, sondern im blockierenden DQBUF.
- **`85fbd1fd` (nur O_NONBLOCK):** (a) EAGAIN in 4 µs, aber poll weiter immer „bereit“ → mit timps dreht der FS-Worker (FS(0)-tick 2–10 % CPU statt ~0 %). So nicht einsetzbar.
- **`5fe6bbb2` (+ poll wirklich verdrahtet):** 6/6 PASS (EAGAIN 3 µs, poll ohne Frame 0, POLLIN nach 62 ms, POLLERR nach STREAMOFF, EINTR ohne Pufferverlust). timps mit `claude/t31-integration`, 5 min 25 fps: 0 Fehler, FS-Worker 0–0,3 % CPU; `kill -9` 3× ohne Zombie, Neustart sauber.
- **rmmod-Oops nach `kill -9`** (mit `5fe6bbb2`): `rmmod tx_isp_t31` → `Unable to handle kernel paging request`, Modul hing in „Unloading“, Neustart nötig ([`isp-nonblock/oops.txt`](isp-nonblock/oops.txt)). Ursache (Agent, per Symbolauflösung): Use-after-free des Sensormoduls, **schon in `6caeb75e` vorhanden**. Nur `IMP_ISP_DelSensor` meldete den Sensor beim ISP ab; nach `kill -9` von timps fehlt das, `rmmod` des Sensors gibt dessen Speicher frei, und `tx_isp_exit()` ruft danach `reset` über den veralteten Zeiger auf. Frühere rmmod-Tests waren sauber, weil vorher DelSensor lief.
- **Fix `d9b68caa`:** `tx_isp_subdev_deinit()` löst einen noch angemeldeten Sensor vom ISP (Liste, Slot, `ourISPdev->sensor`, gesicherte ops). Test ([`isp-nonblock/rmmod-cycles-d9b68caa.txt`](isp-nonblock/rmmod-cycles-d9b68caa.txt)): 3 Zyklen insmod → `fc_nonblock_test` (6/6) → timps 40 s → Ende per `kill -9` / sauberem Stopp / `kill -9` → rmmod Sensor + tx-isp: alle sauber, kein Oops. Danach Neustart auf das geflashte Modul.
- Restrisiko laut Agent: nach `kill -9` laufen ISP/AE-Threads weiter; ein Sensor-Zugriff, der genau mit dem Entladen des Sensors zusammenfällt, ist weiter möglich (sehr kleines Fenster). Vollständig wäre ein Stopp von ISP/Sensor beim letzten `close` von `/dev/tx-isp`.
- Branch `claude/t31-isp-dqbuf-nonblock` (`85fbd1fd` → `5fe6bbb2` → `d9b68caa`) gepusht. Für den Einsatz im Image müsste `OPEN_TX_ISP_VERSION` umgestellt und neu geflasht werden (nicht gemacht).

## OSD standardmäßig an (`claude/t31-osd-default`) – bestanden

Bisher blendete OpenIMP das OSD auf T31 nur mit `OPENIMP_T31_OSD=1` ein (timps legt die Regionen an, im Bild erschien nichts). `18127d2` (auf `claude/t31-integration`) schaltet das IPU-Backend standardmäßig ein, `OPENIMP_T31_OSD=0` schaltet es ab. libimp md5 `4569960ed6d62804dcd77d367c73c50f`. Test per Bind-Mount, 15 fps (`/etc`-Konfiguration als Kopie): `phase.sh osd-s 300 10` → Main/Sub 4446/4445 Frames, 0 Fehler, 27/27 Snapshots Ø 0,23 s, timpsd 8,5–12,6 % CPU, `[ERR]` 0; OSD (Datum/Uhrzeit, Name, Uptime, Logo) im Main-Snapshot und im Sub-Stream sichtbar (lokal angesehen, nicht eingecheckt); `cycle2.sh osd-c … 12` → 12/12, dieselbe PID.

## Neues Image geflasht (komplette OTA, mit Zustimmung)

thingino `ciao` (Build wie zuvor) mit open-tx-isp `claude/t31-isp-dqbuf-nonblock` `d9b68caa` (Modul md5 `da108912…`) und OpenIMP `claude/t31-osd-default` `18127d2` (libimp md5 `4569960e…`, identisch mit dem getesteten Build). Erst nur das Modul (08:10, mit Flash-libimp `3f501a2`: 3 min Main+Sub 0 Fehler, FS-Thread 0,2 % CPU, zwei Neustarts über `S95timps` in 1–2 s), dann beides (08:35). Nach dem Flash: timps läuft über `S95timps`, Log `T31 IPU OSD backend enabled`, Main und Sub dekodierbar, OSD im Bild, kein Oops.

## Endzustand der Kamera (2026-10-01, 08:37)

Neues Image (siehe oben): Modul `d9b68caa`, libimp `18127d2` mit OSD, kein Bind-Mount, timps über `S95timps` mit `/etc/timps.conf` (15 fps), Main und Sub dekodierbar. `/etc` und Overlay unverändert; Testdateien lagen nur in `/tmp`.
