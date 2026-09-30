# T31 Hardware-JPEG: Testanleitung für eine lokale Claude-Code-Session

Diese Anleitung ist für eine Claude-Code-Session gedacht, die auf einem Rechner im
selben Netz wie eine **T31-Testkamera** läuft und per SSH auf sie zugreifen kann. Sie
setzt keine Vorkenntnisse aus früheren Sessions voraus.

Hintergrund und Registerkarte stehen in [`T31_HW_JPEG_RE.md`](T31_HW_JPEG_RE.md), offene Punkte
und ein Code-Review in [`T31_HW_JPEG_RE_FOLLOWUP.md`](T31_HW_JPEG_RE_FOLLOWUP.md). Die
Kurzfassung:

- Der Software-Encoder von OpenIMP kostet grob 0,3 s CPU pro 1080p-Bild und blockiert
  während dieser Zeit H.264-Submits.
- Die Original-`libimp` nutzt dafür einen eigenen Hardware-Core im AVPU (Core 1). Der
  braucht ~8 ms und belastet die CPU praktisch nicht.
- OpenIMP baut den Hardwarepfad nach. Auf einer T31X geprüft (gültige Bilder, timpsd
  18,5 % statt 66,9 % CPU) und seitdem **Standard**. `OPENIMP_T31_HW_JPEG=0` schaltet
  zurück auf den Software-Encoder (Opt-out). Dieser Test prüft das auf weiteren Kameras.

---

## 0. Regeln

Die Regeln sind verbindlich. Bei Unklarheit fragst du den Menschen.

1. **Nur ein Testgerät.** Beim Registerstochern kann die AVPU hängen. Dann hilft nur ein
   Neustart, bei einem Watchdog-Loop eventuell ein Stromzyklus.
2. **Nichts dauerhaft ändern.**
   - Kein Flashen, kein Schreiben nach `/etc` oder auf das Overlay.
   - Neue Dateien nur nach `/tmp`.
   - Die neue `libimp.so` nur per Bind-Mount einhängen, ein Neustart macht alles rückgängig.
3. **Git:** Im Repo `Lu-Fi/openimp` nur in Branches schreiben, die du selbst neu anlegst
   (z. B. `claude/t31-hw-jpeg-results`). Niemals auf `main`, `claude/t31-re`, `claude/t31-hw-jpeg` oder
   andere bestehende Branches pushen, niemals force-pushen.
4. **Vor jedem Schritt, der den Streamer stoppt,** kurz Bescheid geben: Die Kamera
   liefert dann kein Bild.
5. **Ergebnisse vollständig melden,** auch Fehlschläge. Ein sauber dokumentiertes „geht
   nicht, weil …“ ist genauso wertvoll.

---

## 1. Voraussetzungen prüfen

Auf dem lokalen Rechner:

```sh
git clone https://github.com/Lu-Fi/openimp && cd openimp
git switch claude/t31-re
git log --oneline -15
```

Der Branch enthält (u. a.):

| Commit | Inhalt |
|---|---|
| `t31: allow disabling the AVPU companion stage …` | Schalter `OPENIMP_T31_COMPANION_STAGE=0` |
| `t31: opt-in hardware JPEG …` | Hardwarepfad; inzwischen Standard, `OPENIMP_T31_HW_JPEG=0` schaltet ihn ab |
| `t31: log hardware JPEG state …` | `HWJPEG:`-Meldungen im Syslog |
| `tools: add t31_hwjpeg_probe …` | eigenständiges Testtool |

Auf der Kamera (per SSH):

```sh
uname -a; cat /etc/os-release 2>/dev/null | head -5
grep -i -m1 'soc\|system type' /proc/cpuinfo         # muss T31 sein
ls -l /dev/avpu                                       # muss existieren
lsmod | grep -i -E 'avpu|tx.isp'                      # welcher Treiber?
ls -l /usr/lib/libimp.so; strings /usr/lib/libimp.so | grep -i -m3 openimp
ls /etc/init.d/ | grep -i -E 'timps|prudynt|raptor|rvd|streamer'
df -h /tmp                                            # Platz für ~2 MB
```

Notiere:

- **Userspace:** Läuft OpenIMP (`strings` findet „openimp“) oder die Original-`libimp`?
- **Streamer:** welcher, und mit welchem Init-Skript?
- **Befehlszeile des Streamers:** Das Init-Skript lesen (`cat /etc/init.d/S95…`). Stufe 3
  startet den Streamer von Hand mit zusätzlichen Umgebungsvariablen.

Die Kamera muss die Variante mit offenem Stack haben (thingino-Branch `aperto`, open-tx-isp +
OpenIMP). Für Stufe 1 reicht jeder T31 mit `/dev/avpu`.

---

## 2. Stufe 1: Testtool `t31_hwjpeg_probe` (ohne Streamer)

Das Tool kodiert **ein** Bild direkt über den Hardware-Core und prüft dabei die
Kernannahmen der Rekonstruktion:

- IRQ-Slot 4
- Registerblock `0x8400–0x8428`
- Tabellenformat (EP1)
- ob die Hardware die JFIF-Header selbst schreibt

### Bauen

Statisch, damit es unabhängig von der libc der Kamera läuft. Entweder mit der
thingino-Toolchain (`<thingino>/output/<branch>/<target>/host/bin/mipsel-linux-gcc`) oder
mit einem Debian/Ubuntu-Cross-Compiler (`gcc-mipsel-linux-gnu`):

```sh
mipsel-linux-gnu-gcc -O2 -march=mips32r2 -static -Wall -Wextra \
    -o t31_hwjpeg_probe tools/t31_hwjpeg_probe.c
scp t31_hwjpeg_probe root@<kamera>:/tmp/
```

(Ältere Dropbear-Versionen brauchen `scp -O`.)

### Ausführen

Das AVPU-Treibermodul bedient nur **einen** Client, also muss der Streamer gestoppt sein:

```sh
/etc/init.d/<S95streamer> stop
sleep 1
/tmp/t31_hwjpeg_probe -v -e /tmp/ep1.bin 2>&1 | tee /tmp/probe-1080p.txt
/tmp/t31_hwjpeg_probe -W 640 -H 360 -o /tmp/hwjpeg-360.jpg 2>&1 | tee /tmp/probe-360p.txt
dmesg | tail -40 > /tmp/probe-dmesg.txt
/etc/init.d/<S95streamer> start
```

Ohne `-i` kodiert das Tool ein eingebautes Testbild. Oben liegen horizontale
Graustufen, in der Mitte ein 32-px-Schachbrett, unten vertikale Graustufen, im unteren
Farbbereich acht Farbbalken. Die Ausgabe landet in `/tmp/hwjpeg.jpg`.

### Auswerten

Die Dateien holen (`scp root@<kamera>:/tmp/{hwjpeg.jpg,hwjpeg-360.jpg,probe-*.txt,ep1.bin} .`)
und prüfen:

```sh
file hwjpeg.jpg
ffprobe -v error -show_entries stream=width,height,pix_fmt hwjpeg.jpg
python3 -c "from PIL import Image; im=Image.open('hwjpeg.jpg'); im.load(); print(im.size, im.mode)"
```

Dann das Bild ansehen. Stimmen Graustufen, Schachbrett und Farbbalken?

| Beobachtung | Bedeutung | Nächster Schritt |
|---|---|---|
| `SOI present`, `EOI present`, Bild korrekt | Rekonstruktion stimmt, die Hardware schreibt die Header | weiter mit Stufe 2 |
| `no IRQ slot 4 within 2000 ms` | Core startet nicht oder der IRQ kommt nicht an | Registerwerte aus `-v` und `dmesg` melden; mit `-n` (ohne Core-0-Init) wiederholen |
| IRQ kommt, `len` = 0 oder unplausibel | Core läuft, schreibt aber nichts | `0x8430/0x8438` melden; `/tmp/hwjpeg-stream.raw` holen |
| `ERROR/overflow bit` | Puffer zu klein oder Tabellen ungültig | mit `-q 30` wiederholen; Ausgabe melden |
| `no SOI`, aber Daten vorhanden | Hardware liefert nur Entropie-Daten | erste 64 Bytes von `hwjpeg.jpg` als Hexdump melden (`xxd hwjpeg.jpg \| head -4`) |
| Bild dekodiert, aber Blockartefakte oder falsche Helligkeit | Quant-Tabellen oder Reziproke in falscher Byte-Reihenfolge | `ep1.bin` mitliefern; siehe unten „EP1 gegen Stock“ |
| Farben vertauscht (Blau/Rot) | U/V-Reihenfolge | melden |
| Kamera hängt | AVPU blockiert | Neustart; genau notieren, nach welchem Aufruf |

Zusätzliche Probe-Läufe, nachdem der Basistest funktioniert:

```sh
/tmp/t31_hwjpeg_probe -s 65536 -o /tmp/overflow.jpg      # erwartet: Overflow-Bit (0x8438 Bit 1)
/tmp/t31_hwjpeg_probe -c 0x31 -o /tmp/bit8off.jpg         # Bit 8 aus: was ändert sich?
/tmp/t31_hwjpeg_probe -c 0x00080131 -o /tmp/dri8.jpg      # Restart-Intervall 8 in [31:16]: RST-Marker im JPEG?
```

Jeweils Ausgabe, `0x85f8`-Werte und `xxd <datei> | head -4` melden. Ob `dri8.jpg`
Restart-Marker enthält: `python3 -c "d=open('dri8.jpg','rb').read(); print([hex(d[i+1]) for i in range(len(d)-1) if d[i]==0xff and 0xd0<=d[i+1]<=0xd7][:5], b'\xff\xdd' in d)"`.

Mit einem echten Kamerabild (optional, OpenIMP-Streamer läuft vorher):

```sh
# einmal pro Kanal ein Quellbild ablegen lassen
OPENIMP_T31_DUMP_SOURCE_DIR=/tmp <streamer-befehlszeile> &   # siehe Stufe 3
ls -l /tmp/openimp-source-ch*.nv12
# Streamer stoppen, dann:
/tmp/t31_hwjpeg_probe -i /tmp/openimp-source-ch0-1920x1080.nv12 -W 1920 -H 1080 -o /tmp/real.jpg
```

Das Tool akzeptiert NV12 gepackt (`W*H*3/2` Bytes) oder mit auf 16 Zeilen aufgefüllter
Luma-Ebene. Hat die Dump-Datei eine andere Größe, wird sie als gepackt gelesen. Die Farben
sind dann verschoben: bitte melden, mit Dateigröße.

---

## 3. Stufe 2: `libimp.so` aus diesem Branch bauen

Die `libimp.so` muss gegen die libc der Kamera (uClibc oder musl) gelinkt werden. Das
geht nur mit der thingino-Toolchain des passenden Kameraprofils. Gibt es noch keinen
thingino-Build, den Menschen fragen. Nach einem `aperto`-Build (`make` für das Profil der
Testkamera) liegt die Toolchain unter `<thingino>/output/<branch>/<target>/host/bin/`.

```sh
cd openimp
TC=<thingino>/output/<branch>/<target>/host/bin/mipsel-linux
THINGINO_DIR=<thingino> TOOLCHAIN_PREFIX=$TC T31_OUTPUT_DIR=$PWD/build/t31 sh build-t31.sh
file build/t31/libimp.so
scp build/t31/libimp.so root@<kamera>:/tmp/libimp-hwjpeg.so
```

(`build-t31.sh` ruft `$TOOLCHAIN_PREFIX-gcc` und `$TOOLCHAIN_PREFIX-strip` auf.)

Auf der Kamera einhängen, nur bis zum nächsten Neustart:

```sh
cp /usr/lib/libimp.so /tmp/libimp-orig.so           # zum Vergleichen
mount --bind /tmp/libimp-hwjpeg.so /usr/lib/libimp.so
```

Zurück: `umount /usr/lib/libimp.so` oder Neustart.

---

## 4. Stufe 3: Tests im laufenden Streamer

Den Streamer für jeden Durchgang **von Hand** starten, damit die Umgebungsvariablen
sicher ankommen. Die Befehlszeile aus dem Init-Skript übernehmen:

```sh
/etc/init.d/<S95streamer> stop
<VAR>=<wert> <streamer-befehlszeile> > /tmp/run-<name>.log 2>&1 &
sleep 20
logread 2>/dev/null | grep -E 'HWJPEG|companion stage' | tail -20   # oder: grep … /tmp/run-<name>.log
```

Prüfen, dass die Variable wirklich gesetzt ist:
`tr '\0' '\n' < /proc/$(pidof <streamer>)/environ | grep OPENIMP`

### 4a. Companion-Stage A/B (H.264-Regression)

Hintergrund: Mit `OPENIMP_T31_HW_JPEG=0` startet OpenIMP nach **jedem** H.264-Frame zusätzlich den JPEG-Core. Die
Einstellungen dafür sind aus einem Hersteller-Trace abgeschrieben, in dem parallel ein
JPEG-Kanal lief. Laut Herstellercode gehört das nicht zu H.264. Es schreibt vermutlich in
einen H.264-Stream-Puffer, der gerade noch beim Streamer liegen kann.

Hardware-JPEG (Standard) schaltet die Companion-Stage immer ab. Für diesen Vergleich
deshalb in beiden Läufen `OPENIMP_T31_HW_JPEG=0` setzen:

| Lauf | Variablen |
|---|---|
| A | `OPENIMP_T31_HW_JPEG=0` (Companion-Stage an) |
| B | `OPENIMP_T31_HW_JPEG=0 OPENIMP_T31_COMPANION_STAGE=0` |

Je Lauf 5 Minuten RTSP abgreifen:

```sh
ffmpeg -rtsp_transport tcp -i rtsp://<kamera>/<pfad> -t 300 -c copy run-A.mp4
ffprobe -v error -count_frames -select_streams v -show_entries stream=nb_read_frames run-A.mp4
ffmpeg -v error -i run-A.mp4 -f null - 2>&1 | head -20      # Dekodierfehler?
```

Außerdem `logread` bzw. das Log nach `timeout`, `stall`, `error` durchsuchen und das Bild
kurz ansehen.

**Erwartung:** B ist mindestens so gut wie A. Wird B schlechter (weniger Frames, Timeouts,
Artefakte), ist das ein wichtiger Befund: Dann hängt H.264 wider Erwarten davon ab.

### 4b. Hardware-JPEG

| Lauf | Variablen |
|---|---|
| C | `OPENIMP_T31_HW_JPEG=0` (Software-JPEG) |
| D | keine (Hardware-JPEG, Standard) |

Last erzeugen, von einem anderen Rechner oder auf der Kamera: jede Sekunde ein Snapshot
bzw. einen MJPEG-Stream offen halten. Den Endpunkt aus der Doku des Streamers nehmen.

```sh
while true; do curl -s -o /dev/null -w '%{http_code} %{size_download} %{time_total}\n' \
    'http://<kamera>:<port>/<snapshot-pfad>'; sleep 1; done
```

Messen:

- **CPU:** `top -b -H -n 6 -d 5 > /tmp/top-<lauf>.txt` während der Last
  (pro Thread, Streamer-Threads und `idle`).
- **Log D:**
  - `HWJPEG: T31 hardware JPEG enabled (OPENIMP_T31_HW_JPEG=0 selects the software encoder)`
  - `HWJPEG: ready, ep1=…`
  - `HWJPEG: 1920x1080 q75 -> … bytes`
  - oder, als Fehler geloggt, `HWJPEG: disabled for this process, all JPEG frames now use the software encoder: <Grund>`
- **Log C:** `HWJPEG: T31 hardware JPEG off (OPENIMP_T31_HW_JPEG=0), software encoder`
- **Bilder aus D:** ein paar Snapshots speichern und ansehen. Das Bild muss korrekt sein.
- **H.264 in D:** wie in 4a kurz prüfen, dass der Stream sauber bleibt.

**Erwartung:** In D sinkt die CPU-Last des Streamers während der Snapshots deutlich, die
JPEGs sind korrekt, und H.264 bleibt stabil.

**Wenn `disabled: …` erscheint:**

| Grund | Bedeutung |
|---|---|
| `repeated completion timeouts` | IRQ 4 kam dreimal in Folge nicht an. Die vorherigen Zeilen `HWJPEG: no IRQ 4 within 200 ms (status …, 0x85f8 …)` mitliefern. |
| `hardware did not emit a JFIF stream` | kein `FF D8` am Anfang oder kein `FF D9` in den letzten 64 Bytes. Siehe Stufe 1, Header |
| `implausible length` | siehe Stufe 1 |
| `DMA allocation failed` / `EP1 … aligned` | Speicher. Die Log-Zeile `HWJPEG: ready …` fehlt, bitte melden. |

Weitere Meldungen, die **nicht** abschalten:

- `HWJPEG: no AVC IRQ waiter yet, software until one runs`: Der gemeinsame IRQ-Thread
  startet mit dem ersten H.264-Kanal. Bis dahin kodiert der Pfad in Software. Erscheint
  danach nie ein `HWJPEG: … bytes`, läuft kein H.264, bitte melden.
- `HWJPEG: AVC core not initialised yet, software until it is`: Der AVPU-Core wird mit dem
  ersten H.264-Frame initialisiert (und nach dem letzten H.264-Kanal wieder abgeschaltet).
  Solange kodiert der Pfad in Software, ohne das als Timeout zu zählen.
- `HWJPEG: stream overflow …`: Das eine Bild geht in Software, beim nächsten wird der
  Stream-Puffer verdoppelt.
- `HWJPEG: cannot grow the stream buffer to … bytes, keeping …`: Die Verdopplung bekam
  keinen Speicher. Der alte Puffer bleibt, nur Bilder, die ihn wieder sprengen, gehen in
  Software. Diese Größe wird nicht erneut versucht.

**Wenn die Bilder aus D kaputt aussehen** (Streifen, alte Bildinhalte, Blockmüll), der
Probe aus Stufe 1 aber saubere Bilder liefert: Durchgang D mit zusätzlich
`OPENIMP_T31_HW_JPEG_SRC_COHERENT=1` wiederholen. Dann liegt auch das Quellbild in
kohärentem statt gecachtem Speicher. Wird es damit sauber, ist der Cache-Flush des
gecachten rmem auf T31 die Ursache. Das ist ein wichtiger Befund.

Die Stufe 3a (Companion-Stage) vor 3b auswerten: Hardware-JPEG (Standard) schaltet die
Companion-Stage immer mit ab, sonst lassen sich H.264-Auffälligkeiten nicht zuordnen.

---

## 5. Ergebnisbericht

Einen Bericht `T31_HW_JPEG_RESULTS.md` schreiben. Entweder an den Menschen übergeben oder
auf einem **neuen eigenen** Branch `claude/t31-hw-jpeg-results` im Fork pushen (Regel 3).

Inhalt:

1. **Gerät:**
   - Kameramodell, SoC-Variante (`/proc/cpuinfo`)
   - thingino-Stand, Kernel
   - Streamer
   - libimp: OpenIMP-Commit bzw. Original
2. **Stufe 1:**
   - komplette Ausgaben von `t31_hwjpeg_probe` (1080p und 360p)
   - Bewertung der Bilder
   - Hexdump der ersten 64 Bytes
   - `dmesg`-Auszug
3. **Stufe 3a:** Frames, Fehler und Auffälligkeiten A gegen B.
4. **Stufe 3b:**
   - CPU-Werte C gegen D (Tabelle)
   - `HWJPEG:`-Logzeilen
   - JPEG-Größen und Bildqualität
   - H.264-Stabilität
5. **Alles Unerwartete,** mit genauem Zeitpunkt und dem Befehl davor.

Dateien (JPEGs, `ep1.bin`, Logs, `top`-Ausgaben) beilegen bzw. mitcommitten, keine Videos.

---

## Anhang: EP1 gegen Stock abgleichen (falls Tabellen verdächtig)

Wenn das Bild dekodiert, aber falsch aussieht, liegt der Fehler meist im Tabellenpuffer:

- Byte-Reihenfolge der Quant-Bytes
- Reziproke
- Kopfworte bei `0x180`

Der sicherste Abgleich ist ein EP1-Dump der Original-`libimp` bei laufendem JPEG-Kanal.

1. Die Adresse steht im Register `0x8418` (per `devmem` oder `tools/avpu_peek.c` lesen,
   während die Original-`libimp` JPEG erzeugt).
2. Die 0x790 Bytes ab dieser physischen Adresse sichern (`devmem` wortweise oder
   `tools/t31_phys_dump.c`).
3. Mit `-E stock-ep1.bin` an `t31_hwjpeg_probe` übergeben. Ist das Bild damit korrekt, per
   `cmp -l ep1.bin stock-ep1.bin` die Unterschiede zum eigenen Generator melden.
