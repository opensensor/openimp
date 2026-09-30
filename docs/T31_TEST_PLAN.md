# Testplan für den Branch `claude/t31-re` (für eine lokale Claude-Code-Session)

Dieses Dokument ist der **Einstieg**. Es sagt, was getestet wird, in welcher Reihenfolge,
auf welcher Kamera, und was zurückgemeldet wird. Die Einzelschritte stehen in den verlinkten
Anleitungen. Alles hier ist bisher nur gebaut und statisch geprüft, **nichts ist auf einer
Kamera gelaufen**. Genau das soll dieser Test nachholen.

Du arbeitest auf einem Rechner im selben Netz wie die Testkamera und erreichst sie per SSH.

**Kurzweg:** Der Branch `claude/t31-all` enthält alles aus `claude/t31-re`, `claude/t31-perf`,
`claude/t31-avc-stability` und `claude/t31-ivs-move`. Für B und alle Tests ab C diese eine
Bibliothek bauen; wo unten ein anderer Branch genannt ist, gilt `claude/t31-all`. Die neuen
Funktionen sind per Umgebungsvariable schaltbar (siehe jeweiligen Test). Zeigt sich ein Fehler,
den Test mit der Bibliothek des dort genannten Einzel-Branches wiederholen, um ihn einzugrenzen.

---

## 0. Regeln (verbindlich)

1. **Nur ein Testgerät.** Die AVPU oder IPU kann hängen, dann hilft nur ein Neustart.
2. **Nichts dauerhaft ändern.**
   - Nicht flashen, nichts unter `/etc` oder im Overlay ändern.
   - Neue Dateien nur nach `/tmp`.
   - Die neue `libimp.so` nur per Bind-Mount einhängen, ein Neustart stellt alles wieder her.
3. **Git:** Im Fork `Lu-Fi/openimp` nur in Branches schreiben, die du selbst **neu** anlegst.
   Nie auf `main` oder einen bestehenden Branch pushen (etwa `claude/t31-re`, `claude/t31-all`),
   nie force-pushen.
4. **Vor jedem Stoppen des Streamers kurz Bescheid geben.** Bei Unklarheit fragen.
5. **Alles melden,** auch Fehlschläge, mit dem genauen Befehl davor.
6. **Kamerabilder** (Snapshots, Testausgaben mit Kamerabild) nur mit Einverständnis des
   Menschen weitergeben oder committen. Die Testbilder der Tools sind unkritisch.

---

## 1. Was auf dem Branch liegt und welcher Test es abdeckt

| Änderung | Datei(en) | Test |
|---|---|---|
| Testtool Hardware-JPEG-Core | `tools/t31_hwjpeg_probe.c` | A1 |
| Testtool IPU-OSD + Auswertung | `tools/t31_ipu_osd_probe.c`, `tools/ipu_osd_analyze.py` | A2 |
| OSD-Strukturen im Vendor-Layout (32 statt 72 Byte), behebt Stack-Überschreiben in `IMP_OSD_GetRgnAttr` | `src/t31/openimp_t31_osd_abi.h`, `src/t31/openimp_t31_services.c` | C |
| Schalter `OPENIMP_T31_COMPANION_STAGE=0` (unnötigen JPEG-Core-Start pro H.264-Frame abschalten) | `src/t40/codec-t40.c` | D |
| Hardware-JPEG in OpenIMP, Schalter `OPENIMP_T31_HW_JPEG=1` | `src/t40/codec-t40.c`, `src/hw_encoder.c` | E |
| `IMP_ISP_Tuning_SetIntegrationTime` für T20/T21/T30 | `src/isp/isp_tseries.c` | F (optional, T20-Kamera) |
| Tracer für die Original-libimp | `tools/oem_trace/` | Anhang (optional) |
| Nur auf `claude/t31-perf`: schnellere Software-JPEG (bitgleiche Ausgabe) | `src/hw_encoder.c` | H |
| Nur auf `claude/t31-perf`: Hardware-JPEG liest die Frame-Kopie direkt aus rmem (eine 3-MB-Kopie weniger) | `src/t40/openimp_p2_encoder.c`, `src/t40/codec-t40.c` | H |
| Nur auf `claude/t31-perf`: OSD-Zeichnen mit der IPU, Schalter `OPENIMP_T31_OSD=1` | `src/t31/openimp_t31_services.c`, `src/t40/openimp_p2_encoder.c` | G |
| Nur auf `claude/t31-avc-stability` (enthält `claude/t31-perf`): H.264-Pfad erholt sich statt einzufrieren, Schalter `OPENIMP_T31_AVC_LEGACY=1` für A/B | `src/t40/codec-t40.c`, `src/t40/openimp_p2_encoder.c` | S |
| Nur auf `claude/t31-ivs-move`: IVS-Bewegungserkennung (move, base move) statt Stubs, Vergleichstool | `src/t31/openimp_t31_ivs*.{c,h}`, `src/kernel_interface.c`, `tools/t31_ivs_compare.c` | I |

---

## 2. Benötigte Hardware

- **Pflicht:** eine **T31**-Testkamera mit thingino `aperto` und offenem Stack
  (open-tx-isp + OpenIMP). Prüfen:
  `strings /usr/lib/libimp.so | grep -i -m1 openimp` findet etwas.
- **Für A1/A2** reicht jede T31 mit `/dev/avpu` und `/dev/ipu`, auch mit Original-Stack.
- **Optional:** eine T20-Kamera mit OpenIMP (Test F); eine T31 mit Original-Stack (Anhang).
- **Toolchain:** Die thingino-Toolchain für das Kameraprofil
  (`<thingino>/output/<branch>/<target>/host/bin/mipsel-linux-`) wird für `libimp.so` (Test B)
  gebraucht. Die Testtools (A1, A2) sind statisch und gehen auch mit `gcc-mipsel-linux-gnu`.
  Gibt es noch keinen thingino-Build, den Menschen fragen.

---

## 3. Reihenfolge

Von risikoarm nach risikoreich. Nach jedem Schritt die Ergebnisse notieren. Bei einem
**Abbruchkriterium** stoppen und melden, statt weiterzumachen.

### A1. Hardware-JPEG-Testtool (ohne Bibliothekswechsel)

Anleitung: [`T31_HW_JPEG_TEST.md`](T31_HW_JPEG_TEST.md), Abschnitt 2 inklusive der
Zusatzläufe (`-s`, `-c`).

- **Erfolg:** `SOI present`, `EOI present`, das Testbild ist korrekt.
- **Abbruchkriterium für E:** A1 liefert kein gültiges JPEG. Dann E überspringen, D trotzdem
  machen.

### A2. IPU-OSD-Testtool (ohne Bibliothekswechsel)

Anleitung: [`T31_OSD_IPU_TEST.md`](T31_OSD_IPU_TEST.md), inklusive `-n 200`.

- **Erfolg:**
  - Alle vier Ebenen erscheinen an der richtigen Stelle, außerhalb ändert sich nichts.
  - Farben und Alpha stimmen.
  - Die drei Zeiten sind gemessen.
- Diese Ergebnisse steuern die OSD-Implementierung. Hier gibt es kein Abbruchkriterium für
  die anderen Tests.

### B. `libimp.so` bauen und einhängen (`claude/t31-all`)

Anleitung: [`T31_HW_JPEG_TEST.md`](T31_HW_JPEG_TEST.md), Abschnitt 3.

```sh
cd openimp && git switch claude/t31-all      # oder der Einzel-Branch des Tests
TC=<thingino>/output/<branch>/<target>/host/bin/mipsel-linux
THINGINO_DIR=<thingino> TOOLCHAIN_PREFIX=$TC T31_OUTPUT_DIR=$PWD/build/t31 sh build-t31.sh
scp build/t31/libimp.so root@<kamera>:/tmp/libimp-t31re.so
# auf der Kamera:
cp /usr/lib/libimp.so /tmp/libimp-orig.so
mount --bind /tmp/libimp-t31re.so /usr/lib/libimp.so
```

Für die Tests C–E startest du den Streamer von Hand, mit den Variablen des jeweiligen Tests
(Befehlszeile aus dem Init-Skript). Siehe [`T31_HW_JPEG_TEST.md`](T31_HW_JPEG_TEST.md),
Abschnitt 4.

### C. Regression mit der neuen Bibliothek, ohne Schalter

- Streamer ohne zusätzliche Variablen starten, 10 Minuten laufen lassen:
  - RTSP-Stream öffnen
  - ein paar Snapshots abrufen
  - OSD in der Streamer-Konfiguration eingeschaltet lassen
- Vergleich mit der Original-OpenIMP (`/tmp/libimp-orig.so`), gleicher Ablauf:
  - Startet der Streamer?
  - Laufen H.264 und JPEG?
  - Stürzt etwas ab? (`dmesg`, `logread`)
  - Meldet der Streamer OSD-Fehler?
- **Erwartung:**
  - Verhalten wie vorher.
  - Das OSD ist weiterhin **unsichtbar**. Das ist bekannt, das Zeichnen kommt erst nach A2.
  - Die OSD-Aufrufe des Streamers laufen ohne Fehler, und nichts stürzt ab.
- **Abbruchkriterium:** Absturz oder Stream bricht ab, obwohl er mit der Original-OpenIMP
  läuft. Dann nicht weiter, sondern melden (mit `logread`/`dmesg`).
- **FrameSource-Zyklen (rmem-Leck, behoben auf `claude/rmem-allocator`, in `claude/t31-all`
  enthalten):** Den Kanal mindestens 20-mal aus- und einschalten lassen. Bei timps geht das,
  indem sich der letzte RTSP-Client trennt und wieder verbindet; ein Tag/Nacht-Wechsel geht
  auch. Danach muss der Stream noch laufen, und im Log darf kein
  `rmem out of memory` stehen. Zum Vergleich: vorher scheiterte `EnableChn` nach 5–7 Zyklen.
  Mit `OPENIMP_RMEM_NO_REUSE=1` lässt sich das alte Verhalten nachstellen.
  Auf Bildfehler direkt nach einem Zyklus achten. Die würden bedeuten, dass noch Hardware in
  einen schon freigegebenen Puffer schreibt.

### D. Companion-Stage A/B

Anleitung: [`T31_HW_JPEG_TEST.md`](T31_HW_JPEG_TEST.md), Abschnitt 4a. Lauf A ohne Variablen,
Lauf B mit `OPENIMP_T31_COMPANION_STAGE=0`, je 5 Minuten RTSP mitschneiden und vergleichen.

### E. Hardware-JPEG im Streamer

Anleitung: [`T31_HW_JPEG_TEST.md`](T31_HW_JPEG_TEST.md), Abschnitt 4b. Lauf C ohne, Lauf D mit
`OPENIMP_T31_HW_JPEG=1`. CPU-Last per `top`, `HWJPEG:`-Zeilen im Log, Bildqualität.

### F. Optional: `SetIntegrationTime` auf T20

Nur mit einer T20-Kamera mit OpenIMP und timps als Streamer.

1. `libimp.so` für T20 bauen:
   `THINGINO_DIR=<thingino> TOOLCHAIN_PREFIX=$TC T20_OUTPUT_DIR=$PWD/build/t20 sh build-t20.sh`
2. Wie in B einhängen.
3. Vorher prüfen, ob timps gegen die bisherige OpenIMP überhaupt startet. Bisher fehlte ihm
   dort genau diese Funktion, also sollte der Start scheitern.
4. Mit der neuen Bibliothek timps starten und `image.ae_it_max_us` setzen (z. B. `10000`, per
   `POST /control` oder in `/etc/timps.conf` in einer Kopie unter `/tmp`).
5. Einen RTSP-Client verbinden. Die Kappung greift nur, während Frames fließen.
6. Im timps-Log die Meldung `image.ae_it_max_us=… -> capped AE at … lines; GetExpr now reports
   max=…` suchen.

- **Erfolg:** Das gemeldete Maximum entspricht dem Wunsch, kein `SDK rejected the cap`.

### G. OSD im Streamer (Branch `claude/t31-perf`)

Erst nach A2 und C. Anleitung: [`T31_OSD_IPU_TEST.md`](T31_OSD_IPU_TEST.md), Stufe 2.
Für G und H die Bibliothek aus `claude/t31-perf` statt `claude/t31-re` bauen (sonst wie B).

- **Erfolg:** OSD sichtbar in RTSP und Snapshot, kein `backend disabled`, CPU-Last nicht höher
  als ohne Variable (Ziel: niedriger als mit der Original-libimp, falls Vergleich möglich).

### H. JPEG-Optimierungen (Branch `claude/t31-perf`)

Wie E, aber mit der Bibliothek aus `claude/t31-perf`:

1. Ohne Variablen: Snapshots holen, CPU-Last beim MJPEG-Abruf mit E (Lauf C) vergleichen. Die
   Bilder müssen identisch aussehen wie mit `claude/t31-re`.
2. Mit `OPENIMP_T31_HW_JPEG=1`: Snapshots/MJPEG, `HWJPEG:`-Zeilen, CPU-Last mit E (Lauf D)
   vergleichen.

- **Erfolg:** gleiche Bilder, gleiche oder niedrigere CPU-Last, keine neuen Fehler im Log.

### I. IVS-Bewegungserkennung (Branch `claude/t31-ivs-move`)

Erst nach C. Anleitung: [`T31_IVS_MOVE.md`](T31_IVS_MOVE.md), Abschnitt 6. Die Bibliothek aus
`claude/t31-ivs-move` bauen (sonst wie B), dazu das Tool `t31_ivs_compare`.

1. Stufe 1: `t31_ivs_compare` gegen die Stock-libimp, einmal mit SIMD, einmal mit
   `/tmp/closesimd`. Läuft neben dem Streamer, ohne Kamerabild.
2. Stufe 2: timps mit `-c /tmp/timps-ivs.conf`, `motion.enabled = 1` und
   `OPENIMP_T31_IVS_STATS=1`. Ruhige Szene, Gehtest (nur mit Einverständnis), Empfindlichkeit
   live, Last und Bildrate mit und ohne Bewegungserkennung.

- **Erfolg:**
  - Stufe 1: `differ=0` bei move und base in beiden Läufen (`oob-differ` ist erlaubt).
  - Stufe 2: Treffer bei Bewegung, keine bei ruhiger Szene, kein `stalled`, Bildrate des
    Hauptstreams unverändert.
- **Abbruchkriterium:** Absturz oder eingefrorener Stream mit der neuen Bibliothek. Dann melden.

### S. H.264-Stabilität (Branch `claude/t31-avc-stability`)

Anleitung: [`T31_AVC_STABILITY_TEST.md`](T31_AVC_STABILITY_TEST.md). Der Branch enthält auch
alles aus `claude/t31-perf`, G und H können also mit derselben Bibliothek laufen.

- **Erfolg:** Dauerlauf Main + Sub ohne Einfrieren; nach einem Hänger läuft der Stream nach
  höchstens ca. 2 s mit einem IDR weiter.

---

## 4. Ergebnisbericht

Eine Datei `T31_TEST_RESULTS.md` mit einem Abschnitt pro Test (A1, A2, C, D, E, F, G, H, I, S):

- **Status:** bestanden / fehlgeschlagen / übersprungen, mit Grund.
- **Die jeweils in der Einzelanleitung verlangten Ausgaben:**
  - Tool-Ausgaben
  - `ipu_osd_analyze.py`-Ausgabe
  - Logzeilen `HWJPEG:` / `companion stage` / `[IVS]`
  - `t31_ivs_compare`-Ausgaben
  - `top`-Werte
  - Frame-Zählungen
- **Vorne ein Kopf mit Geräteangaben:**
  - Modell, SoC-Variante (`/proc/cpuinfo`)
  - thingino-Stand, Kernel
  - Streamer und Version
  - Commit von `claude/t31-re` (`git rev-parse --short HEAD`)

Den Bericht und die kleinen Dateien, also Tool-Ausgaben, PNGs aus A2, `ep1.bin` und Logs,
auf einem **neuen eigenen** Branch `claude/t31-test-results-<datum>` im Fork pushen, unter
`test-results/`. Keine Videos, keine Kamerabilder ohne Einverständnis. Oder alles direkt dem
Menschen übergeben.

Zum Schluss den Bind-Mount lösen (`umount /usr/lib/libimp.so`) oder die Kamera neu starten und
prüfen, dass der Streamer normal läuft.

---

## Anhang: Traces der Original-libimp (optional)

Steht zusätzlich eine T31 mit **Original**-Stack zur Verfügung, beantwortet ein Mitschnitt
der Original-Bibliothek viele offene Fragen direkt: JPEG-Header, Tabellenpuffer,
IPU-Parameter. Anleitung: [`RE_TRACE_GUIDE.md`](RE_TRACE_GUIDE.md). Das ist kein Teil des
Pflichttests.
