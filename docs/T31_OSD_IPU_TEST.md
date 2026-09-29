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
git switch claude/t31-osd-abi
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
