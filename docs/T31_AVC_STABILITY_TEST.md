# T31 H.264-Stabilität: Testanleitung (Branch `claude/t31-avc-stability`)

Für eine lokale Claude-Code-Session mit SSH-Zugang zu einer **T31-Testkamera** mit offenem
Stack (open-tx-isp + OpenIMP). Es gelten die Regeln aus
[`T31_TEST_PLAN.md`](T31_TEST_PLAN.md), Abschnitt 0:
- nur ein Testgerät
- nichts dauerhaft ändern, Dateien nur nach `/tmp`, `libimp.so` nur per Bind-Mount
- nur in selbst angelegte neue Branches pushen
- Bescheid geben, bevor der Streamer gestoppt wird

Hintergrund: [`T31_AVC_STABILITY_RE.md`](T31_AVC_STABILITY_RE.md) (Befunde F1–F14; die
Zeilennummern dort beziehen sich auf `claude/t31-re`).

## Was der Branch ändert

| Befund | Änderung | Sichtbar im Log |
|---|---|---|
| F1 | Nach 2 s ohne Completion: Core-Reset, offene Befehle verwerfen, nächstes Bild IDR. Eine verlorene IRQ eines fertigen Frames wird ab 0,5 s über den Writeback erkannt. P2 hält den Core-Lock nur noch wenige Sekunden statt bis zu einer Stunde. | `AVC: completion timeout, core reset`, `AVC: completion found without IRQ` |
| F2 | IRQ ohne Größen-Writeback wird ignoriert statt den Frame zu verwerfen | `AVC: IRQ … without size writeback … ignored` |
| F3 | Ein IRQ-Waiter pro Gerät mit Nutzerzähler; kein 100-%-CPU-Spin bei `EINTR` | ggf. `keeps returning EINTR` |
| F4 | Zerstören eines Kanals stört den anderen nicht mehr | `AVC: … other session(s) active, core left initialized` |
| F5 | Verworfenes Bild erzwingt ein IDR; passt ein Bild nicht in den Stream-Puffer, steigt der QP | `AVC: picture larger than the stream buffer` |
| F6 | Software-JPEG blockiert H.264 nicht mehr | – |
| F7 | Core-Init nur einmal pro Gerät | – |
| F8 | `RequestIDR` geht nicht mehr verloren | – |

Der Schalter `OPENIMP_T31_AVC_LEGACY=1` stellt das alte Verhalten von F1 und F2 wieder her
und schaltet den Core beim Zerstören eines Kanals wieder immer ab. F3 und F5–F8 sowie das
Verwerfen von IRQs ohne Besitzer bleiben aktiv. Der A/B-Vergleich deckt also vor allem F1/F2 ab.

Weitere Meldung: `AVC: size written back but core still running` heißt, eine Größe stand schon
im Speicher, der Core lief aber noch. Dann wird auf die eigene IRQ gewartet. Einzelne Treffer
sind harmlos, viele sprechen dafür, dass die Hardware die Größe vor dem Frame-Ende schreibt.

Die Meldungen in der Tabelle erscheinen immer (stderr und syslog, also auch in `logread`).
Mit `OPENIMP_DEBUG_TRACE=1` kommen sehr viele Detailzeilen pro Frame dazu; das nur für die
Fehlersuche einschalten, nicht für den Dauerlauf.

## Bauen und einhängen

Wie im Testplan, Schritt B, aber mit diesem Branch:

```sh
cd openimp && git fetch origin && git switch claude/t31-avc-stability
TC=<thingino>/output/<branch>/<target>/host/bin/mipsel-linux
THINGINO_DIR=<thingino> TOOLCHAIN_PREFIX=$TC T31_OUTPUT_DIR=$PWD/build/t31 sh build-t31.sh
scp build/t31/libimp.so root@<kamera>:/tmp/libimp-stab.so
# auf der Kamera:
mount --bind /tmp/libimp-stab.so /usr/lib/libimp.so
```

Den Streamer für die Tests von Hand starten (Befehlszeile aus dem Init-Skript) und die
Ausgabe nach `/tmp/stab-<lauf>.log` umleiten.

## Tests

### S1. Dauerlauf Main + Sub (Pflicht)

1. Streamer mit Haupt- und Substream starten, beide per RTSP abrufen, dazu alle 10 s ein
   Snapshot.
2. Mindestens 2 Stunden laufen lassen, besser über Nacht (niedrige Bildrate, Nachtbild).
3. Auf dem PC mitprüfen, ob der Stream Fehler hat:
   `ffmpeg -v error -rtsp_transport tcp -i rtsp://<kamera>/<pfad> -f null - 2>&1 | tee ffmpeg-main.txt`
4. Am Ende zählen:

   ```sh
   L=/tmp/stab-s1.log
   grep -c 'completion timeout' $L
   grep -c 'without size writeback' $L
   grep -c 'completion found without IRQ' $L
   grep -c 'keeps returning EINTR' $L
   ```

- **Erfolg:** beide Streams laufen durch, `ffmpeg` meldet höchstens vereinzelte Fehler, alle
  Zähler sind 0 oder klein und jedes `completion timeout` wird nach höchstens ca. 2 s von
  einem IDR gefolgt (Stream läuft sichtbar weiter).
- Ein Vergleichslauf mit der bisherigen Bibliothek (`claude/t31-perf` oder Original-OpenIMP)
  unter gleichen Bedingungen ist sehr hilfreich.

### S2. Snapshot-Last (F6)

1. Nur RTSP Main laufen lassen, Bildintervalle messen:
   `ffprobe -rtsp_transport tcp -show_frames -select_streams v -of csv=p=0 -show_entries frame=pts_time -read_intervals %+30 rtsp://… > s2-ohne.csv`
2. Dasselbe, während parallel Snapshots in einer Schleife geholt werden
   (`while true; do curl -s -o /dev/null http://<kamera>/image.jpg; done`; Pfad je nach
   Streamer).
3. Beide Läufe mit dieser Bibliothek und mit der alten.

- **Erfolg:** Mit dieser Bibliothek zeigen die Bildintervalle unter Snapshot-Last keine
  deutlich größeren Lücken als ohne.

### S3. Kanal neu anlegen (F3, F4)

Nur wenn der Streamer einen einzelnen Stream zur Laufzeit neu konfigurieren kann (z. B.
Auflösung oder Bitrate des Substreams ändern, sodass der Encoder-Kanal neu angelegt wird).

1. Main per RTSP abrufen.
2. Den Substream 10-mal im Abstand von 30 s neu konfigurieren.
3. Dabei `top -b -d 5 -n 60 | grep -E '<streamer>'` mitschreiben.

- **Erfolg:** Main läuft ohne Aussetzer weiter, kein Thread bei 100 % CPU, der Substream
  kommt jedes Mal wieder.
- Kein `keeps returning EINTR` im Log.

### S4. A/B mit `OPENIMP_T31_AVC_LEGACY=1` (optional)

S1 für 1 Stunde mit `OPENIMP_T31_AVC_LEGACY=1` wiederholen. Treten dort Hänger oder
schwarze Streams auf, die ohne den Schalter nicht auftreten, belegt das F1/F2.

## Bekannte offene Punkte

- Zwei direkt aufeinanderfolgende IDRs tragen beide `idr_pic_id = 0`. Das verletzt die
  H.264-Norm, die meisten Decoder stört es nicht. Durch F1/F5/F8 kommt das häufiger vor. Wenn
  `ffmpeg -v error` genau bei solchen Stellen Fehler meldet, bitte notieren.

## Rückmeldung

- Pro Test: Status, Dauer, die Zählerwerte, `ffmpeg`-Fehlerzahl, Auffälligkeiten.
- Bei einem Hänger: die 50 Logzeilen davor, `dmesg | tail -50`, `top`-Ausschnitt.
- Den Bericht in `T31_TEST_RESULTS.md` (Abschnitt „S“) aufnehmen, siehe Testplan.

| Beobachtung | Bedeutung |
|---|---|
| Viele `without size writeback` | Stale IRQs (harmlos) oder Writeback kommt spät. Zeitlich mit `completion found without IRQ` vergleichen. |
| `completion timeout` gefolgt von weiteren Timeouts | Recovery greift nicht, der Core bleibt hängen. Logzeilen davor mitschicken. |
| `keeps returning EINTR` | Waiter auf einer entblockten fd, F3 greift nicht vollständig |
| Stream friert ein, aber kein Timeout im Log | Hänger außerhalb des AVC-Pfads (FrameSource/ISP): `dmesg` und `logread` mitschicken |
