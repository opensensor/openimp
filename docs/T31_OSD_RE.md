# T31 OSD in der Hersteller-libimp: Reverse Engineering und Umsetzungsplan für OpenIMP

Stand: 2026-09-29. Reine Recherche, kein Repository wurde verändert.

Markierungen: **[V]** = verifiziert (Quelle direkt gelesen), **[I]** = abgeleitet/plausibel,
**[?]** = offen, nur auf dem Gerät zu klären.

Quellen:
- `H:` = `docs/re/libimp.so_hlil.txt` (Binary-Ninja-HLIL der T31-libimp, Zeilennummern
  plus Adresse)
- `OI:` = OpenIMP (Fork `Lu-Fi/openimp`, Branch `claude/t31-hw-jpeg`, Commit `01e7eff`)
- `K:` = IPU-Kerneltreiber (GPL) aus Thingino,
  thingino-firmware (Branch `aperto`) `package/all-patches/linux/3.10.14/0001-from-3.10.14-to-thingino.patch`
  (`drivers/video/jz_ipu/jz_ipu_v13.{c,h}`, `jz_regs_v13.h`; im Patch ab Zeile 2050755 / 2051895 /
  2052367) sowie `0007-ipu-wedge-mitigation.patch`
- `VH:` = Hersteller-Header T31 1.1.6, ingenic-headers `T31/1.1.6/en/imp/` (timps-Submodul `include/`)
- `TP:` = timps-Quellen timps (`Lu-Fi/timps`) `src/hal/`

---

## 0. Kurzfassung

- **Das OSD ist gemischt.** `OSD_REG_PIC`, `OSD_REG_PIC_RMEM` und `OSD_REG_COVER` blendet der
  **Hardware-IPU-Block** (`/dev/ipu`, Kerneltreiber `jz_ipu_v13`) direkt in den NV12-Frame
  (in-place). `OSD_REG_LINE`, `OSD_REG_RECT` und `OSD_REG_BITMAP` zeichnet die **CPU** deckend in
  den NV12-Frame. **[V]**
- Weder der ISP (tx-isp) noch der AVPU-Encoder haben dabei eine Rolle. `open-tx-isp` hat für T31
  keine OSD-Schnittstelle. **[V]**
- Der Aufruf pro Frame: Ein Thread mit dem Namen `group_update` liefert den Frame an die
  OSD-Gruppe. Dort laufen `osd_update` → `osd_process` → `OSD_Draw_Layer_Cover_Pic` →
  `ipu_osd` → `ioctl(/dev/ipu, IOCTL_IPU_START)`. Danach geht der Frame an den Encoder. **[V]**
- **Warum die Messung ~1 % pro Region und Stream zeigt, fast unabhängig von der Fläche:** Die libimp
  macht für normale PIC- und COVER-Regionen **pro Region und pro Frame einen eigenen IPU-Lauf**
  (Batch-Größe 1). Jeder Lauf kopiert die Bitmap erneut in einen IPU-Puffer und macht einen
  Cache-Flush über **fest 1 MiB** (`IOCTL_IPU_BUF_FLUSH_CACHE`). Danach folgen ioctl, IRQ und das
  Warten. Diese Fixkosten fallen bei statischem Text genauso an wie bei wechselndem. **[V]** für
  den Mechanismus, **[I]** für die Kostenverteilung.
- **OpenIMP:** Die OSD-Gruppe liegt heute gar nicht im Frame-Pfad. Es gibt aber einen idealen Hook
  (`OI: src/t40/openimp_p2_encoder.c:1213–1239`, zwischen `IMP_FrameSource_GetFrame` und
  `AL_Codec_Encode_Process`). Außerdem sind die OSD-Strukturen in OpenIMP **ABI-inkompatibel**
  zu den Hersteller-Headern, mit denen die Streamer gebaut werden. `IMP_OSD_GetRgnAttr`
  überschreibt den Stack des Aufrufers. **[V]**
- **Machbarkeit: gut.** Empfohlen wird ein IPU-Backend nach dem Vorbild der libimp, aber
  effizienter: Bitmaps liegen dauerhaft im DMA-Speicher, bis zu 4 Layer pro IPU-Lauf, kein
  Flush pro Frame. Dazu kommt ein kleines CPU-Backend für LINE/RECT/BITMAP (und als Fallback).
  **Aufwand ca. 7–10 Personentage** inklusive Gerätetests.

---

## 1. Wo wird OSD angewandt? (Frage 1)

### 1.1 Befund pro Regionstyp

| Regionstyp (`VH: imp_osd.h`) | Wo | Beleg |
|---|---|---|
| `OSD_REG_PIC` (5) | IPU-Hardware, Bitmap wird pro Frame in den IPU-Puffer kopiert | `H:111932–111978` (Layer-Aufbau), `H:110515/110560/110615/110665` (memcpy in `ipu_osd`) **[V]** |
| `OSD_REG_PIC_RMEM` (6) | IPU-Hardware, Bitmap liegt im OSD-Pool (rmem), physische Adresse wird direkt übergeben, bis zu 4 Layer pro Lauf | `H:111978` (`osd_mem_vir_to_phy`), `H:111549f` (`var_4c = 4` bei Typ 6) **[V]** |
| `OSD_REG_COVER` (4) | IPU-Hardware im **Masken-Modus** (einfarbig, ohne Speicherzugriff) | `H:111704–111777`, `H:110487` (`_ipu_set_osdx_mask` bei Daten-Pointer 0) **[V]** |
| `OSD_REG_RECT` (2) | CPU: 4 × `osd_draw_line` | `H:112464–112520` **[V]** |
| `OSD_REG_LINE` (1) | CPU: `osd_draw_line` | `H:112747` **[V]** |
| `OSD_REG_BITMAP` (3) | CPU: Schleife über 8-Bit-Maske | `H:112554–112745` **[V]** |

### 1.2 Hardware-Block: IPU (`/dev/ipu`)

- `ipu_init` öffnet `open("/dev/ipu", O_RDWR)` (`H:110326`). Die Fehlermeldung lautet
  `"ipu: Cann't open /dev/ipu"`. **[V]**
- ioctl-Nummern in `ipu_osd`: **[V]**
  - `0x20004976` (`H:110684`) = `_IO('I', 118)` = `IOCTL_IPU_BUF_FLUSH_CACHE`. Argument
    `{void *addr = ipu_vbuf, unsigned size = 0x100000}`. Der Kernel ruft damit
    `dma_cache_sync(addr, size, DMA_BIDIRECTIONAL)` auf (K: `ipu_ioctl`).
  - `0x2000496a` (`H:110690`) = `_IO('I', 106)` = `IOCTL_IPU_START`. Argument: eine 0x98 Byte
    große `struct ipu_param` (siehe §3.1). Der Kernel kopiert sie, programmiert die Register,
    startet die IPU und wartet synchron auf den OUT_END-IRQ (K: `ipu_start`).
  - Die MIPS-Kodierung von `_IO` hat Bit 29 gesetzt, daraus ergibt sich `0x2000_49xx`.
- Kernel-Konfiguration: T31-Kernel 3.10.14 in Thingino haben `CONFIG_JZ_IPU=y`,
  `CONFIG_JZ_IPU_V13=y`
  (`thingino-firmware/board/ingenic/xburst1/kernel/3.10.14/t31.generic.config:1185–1187`). **[V]**
  Der Treiber registriert das Misc-Device `ipu` (K: `ipu_probe`, `sprintf(ipu->name,"ipu")`). **[V]**
- Registerblock (K: `jz_regs_v13.h`): OSD-Kanäle 0–3 haben `IPU_OSD_IN_CHx_{Y,UV}_{ADDR,STRIDE}`
  (0xbc–0xf8), `IPU_OSD_CHx_GS` (0xfc–0x108), `IPU_OSD_CHx_POS` (0x10c–0x118),
  `IPU_OSD_CHx_PARA` (0x11c–0x128), `IPU_OSD_CH_BK_PARA` (0x12c) und `IPU_OSD_CHx_BAK_ARGB`
  (0x130–0x13c). Diese Register programmiert nur der Kernel, der Userspace sieht nur das
  ioctl-ABI. **[V]**
- Der Hintergrund ist der NV12-Frame selbst. Eingang und Ausgang sind **dieselbe physische
  Adresse**: Y bei `bg_buf_p`, UV bei `bg_buf_p + bg_w*bg_h`, Stride = `bg_w`
  (K: `_ipu_set_bg_buffer`, `_ipu_set_bg_route`). Das OSD ist also in-place. **[V]**
- Der Userspace schreibt dabei **keine** Framedaten per CPU. Der Frame wird nur über seine
  physische Adresse (`frame+0x18`) übergeben (`H:111704–111708`, `var_1c8 = *(frame+0x18)`). **[V]**

### 1.3 Keine ISP- oder Encoder-Beteiligung

- In der T31-HLIL gibt es keine `*_ISP`-OSD-Funktionen (grep nach `osd.*isp`/`IMPIspOsd` ist
  leer). **[V]**
- `open-tx-isp/driver/t31` hat nur ein ungenutztes Feld `ipu_clk`
  (`driver/t31/include/tx_isp_core_device.h:31`) und keine OSD-ioctls. **[V]**
- Der AVPU-Encoder bekommt den fertig überlagerten Frame. **[V]** (Reihenfolge siehe §2)

### 1.4 CPU-Zeichenpfad (LINE/RECT/BITMAP)

- Er schreibt direkt über die virtuelle Frameadresse (`frame+0x1c`). Der Stride ist
  `align16(width)`, die UV-Ebene beginnt bei `align16(height) * stride` (`H:111105–111182`,
  `H:112736f`). **[V]**
- Er ist deckend, ohne Alpha-Blending (Details §3.6).
- Die CPU zeichnet **vor** dem IPU-Pass: `osd_process` ruft `OSD_Draw_Layer_Cover_Pic` erst nach
  der Schleife auf (`H:112756`). **[V]**
- Ob der Hersteller dazwischen Cache-Wartung macht, ist offen **[?]**. In `osd_process` ist kein
  Flush sichtbar. Vermutlich ist die Frame-Abbildung der Hersteller-VBM ungecacht, oder der
  Effekt ist nie aufgefallen.

---

## 2. Datenfluss pro Frame (Frage 2)

### 2.1 Graph und Thread

1. `IMP_OSD_CreateGroup(n)` (`H:112813`):
   - erlaubt nur `n < 4` (`H:112815`) **[V]**
   - ruft `create_group(4 /*DEV_ID_OSD*/, n, "<name>-n", osd_update)` auf (`H:112837`) **[V]**
   - legt 512 Knoten-Slots für die Gruppe an (`H:112840ff`) **[V]**
2. `IMP_System_Bind(FS→OSD)` und `(OSD→ENC)` hängen die Gruppen als Beobachter ein. Der
   generische `group_update` (`H:21988–22005`):
   - setzt den Threadnamen per `prctl(PR_SET_NAME, "group_update")`
   - ruft den Update-Callback der Gruppe auf (für OSD `osd_update`)
   - gibt den Frame bei Fehler oder fehlenden Beobachtern per `VBMReleaseFrame` zurück **[V]**

   Daher kommt das „group_update“-Threadpaar aus der Messung: ein Thread pro Stream, der die
   Frames von der FrameSource an die jeweilige OSD-Gruppe übergibt. **[I]**
3. `osd_update(module, frame)` (`H:112761–112791`):
   - ruft `osd_process(module, frame, 0)` **synchron** auf **[V]**
   - reicht den Frame dann an die Beobachter weiter (Encoder-Gruppe), bei mehreren Beobachtern
     mit `VBMLockFrame` **[V]**
   - Das OSD ist damit fertig, bevor der Encoder den Frame sieht.
4. Sonderpfad `osd_update_left` (`H:112793–112811`):
   - Frames mit Flag-Byte `frame+0x3e4 == 1` sind „Low-Latency/Teilframes“. Hier liest die
     libimp über `read_reg_32_addr(*(frame+0x3e8))` den Zeilenzähler des ISP und wartet, bis
     die Regionszeilen geschrieben sind („OSD wait too long yAvail…“, `H:112106–112160`). **[V]**
   - OpenIMP liefert immer fertige Frames, dieser Pfad ist also **nicht nötig**. **[I]**

### 2.2 `osd_process` (`H:112373–112759`)

- Sie nimmt das Semaphor `semRgnList` (Gerätebasis +0x2b0a0, `H:112378`). Dieses Semaphor nimmt
  jede API-Funktion, die Regionsdaten ändert: `IMP_OSD_UpdateRgnAttrData` (`H:113457`),
  `IMP_OSD_SetRgnAttr` (`H:113797ff`), `IMP_OSD_ShowRgn` (`H:114867`) und `IMP_OSD_CreateRgn`.
  Die Verarbeitung pro Frame und alle API-Updates sind damit gegenseitig ausgeschlossen. Ein
  API-Aufruf blockiert maximal für die Dauer eines Gruppen-Passes (inklusive IPU-Laufzeit). **[V]**
- Sie bearbeitet die Gruppe nur, wenn sie „gestartet“ ist. `IMP_OSD_Start`/`Stop` setzen nur
  dieses Flag (`H:114896`). **[V]**
- Sie läuft über die Liste der Knoten der Gruppe. Die Liste ist **nach `layer` aufsteigend
  sortiert**:
  - `IMP_OSD_RegisterRgn` hängt einen Knoten am Ende an (`H:113282ff`).
  - `IMP_OSD_SetGrpRgnAttr` sortiert ihn nach `layer` um (`H:114648ff`).
  - Spätere Knoten liegen oben. **[V]** für die Sortierung, **[I]** für „aufsteigend = oben“.
- Regionen mit `show == 0` überspringt sie (`H:112462`). **[V]**
- Für jede sichtbare Region vom Typ LINE, RECT oder BITMAP zeichnet sie sofort per CPU.
  Am Ende ruft sie `OSD_Draw_Layer_Cover_Pic(frame, liste, osd, 0)` für COVER, PIC und PIC_RMEM
  auf (`H:112756`). **[V]**
- Bei `SetRgnAttrWithTimestamp` hängt eine Timestamp-FIFO an der Region. Die Region gilt nur für
  Frames, deren Zeitstempel (`frame+0x20/0x24`) im Fenster `[ts-minus, ts+plus]` liegt
  (`H:112386–112455`). **[V]** Für die erste Version ist das optional.

### 2.3 `OSD_Draw_Layer_Cover_Pic` (`H:111495–112371`)

- Sie baut eine 0xa8 Byte große Userspace-Struktur mit einem Kopf und 4 Layer-Slots zu je 0x24
  Byte. Die Felder pro Layer: fmt, para, x, y, w, h, vaddr, paddr, color. **[V]**
- Sie setzt das Enable-Bit `1<<slot` (`H:111704`). **[V]**
- Kopf: **[V]**
  - `bg_w` = `align16(frame.width)`
  - `bg_h` = `align16(frame.height)`
  - Bei `frame+0x28 == 1` werden Höhe und Breite getauscht, vermutlich für gedrehte Frames
    (`H:111558–111566`). **[I]** für die Bedeutung.
  - `bg_fmt` = `0x18` (HAL NV12)
  - `bg_buf_p` = `frame+0x18`
- **Batch-Größe** `var_4c`:
  - Sie ist 1, außer im Low-Latency-Modus oder wenn eine Region vom Typ `PIC_RMEM` vorkommt.
    Dann ist sie 4 (`H:111503–111506`, `H:111549f`). **[V]**
  - Nach jedem Layer ruft sie `ipu_osd()` auf, sobald `var_50 >= var_4c`, und setzt die Struktur
    zurück (`H:112067–112100`). **[V]**
  - Für normale PIC- und COVER-Regionen bedeutet das **einen IPU-Lauf pro Region**.
- Prüfung pro Region:
  - Es wird nur geprüft, ob die **kleinste** Koordinate im Bild liegt: `min(p0.x,p1.x) <= w-1`
    und `min(p0.y,p1.y) <= h-1`. Sonst kommt die Meldung „invalid param … keep the smallest
    coordinates within the picture range“ (`H:111588`). **[V]**
  - Ein Überhang nach rechts oder unten wird **nicht** abgeschnitten, sondern an die IPU
    durchgereicht. **[V]**
  - Der Fehler zeigt sich erst in der IPU, und zwar in jedem Frame (vgl. `TP: imp_osd.c:222–233`).
    **[I]**
  - `offPos` aus `IMPOSDGrpRgnAttr` geht nur in diese Prüfung ein. Die Layer-Position ist
    `min(p0,p1)` **ohne** `offPos` (`H:111975ff`, `H:111770ff`). **[I]**
  - Im CPU-Pfad (LINE, RECT, BITMAP) wird `offPos` addiert. **[V]**
- Debug-Detail: Existiert `/tmp/mountdir/osdsnap<handle>.pic`, schreibt die libimp die Bitmap der
  Region hinein (`H:111983ff`). **[V]**

### 2.4 `ipu_osd` (`H:110415–110699`): Puffer und Locking

- Sie nimmt den prozessweiten `ipu_mutex`. `ipu_buf_lock`/`unlock` sind leer (`H:110387–110395`).
  **[V]**
- IPU-Scratchpuffer ist der OSD-Pool:
  - Er wird in `OSDInit` angelegt (`H:111395`) mit `IMP_Alloc(alloc_ipu, pool_size, "osdDev")`,
    also physisch zusammenhängend (rmem).
  - Die Größe setzt `IMP_OSD_SetPoolSize`. Der Standardwert ist winzig, weshalb timps ihn
    explizit setzt (`TP: hal_ingenic.c:1163–1176`).
  - `ipu_init` übernimmt virtuelle Adresse, physische Adresse und Größe (`H:110330–110337`).
    **[V]**
- Für jeden Layer mit `vaddr != 0 && paddr == 0` (normale PIC): `memcpy` der ganzen Bitmap in den
  Pool, 4-Byte-aligned hintereinander, und Übergabe der physischen Pool-Adresse
  (`H:110515`, `H:110560`, `H:110615`, `H:110665`). **[V]**
  - Reicht der Pool nicht: `"ipu buffer too small, OSD need Buffer size is %d"`
    (`H:110461`). **[V]**
  - Identische Layer in einem Batch teilen sich die Kopie (`chx_share_osd_mem`,
    `H:110397–110413`). **[V]**
- Layer mit `vaddr == 0` (COVER) laufen über `_ipu_set_osdx_mask` und die Farbe im Feld
  `bak_argb` (`H:110487`). **[V]**
- Danach folgt immer `IOCTL_IPU_BUF_FLUSH_CACHE` über `{ipu_vbuf, 0x100000}` und dann
  `IOCTL_IPU_START` (`H:110681–110690`). **[V]**

### 2.5 Wie Attribute angewendet werden, und `UpdateRgnAttrData`

- `IMP_OSD_CreateRgn` und `IMP_OSD_SetRgnAttr` **kopieren die Bitmap in einen eigenen Puffer der
  libimp**:
  - `calloc(w*h*bpp)` für PIC und BITMAP (`H:113196`)
  - `OSD_mem_alloc` aus dem OSD-Pool für PIC_RMEM (`H:113184`)
  - Kopie per `memcpy` (`H:113052`)

  Der Aufrufer darf seinen Puffer danach freigeben. **[V]**
  - bpp auf T31: 4 für 32-Bit-Formate, 2 für `PIX_FMT_RGB555LE`/`BGR555LE`, 1,5 für NV12/NV21
    (`H:113131`, `H:113180`).
  - Konvertierung `Convert1555To8888` (`H:110852`) gibt es nur auf T10/T20
    (`get_cpu_id() < 3`), auf T31 wird roh kopiert. **[V]**
- `IMP_OSD_UpdateRgnAttrData` (`H:113436–113751`) kopiert die neuen Daten **in-place** in den
  vorhandenen Regionspuffer (Größe aus dem gespeicherten Rechteck). Dabei hält sie das
  Semaphor `semRgnList` (`H:113457–113470`). Bei LINE, RECT und COVER aktualisiert sie nur
  `color`/`linewidth`. **[V]**
  - Es gibt **kein Double-Buffering**. Tearing wird durch das gemeinsame Semaphor verhindert,
    und die IPU arbeitet ohnehin auf einer Kopie pro Frame. **[V]**/**[I]**
- `IMP_OSD_ShowRgn` setzt `show` im Gruppenknoten. Beim Ausblenden leert es die
  Timestamp-FIFO (`H:114867`). **[V]**
- Die Parameter des Gruppenknotens aus `IMPOSDGrpRgnAttr`: `show`, `offPos`, `scalex/y`,
  `gAlphaEn`, `fgAlhpa`, `bgAlhpa`, `layer`. Im Knoten stehen sie als Wörter [2..10]. Genutzt
  werden `show`, `offPos` (nur CPU-Pfad und Prüfung), `gAlphaEn`, `fgAlhpa` und `layer`.
  `scalex`, `scaley` und `bgAlhpa` werden im Zeichenpfad **nicht** gelesen. **[I]** (in den
  gelesenen Funktionen nicht referenziert)
- Grenzen: 4 Gruppen (`H:112815`), 512 Regionen (`H:111453`, `H:112840`). **[V]**

---

## 3. Regionstypen, Formate, Mathematik (Frage 3)

### 3.1 ioctl-ABI `IOCTL_IPU_START` (Schnittstellenfakten aus K: `jz_ipu_v13.h`)

`struct ipu_param` ist 38 × u32 = 0x98 Byte groß. Die libimp baut exakt diese Größe
(`H:110426`, `memset(&str,0,0x98)`). **[V]**

| Wortindex | Feld | libimp-Belegung für OSD |
|---|---|---|
| 0 | `cmd` | Bit 0–3 = OSD-Kanal 0–3 aktiv (`IPU_CMD_OSD0..3`), Bit 4 = CSC (nicht OSD) |
| 1, 2 | `bg_w`, `bg_h` | `align16(w)`, `align16(h)` = Stride und Y-Höhe des NV12-Frames |
| 3 | `bg_fmt` | `0x18` (HAL NV12) |
| 4 | `bg_buf_p` | physische Adresse des Frames (in-place) |
| 5 | `out_fmt` | ebenfalls `0x18` |
| 6 + 8k … 13 + 8k | Kanal k: `fmt`, `para`, `bak_argb`, `pos_x`, `pos_y`, `src_w`, `src_h`, `buf_p` | siehe unten |

- Der Kernel berechnet `src_w*4` (RGB) bzw. `src_w*2` (5551) als Zeilen-Stride der Bitmap. Die
  Bitmap muss also **ohne Padding** vorliegen. Die UV-Adresse eines NV12-Layers ist
  `buf_p + w*h` (K: `_ipu_set_osd_chx_route`, `_ipu_set_osdx_buffer`). **[V]**
- Ist `para == 0`, setzt der Kernel einen Standardwert. **[V]**
- Der Kernel schneidet **nicht** ab und prüft keine Grenzen. **[V]**

### 3.2 Das `para`-Wort pro Kanal (T31-Pfad)

Die Bitbelegung folgt `CH_BK_PARA` in K: `jz_regs_v13.h`, analog für die Kanäle. **[I]**

- **Bit 0:** Kanal an.
- **Bit 1–2:** Alpha-Modus. 0 = Pixel-Alpha, 1 = globales Alpha, 2 = Pixel-Alpha × globales Alpha.
- **Bit 3–10:** globales Alpha.
- **Bit 11–13:** Bildtyp. 0 = ARGB-Familie, 1 = 16-Bit-5551, 2 = NV12, 3 = NV21.
- **Bit 14–17:** Byte-Reihenfolge. 0 = ARGB, 5 = ABGR, 8 = RGBA, 0xD = BGRA.
- **Bit 23:** Masken-Modus (Farbe aus `bak_argb`). **[I]**
- **Bit 24–25:** 2 bei RGB-Quellen, 1 bei NV12/NV21-Quellen. Bedeutung **[?]**, 1:1 übernehmen.

T31 wird so erkannt: `get_cpu_id()` liefert für Soc-ID 0x31 die Werte 15–23 (`H:26100–26140`).
Die T31-Äste sind `label_bd830`, `label_bdad8`, `label_c0f20` und `label_c1138`. **[V]**

**Bitmap-Layer** (`_ipu_set_osdx_para`, `H:110159–110315`; T31-Ast `H:110205–110222`,
Ergebnis `H:110313`) **[V]**:
- Der Aufrufer übergibt in `para` einen Wert `alpha | 0x100`, wenn `gAlphaEn <= 0`, sonst
  `fgAlhpa` (`H:111934`).
- Mit `0x100` (Pixel-Alpha): Grundwert `0x020347F9`.
- Ohne `0x100` (globales Alpha): `0x020347FB` für NV12/NV21-Layer (Modus 1), sonst
  `0x020347FD` (Modus 2 = Pixel × global).
- Danach werden Bit 11–17 und 24–25 je nach Format ersetzt und
  `para = (alpha & 0xff) << 3 | (v & 0xfffff807)` gesetzt.

**Masken-Layer / COVER** (`_ipu_set_osdx_mask`, `H:110097–110157`; T31: `H:110145–110155`) **[V]**:
- `((alpha<<3) | 0x02034005) & 0xfffc3fff | 0x00800000` bei `gAlphaEn > 0`
  (Modus 2 = Pixel × global).
- `((alpha<<3) | 0x02034001) & 0xfffc3fff | 0x00800000` sonst (Modus 0 = Pixel-Alpha).

### 3.3 Pixelformate für PIC (Tabelle `CSWTCH.41`, `H:137486`, Nutzung `H:111928`)

IMP-Enum laut `VH: imp_common.h:93–139`, fortlaufend ohne explizite Werte **[V]**:

| IMP `PIX_FMT_*` (Wert) | HAL/IPU-Code | Bedeutung (K) | Bitmap-Bytes pro Pixel |
|---|---|---|---|
| `ARGB` (14) | 9 | ARGB_8888 | 4 |
| `RGBA` (15) | 1 | RGBA_8888 | 4 |
| `ABGR` (16) | 8 | ABGR_8888 | 4 |
| **`BGRA` (17)** | **5** | **BGRA_8888, im Speicher B,G,R,A = little-endian `0xAARRGGBB`** | 4 |
| `RGB555LE` (21) | 6 | RGBA_5551 | 2 |
| `BGR555LE` (25) | 0x1a | BGRA_5551 | 2 |
| `NV12` (10) / `NV21` (11) | 0x18 / 0x19 | YUV-Layer | 1,5 |
| `YUV420P` (0) | 0x13 | wird von `_ipu_set_osdx_para` abgelehnt („ipu: err osd fmt not support“) | – |
| alle anderen < 26 und ≥ 26 | 5 (wie BGRA) | – | 4 |

Die Spalte „Bytes pro Pixel“ ist **[V]**. Die Aufschlüsselung der ARGB-Reihenfolge folgt
K: `jz_regs_v13.h` `CH_BK_ARGB_TYPE_*`. **[V]**

Bei COVER **muss** `fmt == PIX_FMT_BGRA (17)` sein, sonst kommt „COVER cannot support this
format“ (`H:111762`). **[V]**

Praxis: prudynt und timps nutzen `OSD_REG_PIC` mit `PIX_FMT_BGRA` und `OSD_REG_COVER` mit
`PIX_FMT_BGRA` (`TP: imp_osd.c:348–353, 432–438`). **[V]**

### 3.4 Alpha-Logik

- **PIC:**
  - Mit `gAlphaEn == 0` zählt nur das Alpha pro Pixel aus der Bitmap. `fgAlhpa` steht zwar im
    Feld, wird bei Modus 0 aber von der HW ignoriert **[I]**.
  - Mit `gAlphaEn > 0` gilt Pixel-Alpha × `fgAlhpa` (RGB) bzw. nur `fgAlhpa` (NV12-Layer).
    **[V]** für die Bits, **[I]** für die HW-Semantik.
- **COVER:** Die Farbe `0xAARRGGBB` wird in der libimp nach „AYUV“ umgerechnet (§3.5).
  - Bei `gAlphaEn > 0` wird zuerst `A' = min(255, ((fgAlhpa+1) * A) >> 8)` gerechnet
    (`H:111737`), dann Modus 2 mit global = `fgAlhpa`. Effektiv wirkt `fgAlhpa` also doppelt.
    **[V]**/**[I]**
  - Bei `gAlphaEn == 0` bleibt `A` unverändert (Modus 0).
- Genaue HW-Mischung und Rundung (etwa `(a*fg + (255-a)*bg + 127)/255` oder `>>8`, und ob das
  `CH_BK_PREM`-Bit wirkt, das der Kernel bei OSD setzt) sind **[?]**. Das ist nur per Gerätetest
  bestimmbar (§5.6).

### 3.5 Farbkonvertierung in der libimp (COVER-Farbe, LINE/RECT-Farbe)

Aus `H:111715–111730` (Cover) und `H:111100–111110` (Linie) **[V]**. Mit R, G, B aus
`0xAARRGGBB` gilt in Ganzzahlen, `>>` arithmetisch:

```
Y = (66*R + 129*G + 25*B + 128) >> 8            // kein +16!
U = ((-38*R - 74*G + 112*B + 128) >> 8) + 128
V = ((112*R - 94*G - 18*B + 128) >> 8) + 128
Wort = A<<24 | Y<<16 | U<<8 | V
```

- Dem Y fehlt der übliche Offset +16 von BT.601. Weiß ergibt Y=219, Schwarz Y=0. Das ist so im
  Code, **[V]**. Ob die IPU bei Masken noch einen Luma-Offset addiert (Register
  `CSC_OFSET_PARA` = 0x10/0x80), ist **[?]**.
- Für BGRA-Bitmaps rechnet die **IPU selbst** RGB nach YUV. Die Koeffizienten setzt der Kernel
  (K: `_ipu_set_osd_chx_route`, ca. ×1024):
  - Y ≈ 0,299 R + 0,587 G + 0,114 B
  - U ≈ −0,147 R − 0,289 G + 0,436 B
  - V ≈ 0,615 R − 0,515 G − 0,100 B
  - Offsets 16 und 128

  Das ist eine Mischform (analoges YUV mit Luma-Offset). **[V]** für die Konstanten,
  **[?]** für die exakte Wirkung. Relevant ist das nur, wenn ein CPU-Fallback farbgleich sein
  soll.

### 3.6 CPU-Pfade (LINE, RECT, BITMAP)

**LINE/RECT** (`osd_draw_line`, `H:110885–111389`):
- Gezeichnet wird nur, wenn `fmt == PIX_FMT_MONOWHITE (8)` (`H:111098`). Das Hersteller-Sample
  nutzt genau das. **[V]** für die Bedingung, **[I]** dafür, dass sonst nichts gezeichnet wird.
- Farbe nach §3.5, **deckend** geschrieben:
  - Y-Byte pro Pixel
  - UV-Paar an `(x & ~1)` in Chroma-Zeile `y/2` (`H:111182ff`) **[V]**
- Linienbreite `linewidth`, zentriert (Start `pos - lw/2`).
- Fast waagerechte oder senkrechte Linien (`|dx| < 2*lw`) werden als Rechteck gefüllt, sonst
  als DDA mit Float-Steigung. **[V]**/**[I]**
- RECT = 4 Linien (`H:112502–112530`).
- Koordinaten + `offPos`, abgeschnitten auf `[0, w-1] × [0, h-1]`.

> **Abweichung in OpenIMP** (Branch `claude/osd-thick-lines`): Die
> Stock-Stempelkette ist quer zur Linie **nicht** `lw` breit, sondern
> `lw*cos(θ) + (lw-1)*sin(θ)` — bei 45° genau `2*lw - 1` Zeilen = `1,41*lw` (bei `lw = 8`
> gemessen 10,6 px statt 8). OpenIMP zeichnet Diagonalen deshalb **ab `lw >= 2`** als Bande der
> Breite `lw` (`round(lw*hypot/major)` Pixel pro Schritt, nur Integer-Arithmetik). Unverändert
> bzw. byte-gleich zum Stock bleiben `lw = 1` (jede Richtung), exakt waagerechte und senkrechte
> Linien und damit RECT. Die Änderung ist bewusst und sichtbar: Diagonalen werden **dünner** als
> im Stock gezeichnet. Details, Messwerte und der noch offene Gerätetest:
> `docs/archive/HANDOVER_OIMP_OSDTHICK.md`.

**BITMAP** (`H:112554–112745`):
- 1 Byte pro Pixel (Größe `w*h`, `H:113470`).
- Für jedes Byte `!= 0` wird `Y := Byte` und an der Chroma-Position `(x, y/2)` `0x80`
  geschrieben. Das ergibt eine graue bzw. weiße Maske ohne Alpha. **[V]**

### 3.7 Grenzen und Fallen (Gerät, aus timps belegt)

- Im gedrehten Modus (`frame+0x28`) scheitert der IPU-Pass bei ungeraden Breiten oder Höhen, und
  ein fehlerhafter Layer „vergiftet“ den ganzen Gruppenpass (`TP: imp_osd.c:202–207`).
  **[V als Gerätebeobachtung von timps]**
- Regionen außerhalb des Bildes erzeugen den IPU-Fehler in jedem Frame
  (`TP: imp_osd.c:222–233, 327–331`).
- IPU-„Wedge“ etwa alle 500k Operationen (T20-Familie). Thingino patcht den Treiber mit 100 ms
  Timeout, Reset und Retry (`0007-ipu-wedge-mitigation.patch`), gleicher Treiber für T31. **[V]**

### 3.8 Deutung der Messung (~0,7–1,1 % pro Region und Stream)

Pro normaler PIC- oder COVER-Region und Frame fallen an (§2.3, §2.4): ein `memcpy` der Bitmap,
ein `dma_cache_sync` über **1 MiB** (auf MIPS r4k bei Größe ≥ Cachegröße praktisch ein
Blast des ganzen D-/L2-Cache plus kalte Caches danach), ein ioctl, das Programmieren der
Register, IRQ und Wakeup. Das sind Fixkosten, unabhängig von Fläche und Textänderung.

Bei 25 fps entspricht 1 % eines 1-GHz-Kerns etwa 400k Zyklen pro Region und Frame. Das passt
zu einem Blast des ganzen Cache und den Refills. **[I]**

Das `memcpy` wächst mit der Fläche, ist bei typischen 20–50 KB aber klein. **[I]**

---

## 4. Was OpenIMP heute hat (Frage 4)

1. **Nur Buchhaltung, nichts wird gezeichnet** (`OI: src/t31/openimp_t31_services.c:72–319`). **[V]**
   - `IMP_OSD_Start`/`Stop` setzen nur `started` (`:293–319`).
   - `CreateGroup` erzeugt kein Modul und keine Gruppe im Graphen (`:82–90`).
   - `IMP_OSD_AttachToGroup` fehlt.
2. **Bind:** `IMP_System_Bind` merkt sich nur Kanten (`OI: src/t40/openimp_p2_encoder.c:821–850`,
   der T31-Build nutzt diese Datei, `build-t31.sh:48`). `p2_find_source_channel` (`:567–590`)
   läuft von der Encoder-Gruppe über die Bind-Kanten zurück zur FrameSource. Eine
   `DEV_ID_OSD`-Zelle dazwischen wird **transparent übersprungen**, die OSD-Gruppe liegt also
   nicht im Datenpfad. **[V]**
3. **Hook existiert:** Der Encoder zieht Frames selbst, synchron im Thread des Aufrufers von
   `IMP_Encoder_PollingStream`. **[V]**
   - `while (IMP_FrameSource_GetFrame(ch->source_channel, &frame) …)` (`:1213`)
   - danach `p2_copy_requested_jpeg_frames(...)` (`:1236`, JPEG-Kopie per CPU)
   - danach `AL_Codec_Encode_Process(ch->codec, frame, frame)` (`:1239`)

   Die Frame-Freigabe erfolgt auf T31 erst nach der Encoder-Completion (`:56–65`). Ein Aufruf
   `openimp_osd_apply(osd_grp, frame)` direkt nach `:1218` und **vor** `:1236` liefert die
   Semantik des Herstellers: Encoder und JPEG-Snapshot sehen das OSD.
4. **Framebeschreibung passt** (`OI: src/kernel_interface.c:922–941`,
   `openimp_p2_encoder.c:~88–102`, `P2SyntheticFrame`): +8 width, +0xc height, +0x18 phys,
   +0x1c virt, wie beim Hersteller. **[V]**
   - NV12-Layout: UV bei `width * align16(height)`, `picWidth` muss 16-aligned sein
     (`framesource_tseries.c:404–418`, `:1203`). Das ist genau das, was die IPU mit
     `bg_w = align16(w)`, `bg_h = align16(h)` erwartet. **[V]**
5. **Speicher und Cache:**
   - Frames liegen im per `/dev/rmem` gemappten, **gecachten** Arena-Bereich
     (`dma_alloc.c:426–447`). **[V]**
   - `DMA_RmemFlushCache(virt, size, dir)` gibt es schon (`dma_alloc.c:928–1044`,
     1 = WBACK, 2 = INV, 0 = beides über `dma_cache_sync`).
   - `DMA_AllocDescriptor`, `DMA_VirtToPhys` und `IMP_Free` stehen für physisch zusammenhängende
     Bitmap-Puffer bereit (`dma_alloc.c:710, 781, 819`). **[V]**
   - Der JPEG-Pfad invalidiert den Frame vor seiner CPU-Kopie (`openimp_p2_encoder.c:248`).
     Er wäre also auch nach einem IPU-Pass korrekt. **[V]**
   - Eine IPU-Nutzung gibt es in OpenIMP nicht.
6. **ABI-Fehler (kritisch, unabhängig vom Zeichnen)** **[V]**. Die Streamer werden gegen die
   Hersteller-Header gebaut (timps: `include/T31/...`, `TP: imp_osd.c:17`), OpenIMP gegen
   eigene:
   - `IMPOSDRgnAttr` ist in OpenIMP **72 Byte** groß (Test mit `gcc -m32`: attr=72, data=48),
     beim Hersteller **32 Byte** (`VH: imp_osd.h`, `type, IMPRect{p0,p1}, fmt,
     union{8 Byte}`).
   - OpenIMP verschachtelt fälschlich ein zweites `type/rect/fmt` in `IMPOSDRgnAttrData`
     (`OI: include/imp/imp_osd.h:59–91`).
   - `IMPRect` in OpenIMP ist `{x,y,width,height}` (`include/imp/imp_common.h:158–163`), beim
     Hersteller `{IMPPoint p0, p1}`, also inklusive Eckpunkte.
   - `PIX_FMT_BGRA` ist in OpenIMP **12** (`imp_common.h:63`), beim Hersteller **17**.
   - Folgen:
     - `IMP_OSD_GetRgnAttr` (`services.c:226`) schreibt 72 Byte in eine 32-Byte-Struktur des
       Aufrufers, der **Stack wird überschrieben**.
     - `CreateRgn`/`SetRgnAttr` (`:131`, `:205`) lesen 40 Byte über das Ende hinaus.
     - `UpdateRgnAttrData` (`:273`) liest 48 statt 8 Byte.
   - `IMPOSDGrpRgnAttr` ist in beiden 36 Byte groß und kompatibel.

   Eine Implementierung muss intern die Hersteller-Layouts verwenden, mit `_Static_assert` auf
   32 bzw. 36 Byte, und die numerischen Werte des Hersteller-Enums (8, 10, 11, 14–17, 21, 25).
   Das eigene `IMPRect` in `imp_common.h` sollte man nicht global ändern, weil IVS und andere es
   nutzen.

---

## 5. Machbarkeit und Umsetzungsplan (Frage 5)

### 5.1 Architekturentscheidung

Empfohlen wird ein **IPU-Backend als Hauptpfad** plus ein kleines **CPU-Backend**.

| Kriterium | IPU (`/dev/ipu`) | CPU-Blend im Frame |
|---|---|---|
| Treue zum Hersteller | identisch, HW-Blend und Farbraum | Näherung, Rundung und Farbe müssen kalibriert werden |
| CPU pro Frame | ~1 ioctl pro ≤ 4 Layer, **kein** Flush pro Frame nötig (s. u.) | ∝ sichtbare Pixel; Text 400×48 ≈ 0,1–0,5 % bei 25 fps; große halbtransparente Cover teuer (¼ 1080p ≈ 5–10 %) |
| Cache-Wartung | keine für den Frame (IPU arbeitet physisch, CPU fasst den Frame nicht an), Bitmaps einmalig bei Update flushen | Zeilenbänder invalidieren und nach dem Schreiben zurückschreiben (Frame ist gecacht) |
| Risiko | IPU-Wedge (Kernel-Patch vorhanden), Treiber muss existieren | keine HW-Abhängigkeit |

Die Kostenangaben für die CPU-Spalte sind **[I]**.

- Das CPU-Backend wird ohnehin für LINE, RECT und BITMAP gebraucht, wie beim Hersteller.
- Es dient als Fallback, wenn `/dev/ipu` fehlt oder nach wiederholten IPU-Fehlern.
- Es dient als Referenz für Tests.
- Alternative: RECT und achsenparallele LINE lassen sich als **IPU-Masken-Layer** abbilden
  (4 dünne Cover-Layer = 1 IPU-Lauf). Dann bleibt nur die schräge Linie auf der CPU.

### 5.2 Schritte

1. **ABI reparieren** (Voraussetzung):
   - private Structs im Hersteller-Layout in `openimp_t31_services.c`, oder eigener Header
     `src/t31/osd_abi.h`: `vimp_rect {p0x,p0y,p1x,p1y}`, `vimp_osd_rgn_attr` (32 B),
     `vimp_osd_attr_data` (8 B-Union)
   - die öffentlichen Prototypen in der `.c` mit `void *` bzw. korrekt typisiert
   - `_Static_assert`s
   - Konstanten `VPIX_MONOWHITE=8, NV12=10, NV21=11, ARGB=14, RGBA=15, ABGR=16, BGRA=17,
     RGB555LE=21, BGR555LE=25`
   - Region-Handles `0..511`, Gruppen `0..3` (Grenzen wie Hersteller)
2. **Regionsspeicher:**
   - Beim Anlegen oder Ändern die Bitmap **in einen DMA-Puffer kopieren**:
     `DMA_AllocDescriptor`, Größe `w*h*bpp`, Zeilen ohne Padding.
   - Danach `DMA_RmemFlushCache(buf, size, 1 /*WBACK*/)`.
   - **Double-Buffering:** Updates schreiben in den inaktiven Puffer, flushen und tauschen den
     Zeiger unter dem OSD-Lock. So wird pro Frame nichts kopiert und nichts gerissen.
   - `IMP_OSD_SetPoolSize` wird als Budget oder Hinweis akzeptiert.
   - COVER: AYUV-Wort nach §3.5 vorberechnen, `fgAlhpa`-Skalierung wie beim Hersteller.
3. **Pipeline-Hook:**
   - In `p2_find_source_channel` die erste `DEV_ID_OSD`-Zelle auf dem Weg merken
     (`ch->osd_group`).
   - Nach `IMP_FrameSource_GetFrame` (`openimp_p2_encoder.c:1218`) aufrufen:
     `openimp_t31_osd_apply(ch->osd_group, frame)`.
   - Nur wenn die Gruppe gestartet ist und mindestens eine sichtbare Region hat.
   - Nur auf T31 kompilieren (`#if defined(PLATFORM_T31)`).
   - Optional `IMP_OSD_AttachToGroup` als Alias auf `IMP_System_Bind`.
4. **IPU-Backend:**
   - `open("/dev/ipu", O_RDWR)` lazy, einmal pro Prozess, mit Mutex.
   - Pro Frame die sichtbaren PIC- und COVER-Regionen nach `layer` sortieren und in Gruppen zu
     **4 Layern** in eine `ipu_param` (0x98 B, eigenständig nach §3.1 definiert) packen.
   - `bg_w=align16(w)`, `bg_h=align16(h)`, `bg_fmt=out_fmt=0x18`, `bg_buf_p = frame+0x18`.
   - `para` nach §3.2 bauen, `buf_p` = physische Adresse des aktiven DMA-Puffers bzw. 0 für Masken.
   - `ioctl(fd, _IO('I',106), &param)`.
   - **Kein** `IOCTL_IPU_BUF_FLUSH_CACHE` pro Frame: Die Bitmaps sind beim Update schon
     zurückgeschrieben, der Frame ist in der CPU nicht schmutzig.
   - Vorab prüfen: `x>=0, y>=0, x+w<=bg_w, y+h<=bg_h`, `w,h>=1`. Im gedrehten Fall gerade Maße
     und Position.
   - Ungültige Regionen einzeln verwerfen und rate-limitiert loggen. Anders als beim Hersteller
     soll ein fehlerhafter Layer nicht den ganzen Pass „vergiften“.
   - Bei `ioctl < 0`: einmal wiederholen. Nach N Fehlern in Folge für diese Gruppe auf das
     CPU-Backend umschalten.
5. **CPU-Backend:**
   - LINE/RECT: `fmt==8`, deckend, Farbe nach §3.5, Breite zentriert.
   - BITMAP: Byte ≠ 0 bedeutet Y=Byte, UV=0x80.
   - Optional PIC/COVER als Fallback:
     - beim Update auf vorgerechnete Spans mit α>0 umrechnen
     - Y, U, V und α vorgerechnet, 4:2:0-Chroma mit gemitteltem α pro 2×2-Block
     - α=255 als `memcpy`/`memset`, sonst `dst += ((src - dst) * a + 128) >> 8`
   - Cache: vor dem Lesen die betroffenen Zeilenbänder (Y und UV) mit dir=2 invalidieren, nach
     dem Schreiben mit dir=1 zurückschreiben. Je ein ioctl pro Band, nicht pro Zeile.
   - Reihenfolge wie beim Hersteller: erst die CPU-Pfade, dann den IPU-Pass. Das Writeback muss
     **vor** dem IPU-Lauf passieren, sonst überschreibt ein späteres Writeback die Ausgabe der
     IPU.
6. **Timestamp-FIFO** (`SetRgnAttrWithTimestamp`): in der ersten Version wie `SetRgnAttr`
   behandeln (heute schon so, `services.c:210–215`), später optional.

### 5.3 Performance-Überlegungen (ein MIPS32r2-Kern, ~1 GHz)

- **IPU-Pfad:**
  - ~1 ioctl pro Stream und Frame (≤ 4 Regionen), Kernelzeit grob 20–50 µs (etwa 60
    MMIO-Writes, copy_from_user 152 B, IRQ, Wakeup). **[I]**
  - Bei 2 Streams × 25 fps sind das ≈ 0,1–0,3 % eines Kerns, statt beim Hersteller ≈ 1 % pro
    Region und Stream.
  - Die Wartezeit auf die IPU (≤ ~0,8 ms für ein volles 1080p-Composite laut
    Taktzähler-Messung im Wedge-Patch, gemessen auf T20) **schläft**, kostet also keine CPU.
    Sie verlängert nur die Latenz pro Frame. **[I]**
- **Kein Flush pro Frame:** Das ist der größte Hebel gegenüber dem Hersteller. Dort wird pro
  Region 1 MiB geflusht. **[I]** für den Anteil.
- **CPU-Pfad:**
  - nur Pixel innerhalb der Region, dazu Spans ohne α=0 (Text ≈ 20–30 % Deckung)
  - Zeilen-Lesevorgänge sind sequentiell, Prefetch hilft
  - UV-Ebene halb so viele Zeilen
  - `memset` für deckende Cover
- DDR-Bandbreite der IPU: ob sie den ganzen Frame oder nur die Layer-Rechtecke streamt, ist
  **[?]**. Die Zahl im Wedge-Patch (≈157k Takte für 1920×1088) wäre bei ≈200 MHz für ein
  vollständiges Lesen und Schreiben von 3 MB unplausibel schnell. Wahrscheinlich werden also
  nur Rechtecke bearbeitet. **[I]** Auf dem Gerät messen (§5.6).

### 5.4 Risiken

1. **IPU-Stabilität:** Wedges werden mit dem Thingino-Patch in ≤ 100 ms zurückgesetzt. timps
   beobachtet `ipu_osd error` beim Wiederaktivieren von Kanälen und bei ISP-Neustart
   (timps `dev_notes/TODO.md:669–790`). Gegenmittel: IPU nur bei laufendem Stream
   nutzen, Fehlerzähler, Fallback auf die CPU.
2. **`/dev/ipu` fehlt** (anderer Kernel oder Konfiguration): CPU-Fallback.
3. **Gleichzeitigkeit mit anderen IPU-Nutzern:** Der Kernel serialisiert per Mutex. In OpenIMP
   gibt es keine anderen Nutzer. Die libimp-CSC-Funktionen (`ipu_csc_nv12_to_*`) werden in
   OpenIMP nicht verwendet.
4. **Semantik der Bits 24/25 und 23 und die Blend-Rundung:** Werte des Herstellers 1:1
   übernehmen und per Test bestätigen.
5. **Rotation** (`frame+0x28`, timps-Workarounds): In der ersten Version OSD bei gedrehten
   Streams auf das obere Band begrenzen, wie beim Hersteller, oder die Rotation unterstützen,
   wenn OpenIMP sie hat. Zu klären.
6. **ABI-Fix** ändert Verhalten für Aufrufer, die gegen die OpenIMP-Header bauen. Das betrifft
   wohl nur interne Tests.
7. **Lizenz:** Die Kernelquellen sind GPL. Für MIT-Code nur die **Schnittstelle** (ioctl-Nummern,
   Feldreihenfolge der 0x98-Byte-Struktur, Formatcodes) als Fakten übernehmen und die Struktur
   selbst neu deklarieren, keinen Treibercode kopieren. Die libimp-Details oben sind als
   Verhalten beschrieben.

### 5.5 Was auf dem Gerät zu validieren ist

- Ob `/dev/ipu` existiert (`ls -l /dev/ipu`) und die Zugriffsrechte.
- BGRA-Text und -Logo an allen Ecken, auch mit ungeraden x, y, w, h (nicht gedreht).
- Farbe und Alpha im Vergleich zur Hersteller-libimp: gleiche Bitmap, gleiche Position, YUV
  des Frames vergleichen, etwa per Snapshot über `IMP_FrameSource_SnapFrame` oder einen
  Rohframe-Dump.
- COVER mit `gAlphaEn=0/1` und verschiedenen `fgAlhpa` und `A`.
- 2 Streams gleichzeitig (main 1080p + sub), 4+ Regionen, Updates mit 1 Hz (timps-Updater) und
  ein Stresstest mit 30 Hz Updates. Es darf kein Tearing und kein Leck geben.
- CPU-Last pro Region im Vergleich zur Hersteller-Messung: `top -H`, Threads der Encoder-Poller.
- JPEG-Snapshot enthält das OSD.
- Verhalten bei Stream-Stop/-Start, ISP-Restart und Tag/Nacht-Wechsel: keine IPU-Fehler.

### 5.6 Standalone-Test der IPU (vor der Integration)

Ein kleines Tool `tools/t31_ipu_osd_probe.c`, analog zu `t31_hwjpeg_probe`:
1. Mit `DMA_AllocDescriptor` oder einem minimalen `/dev/rmem`-Mapping einen NV12-Frame
   (z. B. 640×368) und eine BGRA-Bitmap (z. B. 64×32) anlegen. Den Frame mit einem
   Graukeil / einer Rampe füllen, die Bitmap mit einem α-Gradienten × Farbfeldern.
2. Beide zurückschreiben (dir=1), `ipu_param` mit einem Layer bauen, `IOCTL_IPU_START` aufrufen,
   den Frame invalidieren (dir=2) und lesen.
3. Mit der CPU-Referenz vergleichen. Daraus ableiten:
   - die exakte Mischformel (Rundung, `>>8` gegen `/255`, Premultiplied ja/nein)
   - die Farbmatrix
   - die Semantik der Modi 0/1/2
   - Masken-Farbe YUV gegen RGB
   - ob ungerade Maße oder Position funktionieren
4. Die ioctl-Dauer mit `clock_gettime` messen, für 1 Layer und 4 Layer sowie 360p und 1080p.
   Daraus folgen Latenz und die Frage „ganzer Frame oder nur Rechteck“.

Diese Werte fließen dann in das CPU-Fallback, damit beide Pfade farbgleich sind.

### 5.7 Aufwandsschätzung

| Arbeitspaket | Personentage |
|---|---|
| ABI-Fix (Hersteller-Layouts, Enum-Konstanten, Grenzen) | 0,5 |
| Regionsspeicher mit DMA-Puffern, Double-Buffering, Format- und Größenprüfung, Cover-AYUV | 1,5 |
| IPU-Backend (Param-Aufbau, Batching, Fehlerbehandlung, Fallback-Umschaltung) | 1,5 |
| Pipeline-Hook (OSD-Gruppe im Bind-Pfad, Start/Stop, T31-Guard) | 0,5–1 |
| CPU-Backend LINE/RECT/BITMAP (+ optional PIC/COVER-Fallback mit Spans) | 1,5–2 |
| Probe-Tool und Gerätevalidierung (Farbe, Alpha, Timing, 2 Streams, Stress, Restart) | 2–3 |
| **Summe** | **≈ 7–10** |

Ein Minimalziel „timps/prudynt-Text und -Logos sichtbar“ (PIC/BGRA und COVER nur über die IPU,
ohne CPU-Pfade) ist in **≈ 3–4 Tagen** erreichbar.

---

## 6. Offene Punkte

- Exakte Mischformel und Rundung der IPU, Wirkung von `CH_BK_PREM` sowie der Bits 23/24/25
  im Kanal-`para`. **[?]**
- Ob die IPU nur die Layer-Rechtecke oder den ganzen Frame streamt (Bandbreite, Latenz). **[?]**
- Ob die Hersteller-libimp beim CPU-Linien-Pfad Cache-Wartung am Frame macht (und warum das
  bei gecachten Frames korrekt ist). **[?]**
- Bedeutung von `frame+0x28` (Tausch von Breite und Höhe, vermutlich Rotation) und
  `frame+0x3e4/0x3e8` (Low-Latency-Teilframes, ISP-Zeilenzähler). Für OpenIMP nicht nötig.
  **[I]**
- `scalex`, `scaley` und `bgAlhpa` werden im Zeichenpfad nicht genutzt. **[I]**
- Mögliche Überlappung von IPU-Scratch (Pool-Anfang) und PIC_RMEM-Allokationen im selben Pool
  bei der Hersteller-libimp. Für OpenIMP irrelevant. **[?]**
