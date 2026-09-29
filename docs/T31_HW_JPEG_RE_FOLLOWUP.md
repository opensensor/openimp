# T31 Hardware-JPEG: Nachtrag zum RE-Bericht und Review der Implementierung

Stand: 2026-09-29. Bezieht sich auf `docs/T31_HW_JPEG_RE.md` und den Branch
`claude/t31-hw-jpeg` (HEAD `01e7eff`) im Fork `Lu-Fi/openimp`. Nur Recherche: Es wurden
keine Repository-Dateien geändert.

Quellen:

- HLIL `libimp.so_hlil.txt` (`L<zeile>` / `0x<adresse>`)
- Stock-Trace `stock_logs.txt` (`T<zeile>`)
- Kernel `avpu/t31/*.c`
- Code (`datei:zeile`)

Kennzeichnung wie im Bericht: **[V]** belegt, **[I]** abgeleitet, **[?]** offen.

Anmerkung zum Trace: Laut `logread` im selben Log lief **rvd/Raptor** (IMP-1.1.6, T31-X,
JPEG q75 1 fps), nicht prudynt. Der Trace besteht aus aneinandergehängten
`dmesg`-Ausgaben (T755, T1764, T2792). An diesen Stellen fehlen Zeilen. Dadurch hat z. B.
der Job bei T752 scheinbar keine Completion. Das ist ein Artefakt der Aufnahme, kein
verlorener IRQ.

---

## 0. Kurzfazit

**Neu belegt:**

- **Feldnamen in Wort 0 und 2** [V]. `AL_Dump_TEncChanParam` druckt die Felder mit Namen
  (L46102–46111, `0x41180–0x411e0`):
  - `+0x5d` = `LossLess` → Bit 9
  - `+0x5e` = `RestartInterval` → [31:16]
  - `+0x60` = `AspectRatioUnit` → [13:12]
  - `+0x62/+0x64` = `X_Density`/`Y_Density` → Wort 2
  - `iInitialQP` = `tRCParam+0x18` = ChParam+0x80 (L46027) → JPEG-Quality
- **`0x8438` Bit 1 = Stream-Overflow** [V]. Fehlercode `0x88` heißt laut
  `AL_Encoder_ErrorToString` (L46221, Case `0x41888`) „Stream Error: Stream overflow“. Das
  Statusfeld `Status+0x01` ist dasselbe Feld, das beim AVC-Pfad das
  „Puffer-voll“-Flag trägt (`0x6d9a4`).
- **EP1 ist byte-identisch zum Hersteller** [V]. Ich habe die Herstellertabellen aus der
  HLIL extrahiert (`ZIGZAG_SCAN`, Quant-, `HUFFMAN_TABLE_*` bei `0xe2fc0–0xe35f0`) und
  `JpegTables_InitQuant`/`InitHuffman` nachsimuliert. `HW_Encoder_BuildJpegEp1()` erzeugt
  für **q = 1…100 exakt dieselben 0x790 Bytes**. Die Codebücher des Herstellers sind
  **Konstanten aus `.rodata`**, die per `memcpy` kopiert werden. Das „Next-Symbol“-Feld ist
  also fest vorgegeben und wird nicht berechnet.
- **Header-Frage** [I, sehr stark]:
  - In ganz `libimp` gibt es keine JPEG-Marker-Konstante (`FFD8/FFDB/FFC4/FFE0/FFDA/FFD9`)
    und keinen String „JFIF“.
  - Die Hardware bekommt Density, Unit und Restart-Intervall. Die braucht man nur für
    APP0/DRI.
  - EP1 enthält DQT-Bytes und BITS/HUFFVAL, die nur für DQT/DHT gebraucht werden.
- **JPEG-Clock bleibt dauerhaft an** [V]. `Process()` gatet nur Cores `< numCore` ab,
  also nie den JPEG-Core (L66300–66357). Das erklärt, warum `0x85F4` im Trace nur einmal
  geschrieben wird (T146).
- **JPEG und AVC teilen sich beim Hersteller die Stream-Puffer und den Quellframe** [V].
  `0x841C` ∈ {`0x06f75300`, `0x06e8d700`} (1080p) bzw. {`0x0715d000`, `0x07135600`}
  (360p) sind exakt die AVC-`CL[048]`-Puffer (T131). Die Größe `0xe7680` ist deshalb die
  Größe des AVC-Stream-Puffers und keine JPEG-Formel.
- **Keine zusätzlichen Registerzugriffe** [V]. Alle 34 JPEG-Jobs im Trace haben dieselbe
  Sequenz. Die Stock-libimp liest `0x85F8` nicht, schaltet die Clock nicht ab, schreibt
  keinen Ack und löscht die Maske nie.

**Die wichtigsten Befunde in der Implementierung** (Details in Teil B):

1. **B1:** Ohne AVC-IRQ-Thread gibt es keine Completion.
   - Beim Start kann der JPEG-Thread die Encode-Lock vor dem ersten AVC-`Process()`
     bekommen. Ebenso bei reinem JPEG oder nach dem Teardown des Host-AVC-Kanals.
   - Folge: Timeout, und HW-JPEG ist für den Rest des Prozesses abgeschaltet.
2. **B2:** `pthread_cond_timedwait` läuft mit `CLOCK_REALTIME`. Der NTP-Sprung beim Boot
   (im Log: „time disparity of 3298 minutes“) erzeugt einen Schein-Timeout und schaltet
   HW-JPEG dauerhaft ab.
3. **B3:** Die Puffer werden global für die Auflösung des ersten JPEG-Frames bemessen.
   Kommt bei rvd zuerst 360p, läuft 1080p danach dauerhaft in Software, und es gibt bei
   jedem Frame einen Log-Eintrag.
4. **B4:** Die Qualität ist fest auf 75. `iInitialQP` wird ignoriert, und die P2-Schicht
   verliert die Quality > 51 ohnehin.
5. **B5–B9:** Kleinere Punkte:
   - EP1-Rest wird nicht genullt
   - Chroma-Padding-Zeilen fehlen im Quellpuffer
   - Cache-Richtung `dir=2` statt des bewährten `dir=0`
   - EOI-Check ist zu strikt
   - Teardown kann einen laufenden JPEG-Job abschießen

---

## Teil A – Offene Punkte

### A1. Kommandowort 0 (`0x8400`), `0x85F0`, `0x85E4`, Statusregister

**Wort 0 – vollständige Bitkarte** (`JpegParamToCtrlRegs` L73828, `0x6f5f4–0x6f6a0`).
Die Felder des lokalen Parameterblocks werden in `SetJpegParam` gefüllt (`0x6b3ec–0x6b458`):

| Bits | Quelle im Param-Block | Herkunft | Bedeutung | Status |
|---|---|---|---|---|
| [1:0] | `u32 @+8` | `ePicFormat>>8 & 0xF` | ChromaMode 0 = 4:0:0, 1 = 4:2:0, 2 = 4:2:2 (Assert `0x6b5c8`) | [V] |
| [5:4] | `u8 @+5` & 3 | `ChromaMode ? 3 : 1` (`0x6b450–0x6b458`) | Komponentenzahl (SOF Nf / SOS Ns) | [V] Wert, [I] Name |
| [8] | `u8 @+0` | **Konstante 1** (`0x6b438`, `var_38.b = 1`) | unbekannt | [V] immer 1, [?] Bedeutung |
| [9] | `u8 @+1` | ChParam+0x5d = **`LossLess`** (L46102) | Lossless-JPEG | [V] Name |
| [13:12] | `u8 @+4` | ChParam+0x60 = **`AspectRatioUnit`** (L46104) | JFIF „units“ | [V] Name, [I] JFIF-Feld |
| [31:16] | `u16 @+2` | ChParam+0x5e = **`RestartInterval`** (L46103) | DRI / RSTn | [V] Name |

Weitere Punkte zu Wort 0 bis 3:

- **Bit 8** hat außer der Konstante keine Quelle. Auch `CtrlRegsToJpegParam` (L73857)
  dekodiert es nur für Traces zurück.
  - Die HLIL zeigt keine Semantik.
  - Kandidaten [?]: „Header erzeugen“, „Start of frame“/„erstes Segment“,
    „Standard-Tabellen“.
  - Prüfung am Gerät: den Probe mit Wort 0 = `0x031` laufen lassen (Vorschlag: Option
    `-c <word0>` in `tools/t31_hwjpeg_probe.c`) und vergleichen, ob danach SOI/DQT/DHT
    fehlen.
- Die übrigen Bits von Wort 0 (2–3, 6–7, 10–11, 14–15) sind **immer 0** [V]. Die Maske
  `0xCFFF` in `0x6f6a0` räumt auch [15:12] ab.
- **Wort 2:** `X_Density<<16 | Y_Density` (L46105/46106). Die Werte kommen aus
  `setDensity` (L51629) mit dem Aspect-Enum `Settings+0xFC`:
  - 4 (Default) → 1:1
  - 2 → 4:3
  - 0/3 → 16:9

  [V]
- **Wort 3:** Pitch als **u16** in [15:0], [31:16] bleibt erhalten und ist nach dem
  Memset 0 (`0x6f6ac`). Pitch > 65535 ist also nicht darstellbar. [V]

**`0x85F0 = 1` vor jedem Job** (`AL_EncCore_EncodeJpeg` `0x6ce30`, Trace bei jedem der 34
Jobs direkt vor der Zone):

- Es ist dasselbe Register wie `0x83F0` (`ResetCore` `0x6c398`, AVC: 1, 2, 4
  nacheinander) im Fenster von Core 1.
- Für JPEG wird nur Bit 0 gepulst. Eine 0 wird nie geschrieben, und es wird weder gewartet
  noch zurückgelesen.
- ⇒ Selbstlöschender Soft-Reset/Clear der (einzigen) JPEG-Pipeline vor der
  Parametrierung. [V Ablauf; I Semantik, per Analogie zu `0x83F0` stark]

**`0x85E4 = 1`** (`0x6ce80`):

- Analog zu `0x83E4`: AVC schreibt dort 2 (Enc1) bzw. 8 (Enc2) als CL-Push
  (`0x6cd18/0x6cdc0`).
- Beim JPEG-Core ist das Bit 0 und damit ein „Start aus Registern“, ohne Command-List.
  `0x85E0` (CL_ADDR) wird nie geschrieben. [V Wert, I Semantik]

**`0x85F8`:**

- Wird vom Hersteller für JPEG **nie gelesen** [V]. `IsEnc1/2AlreadyRunning` (`0x6c678`,
  `0x6c6e8`) prüfen nur `(core<<9)+0x83F8` aus dem AVC-Pfad `AL_EncCore_Encode1/2`.
- Im Trace hat jede JPEG-Completion genau drei RD-ioctls, und das entspricht den drei
  Status-Reads (T193–T198). Vor dem Start gibt es nur die RMW-Reads für `0x8014`/`0x85F4`.
- Bedeutung bei JPEG [?]. Vermutlich Bit 1 = busy, wie bei AVC. Nur zur Diagnose lesen.

**Status `0x8430`** (→ `Status+0x70`, `0x6d388`):

- Wird sonst nur von `JpegStatusToStatusRegs` (L73244) für den Trace-Dump zurückgeschrieben.
- `+0x70` liegt hinter allen Feldern der AVC-SliceStatus (`EncodingStatusRegsToSliceStatus`
  L73068 füllt höchstens `+0x6c`). Es ist also ein JPEG-eigenes Feld ohne Konsument.
- Bedeutung [?]. Hypothesen: Taktzyklen, Bitzähler, MCU-Zähler.
- Prüfung am Gerät: den Wert für 1080p/360p und q50/q90 notieren und mit Laufzeit bzw.
  Länge korrelieren.

**Status `0x8434`:** Länge in Bytes. Sie wird **unmaskiert** übernommen (AVC maskiert
`& 0x3fffffff`, `0x6d978`). [V]

**Status `0x8438`:**

- Nur Bit 1 wird ausgewertet: `(v>>1)&1` → `Status+0x01` (`0x6d3e4`) → Fehler `0x88` =
  **„Stream Error: Stream overflow“** (L46221 ff.) [V].
- Die Completion samt IRQ kommt auch bei Overflow. Der Fehlerpfad existiert nur in
  `EndJpegEncoding`, also nach dem IRQ. [V Pfad, I Hardwareverhalten]
- Alle anderen Bits [?].

### A2. EP1-Details

**Quant-Bytes:** Byteweise Stores `sb` (`*($a3_1-1) = $s3_1 & 0xff`, `0x4b538`/`0x4b584`),
Zigzag-Index k an EP1+k bzw. EP1+0x40+k [V]. Es gibt kein Packen in Worte. Die
Byte-Reihenfolge ist damit Adressreihenfolge. Die BITS-Arrays dagegen sind als u32 mit
MSB-first-Inhalt abgelegt (`0x4b690–0x4b70c`). Der Unterschied ist echt und in der
Implementierung korrekt nachgebildet. [V]

**Weitere Belege zu Quant und Reziproken:**

- Die Clamps bestätigen sich [V]:
  - `v == 0` → 1 (`0x4b584`)
  - `v ≥ 256` → `0xFF` (`0x4b578`)
  - `S &= 0xFFFF`
  - `Q == 0` → Trap (`0x4b4c8`)
- Reziproke: `(int16)((0x10000 + (q>>1)) / q) − 1`, als u16 an `2·ZIGZAG_SCAN[k]`
  abgelegt, also in Rasterreihenfolge (`0x4b5e0–0x4b608`) [V].

**`+0x180 = 1`, `+0x184 = 0`:**

- Beides sind Konstanten, unabhängig von Tabelleninhalt, Quality und Farbmodus [V]:
  - Farbe: `0x4b6f0/0x4b6f4`
  - Mono: `+0xC0/+0xC4` (`0x4b808/0x4b868`)
  - Lossless: `+0x00/+0x04` (`0x4b7c4/0x4b7e4`)
- Hypothese [I, schwach]: Liest man das Wort LE byteweise, ergibt sich
  `[01,00,00,00]` = Kopfsymbole der HUFFVAL-Listen in BITS-Reihenfolge (AC-L = `0x01`,
  DC-L = `0x00`, AC-C = `0x00`, DC-C = `0x00`). Das passt exakt zu den Standardtabellen.
  `+0x184` wäre dann Reserve.
- Gegenargument: Im (ungenutzten) Lossless-Layout steht ebenfalls 1 an erster Stelle,
  obwohl dort DC-L zuerst kommt und dessen Kopf 0 ist.
- Alternative [?]: Die Hardware findet den Kopf selbst, nämlich als Symbol mit Code 0 und
  minimaler Länge (kanonisch).
- **Für die Implementierung egal:** Der Hersteller unterstützt ausschließlich die fest
  kodierten Annex-K-Tabellen. Es gibt keine `SetJpegeQl`/Custom-Tabellen in dieser libimp
  (grep leer). Konstanten 1:1 übernehmen reicht, und genau das tut der Code.

**Codebuch-Kette (Stichproben aus `.rodata`)** [V]:

| Tabelle | Eintrag | Wert | Bedeutung |
|---|---|---|---|
| AC-Luma | [0] | `0x01000002` | Symbol `0x01`: Code `00`, next `0x02` |
| AC-Luma | [159] | `0x0FFFFEFF` | Symbol `0xFA`: Code `0xFFFE`, Länge 16, next `0xFF` = Ende |
| AC-Luma | [160] | `0x0A07F924` | ZRL |
| AC-Luma | [161] | `0x03000A04` | EOB |
| DC-Chroma | [11] | `0x0A07FEFF` | Ende der Liste |

**Lossless-Modus** (Bit 9, `0x4b7bc ff.`):

- DC-Codebücher mit **18** Einträgen: 12 kopierte, 6 genullte (`0x4b760–0x4b764`). Das
  passt zu den 17 SSSS-Kategorien von Lossless-JPEG.
- Ohne Quant-Tabellen.
- ⇒ `LossLess` ist echtes Lossless-JPEG (Prozess 14) [I stark; der Name ist jetzt V].

**Alignment und Größe:**

- `AL_IntermMngr_GetEp1Addr` (L61939, `0x5c9f0`): `addr & 0x1f` ≠ 0 → Assert
  (`GetSubBufLocation.part.0`). Die 32-Byte-Ausrichtung ist hart [V].
- Beobachtet sind 256-Byte-ausgerichtete Adressen (`0x0718dd00`, `0x0719f600`) [V].
- Allokiert werden `0x6400` Bytes, einmal komplett genullt (`AL_CleanupMemory`, `0x4b224`),
  danach die Tabellen bis `0x790` (das Nullen des Rests `0x738–0x790` ist explizit,
  `0x4b7a0`).
- Wie viele Bytes die Hardware tatsächlich liest, ist offen [?]. Ein Nutzer von
  `0x790…0x6400` ist nicht sichtbar. `TraceJpeg` dumpt als `.tab.hex` die ganze
  Descriptor-Größe (`0x4f948`), das sagt aber nichts über die Hardware.

**Lebenszyklus** [V]:

- `PushIntermBuffer` kopiert das Master-Image per `memcpy` in den Intermediate-Puffer
  (`0x6a4c0`).
- Pro Kanal gibt es genau **einen** EP1-Ort: `0x8418` ist pro Kanal im gesamten Trace
  konstant (Tabelle in A5).
- Eine eigene Cache-Pflege für EP1 gibt es nicht. Sie ist im 1-MiB-Flush jedes Jobs
  enthalten (A4).

### A3. JFIF-Header, Restart-Marker, EOI

Belege [V] für „keine Software erzeugt Header“:

1. **Kein Marker-Schreiber:** In der gesamten HLIL gibt es kein `0xffd8`, `0xd8ff`,
   `0xffdb`, `0xffc4`, `0xffe0`, `0xffda` oder `0xffd9` in Codekontext und keine
   Byte-Stores von `0xd8/0xdb/0xc4/0xe0`. Die einzigen Treffer sind `st_size` von
   ELF-Symbolen. Einen String „JFIF“ gibt es nicht.
2. **`generateNals` ist leer** (L51582, `0x4b1d8`, `__pure`).
3. **`updateHlsAndWriteSections`** (L51609, `0x4b298`) kopiert nur die vorhandene
   Section `{offset, len, ...}` aus dem Part-Table im Stream-Puffer und hängt einen
   End-Section-Eintrag mit Flag `0x20000000` an.
4. **`EndJpegEncoding`** (L70417, `0x68ca0–0x68cbc`) trägt `{StreamBuf+0x14 (=0x200),
   0x8434, −1, −1}` ein. Die Section beginnt also exakt am Hardware-Startoffset und ist
   exakt `0x8434` lang.

Indizien [I, sehr stark], dass die **Hardware** SOI/APP0/DQT/SOF/DHT/(DRI)/SOS…EOI
schreibt:

- Die Hardware erhält `X/Y_Density`, `AspectRatioUnit` und `RestartInterval` (Namen [V]).
  Die ersten drei wirken ausschließlich auf den APP0-Header. `RestartInterval` wirkt auf
  DRI und die RSTn-Marker.
- EP1 enthält Zigzag-Quantbytes. Diese braucht nur DQT; der Quantisierer nutzt die
  Rasterreziproken.
- EP1 enthält BITS und eine HUFFVAL-Kette. Diese braucht nur DHT; der Entropiecoder
  braucht nur Code und Länge.
- rvd bzw. die IMP-API liefern verwendbare JPEG-Dateien, obwohl die libimp keinen Header
  schreibt.

Restart-Marker: `RestartInterval` ist per Default 0 (Memset in `AL_Settings_SetDefaults`,
keine JPEG-Default-Zuweisung in `AL_sSettings_SetDefaultJPEGParam` L43966), im Trace sind
[31:16] = 0. ⇒ Kein DRI/RSTn im Stock-Betrieb [V].

EOI und Padding: [?]. Ob `0x8434` nach `FFD9` noch Füllbytes enthält (z. B. eine
Ausrichtung auf 4/8/32 Byte), ist nicht belegt. Beim Hersteller wäre das unschädlich, weil
Decoder Bytes nach EOI ignorieren.

Stream-Inhalt im Trace: **keiner** [V]. Der Logger protokolliert nur WR-Werte. RD-Werte,
RD-Adressen und Stream-Puffer werden nicht geloggt; es gibt nur AVC-CL-Dumps.
`TraceJpeg` würde `.bit.hex` mit `min(Puffergröße, Statuslänge)` schreiben (`0x4f984–0x4f9f4`),
ist in IMP aber nie aktiv.

### A4. Puffer- und Cache-Anforderungen

**Stream-Puffer** [V Trace]:

- Basis 256-aligned.
- Wort 8 = Größe (`0xe7680`/`0x27780`, Vielfache von `0x80`)
- Wort 9 = `0x200`
- Wort 10 = Größe − `0x200`
- **Neu:** Der JPEG-Kanal benutzt denselben Zweier-Pool wie der AVC-Kanal gleicher
  Auflösung. `CL[048]` = `06e8d700`/`06f75300` (1080p, 16×) und `07135600`/`0715d000`
  (360p, 18×) tauchen als `0x841C` der JPEG-Jobs auf (T131 vs. T398/T740).
- Die Puffergröße ist daher die AVC-Max-NAL-Größe der Auflösung und keine JPEG-spezifische
  Formel. Eine Mindestgröße ist nicht belegt [?].
- Wort 8 vs. 10 [I]:
  - Allegro-Konvention wie AVC-`CL[049..051]`: Puffergröße (Ende bzw. Wrap-Grenze),
    Schreiboffset, erlaubtes Budget.
  - Bei Überschreitung des Budgets meldet `0x8438` Bit 1 den Overflow.
  - Die Rundung auf 32 Byte bei der AVC-Overflow-Berechnung (`0x6d9a4`) legt nahe, dass
    Größe und Budget mindestens 32-Byte-granular sein sollten [I].

**Quellpuffer:**

- NV12 semi-planar [V]: ein srcC-Zeiger (Wort 5) und ChromaMode 1.
- 4:2:2 und 4:0:0 sind laut Feldern möglich [V], werden aber von IMP nicht benutzt.
- Pitch u16 [V].
- UV = Y + Pitch·align16(H) beobachtet (1920·1088, 640·368) [V].
- Adressen 256-aligned [V beobachtet]; geprüft wird in Software nichts [V].
- Breite:
  - `IMP_FrameSource_SetChnAttr` erzwingt `picWidth` als Vielfaches von 16 (L96211,
    `0x9ca00`) [V].
  - Allegro prüft „Width shall be multiple of 2 on 420“.
  - Die Höhe darf ungerade zu 16 sein (1080) [V].
  - Welche Zeilen die Hardware für die letzte MCU-Zeile liest, ist offen [?]. Bei 4:2:0
    und H = 1080 wahrscheinlich Luma bis Zeile 1087 und Chroma bis 543. Die
    Framesource-Puffer haben diese Zeilen (1080p = `0x2fd000` = 1920·1088·1.5,
    `src/kernel_interface.c:465`).
- Der Hersteller liest den Framesource-Frame **direkt**, denselben, den AVC gerade
  benutzt: JPEG-`0x8410` = AVC-`CL[032]` = `0x071a5f00` [V].

**Cache-Pflege beim Hersteller** [V]:

- In `libimp` gibt es genau zwei Aufrufer von `Rtos_FlushCacheMemory(ptr, 0x100000)`:
  - `AL_EncCore_Encode1` (`0x6cc84`)
  - `AL_EncCore_EncodeJpeg` (`0x6cdf8`)

  Der Aufruf geht über `IMP_FlushCache` → `/dev/rmem` ioctl `0xc00c7200` mit `dir = 1`
  (L46615, L23910).
- `Rtos_InvalidateCacheMemory` (`dir = 2`, L46610) hat **keinen** Aufrufer.
- Der Hersteller macht also weder ein Invalidate des Quellframes noch des Stream-Puffers
  nach der Completion. Er verlässt sich auf den 1-MiB-Flush vor jedem Job. Unter MIPS
  (r4k-Cache-Code: `_dma_cache_wback` = `r4k_dma_cache_wback_inv`; Größe ≥
  D-Cache/L2 → `blast_dcache`/`blast_scache`) ist das ein globales Writeback+Invalidate
  des ganzen Caches [I, Kernelquellen von rmem nicht im Repo].
- Danach berührt die CPU den Stream-Puffer bis zur Completion nicht, deshalb ist das Lesen
  nach dem IRQ kohärent. Im Trace liegt zwischen RMW `0x8014` und `WR 0x85f0` jeweils eine
  Lücke von ≈120 µs (z. B. T151 → T154, 128.883279 → 128.883402). Das ist der
  ioctl-Aufruf, der nicht im avpu-Log erscheint [I].

### A5. Stock-Sequenz Zeile für Zeile, abgeglichen mit der Rekonstruktion

Einmalig (T69–T88, 128.741):

- `0x8010 = 0x1000`
- `0x83F0` = 1, 2, 4
- `0x8018 = 0xFFFFFF`
- `0x8054 = 0x80`
- WAIT_IRQ-Thread startet (T86)

Kanalanlage JPEG (T90, T95):

- `0x8014` = `0x10` je JPEG-Kanal, **vor jedem Frame**.
- Das ist `AL_EncCore_SetJpegInterrupt`, aufgerufen aus der Kanalerzeugung bei Codec 4
  (L71109, `0x69f44`). **Neu [V]**: Bit 4 wird schon bei der Kanalanlage gesetzt, nicht
  erst pro Job.

Pro JPEG-Job (34 Starts, 33 Completions sichtbar; die fehlende liegt in einer dmesg-Lücke):

1. Nur beim ersten Job: RD, `WR 0x85F4 = 1` (T146). Der Grund steht in A1: GC-Off gilt
   nur für Cores `< numCore`.
2. RD, `WR 0x8014 = 0x11` bzw. `0x10`, je nachdem, ob AVC gerade läuft. 32 von 32 Fällen
   sind vollständig mitgeloggt.
3. ≈120 µs Cache-Flush (nicht geloggt).
4. `WR 0x85F0 = 1`, `0x8400…0x8428` in aufsteigender Reihenfolge, `WR 0x85E4 = 1`.
5. `WAIT_IRQ` ret, genau drei RD (`0x8430/34/38`). Danach kein WR, kein Ack, keine
   Maskenänderung.

Laufzeiten (Start → WAIT_IRQ-Rückkehr):

| Auflösung | Messungen | Spanne |
|---|---|---|
| 1080p | n = 15 | 7,87–8,30 ms |
| 360p | n = 17 | 0,92–1,27 ms |

AVC-Completion:

- RD, `0x8014 = 0x10` (`DisableEnc1Interrupt` löscht nur Bit 0), RD,
  `0x83F4 = 0x00010000` (T252–T261).
- Bit 4 bleibt dauerhaft gesetzt.
- `0x83F4` liest `0x10001` zurück, Bit 16 ist also ein Hardware-Statusbit [I].

Es gibt keine anderen Register: Die Menge der geschriebenen Register im gesamten Trace ist
{`0x8010`, `0x8014`, `0x8018`, `0x8054`, `0x83E0`, `0x83E4`, `0x83F0`, `0x83F4`,
`0x8400`–`0x8428`, `0x85E4`, `0x85F0`, `0x85F4`} [V].

JPEG-Jobs laufen nie überlappend (Core 1 seriell), aber parallel zu AVC [V].

Die Rekonstruktion (`codec-t40.c:7703–7712`) hat dieselbe Reihenfolge:

- GC-RMW
- Masken-RMW
- Reset
- Zone
- Start

Abweichungen: Sie schreibt `0x85F4` bei jedem Job neu, was harmlos ist. Sie löscht Bit 4
nach jedem Job wieder (`:7730`); der Hersteller löscht nie. Das ist beides zulässig,
erzeugt aber zwei ioctls mehr pro Job.

---

## Teil B – Review der Implementierung

### B0. Geprüft und korrekt

- **EP1 (`src/hw_encoder.c:912–1004`)** [V]: byte-identisch zum Hersteller für
  q = 1…100 (4:2:0).
  - Methode: Tabellen aus L131064–131125 extrahiert, `InitQuant`/`InitHuffman` in Python
    nachgebaut, gegen die kompilierte Funktion verglichen, 0 Abweichungen.
  - Auch die berechneten Codebücher, einschließlich der Next-Kette, stimmen mit den
    `.rodata`-Konstanten überein.
  - Der Probe-Generator ist laut Commit identisch. Er nutzt die gleiche
    Konstruktionsmethode (`k_natural` = `ZIGZAG_SCAN`).
- **Kommandowerte (`codec-t40.c:7685–7695`):**
  - Wort 0 `0x131`, Wort 1 `(W−1)<<16 | (H−1)`, Wort 2 `0x00010001`, Wort 3 Pitch,
    Worte 8/9/10 = Größe/`0x200`/Größe−`0x200`. Alles wie im Stock.
  - Registerreihenfolge wie im Stock (A5).
- **Clock-RMW** `(clk & ~3) | 1` entspricht exakt `SetClockCommand` (`0x6c4b8`).
- **IRQ-Demux (`:5121`):** Slot 4 wird vor der AVC-Owner-Logik abgefangen.
  - `in_flight` wird vor dem Start gesetzt (`:7698`), deshalb kein Lost-Wakeup.
  - Die Maske bleibt bei AVC-Completion erhalten (`:4814` schreibt `0x10`, solange ein Job
    läuft).
  - Bei serialisiertem Betrieb ist `0x8014 = 0` nach AVC unkritisch, weil kein JPEG-Job
    offen ist.
- **Statusauswertung** Bit 1 → Fallback pro Frame (`:7740`). Das entspricht dem
  Hersteller-Overflow.
- **Companion-Stage** ist bei `OPENIMP_T31_HW_JPEG=1` aus (`:7797`). Das ist zwingend,
  weil sie sonst jeden JPEG-Job per `0x85F0`/`0x85E4` abwürgen würde.
- **Nebenläufigkeit:** Alle Encodes laufen unter `g_t31_encode_core_lock` (`:9773`), der
  globale Einzelkontext `g_t31_hwjpeg` ist damit geschützt.
- **Quelllayout:** Pitch = Breite, UV nach align16(H) Zeilen.
  - Der P2-JPEG-Frame ist eine Heap-Kopie mit `source->size` Bytes
    (`openimp_p2_encoder.c:261–283`).
  - Das Layout kommt aus V4L2: `sizeimage` 1080p = `0x2fd000` = 1920·1088·1.5
    (`kernel_interface.c:465`), passend zum Stock-UV-Offset.
  - Stimmt [V für Stock, I für OpenIMP-FS], solange `bytesperline == width`.
  - Die Prüfung `width % 16 == 0` (`:7658`) entspricht der Framesource-Regel
    (`0x9ca00`).
- **NV21 wird abgelehnt** (`:7659`). Richtig, die Hardware kennt nur CbCr-Reihenfolge
  (NV12) [I].

### B1. Completion hängt am AVC-IRQ-Thread – Start-Race, JPEG-only, Teardown (hoch)

`t31_hwjpeg_setup` (`codec-t40.c:7612`) öffnet nur das gepoolte fd. Ein `WAIT_IRQ`-Thread
existiert auf T31 aber nur, wenn ein **AVC**-Kontext ihn gestartet hat (`:8325–8350`,
`g_tseries_irq_host`), und er wird mit dem Host-Kontext beendet (`:7268–7280`).

- **Start-Race:**
  - Der AVC-Thread kopiert den JPEG-Frame unter `p2_core_lock` und ruft danach
    `AL_Codec_Encode_Process` auf (`openimp_p2_encoder.c:1233–1239`).
  - Der aufgeweckte JPEG-Thread kann `g_t31_encode_core_lock` vor dem allerersten
    AVC-`Process()` bekommen. Das Gerät wird lazy dort geöffnet (`:8253`).
  - Folge: Der IRQ landet in der Kernelliste ohne Waiter, nach 200 ms kommt ein Timeout,
    und `t31_hwjpeg_disable` schaltet HW-JPEG **für die ganze Prozesslaufzeit** ab.
- **JPEG-only:** Es gibt nie einen Waiter und damit nie HW-JPEG. Außerdem fehlt die
  globale Init: `0x8010 = 0x1000` und `0x8054 = 0x80` schreibt nur die AVC-Session-Init
  (`:8995–9000`). Der Hersteller macht sie immer (T69–T88).
- **Teardown/Neuanlage** des Host-AVC-Kanals: Der Waiter wird gejoint, und in dieser Lücke
  (oder dauerhaft, falls nur ein anderer AVC-Kanal übrig ist) laufen JPEG-Jobs in den
  Timeout.

Fix:

- Einen prozessweiten, vom Device-Pool gehaltenen Waiter einführen, der gestartet wird,
  sobald *irgendwer* (AVC oder HW-JPEG) `/dev/avpu` öffnet, und erst beim letzten Nutzer
  gejoint wird. Das T41-Modell `avpu_t41_irq_host_get/put` (`:5191–5256`) lässt sich
  übernehmen.
- Mindestens: In `t31_hwjpeg_encode` nur starten, wenn `g_tseries_irq_host` gesetzt ist,
  sonst **diesen Frame** in Software (nicht `disable`).
- In `t31_hwjpeg_setup` die globale Init nachholen, falls noch keine AVC-Session sie
  gemacht hat.

### B2. Timeout auf `CLOCK_REALTIME` (hoch, einfach zu beheben)

`codec-t40.c:7714` benutzt `clock_gettime(CLOCK_REALTIME)` mit einem statisch
initialisierten Condvar (Default-Clock REALTIME, `:7555–7558`).

- Ein Vorwärtssprung der Uhr während der ≈8 ms Wartezeit lässt `pthread_cond_timedwait`
  sofort `ETIMEDOUT` liefern. Der NTP-Step beim Boot passiert genau dann, wenn Streams
  starten; im selben Log steht „time disparity of 3298 minutes“.
- Die Folge ist `disable("completion timeout")` für immer.
- Zusätzlich schreibt der Timeoutpfad `0x85F0 = 1` in einen noch laufenden Job (`:7732`).

Fix: Die Condvar mit `pthread_condattr_setclock(CLOCK_MONOTONIC)` initialisieren, zum
Beispiel per `pthread_once`. Außerdem: Nach einem ersten Timeout nicht sofort dauerhaft
deaktivieren, sondern z. B. erst nach drei aufeinanderfolgenden. Vor dem Reset `0x85F8`
diagnostisch lesen und loggen.

### B3. Puffer werden nur einmal bemessen (mittel)

`codec-t40.c:7662–7668`: `setup` allokiert `src`/`stream` nach der **ersten** Auflösung.
Beide JPEG-Kanäle teilen den globalen Kontext. Bei rvd (1080p + 360p) hängt es von der
Thread-Reihenfolge ab. Kommt 360p zuerst, läuft 1080p dauerhaft in Software, und
`IMP_LOG_INFO` (`:7666`) loggt **jeden Frame**.

Fix: Bei Bedarf die größere Allokation nachziehen (alte freigeben mit
`avpu_release_dma_buf`) oder pro Auflösung bzw. Kanal eigene Puffer anlegen. Das Log auf
einmalig drosseln.

### B4. Qualität fest 75 (mittel, funktional)

`codec-t40.c:9719` übergibt fest `75u`. Beim Hersteller ist die Quality = `iInitialQP`
(ChParam+0x80 = `tRCParam+0x18`; L46027 `printf("iInitialQP=%d")`, genutzt in `0x4b250`).

Die P2-Schicht verliert Werte > 51 bereits beim Parametersatz
(`openimp_p2_encoder.c:751–752`: `qp > 51 → 26`). `IMP_Encoder_SetDefaultParam` begrenzt
auf 1…99 (`:1570–1572`).

Fix: Für `IMP_ENC_TYPE_JPEG` die Quality 1…100 unbegrenzt in einem eigenen Feld
mitführen, etwa `params+0x84` ohne 51er-Clamp nur für JPEG, und in `t31_hwjpeg_encode`
lesen. Dann wird EP1 bei einer Änderung automatisch neu gebaut (`:7672`).

Hinweis: Mit einem globalen EP1 und zwei Kanälen unterschiedlicher Quality würde das EP1
bei jedem Kanalwechsel neu gebaut und geflusht. Das kostet wenig, aber pro Kanal ein
eigenes EP1 wäre sauberer, wie beim Hersteller.

### B5. EP1-Rest nicht genullt (niedrig bis mittel)

- `avpu_alloc_encoder(fd, 0x1000, ...)` (`:7619`) liefert rmem ohne Nullung.
- `HW_Encoder_BuildJpegEp1` nullt nur `0x790` Bytes (`hw_encoder.c:986`), der Flush deckt
  nur `0x790` ab (`:7676`).
- Der Hersteller nullt den ganzen EP1 (`0x6400`, `AL_CleanupMemory` `0x4b224`). Welche
  Länge die Hardware liest, ist unbekannt (A2).

Fix: Nach der Allokation einmal `memset(ep1.map, 0, ep1.size)` und das ganze
`ep1.size` flushen.

### B6. Quellkopie ohne Chroma-Padding-Zeilen (niedrig)

`src_size = uv_offset + W·ceil(H/2)` (`:7647`):

- Allokation und Kopie enden nach 540 Chroma-Zeilen.
- Liest die Hardware für die letzte MCU-Zeile bis Chroma-Zeile 543 [?], liest sie bis zu
  4·W Bytes hinter dem Puffer. Das ist nur Lesen, also harmlos, aber die Kante bekommt
  Zufallsdaten und damit sichtbare Artefakte am unteren Rand.
- Der Framesource-Frame hat diese Zeilen (`0x2fd000`).

Fix: `src_size = W·align16(H)·3/2` allokieren und `min(frame->size, src_size)` kopieren.

### B7. Cache-Richtung und Kohärenz (mittel, am Gerät verifizieren)

- Nach der Completion invalidiert die Implementierung mit `dir = 2` (`:7752`), vor dem
  Start den ganzen Stream-Puffer mit `dir = 2` (`:7683`).
- Der bewährte T31-AVC-Pfad benutzt nach der Completion `dir = 0` (BIDIRECTIONAL,
  `:5645`).
- Der Hersteller benutzt `dir = 2` nie (A4).
- Ob der T31-rmem-ioctl `dir = 2` korrekt als `DMA_FROM_DEVICE` umsetzt, ist nicht belegt.

Fix: Wie beim AVC-Pfad `dir = 0` verwenden. Die Quelle weiter mit `dir = 1` behandeln
(auf MIPS ist das ohnehin wback+inv).

Hinweis: Der Code enthält widersprüchliche Kommentare, ob der gecachte
rmem-Flush auf T31 zuverlässig ist (`:468–472`, `:757–759`). Der Stream-Pfad von AVC nutzt
für die Kompaktierung deshalb ein unkached `/dev/mem`-Alias (`:8381–8391`).

- Falls die Probe mit kohärenten `GET_DMA_MMAP`-Puffern funktioniert, die Bibliothek aber
  Artefakte zeigt: für `ep1` und `stream` `avpu_alloc_mmap` oder
  `avpu_remap_uncached` nehmen.
- Der Stream ist klein, ein unkached Read ist dort also vertretbar.
- `src` sollte wegen der 3-MB-memcpy-Kosten gecacht bleiben.

### B8. EOI-Prüfung zu strikt (niedrig bis mittel)

`codec-t40.c:7754–7760` verlangt `FFD9` exakt an `length−2`. Falls die Hardware nach EOI
auf eine Wortgrenze auffüllt (A3, [?]), wird HW-JPEG beim ersten Frame dauerhaft
abgeschaltet, obwohl die Ausgabe gültig ist.

Fix: In den letzten ≤ 64 Bytes rückwärts nach `FF D9` suchen, auf diese Länge kürzen und
nur ohne Treffer abschalten. SOI weiterhin strikt prüfen.

### B9. Maskenmanipulation durch Dritte während eines Jobs (niedrig)

`AL_Codec_Encode_Destroy` läuft **nicht** unter `g_t31_encode_core_lock`:

- Ein AVC-Teardown schreibt `0x8014 = 0` und `0x8018 = 0xFFFFFF` (`:7232–7236`).
- Läuft gerade ein JPEG-Job, geht dessen IRQ verloren. Es folgt ein Timeout und damit die
  dauerhafte Abschaltung (B2).

Fix: Teardown-Maskierung und -Ack unter dieselbe Lock wie den Encode legen, oder im
Teardown `t31_hwjpeg_idle_irq_mask()` statt 0 schreiben und die IRQ-Pending-Bits nur für
Core 0 quittieren (`0x0F`).

Außerdem: Ein Stale-Pending-Bit 4 aus einem abgebrochenen Vorgängerprozess würde beim
Entmaskieren sofort als Completion zählen. Das ist nur relevant, wenn JPEG vor der
AVC-Init läuft, die `0x8018` löscht. Härtung: vor dem Entmaskieren `0x8018 = 0x10`
schreiben. Laut Kernel ist das Register W1C (`avpu_ip.c:121`).

### B10. Kleinere Punkte

- `t31_hwjpeg_set_irq_bit(fd, 0)` nach jedem Job (`:7730`) weicht vom Hersteller ab.
  Unschädlich. Bit 4 einmalig zu setzen wäre näher am Hersteller, würde aber die
  Maskenlogik in `:4814` ändern.
- Das RMW auf `0x8014` ist nicht atomar gegen den AVC-Callback (`:4814` schreibt absolut).
  Wegen der Serialisierung kann das nur nach einem AVC-„serialized completion timeout“
  (`:9790–9806`) eintreten. Die Folge wäre ein unnötiges Bit 0, harmlos.
- Timeout 200 ms bei ≈8 ms Hardwarezeit: angemessen.
- Kosten (Design, kein Bug):
  - Doppelte 3-MB-Kopie bei 1080p (P2-Fan-out + `:7679`) plus Flush, alles unter der
    globalen Encode-Lock. AVC wird dabei ≈8 ms Hardwarezeit plus Kopie blockiert.
  - Bei 1 fps irrelevant, bei MJPEG mit 10–15 fps spürbar.
  - Mittelfristig: Fan-out direkt in einen DMA-Puffer kopieren oder, wie der Hersteller,
    den FS-Frame per Phys-Adresse referenzieren, solange die Frame-Referenz gehalten wird.
- `stream_size = W·H/2 + 64 KiB` (`:7648`) ist nur 16-Byte-granular für beliebige
  zulässige Größen. Auf 256 (oder 4 KiB) aufrunden, wie im Stock (A4).
- DMA-Puffer und Pool-Referenz werden nie freigegeben. Solange der Prozess lebt, bleibt
  `/dev/avpu` gebunden, auch wenn alle AVC-Kanäle zerstört sind. Innerhalb eines Prozesses
  ist das unkritisch, weil das fd gepoolt ist; es sollte aber dokumentiert werden.
- `OPENIMP_T31_HW_JPEG=1` schaltet **zugleich** die Companion-Stage für AVC ab. Zuerst
  Phase 0 (Companion aus, HW-JPEG aus) validieren, sonst lassen sich AVC-Regressionen
  nicht zuordnen.
- Probe (`tools/t31_hwjpeg_probe.c`): Die Sequenz ist korrekt, und die kohärenten Puffer
  vermeiden Cache-Fragen.
  - Nützliche Ergänzung: `-c <word0>` (Test Bit 8 und Bit 13:12, Restart-Intervall in
    [31:16]).
  - Nützliche Ergänzung: `-s <stream-size>` (Overflow-Test).
  - `0x85F8` vor dem Start und nach der Completion ausgeben.

---

## Teil C – Verbleibende Unbekannte (nur am Gerät klärbar)

1. Bedeutung von Wort-0-Bit 8. Mit dem Probe testen: `0x031` statt `0x131`.
2. Bedeutung von `0x8430` und der Bits von `0x8438` außer Bit 1. `0x85F8` bei JPEG.
3. Ob die Hardware tatsächlich den vollständigen JFIF-Strom schreibt. Das ist sehr
   wahrscheinlich; der Probe zeigt es sofort. Dazu: Endet die Ausgabe exakt mit `FFD9`,
   oder gibt es Padding?
4. Wirkung von `RestartInterval` (DRI/RSTn) und `AspectRatioUnit` im Header.
5. Wie viele Bytes EP1 die Hardware liest, und ob `+0x180` Kopfsymbole sind. Test:
   `+0x180 = 0` setzen und schauen, ob sich die DHT-HUFFVAL-Reihenfolge ändert.
6. Zeilenzugriff für H mod 16 ≠ 0 (Chroma-Padding) und Mindestgrößen und -ausrichtungen
   von Stream und Quelle. Beobachtet sind 256 B; hart geprüft wird nur die EP1-Ausrichtung
   von 32 B.
7. Ob `rmem`-ioctl `dir = 2` auf T31 korrekt invalidiert (B7).

## Anhang: Verifikation EP1

Das Skript `re/vendor.py` im Scratchpad parst den Hexdump L131060–131135 (Adressen
`0xe2fc0–0xe35f0`) und simuliert `JpegTables_InitQuant` (L51669) und
`JpegTables_InitHuffman` (L51767, Farbpfad `0x4b67c–0x4b918`). Verglichen wurde mit
`re/ep1test.c`: Das sind die Zeilen `hw_encoder.c:662–732` und `:912–1004`, unverändert
kompiliert.

Ergebnis: `mismatching q: 0` für q = 1…100. `ZIGZAG_SCAN` ist eine Permutation
(Zigzag→Raster) und entspricht `k_natural` im Probe.
