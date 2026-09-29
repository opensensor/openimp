# T31 Hardware-JPEG im Allegro-AVPU: Reverse-Engineering-Bericht für OpenIMP

Stand: 2026-09-29. Grundlage ist die Binary-Ninja-HLIL der T31-`libimp.so`
(`libimp.so_hlil.txt` (Repo-Wurzel), im Folgenden `L<zeile>` bzw.
`0x<adresse>`), der OpenIMP-Quellstand bei Commit `9cab219`,
der dort mitgelieferte T31-`avpu`-Kerneltreiber (`avpu/t31/`) und ein Stock-Registertrace
(`stock_logs.txt`, aufgenommen mit einem WR/RD-loggenden `avpu.ko`, prudynt mit
1080p-H.264 + zwei JPEG-Kanälen 1920x1080 und 640x360).

Kennzeichnung:

- **[V]** belegt: steht so in der HLIL und/oder ist im Stock-Trace direkt beobachtet.
- **[I]** abgeleitet: plausibel aus Kontext/Namensgebung/Konvention, nicht direkt belegt.
- **[?]** offen: muss auf dem Gerät geprüft werden.

Der Bericht beschreibt Verhalten und Datenformate und verweist auf Fundstellen. Er kopiert
keinen Herstellercode und soll eine Clean-Room-Neuimplementierung (MIT) ermöglichen. Die
Quantisierungs- und Huffman-Standardtabellen sind die öffentlichen Tabellen aus
ITU-T T.81, Annex K.

---

## 0. Kurzfazit

1. **Der JPEG-Encoder ist ein eigener Hardware-Core und nicht der H.264-Core.** Der
   Scheduler legt `numCore` AVC-Cores an und danach genau einen JPEG-Core mit dem **Index
   `numCore`** (L66623 und L66635, `0x62c78`/`0x62d1c`). Die T31-libimp ruft
   `AL_SchedulerEnc_Init(..., numCore = 1, ...)` auf (L53999, `0x507d4`). Der JPEG-Core
   hat also **Index 1**:
   - Registerfenster `(1<<9)+0x8200…` = **`0x8400–0x85FF`**
   - Reset `0x85F0`, Start `0x85E4`, Clock-Gate `0x85F4`
   - Status `0x8430/0x8434/0x8438`
   - **IRQ-Bit 4** in `0x8014` (IRQ-Slot 4)

   Die Vermutung „gleiches Bit wie Enc1“ stimmt nur für die Formel `1<<(core*4)`. Weil der
   Core-Index ein anderer ist, ergibt sich ein anderes Bit: AVC nutzt Bit 0 (Enc1) und
   Bit 2 (Entropy), JPEG nutzt Bit 4. [V]
2. **Kein Command-List-Mechanismus.** JPEG wird über 11 direkte Registerschreibzugriffe
   (`0x8400–0x8428`) konfiguriert, dann folgt `0x85E4 = 1`. Nach dem IRQ werden drei
   Statusregister gelesen, Länge in Bytes = `0x8434`. [V]
3. **Die Tabellen liegen in einem DMA-Puffer („EP1“, Adresse in `0x8418`).** Er enthält:
   - Quantisierungstabellen in Zigzag-Reihenfolge (DQT-Form)
   - Reziproktabellen (u16) für den Hardware-Quantisierer
   - Huffman-BITS-Arrays
   - Huffman-Codebücher (4 Byte je Symbol)

   Der Puffer wird **einmal pro Kanal** in Software erzeugt. Die Quality-Skalierung ist die
   IJG-Formel. [V]
4. **Die JFIF-Header erzeugt aller Wahrscheinlichkeit nach die Hardware** (SOI…EOI
   inklusive APP0-Dichte, DQT, DHT, SOF, SOS). Die Software trägt nur einen Abschnitt
   `[offset, offset+size)` ein und schreibt keine Header. [I, stark gestützt; am Gerät
   prüfen]
5. **AVC und JPEG laufen im Stock-Trace echt parallel** auf ihren zwei Cores. Die
   Serialisierung beschränkt sich auf:
   - einen gemeinsamen „Register-Mutex“ für Clock-Gating, JPEG-Zonenschreiben und
     JPEG-Statuslesen
   - Scheduler-Mutexe
   - **genau einen** IRQ-Waiter-Thread pro Prozess, der Callbacks je Slot verteilt. [V]
6. **Nebenbefund mit hoher Priorität:** OpenIMP startet auf T31 derzeit **bei jedem
   H.264-Frame den JPEG-Core**. `avpu_t31_start_companion_stage()` in
   `src/t40/codec-t40.c:7515` wird nach jedem `CL_PUSH` aufgerufen (`:9305`) und hält den
   Block `0x8400–0x8428` samt `0x85F0/0x85E4` für eine AVC-„Source-Config“. Dabei zeigen
   Tabellenzeiger und Ausgabepuffer auf AVC-Speicher. Das muss vor jeder
   Hardware-JPEG-Arbeit entfernt bzw. verifiziert werden (Abschnitt 5.5).
7. **Machbarkeit: gut.** Die vorhandenen `/dev/avpu`-Primitiven (`WRITE_REG`, `READ_REG`,
   `WAIT_IRQ`, rmem-Flush) reichen, eine Kerneländerung ist nicht nötig. In OpenIMP fehlen:
   - Demux des IRQ-Slots 4 unabhängig vom AVC-„Owner“
   - gemeinsamer Mutex für `0x8014`-RMW
   - EP1-Generator
   - JPEG-Stream-Pufferverwaltung
   - Entfernung der Companion-Stage

   Aufwand: etwa 1,5–2,5 Personenwochen inklusive Gerätevalidierung.

---

## 1. Architektur: Cores, Registerfenster, IRQ-Slots

| Element | Fundstelle | Aussage |
|---|---|---|
| `AL_SchedulerCpu_Create` | L53999 `0x507d4` | `AL_SchedulerEnc_Init(..., 1 /*numCore*/, 0x2faf0800 /*Takt 800 MHz*/)` [V] |
| `AL_SchedulerEnc_Init` | L66566 ff. | schreibt global `0x8010 = 0x1000` (`0x62be4`), Schleife `AL_EncCore_Init(core i)` für `i < numCore` (`0x62c78`), danach `AL_EncJpegCore_Init(core numCore)` (`0x62d1c`) [V] |
| `AL_EncCore_Init` | L72514 `0x6c8d8` | registriert `EndEncoding` an Slot `core*4` und `EndAvcEntropy` an Slot `core*4+2`, `ResetCore` (`0x83F0` ← 1, 2, 4), `0x8018 ← 0xFFFFFF`, `0x8054 ← 0x80`, Codec-Liste {1, 0} (HEVC, AVC) [V] |
| `AL_EncJpegCore_Init` | L72491 `0x6c7fc` | registriert nur `EndEncoding` an Slot `core*4` (`0x6c894`), Codec-Liste {4} = JPEG (`0x6c8a4`/`0x6c8ac`), **kein Reset**, **kein IRQ-Clear** [V] |
| `getCompatibleCores` | L65725 `0x61230` | vergleicht `eProfile>>24` mit der Codec-Liste des Cores und iteriert Cores `0…numCore` (einschließlich JPEG-Core). JPEG-Kanäle landen daher zwangsläufig auf Core 1 [V] |
| Stock-Trace | `stock_logs.txt` Z. 146–190 | `WR 0x85f4=1`, `0x8014=0x11`, `0x85f0=1`, `0x8400…0x8428`, `0x85e4=1` [V] |

Per-Core-Register (Basis `B = core<<9`, für JPEG gilt `B = 0x200`):

| Register | AVC-Core 0 | JPEG-Core 1 | Bedeutung |
|---|---|---|---|
| `0x8200+B … 0x8228+B` | (nicht als Zone genutzt) | `0x8400–0x8428` | JPEG-Kommandozone (11 Worte) [V] |
| `0x8230+B … 0x8238+B` | – | `0x8430–0x8438` | JPEG-Statuszone (3 Worte) [V] |
| `0x83E0+B` | `CL_ADDR` | – | für JPEG nicht benutzt [V] |
| `0x83E4+B` | `CL_PUSH` (2 = Enc1, 8 = Enc2) | `0x85E4` ← **1** = JPEG-Start | Start/Trigger [V]; Bitbedeutung „1 = JPEG“ [I] |
| `0x83F0+B` | Reset 1, 2, 4 | `0x85F0` ← **1** vor jedem JPEG | Teil-/Soft-Reset [V Schreibwert, I Bedeutung] |
| `0x83F4+B` | Clock-Cmd Bits[1:0] | `0x85F4` Bits[1:0] = 1 | Clock-Gate an (RMW, `SetClockCommand` L72391 `0x6c428`) [V] |
| `0x83F8+B` | Bit1 Enc1 läuft, Bit4 Enc2 läuft | (`0x85F8`) | für JPEG vom Hersteller nicht abgefragt [V] |

Globale Register: `0x8010` (Misc, `0x1000` bei Init), `0x8014` (IRQ-Maske), `0x8018`
(IRQ-Pending, Schreiben löscht), `0x8054` (Top-Ctrl `0x80`), `0x8004` (Board-ID
`0x72000460`, siehe `src/al_avpu.c`).

IRQ-Slots (Index = Bitnummer, von `WAIT_IRQ` geliefert): 0 = Enc1-Ende Core 0,
2 = AVC-Entropy Core 0, **4 = JPEG-Ende Core 1**. [V]

---

## 2. Registerkarte der JPEG-Zonen

### 2.1 Format der Zonen-Deskriptoren [V]

`WriteZoneRegisters` (L72468, `0x6c758`) interpretiert einen 8-Byte-Deskriptor:

- `u32 reg_offset` (relativ zur Zonenbasis)
- `u16 word_index` (Index ins Kommando-/Statuspuffer-Array)
- `u16 count`

Nimmt man die 64-Bit-Konstanten auseinander (L131462/L131463, Datenadressen
`0xe3c28/0xe3c30`), ergibt sich:

| Symbol | Rohwert | reg_offset | word_index | count |
|---|---|---|---|---|
| `AL_ENCJPEG_CMD` | `0x000b000000000000` | 0x00 | 0 | **11** |
| `AL_ENCJPEG_STATUS` | `0x0003000c00000030` | 0x30 | 12 | **3** |
| (Vergleich) `AL_ENC1_STATUS` | `0x001d004100000104` | 0x104 | 0x41 | 29 |

`AL_EncCore_EncodeJpeg` (L72713) schreibt damit die Pufferworte 0…10 nach
`(core<<9)+0x8200 … +0x8228`. Die Statuszone liegt bei `+0x8230 … +0x8238` und wird im
Puffer als Worte 12…14 gespiegelt (`JpegStatusToStatusRegs` L73244 schreibt `+0x30/+0x34/+0x38`).

### 2.2 Kommandozone (T31: `0x8400–0x8428`)

Die Werte werden in `SetJpegParam` gesetzt (L71843–L71936, `0x6b33c–0x6b59c`):

- Worte 0–3 über `JpegParamToCtrlRegs` (L73828, `0x6f5c4`)
- Worte 4–10 direkt (`0x6b498–0x6b4e4`)
- Die Namen „srcY/srcC/tab/bit“ stammen aus `TraceJpegCmd` (L52408), das eine
  `.sram_map.hex` mit genau diesen Bezeichnern für die Worte 4–7 schreibt (`0x4ca94–0x4cb04`,
  Strings `0xe3800/0xe3820/0xe3828/0xe382c`).

| Reg (T31) | Wort | Bits | Inhalt | Quelle im Hersteller | Stock-Wert (1080p / 360p) | Status |
|---|---|---|---|---|---|---|
| `0x8400` | 0 | [1:0] | ChromaMode: 0 = 4:0:0, 1 = 4:2:0, 2 = 4:2:2 (Assert `ChromaMode <= AL_CHROMA_4_2_2`, `0x6b5c8`) | `ePicFormat>>8 & 0xF` | 1 | [V] |
| | | [5:4] | Komponentenzahl: 1 bei Mono, sonst 3 | `0x6b450–0x6b458` | 3 | [V] Wert, [I] Name (SOF-Nf) |
| | | [8] | immer 1 | `0x6b438` | 1 | [V] Wert, [?] Bedeutung (evtl. „Header erzeugen“) |
| | | [9] | Kanalflag `ChParam+0x5D`; unterdrückt die Quant-Tabellen und schaltet auf eine Nur-DC-Huffman-Anordnung (Abschnitt 3.5) | `0x6b418` | 0 | [V] Wert, [I] „Lossless-Modus“ |
| | | [13:12] | `ChParam+0x60` (2 Bit) | `0x6b42c` | 0 | [V] Wert, [I] JFIF-„units“ (0 = Seitenverhältnis) |
| | | [31:16] | `ChParam+0x5E` (u16) | `0x6b43c` | 0 | [V] Wert, [I] Restart-Intervall (DRI) |
| `0x8404` | 1 | [15:0] | Höhe − 1 | `ChParam+6` | `0x0437` (1079) / `0x0167` (359) | [V] |
| | | [31:16] | Breite − 1 | `ChParam+4` | `0x077F` (1919) / `0x027F` (639) | [V] |
| `0x8408` | 2 | [31:16] / [15:0] | JFIF-X-Dichte / -Y-Dichte | `ChParam+0x62/+0x64`, gesetzt von `setDensity` (JPEG_Encoder.c, L51629 ff., `0x4b370`) | `0x0001/0x0001` | [V] Herkunft (Assert-Funktionsname `setDensity`), [I] Semantik |
| `0x840C` | 3 | [15:0] | Quell-Pitch (Bytes, Luma) | Quellpuffer +0x0C (`SetSourceBuffer` L68803) | `0x0780` / `0x0280` | [V] Wert, [I] Pitch statt Breite |
| | | [31:16] | 0 (Memset) | `0x6b46c` | 0 | [V] |
| `0x8410` | 4 | 31:0 | **Y-Physikadresse** | Quelle | `0x071a5f00` | [V] („srcY“) |
| `0x8414` | 5 | 31:0 | **UV-Physikadresse (NV12, interleaved)** | Quelle | `0x073a3f00` = Y + 1920·1088 | [V] („srcC“), Offset = Pitch·align16(H) beobachtet |
| `0x8418` | 6 | 31:0 | **Tabellenpuffer (EP1) phys.** | `AL_IntermMngr_GetEp1Addr` (`0x6a8a8`), muss 32-Byte-aligned sein (Prüfung `0x5c9f0`) | `0x0718dd00` / `0x0719f600` (je Kanal konstant) | [V] („tab“) |
| `0x841C` | 7 | 31:0 | **Bitstream-Puffer phys. (Basis)** | StreamBuf+0x0C | `0x06f75300` | [V] („bit“) |
| `0x8420` | 8 | 31:0 | Puffergröße gesamt | StreamBuf+0x10 | `0x000e7680` / `0x00027780` | [V] Wert, [I] Name |
| `0x8424` | 9 | 31:0 | Startoffset im Puffer | StreamBuf+0x14 | `0x00000200` | [V] (identisch mit dem Abschnitts-Offset in `EndJpegEncoding` `0x68cac`) |
| `0x8428` | 10 | 31:0 | verfügbare Bytes = Größe − Offset | `+0x10 − +0x14` (`0x6b4e4`) | `0x000e7480` | [V] |

Alle beobachteten Adressen sind 256-Byte-aligned [V, Trace]. Hart geprüft wird nur die
32-Byte-Ausrichtung von EP1 [V].

Der gleiche Dreiklang (Größe, Offset, verfügbar) steht auch im AVC-Command-List-Dump
(`CL[049..051] = 000e7680 00000220 000e7460`) und ist damit Allegro-Konvention. [V]

### 2.3 Statuszone (T31: `0x8430–0x8438`)

`AL_EncCore_ReadStatusRegsJpeg` (L72837, `0x6d32c`) liest die drei Register unter dem
Register-Mutex:

| Reg | Ziel im Status | Verwendung | Status |
|---|---|---|---|
| `0x8430` | `Status+0x70` | nur für Trace zurückgeschrieben | [V] Lesen, [?] Bedeutung (Zyklen-/Bitzähler?) |
| `0x8434` | `Status+0x08` | **Länge des erzeugten JPEG in Bytes.** `EndJpegEncoding` trägt den Abschnitt {Offset = StreamBuf+0x14, Länge = dieser Wert} ein (`0x68ca0–0x68cbc`) | [V] |
| `0x8438` | Bit 1 → `Status+0x01` | Fehlerflag. Wenn gesetzt, setzt `EndJpegEncoding` den Fehlercode `0x88` (`0x68cc4`) | [V]; [I] `0x88` = Allegro „Stream-Overflow“ (`0x87` wird an `0x63218` als No-Memory benutzt) |

Beim Abschluss wird kein Register geschrieben: keine Maskenänderung, kein Clear. Den Ack
von `0x8018` macht der Kerneltreiber (Abschnitt 5). [V]

---

## 3. Quantisierungs- und Huffman-Tabellen (EP1-Puffer)

### 3.1 Erzeugung und Lebenszyklus [V]

- Pro Kanal wird ein EP1-Puffer der Größe `AL_GetAllocSizeEP1() = 0x6400` Byte (L43022)
  mit dem DMA-Allocator angelegt (`MemDesc_AllocNamed`, `0x45c00`, L48506).
- Beim Kanal-Setup wird er einmal gelöscht (`AL_CleanupMemory`, `0x45f5c`) und über den
  Encoder-vtable-Eintrag `preprocessEp1` gefüllt (`0x45f8c`). Für JPEG ist das
  `preprocessEp1` in JPEG_Encoder.c (L51587, `0x4b1e0`):
  - Ist `ChParam+0x5D == 0`, wird `JpegTables_InitQuant(Q, chroma, …)` aufgerufen.
  - Danach folgt immer `JpegTables_InitHuffman(chroma, ChParam+0x5D, …)`.
  - Anschließend wird im EP1-Deskriptor Flag `0x10` gesetzt.
- Der Scheduler kopiert das EP1-Master-Image in jeden Intermediate-Puffer
  (`AL_EncChannel_PushIntermBuffer`, L71346 `0x6a4c0`). Pro Frame wird die EP1-Physadresse
  des gerade benutzten Intermediate-Puffers eingetragen (`0x6a8c0`).
- Folge: Die Tabellen werden **nicht pro Frame** neu gerechnet. Das deckt sich mit der
  kanal-konstanten `0x8418` im Trace.

### 3.2 Quality → Quantisierungstabelle [V]

`JpegTables_InitQuant` (L51669, `0x4b490`):

- `Q` = vorzeichenerweiterter Wert aus `ChParam+0x80`. Das ist `RC.iInitialQP`; die
  RC-Struktur beginnt bei `Settings+0x68`, siehe `AL_Settings_SetDefaultRCParam`
  `0x3ddb8` (`+0x18 = -1`). Die JPEG-Defaults setzen MinQP = 1 und MaxQP = 100
  (`AL_sSettings_SetDefaultJPEGParam` `0x3dd84`: `+0x82 = 1`, `+0x84 = 100`). [V Feld,
  I „Quality = iInitialQP“, passt zur IMP-API `IMP_Encoder_SetDefaultParam(..., iInitialQP, ...)`]
- Skalierung (IJG):
  - `S = 200 − 2Q` für `Q ≥ 50`, sonst `S = 5000 / Q`
  - `S &= 0xFFFF`
  - `Q = 0` würde durch null teilen (trap).
- Pro Eintrag: `q = (base·S + 50) / 100`. Wird `q` zu 0, gilt `q = 1`; ab `q ≥ 256` gilt
  `q = 255`.
- Basistabellen: ITU-T T.81 Annex K.1 für Luma und Chroma, im Binary als
  `LUMA_QUANT_TABLE`/`CHROMA_QUANT_TABLE` in Rasterreihenfolge (`0xe3040`/`0xe3000`).
- **Ausgabe in Zigzag-Reihenfolge** (DQT-Reihenfolge). Iteriert wird über `ZIGZAG_SCAN`
  (`0xe2fc0`, Standard-Zigzag→Raster).
  - Luma: 64 Byte ab EP1+0x00
  - Chroma (nur wenn ChromaMode ≠ 0): 64 Byte ab EP1+0x40

### 3.3 Reziproktabellen für den Hardware-Quantisierer [V]

- Direkt hinter den Quant-Bytes (bei Farbe EP1+0x80, bei Mono EP1+0x40) liegen 64 × u16
  (Little Endian, CPU-nativ) je Komponente.
- Chroma liegt 0x80 Byte hinter Luma.
- Wert: `r = ((0x10000 + (q>>1)) / q) & 0xFFFF`, davon −1. Für q = 1 ergibt das `0xFFFF`.
- Abgelegt in **Rasterreihenfolge**: `r[zigzag[k]]` wird aus dem k-ten Zigzag-Quantwert
  berechnet (`0x4b5b4–0x4b610`).
- Damit multipliziert die Hardware mit 0.16-Festkomma-Kehrwerten, statt zu dividieren. [I]

### 3.4 Huffman-Format [V]

`JpegTables_InitHuffman` (L51767, `0x4b64c`) legt zwei Arten von Daten ab.

**(a) BITS-Arrays** (16 Byte je Tabelle = Anzahl Codes je Länge 1…16):
- gespeichert als vier u32-Worte, in denen jeweils 4 Bytes **MSB-first** stehen, also
  Byte-Reihenfolge pro Wort vertauscht
- Beispiel DC-Luma `{0,1,5,1,1,1,1,1,1,0,…}` → Worte `0x00010501, 0x01010101, 0x01000000, 0`

Nebenbei: Binary Ninja zeigt einige dieser Konstanten als Zeiger in Strings an. So ist
„`fog_Strength`“ = `0x00010501`, „`_GetDRC_Strength`“ = `0x00010277`, „`g_SetAeHist`“ =
`0xFFFF`, „`_SetAeHist`“ = `0x10000`, „`etGopParam`“ = `0xCFFF`. Sie wurden über die
Stringadressen `0x104ed+20`, `0x10269+14`, `0xfff2+13`, `0xcff3+12` aufgelöst.

**(b) Codebücher**: je Symbol ein u32 (LE) mit folgendem Aufbau:

| Bits | Inhalt |
|---|---|
| [7:0] | **nächstes Symbol in HUFFVAL-Reihenfolge** (`0xFF` = Listenende) – eine verkettete Liste, mit der die Hardware die DHT-Wertefolge ausgeben kann [I] |
| [23:8] | Huffman-Code (rechtsbündig) |
| [31:24] | Codelänge − 1 |

Beispiel DC-Luma Kategorie 0: Code `00`, Länge 2 → Wort `0x01000001` mit next = 1. [V]

Anordnung der Codebücher:
- **DC**: 12 Einträge, Index = Kategorie 0…11 (48 Byte).
- **AC**: 162 Einträge (648 Byte). Index `(size−1)·16 + run` für size 1…10 und run 0…15,
  dann Index 160 = ZRL (0xF0), Index 161 = EOB (0x00). Geprüft an Luma und Chroma, z. B.
  Chroma-ZRL = `1111111010`, 10 Bit, Chroma-EOB = `00`, 2 Bit.
- Ob die Hardware bei ungewöhnlichen Tabellen den Listenkopf selbst findet (kanonisch
  beginnt der erste Code immer mit lauter Nullen) oder aus den Kopfworten liest, ist
  **[?]**. Für eine Neuimplementierung reicht es, die Standardtabellen byte-identisch
  nachzubauen.

### 3.5 EP1-Speicherbild (Normalmodus, Bit 9 = 0) [V]

Farbe (4:2:0/4:2:2):

| Offset | Größe | Inhalt |
|---|---|---|
| 0x000 | 64 B | Luma-Quant u8, Zigzag |
| 0x040 | 64 B | Chroma-Quant u8, Zigzag |
| 0x080 | 128 B | Luma-Reziproke u16, Raster |
| 0x100 | 128 B | Chroma-Reziproke u16, Raster |
| 0x180 | 4 B | u32 = 1 [?] (evtl. Kopf-Symbol AC-Luma = 0x01 oder ein Flag) |
| 0x184 | 4 B | u32 = 0 [?] |
| 0x188 | 16 B | BITS AC-Luma (Wort-MSB-first) |
| 0x198 | 16 B | BITS DC-Luma |
| 0x1A8 | 16 B | BITS AC-Chroma |
| 0x1B8 | 16 B | BITS DC-Chroma |
| 0x1C8 | 648 B | Codebuch AC-Luma |
| 0x450 | 48 B | Codebuch DC-Luma |
| 0x480 | 648 B | Codebuch AC-Chroma |
| 0x708 | 48 B | Codebuch DC-Chroma |
| 0x738 | 88 B | Nullen (bis 0x790) |

Mono (4:0:0):

| Offset | Größe | Inhalt |
|---|---|---|
| 0x000 | 64 B | Luma-Quant, Zigzag |
| 0x040 | 128 B | Luma-Reziproke, Raster |
| 0x0C0 | 4 B | u32 = 1 |
| 0x0C4 | 4 B | u32 = 0 |
| 0x0C8 | 16 B | BITS AC-Luma |
| 0x0D8 | 16 B | BITS DC-Luma |
| 0x0E8 | 648 B | Codebuch AC-Luma |
| 0x370 | 48 B | Codebuch DC-Luma |
| 0x3A0 | 88 B | Nullen |

**Bit-9-Modus** (`ChParam+0x5D ≠ 0`, Pfad `0x4b7bc`):
- keine Quant-Tabellen
- ab EP1+0: `1, 0`, dann BITS DC-Luma (+ BITS DC-Chroma), Codebuch DC-Luma
  (+ DC-Chroma), Nullen
- nur „DC-artige“ SSSS-Tabellen und keine DQT: das passt zu Lossless-JPEG [I]
- Die IMP-API setzt das Flag nicht (Default 0 durch Memset in `AL_Settings_SetDefaults`
  `0x3de88`). Es kann ignoriert werden.

Wichtig für die Neuimplementierung: Die Hardware liest die Bytes aus dem
Little-Endian-Speicher. Wie sie die u8-Quantbytes innerhalb der 32-Bit-Worte adressiert,
ist nicht belegt. **Die Neuimplementierung muss das Speicherbild byte-exakt nachbilden**
(Abgleich gegen einen Stock-Dump, Abschnitt 8).

---

## 4. Ablauf eines JPEG-Frames beim Hersteller

1. **Einmalig** (Scheduler-Init):
   - `0x8010 = 0x1000`
   - Init von AVC-Core 0 (`0x83F0` 1/2/4, `0x8018 = 0xFFFFFF`, `0x8054 = 0x80`)
   - JPEG-Callback auf Slot 4
   - Diese Schritte laufen auch, wenn nur ein JPEG-Kanal existiert. [V]
2. **Kanalanlage**:
   - EP1 anlegen (DMA/rmem, cached) und mit Tabellen füllen
   - Dichte setzen (`ConfigureChannel`/`setDensity`: Aspect-Enum `ChParam+0xFC`, 2 → 4:3,
     0/3 → 16:9, 4 → 1:1, JPEG-Default 4 → 1:1)
   - Stream-Puffer anlegen
   - Log-Hinweise: „Jpeg channel will not share buff“ und „Jpeg channel need create after
     channel 0“ (L85368, L133288). [V]
3. **Frame-Start**: `CheckAndEncode` (`0x61b08`):
   - Unter dem Scheduler-Mutex (`0x6200c`) wird Core 1 als „running“ markiert.
   - `AL_EncCore_TurnOnGC` → RMW `0x85F4[1:0] = 1` unter dem Register-Mutex (`0x61fec`).
     Im Trace passiert das nur beim ersten JPEG.
   - Dann folgt `AL_EncChannel_Encode` (`0x620d0`). [V]
4. **`SetJpegParam`** (L71843 ff.) unter dem Kanal-Mutex:
   - Request aus der FIFO nehmen
   - Kommandopuffer (0x200 B, `Req+0xA78`) nullen
   - Worte 0–10 füllen (Abschnitt 2.2)
   - optionaler Trace-Hook (`handleJpegInputTrace`)
   - `AL_EncCore_TurnOnRAM` (No-op, `0x6cbbc`)
   - **`AL_EncCore_SetJpegInterrupt`**: RMW `0x8014 |= 1<<4` (`0x6d3f8`)
   - Zeitstempel
   - `AL_EncCore_EncodeJpeg` [V]
5. **`AL_EncCore_EncodeJpeg`** (L72713):
   - `Rtos_FlushCacheMemory(cmdbuf_virt, 0x100000)`. Das wird über
     `IMP_FlushCache` → `/dev/rmem` ioctl `0xc00c7200 {vaddr, 1 MiB, dir = 1}` zum
     Writeback (L23910).
   - Register-Mutex holen, `0x85F0 ← 1`, 11 Zonenregister schreiben, `0x85E4 ← 1`,
     Mutex freigeben. [V]
   - [I] Der 1-MiB-Writeback ist größer als der Cache und führt bei MIPS praktisch zum
     kompletten D-Cache-Writeback. Damit sind auch die per CPU geschriebenen EP1-Bytes
     sicher im RAM.
6. **Hardware** liest NV12 direkt aus dem Framesource-Puffer (keine Kopie) und schreibt ab
   `Basis + Offset (0x200)` bis maximal `Größe − Offset`. [V Parameter, I Semantik]
7. **IRQ Slot 4** → Kernel (Abschnitt 5) → `WAIT_IRQ` liefert 4 → IpCtrl-Callback
   `EndEncoding` (`0x6c2c0`) → `HandleCoreInterrupt(sched, core = 1)` (`0x6290c`) → Kanal
   des Cores → `AL_EncChannel_EndEncoding` → bei Codec 4 **`EndJpegEncoding`**
   (L70417, `0x68b70`; Verzweigung `0x6b870`). [V]
8. **`EndJpegEncoding`**:
   - Status lesen (`0x8430/34/38`)
   - Abschnitt {Offset, Länge = `0x8434`} eintragen, bei Bit 1 Fehler `0x88`
   - Request in den End-FIFO, Arbeits-Puffer freigeben
   - Quellframe freigeben (`shouldReleaseSource` → 1, `0x4b1d0`)
   - danach `Process()` für den nächsten Auftrag. [V]
9. **Header:**
   - `generateNals` ist für JPEG leer (`0x4b1d8`).
   - `updateHlsAndWriteSections` (`0x4b298`) übernimmt nur die bereits vorhandenen
     Abschnittsdaten und fügt einen Abschluss-Abschnitt mit Flag `0x20000000` hinzu.
   - Die Software schreibt also **kein** SOI/DQT/DHT/SOF/SOS/EOI.
   - Gleichzeitig enthält EP1 genau die Daten, die ein Header-Generator braucht
     (DQT-Bytes, BITS, HUFFVAL-Kette), und Wort 2 transportiert JFIF-Dichten.
   - ⇒ Die Hardware erzeugt den kompletten JFIF-Datenstrom. [I, stark; Prüfung:
     Ausgabe beginnt mit `FF D8 FF E0`]

Gemessene Hardwarezeiten aus dem Stock-Trace (Start `0x85E4` bis `WAIT_IRQ`-Rückkehr mit
drei Status-Reads), jeweils parallel zu laufendem 1080p-H.264:

| Auflösung | Messungen |
|---|---|
| 1920x1080 | 8,0 ms (`128.883655 → 128.891691`), 8,2 ms (`128.919882 → 128.928094`) |
| 640x360 | 1,3 ms / 1,1 ms |

[V]

---

## 5. Scheduling und Sharing mit H.264

### 5.1 Parallelität statt Zeitmultiplex [V]

AVC (Core 0) und JPEG (Core 1) sind getrennte Cores mit eigenen Registerfenstern.

- Scheduler-Sicht: Pro Core läuft zu jedem Zeitpunkt höchstens ein Kanal
  (`AL_CoreState_*`, `CheckAndEncode`).
- Trace:
  - `128.882581`: AVC-`CL_PUSH`
  - `128.883655`: JPEG-Start 1080p
  - `.891691`: JPEG fertig
  - `.892340`: JPEG-Start 360p, fertig `.893612`
  - `.894825`: AVC fertig, danach `0x8014 = 0x10`, AVC-Bit 0 wird wieder gelöscht

  Es gibt also **keine** Prüfung der Art „Enc1 läuft schon“ vor einem JPEG. Die
  `IsEnc1/Enc2AlreadyRunning`-Checks (`0x6c678`/`0x6c6e8`) gelten nur für die
  AVC-Command-Lists.

### 5.2 Sperren beim Hersteller [V]

- **Register-Mutex** `sched[0x4be]`: wird allen Cores als `core[7]` mitgegeben
  (`0x62c78`, `0x62d1c`). Er schützt:
  - `SetClockCommand` (RMW `0x83F4`/`0x85F4`)
  - die komplette JPEG-Zone mit Reset und Start
  - `ReadStatusRegsJpeg`
- **Scheduler-Mutex** `sched[0x4bd]`: Core-/Kanalzuordnung, IRQ-Dispatch
  (`HandleCoreInterrupt`).
- **Kanal-Mutex**: `SetJpegParam`, `EndJpegEncoding`-Nachlauf.
- **Nicht** unter einem gemeinsamen Mutex stehen die `0x8014`-RMWs von
  `SetJpegInterrupt` (JPEG) und `EnableInterrupts`/`DisableEnc1Interrupt` (AVC). Das ist
  ein theoretisches Rennen im Herstellercode. In OpenIMP sollte man es vermeiden: Bit 4
  einmalig setzen, danach nie wieder anfassen, oder alle `0x8014`-RMWs unter einen Mutex
  legen.

### 5.3 IRQ-Pfad im Kernel (`avpu/t31/avpu_ip.c`, `avpu_main.c`) [V]

- Der Hardirq-Handler liest Maske `0x8014` und Pending `0x8018`.
- Er quittiert **alle** Pending-Bits (auch die maskierten) durch Rückschreiben nach
  `0x8018`.
- Für jedes gesetzte **und** maskierte Bit reiht er einen Eintrag mit der Bitnummer in
  eine **globale** Liste ein und weckt `codec->chan` auf.
- `WAIT_IRQ` gibt pro Aufruf eine Bitnummer zurück.

Folgerungen:

- Ohne Bit 4 in `0x8014` geht ein JPEG-Ende **still verloren**: Es wird gelöscht, sobald
  irgendein anderer IRQ kommt.
- Der Treiber kennt **genau einen** Client:
  - `avpu_codec_bind_channel` gibt `-ENODEV` zurück, wenn `codec->chan` belegt ist.
  - Vor dem Binden verwirft er alle noch anstehenden IRQs („Previous channel lost irq“).
  - `avpu_codec_unbind_channel` setzt `codec->chan = NULL`, **ohne zu prüfen**, ob der
    schließende Client auch der gebundene ist.
  - Geweckt wird nur `codec->chan`.

### 5.4 Warum OpenIMPs Versuch die AVC-IRQs „tötete“

Der Kommentar in `src/t40/codec-t40.c:8590` bezieht sich **nicht** auf einen Versuch, den
JPEG-Core anzusteuern. Gemeint ist die generische Legacy-Probe `HW_Encoder_Init`
(`src/hw_encoder.c:25–85`). Sie öffnet `/dev/avpu` **per rohem `open()`**, also zusätzlich
zum gepoolten fd des AVC-Pfads (`AL_DevicePool_Open`, Refcount), und schickt ein fremdes
ioctl `VENC_IOCTL_INIT = 0xc0104501` (Typ 'E'). Der Treiber lehnt es mit `-EINVAL` ab und
das fd wird wieder geschlossen.

Mit dem oben beschriebenen Single-Client-Treiber kommen je nach Reihenfolge diese
Mechanismen in Frage [I, der genaue Ablauf ist aus den Quellen nicht eindeutig
rekonstruierbar]:

- Gewinnt die Probe den einzigen Slot (AVC öffnet in OpenIMP erst lazily im ersten
  `Process()`), bekommt der AVC-Pfad `-ENODEV` oder verliert beim Binden anstehende IRQs.
- Das `release()` der Probe setzt `codec->chan = NULL`. Hält ein anderer Client das Gerät
  weiter offen, werden danach **keine** Wake-ups mehr an den AVC-`WAIT_IRQ`-Thread
  zugestellt. IRQs landen dann nur noch in der Liste und die Completion bleibt aus.
- Bei jedem erfolglosen `open()` wird zudem Clock-Enable ohne Gegenstück erhöht (harmlos).

**Wie der Hersteller das vermeidet** [V]:

- ein einziges `AL_Board`/`LinuxIpCtrl` pro Prozess, also ein fd über `AL_DevicePool`
- genau ein `WaitInterruptThread`
- Callback-Tabelle je Slot (Slot 0/2 → AVC-Core, Slot 4 → JPEG-Core)
- eigene Registerfenster je Core
- keine fremden ioctls

### 5.5 Nebenbefund: OpenIMP startet heute schon den JPEG-Core (T31)

- `avpu_t31_start_companion_stage()` (`codec-t40.c:7515–7584`) schreibt nach **jedem**
  AVC-`CL_PUSH` (`:9305`, auch im T41-Zweig `:9198`):
  - `0x85F4 = 1`, `0x85F0 = 1`
  - `0x8400 = 0x131`, `0x8404 = (W−1)<<16 | (H−1)`, `0x8408 = 0x00010001`, `0x840C = W`
  - Y/UV-Adresse des AVC-Quellframes
  - **`0x8418` = `interm_buf + interm_ep1_size`** (kommentiert als „WPP start“)
  - **`0x841C` = der *andere* AVC-Stream-Puffer**
  - `0x8420/24/28`
  - **`0x85E4 = 1`**
- Das ist exakt die JPEG-Zone von Core 1 und ihr Startbefehl. `CURRENT_STATUS.md`
  Z. 90–92/118/143 führt den Block als ungelöstes AVC-Paritätsthema.
- Im Herstellercode ist `WriteZoneRegisters` ausschließlich aus `AL_EncCore_EncodeJpeg`
  aufrufbar (einziger Aufrufer L72719). Ohne JPEG-Kanal schreibt die Stock-libimp diesen
  Block nie. [V]
- Folgen [I]:
  1. Bei jedem H.264-Frame liest der JPEG-Core einen kompletten Frame. Mit Tabellen aus
     einem AVC-Arbeitspuffer schreibt er bis zu `budget` Bytes ab Offset 0x200 in einen
     AVC-Stream-Puffer, der gerade noch bei der Anwendung liegen kann. Das bedeutet
     Speicherbandbreite und mögliche **Korruption bereits ausgelieferter H.264-Daten**.
  2. Ein echter JPEG-Job würde bei jedem AVC-Frame per `0x85F0`/`0x85E4` abgewürgt.
- **Vor jeder Hardware-JPEG-Arbeit** (nach Absprache mit den Maintainern) diesen Block
  hinter ein Flag legen und prüfen, ob H.264 ohne ihn identisch weiterläuft.

---

## 6. OpenIMP-Seite: reichen die Primitiven?

Vorhanden und ausreichend [V]:

- **Registerzugriff**: `AL_CMD_IP_WRITE_REG`/`READ_REG` (`_IOWR('q', 10/11)`, beliebige
  4-Byte-aligned Offsets ≥ `0x8000` im 1-MiB-Fenster ab `0x13200000`,
  `avpu/t31/avpu_main.c`).
- **IRQ**: `AL_CMD_IP_WAIT_IRQ` (`_IOWR('q', 12)`) liefert die Slotnummer (0…19), also
  auch 4. Verteilt wird im `avpu_irq_thread` (`codec-t40.c:5070 ff.`) über eine
  Callback-Tabelle mit 20 Slots.
- **DMA-Speicher**:
  - `avpu_alloc_imp` (rmem, cached, phys + virt)
  - `avpu_alloc_mmap` (`GET_DMA_MMAP`, kohärent; für einen Standalone-Test am einfachsten)
- **Cache-Pflege**:
  - `avpu_flush_cache(..., dir)` über rmem-ioctl `0xc00c7200` (`codec-t40.c:390, 478 ff.`)
  - Quellframe-Invalidate vor AVPU (`:7650 ff.`)
  - Stream-Invalidate nach Completion (`:5623 ff.`)
- **Physadressen der NV12-Frames**: liegen im Process-Pfad vor (`phys_addr`, `virt_addr`,
  UV = Y + Pitch·align16(H), vgl. SW-JPEG `hw_encoder.c:950 ff.`).

Es fehlt [V aus Codeinspektion]:

1. **IRQ-Demux pro Slot.** Auf T31/T40 leitet der Waiter **jeden** IRQ an
   `g_tseries_irq_owner` weiter, also an den letzten AVC-Submitter (`codec-t40.c:5127`).
   Auf T31 ist nur Slot 0 registriert (`:8544 ff.`). Slot 4 muss unabhängig vom
   AVC-Owner an einen JPEG-Kontext gehen. Die Callback-Tabelle darf nicht pro AVC-Kontext
   liegen, sondern muss prozessweit sein.
2. **Ein prozessweiter Register-Mutex** für `0x8014`-RMW, `0x85F4`-RMW und die
   JPEG-Zonensequenz. Die T31-Maske setzt heute nur `0x01` (`avpu_enable_interrupts`
   `:4210`).
3. **JPEG-Backend**:
   - EP1-Generator (Abschnitt 3)
   - Pool von Stream-Puffern
   - Frame-Referenz halten, bis IRQ 4 kommt: Die Hardware liest asynchron aus dem
     Framesource-Puffer.
   - Status-Auswertung und Übergabe an die bestehende Stream-FIFO/`GetStream`-Logik
4. **Entkopplung vom AVC-Lock.** `AL_Codec_Encode_Process` serialisiert auf T31 alle
   Encodes über `g_t31_encode_core_lock` und wartet synchron (`:9470 ff.`). JPEG braucht
   einen eigenen Core-Lock für Core 1, sonst geht die Parallelität verloren.
5. **Entfernung der Companion-Stage** (Abschnitt 5.5).

Kernel-Support: **keine Änderung nötig.** Der Treiber reicht Slot 4 durch, sofern Bit 4
maskiert ist. Einzige harte Regel: das gepoolte fd benutzen und nie ein zweites `open()`
auf `/dev/avpu`.

---

## 7. Machbarkeit und Implementierungsplan

**Urteil:** Gut machbar. Die Registerschnittstelle ist klein (15 Register) und im
Stock-Trace vollständig beobachtet. Die Tabellenformate sind aus dem Code vollständig
rekonstruierbar. Der Treiber braucht keine Änderung. CPU-Last danach: grob 20 ioctls pro
Frame plus Cache-Pflege statt DCT, also nahe null gegenüber heute 20–30 % eines Cores.
Hardwarelatenz ≈ 8 ms bei 1080p.

### Phase 0 – Aufräumen und Verifikation (0,5–1 Tag)

- Companion-Stage hinter ein Flag (z. B. Env `OPENIMP_T31_COMPANION=0`).
- H.264 mit und ohne vergleichen: Bitstream-Hash, Frame-Zählung, IRQ-Verhalten,
  Langzeitlauf.
- Erwartung laut Herstellercode: kein Unterschied.

### Phase 1 – Standalone-Testtool (1–2 Tage), Streamer gestoppt

1. `/dev/avpu` öffnen (einziges fd), Waiter-Thread starten.
2. Global initialisieren wie der Hersteller: `0x8010 = 0x1000`; Core-0-Init (`0x83F0` 1,2,4;
   `0x8018 = 0xFFFFFF`; `0x8054 = 0x80`); `0x8014 |= 0x10`; `0x85F4[1:0] = 1`.
3. Puffer per `GET_DMA_MMAP` anlegen (kohärent, damit fallen Cache-Fragen weg):
   - NV12-Testbild; OpenIMP kann Quellframes dumpen, `openimp-source-ch*.nv12`,
     `codec-t40.c:7480 ff.`
   - EP1 (≥ 0x790 B, 256-aligned)
   - Stream (z. B. W·H + 64 KiB, Offset 0x200)
4. `0x85F0 = 1`, Zone `0x8400–0x8428` schreiben, `0x85E4 = 1`, auf Slot 4 warten.
5. `0x8430/34/38` lesen und `[0x200, 0x200+len)` als `.jpg` speichern.
6. Mit `djpeg`/`ffprobe` prüfen.
7. Zu klärende Punkte: Header vorhanden (`FF D8 FF E0`)? Quant-Byte-Reihenfolge korrekt
   (sonst Blockartefakte)? Überlauf-Bit bei zu kleinem Puffer?

### Phase 2 – Byte-Abgleich gegen Stock (0,5–1 Tag)

- Mit Stock-libimp und JPEG-Kanal den Registertrace aufnehmen (WR-loggender `avpu.ko` wie
  bei `stock_logs.txt`).
- Den EP1-Bereich (Physadresse aus `WR 0x8418`, 0x790 B) per `/dev/mem`/devmem bzw. einer
  Dump-Erweiterung des Debug-Treibers beim Schreiben von `0x85E4` sichern. Die
  AVC-CL-Dump-Funktion im Trace ist das Vorbild.
- Mit dem eigenen EP1 für dieselbe Quality vergleichen.
- Optional denselben NV12-Frame durch Stock und OpenIMP schicken und die JPEGs bitexakt
  vergleichen.
- Herstellereigenes Tracing:
  - `AL_EncTrace_TraceJpeg`/`TraceJpegStatus` schreiben
    `<prefix>.cmd.hex`/`.stat.hex`/`.sram_map.hex` (L52408, L53595, L53641).
  - Aktiviert wird das über `AL_Common_Encoder_SetTraceMode` (`0x43af8`, per GOT
    erreichbar) und `SetTraceFolder`.
  - Die IMP-API ruft das nie auf. Man bräuchte ein `LD_PRELOAD`-Shim, das an das interne
    AL-Encoder-Handle des IMP-Kanals kommt. Das ist aufwendig und liefert nur dieselben 15
    Registerwerte, die der Registertrace schon zeigt.
  - Empfehlung: Registertrace plus EP1-Dump.

### Phase 3 – Integration in OpenIMP (3–5 Tage)

- Prozessweite IRQ-Slot-Tabelle (Slot 4 → JPEG-Dispatcher) neben dem AVC-Owner-Mechanismus.
- Prozessweiter Register-Mutex.
- `0x8014`-Bit 4 einmalig setzen, im Callback nie löschen.
- JPEG-Kontext je Kanal:
  - EP1 bei Kanalanlage bzw. Quality-Änderung erzeugen
  - EP1 per Writeback (`dir = 1`) flushen
  - Stream-Puffer-Ring
- Submit:
  - Quellframe-Invalidate wie im AVC-Pfad (kein Writeback auf ISP-Daten!)
  - Frame-Referenz halten
  - Core-1-Lock
  - Registersequenz unter Mutex
- Completion:
  - Status lesen
  - `[offset, offset+len)` invalidieren
  - Pack an die bestehende `GetStream`-Struktur übergeben
  - Frame freigeben
  - Überlauf behandeln (größerer Puffer, Retry oder Frame verwerfen)
- Mapping der IMP-Quality: Der IMP-JPEG-Kanal übergibt `iInitialQP` als Quality, siehe
  Abschnitt 3.2. Das Clamping 1…100 entspricht den JPEG-Defaults.
- Fallback: Software-JPEG bleibt bei Timeout (z. B. 200 ms ohne Slot 4) oder Fehler.
- Nach einem Timeout ist `0x85F0 = 1` der vermutliche Reset.

### Phase 4 – Härtung (2–3 Tage)

- Dauerlauf mit Main-H.264, Sub-H.264 und JPEG bei unterschiedlichen Raten.
- Auflösungswechsel.
- Zu kleine Puffer.
- IRQ-Zählung: Jede JPEG-Übergabe braucht genau einen Slot 4.
- AVC-Timeouts dürfen nicht zunehmen.
- CPU-Messung.
- T31-Varianten (N/L/X/A/AL/ZX) prüfen: Ist dort jeweils `numCore = 1`, also JPEG = Core 1?
  In der untersuchten libimp ist das fest verdrahtet (L53999).

**Gesamtaufwand:** ca. 7–12 Personentage mit Geräteschleife.

### Risiken (absteigend)

1. **Companion-Stage/AVC-Kopplung:** Falls der AVC-Pfad (fälschlich) von ihr abhängt, muss
   dieser Teil zuerst sauber verstanden werden. Laut Herstellercode sollte er das nicht.
2. **IRQ-Demux und Rennen auf `0x8014`:** Eine verlorene AVC-Maske bedeutet einen hängenden
   H.264-Stream. Gegenmittel: ein Mutex und Bit 4 statisch setzen.
3. **EP1-Semantik:** Byte-Reihenfolge der u8-Tabellen, Bedeutung der Kopfworte `1/0`.
   Gegenmittel: Stock-Bytes 1:1 nachbilden und abgleichen.
4. **Cache-Kohärenz:**
   - EP1-Writeback nach dem Schreiben.
   - Quellframe darf keine dirty lines haben. Wenn OpenIMP per CPU OSD in den Frame
     malt, ist vor dem Submit ein Writeback nötig.
   - Output vor dem Lesen invalidieren.
5. **Frame-Lebensdauer:** Die Hardware liest asynchron. Gibt man den Frame zu früh an die
   Framesource zurück, entstehen Tearing oder ein fremdes Bild.
6. **Header-Annahme:** Falls die Hardware wider Erwarten nur Entropie-Daten liefert,
   braucht es einen kleinen Software-Headerwriter. `hw_encoder.c` hat bereits einen. Dann
   müssen DQT und DHT exakt aus dem EP1-Inhalt stammen, und der Restart-/EOI-Abschluss ist
   zu prüfen.
7. **Unklare Felder:** `0x8430`, Bit 8 in `0x8400`, der Sinn von `0x85F0 = 1`. Für den
   Normalbetrieb genügt es, die Stock-Werte zu übernehmen.

---

## 8. Auf dem Gerät zu prüfen (Checkliste)

- [ ] H.264 ohne Companion-Stage unverändert?
- [ ] Standalone-JPEG: Ausgabe beginnt mit `FFD8 FFE0 … 'JFIF'` und endet mit `FFD9`? Ist `0x8434` = Dateilänge?
- [ ] EP1 (Stock) = EP1 (eigen) byteweise, für Q = 50/75/90 und für Mono?
- [ ] Wirkt `0x8400[31:16]` als Restart-Intervall (DRI/RSTn im Strom)? Wirkt `[13:12]` als JFIF-Unit-Feld?
- [ ] Welche Bits hat `0x8438` außer Bit 1? Was steht in `0x8430`?
- [ ] Überlauf: kleines `0x8428` → Bit 1 gesetzt, IRQ kommt trotzdem?
- [ ] Hält der gleichzeitige Betrieb mit Main- und Sub-H.264 über Stunden, ohne AVC-Timeouts?
- [ ] Mindestausrichtung von Y/UV/Stream (256 B beobachtet) und zulässige Breite/Höhe (Vielfache von 16? ungerade Höhen?).

---

## Anhang A – Fundstellen (HLIL `libimp.so_hlil.txt`)

| Funktion/Daten | Zeile | Adresse |
|---|---|---|
| `AL_sSettings_SetDefaultJPEGParam` | 43966 | `0x3dd84` |
| `AL_Settings_SetDefaultRCParam` | 43978 | `0x3ddb8` |
| `AL_GetAllocSizeEP1` (=0x6400) | 43022 | `0x3c300` |
| EP1-Alloc / `preprocessEp1`-Aufruf | 48506 / 48596 | `0x45c00` / `0x45f8c` |
| JPEG `preprocessEp1` | 51587 | `0x4b1e0` |
| `ConfigureChannel` (`setDensity`) | 51629 | `0x4b370` |
| `AL_CreateJpegEncoder` | 51658 | `0x4b438` |
| `JpegTables_InitQuant` | 51669 | `0x4b490` |
| `JpegTables_InitHuffman` | 51767 | `0x4b64c` |
| `TraceJpegCmd` | 52408 | `0x4c8ec` |
| `AL_EncTrace_TraceJpeg`/`…Status` | 53595 / 53641 | `0x4f7e4` / `0x4fa44` |
| `AL_SchedulerCpu_Create` (numCore=1) | 53999 | `0x507d4` |
| `getCompatibleCores` | 65725 | `0x61230` |
| `CheckAndEncode` (GC on, Launch) | 66126–66144 | `0x61fec–0x620d0` |
| `HandleCoreInterrupt` | 66534 | `0x6290c` |
| `AL_SchedulerEnc_Init` (Core-Init, JPEG-Core) | 66566 / 66623 / 66635 | `0x62aa4` / `0x62c78` / `0x62d1c` |
| `EndJpegEncoding` | 70417 | `0x68b70` |
| `AL_EncChannel_PushIntermBuffer` (EP1-Kopie) | 71346 | `0x6a4c0` |
| EP1-Adresse je Request | 71694 | `0x6a8c0` |
| `SetJpegParam` | 71843–71936 | `0x6b33c–0x6b59c` |
| JPEG-Zweig in `AL_EncChannel_EndEncoding` | 72033 | `0x6b870` |
| `StartEnc1WithCommandList` / `ResetCore` / `SetClockCommand` | 72374 / 72382 / 72391 | `0x6c320` / `0x6c398` / `0x6c428` |
| `WriteZoneRegisters` | 72468 | `0x6c758` |
| `AL_EncJpegCore_Init` | 72491 | `0x6c7fc` |
| `AL_EncCore_Init` | 72514 | `0x6c8d8` |
| `AL_EncCore_EncodeJpeg` | 72713 | `0x6cdc8` |
| `AL_EncCore_ReadStatusRegsJpeg` | 72837 | `0x6d32c` |
| `AL_EncCore_SetJpegInterrupt` | 72854 | `0x6d3f8` |
| `JpegStatusToStatusRegs` | 73244 | `0x6dce0` |
| `JpegParamToCtrlRegs` / `CtrlRegsToJpegParam` | 73828 / 73857 | `0x6f5c4` / `0x6f6e8` |
| `ZIGZAG_SCAN`, Quant- und Huffman-Tabellen | 131064–131110 | `0xe2fc0–0xe35f0` |
| `AL_ENCJPEG_STATUS` / `AL_ENCJPEG_CMD` | 131462 / 131463 | `0xe3c28` / `0xe3c30` |

## Anhang B – OpenIMP-Fundstellen

- `src/t40/codec-t40.c`:
  - Registerdefinitionen `:403–455`
  - Cache-ioctl `:390, 478–560`
  - Allokatoren `:627–720`
  - IRQ-Enable `:4210`
  - Callback-Registrierung und IRQ-Thread `:5035–5170`
  - Companion-Stage `:7515–7584` (Aufruf `:9305`, T41 `:9198`)
  - Quellframe-Invalidate `:7650`
  - AVC-Init-Kommentar mit Stock-Sequenz `:8660–8693`
  - JPEG → Software `:8590`
  - T31-Serialisierung `:9470`
- `src/al_avpu.c`: IpCtrl/Waiter/EncCore-Nachbau (Slots `core*4`, `core*4+2`).
- `src/hw_encoder.c:25–85` (Legacy-Probe mit rohem `open("/dev/avpu")` und `VENC_IOCTL_INIT`), `:915` (Software-JPEG).
- `avpu/t31/avpu_ip.c` (Bind/Unbind, Hardirq), `avpu/t31/avpu_main.c` (ioctls, Registerfenster).
- `stock_logs.txt` Z. 80–700 (Init, AVC-CL, JPEG-Sequenzen, Masken- und Timing-Belege).
