# T31 OSD über die IPU: Testanleitung (Stufe 1, Testtool)

Für eine lokale Claude-Code-Session mit SSH-Zugang zu einer **T31-Testkamera**. Es gelten
dieselben Regeln wie in [`T31_HW_JPEG_TEST.md`](T31_HW_JPEG_TEST.md), Abschnitt 0:
- nur ein Testgerät
- nichts dauerhaft ändern, Dateien nur nach `/tmp`
- nur in selbst angelegte neue Branches pushen
- Bescheid geben, bevor der Streamer gestoppt wird

Hintergrund: [`T31_OSD_RE.md`](T31_OSD_RE.md). Kurz gesagt:
- Die Original-`libimp` blendet Text und Logos (BGRA-Bitmaps) sowie Abdeckflächen mit dem
  IPU-Block (`/dev/ipu`) direkt in das NV12-Bild.
- OpenIMP zeichnet auf T31 bisher gar nichts.
- Dieses Tool prüft die rekonstruierte IPU-Ansteuerung und misst die Kosten. Mit den
  Ergebnissen wird das OSD-Backend in OpenIMP gebaut.

## Bauen

```sh
git clone https://github.com/Lu-Fi/openimp && cd openimp
git switch claude/t31-re
mipsel-linux-gnu-gcc -O2 -march=mips32r2 -static -Wall -Wextra \
    -o t31_ipu_osd_probe tools/t31_ipu_osd_probe.c        # oder die thingino-Toolchain
scp t31_ipu_osd_probe root@<kamera>:/tmp/
```

## Ausführen

Das Tool holt sich DMA-Speicher über `/dev/avpu`, deshalb muss der Streamer gestoppt sein:

```sh
ls -l /dev/ipu                                   # muss existieren (CONFIG_JZ_IPU)
/etc/init.d/<S95streamer> stop; sleep 1
/tmp/t31_ipu_osd_probe -v -n 200 2>&1 | tee /tmp/ipu-probe.txt
dmesg | tail -30 > /tmp/ipu-dmesg.txt
/etc/init.d/<S95streamer> start
```

Das Tool blendet vier Ebenen in ein 640×368-Testbild (Graustufenverlauf):

| Kanal | Inhalt | Position |
|---|---|---|
| 0 | BGRA-Bitmap: 5 Farbbänder, Alpha-Rampe 0…255 von links nach rechts | (32, 32) |
| 1 | dieselbe Bitmap, Pixel-Alpha × globales Alpha 128 | (32, 128) |
| 2 | Abdeckfläche rot, deckend | (224, 32) |
| 3 | Abdeckfläche blau, Alpha 128 | (224, 128) |

Mit `-n 200` misst es außerdem:
- einen IPU-Durchlauf mit 4 Ebenen
- 4 Durchläufe mit je 1 Ebene
- den 1-MB-Cache-Flush, den die Original-`libimp` pro Region und Frame macht

Scheitert der kombinierte Lauf, jede Ebene einzeln testen: `-l 1`, `-l 2`, `-l 4`, `-l 8`
(jeweils mit `-d /tmp/l<n>` in ein eigenes Verzeichnis).

## Auswerten (auf dem PC)

```sh
scp 'root@<kamera>:/tmp/ipu-*' ./ipu/
python3 tools/ipu_osd_analyze.py ./ipu            # braucht numpy, für PNGs Pillow
```

Das Skript zeigt:
- wo sich das Bild verändert hat (je Ebene an der richtigen Stelle, außerhalb nichts)
- welche Farbumrechnung und Alpha-Rundung die gemessenen Werte am besten erklären
- die Farbwerte der Abdeckflächen

Es erzeugt außerdem `ipu-before.png` und `ipu-after.png`. Die bitte ansehen:
- Stehen die Farben in Band-Reihenfolge rot, grün, blau, weiß, schwarz?
- Blendet das Alpha von links nach rechts ein?
- Kanal 1 halb so deckend wie Kanal 0?
- Rot deckend, blau halbtransparent?

## Rückmeldung

- `ipu-probe.txt` und `ipu-dmesg.txt`
- die Ausgabe von `ipu_osd_analyze.py`
- die beiden PNGs und die Bewertung (was sieht falsch aus?)
- die drei Zeiten aus `-n` (µs pro Durchlauf bzw. Flush)

| Beobachtung | Bedeutung |
|---|---|
| `IOCTL_IPU_START` Fehler, `dmesg` „ipu: error …“ | Ein Parameterwort stimmt nicht. Mit `-l` die Ebene finden. |
| Farben der Bitmap vertauscht (rot ↔ blau) | Byte-Reihenfolge im `para`-Wort, Bits 14–17 |
| Bitmap deckend statt Alpha-Rampe | Alpha-Modus, Bits 1–2 |
| Abdeckfläche falsche Farbe / zu dunkel | Farbwort (Y ohne +16?) oder Masken-Bit 23 |
| Bild außerhalb der Kästen verändert | Positions- oder Stride-Problem |

---

# Stufe 2: OSD-Backend in OpenIMP (Branch `claude/t31-perf`)

Erst machen, wenn Stufe 1 (Testtool) plausible Bilder geliefert hat. Auf `claude/t31-perf`
zeichnet OpenIMP die OSD-Regionen mit der IPU, sobald `OPENIMP_T31_OSD=1` gesetzt ist. Ohne die
Variable ändert sich nichts. Seit `claude/t31-osd-default` ist das Backend standardmäßig an;
`OPENIMP_T31_OSD=0` schaltet es ab.

Was das Backend anders macht als die Original-`libimp`:
- bis zu 4 Regionen pro IPU-Durchlauf (Original: ein Durchlauf pro Region), nach `layer` sortiert
- Bitmaps werden nur bei einer Änderung ins rmem kopiert, pro Frame gibt es keinen
  1-MB-Cache-Flush mehr
- nach 10 IPU-Fehlern in Folge schaltet es sich ab (Logzeile `T31 IPU OSD backend disabled`),
  der Stream läuft weiter

## Ablauf

1. `libimp.so` von `claude/t31-perf` bauen und wie im Testplan, Schritt B, per Bind-Mount
   einhängen (`git switch claude/t31-perf` statt `claude/t31-re`).
2. Im Streamer das OSD eingeschaltet lassen (Uhrzeit, ggf. Logo und eine Privatzone).
3. Streamer von Hand starten, einmal ohne und einmal mit Variable:

   ```sh
   /etc/init.d/<S95streamer> stop; sleep 1
   OPENIMP_T31_OSD=1 <streamer-befehlszeile> > /tmp/osd-run.log 2>&1 &
   ```

4. RTSP-Stream öffnen und 5 Minuten laufen lassen. Dabei:
   - `top -b -n 5 -d 2 | grep -E '<streamer>|CPU:'` einmal ohne, einmal mit Variable
   - `grep -E 'OSD' /tmp/osd-run.log`
   - `dmesg | grep -i ipu | tail`
5. Bild ansehen (nur der Mensch oder mit seinem Einverständnis): Steht die Uhrzeit an der
   richtigen Stelle, zählt sie sekündlich weiter, stimmen Farben und Transparenz, ist die
   Privatzone deckend?
6. Wenn der Streamer Snapshots/MJPEG anbietet: einen Snapshot holen. Das OSD muss auch dort
   sichtbar sein (JPEG liest das Bild nach dem Blending).

## Rückmeldung

| Beobachtung | Bedeutung |
|---|---|
| `backend requested`, kein `disabled`, OSD sichtbar | Erfolg. CPU-Last mit/ohne notieren. |
| `disabled: cannot open /dev/ipu` | Kernel ohne IPU-Treiber (`ls -l /dev/ipu`) |
| `disabled: repeated IPU errors` | Parameterwort falsch, `dmesg` mitschicken |
| OSD im RTSP sichtbar, im Snapshot nicht | Cache-Invalidierung nach dem Blending reicht nicht |
| Text flackert oder zeigt alte Uhrzeit | Bitmap-Update (Double Buffer / Write-back) prüfen |
| Farben vertauscht, falsch deckend | wie in der Tabelle von Stufe 1 |
| Stream ruckelt oder FPS sinkt | IPU-Zeit pro Frame zu hoch, `-n 200` aus Stufe 1 vergleichen |
