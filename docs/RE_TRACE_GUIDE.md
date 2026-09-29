# Reverse Engineering auf der Kamera: Traces der Original-libimp aufzeichnen

Anleitung für eine **lokale Claude-Code-Session**, die per SSH auf eine T31-Testkamera
zugreift. Ziel: Ground Truth von der Original-`libimp` des Herstellers aufzeichnen, damit
OpenIMP-Rekonstruktionen nicht mehr geraten, sondern belegt sind.

Der Branch `claude/t31-re` im Fork `Lu-Fi/openimp` enthält alle Werkzeuge:

| Werkzeug | Zweck | Anleitung |
|---|---|---|
| `tools/oem_trace/oem_trace.c` | `LD_PRELOAD`-Tracer: schneidet die ioctls der libimp mit und sichert DMA-Daten | dieses Dokument |
| `tools/t31_hwjpeg_probe.c` | Hardware-JPEG-Core direkt ansteuern | [`T31_HW_JPEG_TEST.md`](T31_HW_JPEG_TEST.md) |
| `tools/t31_ipu_osd_probe.c` + `tools/ipu_osd_analyze.py` | IPU-OSD direkt ansteuern und auswerten | [`T31_OSD_IPU_TEST.md`](T31_OSD_IPU_TEST.md) |
| `tools/ep1_match.py` | EP1-Mitschnitt gegen OpenIMPs Generator vergleichen | unten |

Hintergrund: [`T31_HW_JPEG_RE.md`](T31_HW_JPEG_RE.md),
[`T31_HW_JPEG_RE_FOLLOWUP.md`](T31_HW_JPEG_RE_FOLLOWUP.md), [`T31_OSD_RE.md`](T31_OSD_RE.md).

---

## 0. Regeln

1. **Nur ein Testgerät.** Nichts flashen, nichts unter `/etc` oder im Overlay ändern.
   Dateien nur nach `/tmp`, ein Neustart stellt alles wieder her.
2. **Git:** In `Lu-Fi/openimp` nur in Branches schreiben, die du selbst neu anlegst (z. B.
   `claude/oem-traces-<datum>`). Nie auf `main` oder bestehende Branches pushen, nie
   force-pushen.
3. **Privatsphäre:** Der Tracer speichert JPEG-Bilder der Kamera. Vor dem Aufzeichnen den
   Menschen fragen, worauf die Kamera zeigt (neutrale Szene oder Objektiv abdecken). Bilder
   nur mit seinem Einverständnis committen.
4. Vor dem Stoppen des Streamers Bescheid geben.
5. Alles melden, auch Fehlschläge.

---

## 1. Was die Traces beantworten sollen

| Frage | Belegt durch |
|---|---|
| Schreibt die Hardware die JPEG-Header (SOI…EOI) selbst? | `jpeg-*.bin` beginnt mit `ff d8` |
| Ist unser EP1-Tabellenpuffer byte-identisch zum Original? | `ep1-*.bin` + `tools/ep1_match.py` |
| Welche Werte schreibt das Original in `0x8400` (Bit 8, Restart), `0x8408`, und was steht in `0x8430`/`0x85F8`? | `jpeg start:`-Zeilen und `avpu R 0x8430` im Log |
| Welche `para`-, `fmt`- und `bak_argb`-Werte nutzt die IPU für Text, Logo und Abdeckung? | `ipu  chN …`-Zeilen |
| In welchem Format liegen die OSD-Bitmaps vor? | `osd-*.bin` |
| Wie oft flusht die Original-libimp den Cache für OSD (Kosten)? | `ipu FLUSH_CACHE`-Zeilen pro Frame |
| Was schicken die ISP-Tuning-Funktionen an den Kernel? | `isp TUNING`-Zeilen |
| Wo unterscheidet sich OpenIMP vom Original? | derselbe Trace einmal mit OpenIMP (Abschnitt 5) |

---

## 2. Voraussetzungen

- **T31-Kamera mit dem proprietären Stack:** Original-`libimp.so` und Original-Kernelmodule.
  - Das ist thingino `master` bzw. `aperto` mit `BR2_PACKAGE_THINGINO_ISP_PROPRIETARY=y`.
  - Prüfen: `strings /usr/lib/libimp.so | grep -i -m1 openimp` muss **leer** sein.
- **Ein Streamer, der JPEG und OSD nutzt,** also prudynt oder timps mit OSD und
  Snapshot/MJPEG.
- **Root und `/dev/mem`:** `ls -l /dev/mem`. Ohne `/dev/mem` gibt es nur das Log, keine Dumps.
- **Die thingino-Toolchain des Kameraprofils:** Der Tracer ist eine Shared Library und muss
  gegen die libc der Kamera (uClibc oder musl) gebaut sein. Ein glibc-Cross-Compiler reicht
  **nicht**. Gibt es noch keinen thingino-Build, den Menschen fragen.

---

## 3. Bauen

```sh
git clone https://github.com/Lu-Fi/openimp && cd openimp
git switch claude/t31-re
TC=<thingino>/output/<branch>/<target>/host/bin/mipsel-linux
$TC-gcc -O2 -fPIC -shared -Wall -Wextra -o liboem_trace.so tools/oem_trace/oem_trace.c -ldl
file liboem_trace.so                                   # MIPS32, dynamically linked
scp liboem_trace.so root@<kamera>:/tmp/

# fürs Auswerten auf dem PC
gcc -O2 -o probe-host tools/t31_hwjpeg_probe.c         # nur für -G (EP1 erzeugen)
```

---

## 4. Aufzeichnen (Original-libimp)

1. Die Befehlszeile des Streamers aus dem Init-Skript ermitteln:

   ```sh
   ls /etc/init.d/ | grep -i -E 'prudynt|timps|raptor|rvd'
   cat /etc/init.d/<S95streamer>
   ```

2. Streamer stoppen und von Hand mit dem Tracer starten:

   ```sh
   /etc/init.d/<S95streamer> stop; sleep 1
   rm -rf /tmp/oemtrace
   OEM_TRACE_DIR=/tmp/oemtrace OEM_TRACE_DUMPS=4 LD_PRELOAD=/tmp/liboem_trace.so \
       <streamer-befehlszeile> > /tmp/oemtrace-stdout.log 2>&1 &
   ```

3. 20–30 Sekunden laufen lassen. Dabei:
   - einen RTSP-Stream öffnen (H.264 läuft),
   - 3–5 Snapshots bzw. kurz MJPEG abrufen (JPEG-Jobs),
   - sicherstellen, dass das OSD sichtbar ist (Uhrzeit, Logo, ggf. eine Privatzone).

4. Stoppen und einsammeln:

   ```sh
   kill $(pidof <streamer>); sleep 2
   du -sh /tmp/oemtrace; ls -la /tmp/oemtrace
   tar czf /tmp/oemtrace-stock.tgz -C /tmp oemtrace
   /etc/init.d/<S95streamer> start
   ```

Das Log wächst mit jedem H.264-Frame um einige Registerzeilen, also ein paar KB pro Sekunde.
`/tmp` liegt im RAM, deshalb nicht minutenlang laufen lassen.

Funktioniert `LD_PRELOAD` nicht (keine `trace.log`), prüfen:
- Linkt der Streamer `libimp.so` dynamisch? `cat /proc/<pid>/maps | grep libimp`
- Setzt das Init-Skript die Umgebung zurück?

---

## 5. Aufzeichnen mit OpenIMP (Vergleich)

Hat der Mensch auch eine Kamera oder ein Image mit dem offenen Stack (OpenIMP), denselben Ablauf
dort wiederholen, mit derselben Szene und denselben Aktionen. Ergebnis `oemtrace-openimp.tgz`.
Der Tracer funktioniert unverändert, er sieht nur eine andere `libimp.so`.

---

## 6. Auswerten (auf dem PC)

```sh
mkdir stock && tar xzf oemtrace-stock.tgz -C stock
L=stock/oemtrace/trace.log

# JPEG: Header, Registerwerte, Status
xxd stock/oemtrace/jpeg-000.bin | head -4                  # beginnt mit ffd8ffe0?
python3 -c "from PIL import Image; im=Image.open('stock/oemtrace/jpeg-000.bin'); im.load(); print(im.size)"
grep 'jpeg start:' $L | head -3
grep -E 'avpu R 0x(8430|8434|8438|85f8)' $L | head -12
grep -E 'avpu W 0x85f[04]' $L | head -6

# EP1 gegen unseren Generator
python3 tools/ep1_match.py ./probe-host stock/oemtrace/ep1-*.bin

# IPU/OSD
grep -c 'ipu START' $L                                     # Durchläufe im Mitschnitt
grep 'ipu  ch' $L | awk '{print $4,$5,$6}' | sort | uniq -c | sort -rn | head
grep -c 'ipu FLUSH_CACHE' $L
ls stock/oemtrace/osd-*.bin

# ISP-Tuning
grep 'isp TUNING' $L | awk '{print $5}' | sort | uniq -c | sort -rn | head -20
```

Mit OpenIMP-Trace (Abschnitt 5) zusätzlich die JPEG-Startzeilen und `ipu`-Zeilen
nebeneinanderlegen:

```sh
diff <(grep 'jpeg start:' stock/oemtrace/trace.log | head -1 | cut -d' ' -f3-) \
     <(grep 'jpeg start:' openimp/oemtrace/trace.log | head -1 | cut -d' ' -f3-)
```

Adressen unterscheiden sich natürlich. Interessant sind die Worte 0–3 (`cmd …`) und die Größen.

---

## 7. Ergebnis zurückmelden

Einen Bericht `RE_TRACE_RESULTS.md` schreiben, mit:

1. **Gerät:**
   - Modell, SoC-Variante
   - thingino-Stand
   - Streamer und Version
   - Stack (proprietär/OpenIMP)
2. **Die Antworten auf die Fragen aus Abschnitt 1,** jeweils mit den belegenden Logzeilen.
3. **Die Ausgabe von `ep1_match.py`.**
4. **IPU:** die häufigsten Kanalkonfigurationen (`fmt`, `para`, `bak_argb`) und die Zahl der
   Flushes pro Frame.
5. **Auffälligkeiten.**

Den Bericht und die Trace-Archive (ohne Kamerabilder, außer der Mensch stimmt zu) auf einem
**neuen eigenen** Branch `claude/oem-traces-<datum>` im Fork pushen, z. B. unter `traces/`.
Oder beides direkt dem Menschen übergeben.
