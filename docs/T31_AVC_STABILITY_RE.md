# OpenIMP T31 AVC-Pfad: Ursachen der Instabilität (RE-Bericht)

> Hinweis: Audit-Bericht, erstellt auf `claude/t31-re` (Commit `e7cf6b9`). Die Zeilennummern beziehen sich auf diesen Stand; die Fixes liegen auf `claude/t31-avc-stability`.

Stand: 2026-09-29. Untersucht wurde der Arbeitsbaum `/home/user/openimp-wt`, Branch `claude/t31-re`, Commit `e7cf6b9`.
Vergleichsbasis:
- Binary-Ninja-HLIL der Stock-T31-`libimp.so` 1.1.6 (`docs/re/libimp.so_hlil.txt`, im Folgenden „HLIL Lnnn“ mit Adresse)
- Stock-Registertrace `docs/re/stock_logs.txt` (rvd, 1080p H.264 + JPEG)
- T31-`avpu`-Treiberquellen `avpu/t31/`

Der Bericht enthält keine Vendor-Disassembly. Das Verhalten ist beschrieben, die Fundstellen sind zitiert.

Markierung: **[V]** verifiziert (Code/Trace/HLIL gelesen), **[I]** abgeleitet (Mechanismus aus dem Code geschlossen, nicht auf dem Gerät reproduziert), **[?]** offen.

Nicht erneut berichtet (bekannt):
- Companion-Stage/JPEG-Core-Kick pro AVC-Frame
- `0x8014 = 0` nach AVC-Completion
- rmem-Flush-Workaround über den uncached `/dev/mem`-Alias

Hinweis: thingino pinnt in `package/openimp/openimp.mk` den Upstream-Commit `opensensor/openimp@9cab219`, nicht diesen Branch. Die Zeilennummern beziehen sich auf `claude/t31-re`.

---

## 0. Kurzfassung (nach erwartetem Einfluss sortiert)

| ID | Schwere | Kern |
|---|---|---|
| F1 | **hoch** | Verlorene oder späte AVC-Completion wird nie aufgeräumt. `Process` meldet trotz Timeout Erfolg, der Pending-Eintrag bleibt stehen, der Core wird nicht zurückgesetzt. `PollingStream` hält dabei `p2_core_lock` bis zu ca. 67 min und blockiert damit **alle** AVC-Kanäle. |
| F2 | **hoch** | Der EndEncoding-Callback verbraucht den Pending-Eintrag und schaltet IRQ-Maske und Clock-Gate ab, **ohne** zu prüfen, ob die Hardware den Frame fertig hat (Writeback `+0x104 == 0`). Ein veralteter oder fremder IRQ erzeugt so eine sich selbst erhaltende Off-by-one-Kette: jeder Frame wird verworfen, der Stream ist schwarz oder eingefroren. Dazu kommt ein Wettlauf zwischen „Sticky Recovery“ im Consumer-Thread und dem IRQ-Thread. |
| F3 | **hoch** | Der gemeinsame WAIT_IRQ-Thread gehört dem *ersten* Encoder-Kontext, nicht dem Gerät. Wird dieser Kanal zerstört, verliert der überlebende Kanal alle IRQs. Das `UNBLOCK_CHANNEL`-Flag des Kernels bleibt für die gepoolte fd dauerhaft gesetzt. Ein neu gestarteter Waiter dreht mit `EINTR` bei 100 % CPU und bekommt nie wieder einen IRQ. |
| F4 | hoch/mittel | `Destroy` ist auf T31 nicht mit dem anderen Kanal serialisiert. Es setzt den gemeinsamen Core zurück, maskiert ihn und gated die Clock mitten in einem fremden Frame. Das führt zu F1. Außerdem gibt es ein Use-after-free-Fenster für `g_tseries_irq_owner`, und P2 `DestroyChn`/`UnRegisterChn` laufen ungeschützt gegen ein laufendes `PollingStream`. |
| F5 | mittel | Ein verworfener Frame (ungültige Größe, reiner Header, Error-Fill) befördert trotzdem Rec→Ref und erhöht `frame_num`, erzwingt aber kein IDR. Der Decoder zeigt Artefakte bis zum nächsten GOP. Die Ratenregelung erfährt nichts davon. |
| F6 | mittel | Der Software-JPEG-Encoder läuft unter `g_t31_encode_core_lock`. Jeder Snapshot oder MJPEG-Frame blockiert Main- und Sub-H.264 für die Dauer einer CPU-JPEG-Kodierung. |
| F7 | mittel | `AL_EncCore_Init` (MISC_CTRL, Reset, globales IRQ-Clear `0x8018=0xffffff`, TOP_CTRL) läuft pro Encoder-Session. Stock macht das einmal pro Prozess. |
| F8 | niedrig/mittel | Eine IDR-Anforderung geht verloren, wenn `Process` vor dem Submit abbricht. |
| F9 | niedrig/mittel | Im IRQ-Thread passieren `usleep(2000)` und eine byteweise EBSP-Kopie aus uncached Speicher. Dazu kommen 1-ms-Polling-Schleifen, ein ungedrosseltes `fprintf` pro Frame in `VBMGetFrame` und ein Kernel-`printk` pro IRQ (lokale Treiberkopie). Folge: Jitter und hohe sys-Zeit. |
| F10–F14 | niedrig | Timeouts auf `CLOCK_REALTIME`; keine Prüfung der Frame-Dimension; Fehler bei `prewrite_headers` unbehandelt; RC-Reset bei jeder Bitratenänderung; kleine Lecks bei Create/UnRegister. |

---

## 1. Befunde im Detail

### F1 – Keine Recovery nach Completion-Timeout; globaler Lock bis ca. 67 min [V] (Folgen teils [I])

**Fundstellen**
- `src/t40/codec-t40.c:9864–9908` (`AL_Codec_Encode_Process`, T31-Zweig)
- `src/t40/codec-t40.c:9918–9981` (`AL_Codec_Encode_GetStream`)
- `src/t40/openimp_p2_encoder.c:1234` (Lock), `1239` (Process), `1258–1270` (GetStream-Schleife), `1297–1305` (Freigabe)
- `src/t40/codec-t40.c:9240–9286` (Submit-Gates)

**Ist-Zustand**
1. `AL_Codec_Encode_Process` wartet unter `g_t31_encode_core_lock` bis zu 2000 × 1 ms auf `pending == 0 && completions_drained` geändert. Bei Timeout wird nur geloggt. `ret` bleibt 0 aus `al_codec_encode_process_impl` (`return submitted ? 0 : -1`, `:9777`). Danach wird der Lock freigegeben.
   - Es gibt keinen Core-Reset, kein Maskieren und kein Leeren des Pending-Eintrags.
   - Der Stream-Puffer bleibt `IN_FLIGHT`.
2. P2 `IMP_Encoder_PollingStream` hält `p2_core_lock` (`:1234`) über Process **und** die GetStream-Schleife. Die Schleife läuft `retries = max(timeout_ms, 2000)` mal. Jeder `AL_Codec_Encode_GetStream`-Aufruf wartet intern 20 × `Fifo_Dequeue(…,100)` = 2 s. Die Summe liegt bei ≥ 2000 × 2 s ≈ **4000 s**.
   - In dieser Zeit steht jeder andere AVC-Kanal an `p2_core_lock`.
   - Der vom Aufrufer übergebene `timeout_ms` wird ignoriert.
3. Kommt der IRQ gar nicht (Hardware hängt, IRQ maskiert, siehe F2/F4), dann gilt Folgendes:
   - Der nächste `PollingStream`-Aufruf submittet trotzdem, weil `last_irq_id >= 0` alle Busy-Gates abschaltet (`:9240 ff.`).
   - Dabei wird ein neuer Stream-Puffer belegt.
   - Der Pending-Ring enthält jetzt `[N, N+1]`.
   - Der IRQ von N+1 wird im Callback dem **ältesten** Eintrag N zugeordnet (`avpu_pending_peek`, `:4480`). N hat keinen Writeback und wird verworfen.
   - N+1 bleibt pending, und `Process` läuft wieder in den 2-s-Timeout.
   - Nach `stream_buf_count` (Default 4) aufeinanderfolgenden Hängern liefert `avpu_acquire_stream_buffer` dauerhaft −1 (`:9277–9285`). Ab dann wird nie wieder submittet, also kommt auch nie wieder ein IRQ. Der Kanal ist tot bis zum Destroy. [I]
4. Im Fehlerpfad (`done:`) gibt P2 den FrameSource-Puffer frei, obwohl der AVPU-Job formal noch läuft. Das ISP kann den Puffer überschreiben, bevor ein später Job ihn liest (korrupter Frame). [I]

**Warum destabilisierend**
- Stock hat kein Timeout-Konstrukt dieser Art.
- `AL_EncChannel_EndEncoding` (HLIL L72029, `0x6b82c`) ordnet eine Completion über die *laufende FIFO des Cores* (`getFifoRunning`) dem Kanal zu.
- Der Scheduler startet den nächsten Kanal aus dem Completion-Kontext. Im Trace kommen `0x83f4=0x10000` → `0x83f4=1` → Reset → `CL_PUSH` des Sub-Kanals aus demselben Thread-Stack (`0x75044xxx`) wie das WAIT_IRQ-ioctl (`docs/re/stock_logs.txt:252–288`).
- In OpenIMP reichen dagegen ein einziger verlorener IRQ oder ein fremder Reset (F4), um erst beide Streams für über eine Stunde einzufrieren und danach einen Kanal dauerhaft zu töten.

**Fix (clean-room)**
1. Timeout = Recovery. Bei Timeout in `AL_Codec_Encode_Process` (T31), noch unter `g_t31_encode_core_lock` und `ctx->irq_mutex`:
   - `0x8014 &= ~0x1` per Read-Modify-Write; JPEG-Bit erhalten, wie Stock `AL_EncCore_DisableEnc1Interrupt`, HLIL L72725.
   - Reset-Triplet `0x83f0 = 1,2,4`.
   - `0x8018 = 0x0f` (nur Core-0-Bits quittieren).
   - Danach `TurnOffGC`.
   - Alle Pending-Einträge dieses Kontexts poppen und die Puffer auf `FREE` setzen.
   - `reference_valid = 0` (das nächste Bild wird IDR).
   - Einen Zähler `ctx->submit_epoch++` erhöhen (für F2).
   - Rückgabe −1 mit `errno = ETIMEDOUT`, damit P2 den Frame **nach** dem Reset freigibt.
2. P2: GetStream-Schleife per Deadline begrenzen, z. B. `deadline = now + max(timeout_ms, 2000 ms)`, statt Iterationen zu zählen. `p2_core_lock` nie länger als diese Deadline halten.
3. Optional: Polling durch eine Condvar ersetzen, die `avpu_complete_frame` signalisiert. Das reduziert zugleich F9.

**Verifikation auf dem Gerät**
- Fehlerinjektion per Debug-Env, z. B. `OPENIMP_T31_DROP_IRQ_EVERY=500`: im Callback jeden n-ten IRQ ignorieren.
- Erwartung nach dem Fix:
  - höchstens ca. 2 s Aussetzer, dann IDR;
  - der andere Kanal läuft weiter;
  - `no free AVPU stream buffer` erscheint nie.
- Vor dem Fix: minutenlanges Einfrieren beider Streams (`logread | grep "serialized completion timeout"`).

---

### F2 – Completion wird ohne Hardware-Beleg akzeptiert → Off-by-one und „schwarzer Stream“ [I] (Mechanik [V])

**Fundstellen**
- `src/t40/codec-t40.c:4441–4830` (`avpu_end_encoding_callback`), insbesondere `4546–4561`, `4784–4815`
- `src/t40/codec-t40.c:4009–4069` (`avpu_complete_frame`)
- `src/t40/codec-t40.c:4095–4209` (`avpu_try_recover_sticky_completion`), aufgerufen aus `GetStream` `:9972` ohne `irq_mutex`
- `src/t40/codec-t40.c:5130–5139` (Owner-Auswahl ohne Lock auf T31)
- `src/t40/codec-t40.c:9654–9696` (Owner/Push)

**Ist-Zustand**
- Der Callback schaut auf den ältesten Pending-Eintrag des *aktuellen Owners* und liest dort `+0x104` aus dem Submit-Slot. Das CL-Entry wird pro Frame mit `memset(cmd,0,512)` neu aufgebaut (`:2952`), also ist `+0x104` vor dem Hardware-Writeback **0**.
- Bei `payload == 0` („invalid T31 entropy size“) geht es trotzdem weiter (`:4784–4803`, Kommentar „Continue through normal completion“):
  - `0x8014 = 0` und `TurnOffGC` (`:4814–4815`), also IRQ maskiert und Clock gegated, während der Frame womöglich noch läuft;
  - `avpu_complete_frame` → Pop, `avpu_promote_reference` (Rec↔Ref-Tausch), `frames_encoded++`, Drop, `completions_drained++`.
- Daraufhin sieht `Process` „completed“, gibt den Lock frei, und der nächste Frame wird submittet.
- Quellen für einen „falschen“ IRQ:
  1. **Später IRQ nach Timeout (F1).** Er landet beim nächsten Owner, denn `g_tseries_irq_owner` bleibt nach der Completion stehen und wird auf T31 ohne Lock gelesen.
  2. **Sticky Recovery.** `GetStream` ruft `avpu_try_recover_sticky_completion` ohne `irq_mutex` auf. Das geschieht nach jedem 100-ms-Leerlauf der FIFO, bei niedriger Framerate oder nachts also regelmäßig. Wird Frame N so abgeschlossen, bevor der IRQ-Thread dran war, gilt der echte IRQ von N als „delayed real IRQ“. Der Code-Kommentar `:4163–4166` beschreibt genau das. Er wird dann dem bereits submitteten N+1 zugeordnet.
  3. Ein IRQ, der zwischen Reset/Unmask und Push im Kernel-Queue steht. Der IRQ-Thread wartet auf `irq_mutex`. Nach dem Push sieht er den neuen Pending-Eintrag und verbraucht ihn (`:9671–9696`).
- Folge: N+1 wird mit `payload 0` verworfen und die Clock mitten im Frame gegated. Kommt der Job doch zu Ende, bleibt sein IRQ-Bit wegen `mask = 0` stehen und feuert beim nächsten `avpu_enable_interrupts` (`:4215`) sofort. Dann wird N+2 genauso verworfen.
  - Die Kette kann sich selbst erhalten. Symptom: `GetStream` liefert dauerhaft `ENODATA`, also schwarzer oder eingefrorener Stream.
  - Der Kommentar in `GetStream` `:9950–9966` beschreibt genau dieses Symptom und behandelt nur die Folge.
- Nebeneffekt: Nach dem ersten Drop gilt dauerhaft `frames_encoded > frames_consumed`. Die Sticky Recovery ist danach für immer aus (`:4155`). Das ist inkonsistent.

**Warum destabilisierend / Vergleich Stock**
- Stock gated und maskiert erst im Scheduler-Pfad nach einer über die laufende FIFO zugeordneten Completion (HLIL L72029 ff.; L66357 `AL_EncCore_TurnOffGC` nur für Module, die nicht laufen, `AL_CoreState_IsRunning`).
- `IsEnc1AlreadyRunning` (HLIL L72440, `0x6c678`) prüft `0x83f8 & 2`.
- Stock hat keine Sticky Recovery.

**Fix**
1. Im T31-Callback **zuerst** den Writeback prüfen. Ist `payload == 0` und kein Error-Fill:
   - als *spurious* behandeln und loggen;
   - **kein** Schreiben von `0x8014`/`0x83f4`, **kein** Pop, `return`.
   - Nur ein gültiger Writeback (0 < n ≤ Kapazität) oder ein expliziter Error-Fill gilt als Completion. Error-Fill → verwerfen und IDR (F5).
2. Pro Push `ctx->irq_armed = 1` setzen (unter `irq_mutex`). Der Callback handelt nur bei `armed` und setzt es zurück. Veraltete IRQs mit `armed == 0` werden ignoriert.
3. Sticky Recovery entfernen oder nur im Timeout-Recovery-Pfad (F1) unter `irq_mutex` und nach Prüfung von `0x83f8 & 2 == 0` zulassen.
4. Die Owner-Auswahl im IRQ-Thread auf T31 unter `g_tseries_irq_host_lock` machen, wie bereits auf T41 (`:5129–5176`). Den Owner nach einer gültigen Completion auf `NULL` setzen.

**Verifikation**
- 12 h Main + Sub bei Nacht bzw. niedriger fps.
- Die Zähler für `invalid T31 entropy size`, `completion without pending stream` und `ENODATA` müssen 0 bleiben.
- Zusätzlich künstliche Verzögerung im IRQ-Thread testen (z. B. `usleep(50 ms)` vor dem Dispatch, per Env). Vor dem Fix: Drop-Kette. Nach dem Fix: nur Latenz.

---

### F3 – IRQ-Waiter hängt am ersten Encoder; `UNBLOCK` ist im Kernel dauerhaft [V]

**Fundstellen**
- `src/t40/codec-t40.c:8425–8452` (Host = `&enc->avpu` des ersten AVC-Kanals)
- `:7277–7293` (Destroy: `irq_thread_running = 0`, `UNBLOCK`, Join)
- `:7331–7338` (Host auf `NULL`)
- `:5099–5107` (Waiter: `EINTR` → `continue`)
- Kernel `avpu/t31/avpu_main.c:211–215` (`unblock_channel` setzt `chan->unblock = 1`, nie zurück) und `:219–236` (`wait_irq` liefert bei gesetztem Flag sofort `-EINTR`)
- `avpu/t31/avpu_ip.c:36–39` (nur **ein** offener Kanal pro Gerät, sonst `-ENODEV`)
- `src/device_pool.c` (eine gemeinsame fd, Refcount)

**Ist-Zustand**
- Der Waiter-Thread läuft auf dem Kontext des ersten AVC-Encoders, der `Process` aufruft (oft Sub).
- Wird dieser Kanal zerstört (z. B. Reconfig nur dieses Streams), passiert Folgendes:
  - `UNBLOCK` auf der **gemeinsamen** fd, Join, Host = `NULL`.
  - Der überlebende Kanal hat keinen Waiter mehr. Jedes Frame läuft in F1.
- Wird der zerstörte Kanal neu angelegt, gilt:
  - `g_tseries_irq_host == NULL`, also startet ein neuer Waiter auf derselben fd.
  - Die fd wurde nie geschlossen, weil der andere Codec (bzw. HW-JPEG, `t31_hwjpeg_setup` `:7677`) den Pool-Refcount hält. `chan->unblock` bleibt also 1.
  - `WAIT_IRQ` kehrt sofort mit `EINTR` zurück, der Thread macht `continue`: Busy-Loop mit 100 % CPU, kein IRQ wird je zugestellt, beide Streams sind tot.

**Stock**
- `AL_Board_Create` erzeugt den Waiter einmal pro Prozess in `EncoderInit`:
  - HLIL L84629, `0x823e0` → `AL_Codec_Create` L78957, `0x78d90` → `AL_Board_Create` `0x3612c`.
- Beendet wird er erst in `AL_Codec_Destroy` (L79025, `0x78ffc`, aus EncoderExit).
- `LinuxIpCtrl_Destroy` (L39126, `0x36098`): `UNBLOCK` → Join → Mutex löschen → `AL_DevicePool_Close`.
- `WaitInterruptThread` (L39062, `0x35e28`) **beendet** sich bei jedem ioctl-Fehler, auch bei `EINTR`; nur `perror` wird bei `EINTR` unterdrückt.
- Die Lebensdauer des Waiters ist also Gerätelebensdauer, nicht Kanallebensdauer.

**Fix**
- Waiter als prozessweites Singleton mit Refcount (Codecs + HW-JPEG), analog zu `avpu_t41_irq_host_get/put` (`:5191–5262`), das auf T31 portiert wird. Erzeugen beim ersten Nutzer, zerstören beim letzten.
- `UNBLOCK` nur, wenn der Pool-Refcount danach sicher 0 wird und die fd tatsächlich geschlossen wird. Nur dann gibt der Kernel `chan` frei.
- Waiter: bei `EINTR` den Loop verlassen, wenn `!irq_thread_running`. Sonst nach N aufeinanderfolgenden sofortigen `EINTR` einen Fehler loggen und schlafen (Schutz vor Spin).
- Optional im Treiber (thingino-kontrolliert): `unblock` in `wait_irq` nach der Auslieferung zurücksetzen (One-shot).

**Verifikation**
- Main und Sub laufen lassen. Nur den Waiter-Host-Kanal zerstören und neu anlegen (prudynt/raptor-Reconfig eines Streams bzw. Testprogramm).
- Vorher: Der andere Stream friert ein, `top` zeigt einen Thread bei ca. 100 % (sys).
- Nachher: Beide laufen weiter.

---

### F4 – `Destroy` stört den laufenden Frame des anderen Kanals; UAF-Fenster; P2-Teardown-Rennen [V] (Auswirkung [I])

**Fundstellen**
- `src/t40/codec-t40.c:7390–7411` (`AL_Codec_Encode_Destroy`: nur T41 serialisiert über `openimp_core_acquire`)
- `:7183–7271` (Maske, Ack, Reset-Triplet, GC-off auf den gemeinsamen Core)
- `:7331–7351` (Owner erst **nach** DMA-Freigabe geleert, danach `irq_mutex` freigegeben)
- `src/t40/openimp_p2_encoder.c:1000–1028` (`DestroyChn` ohne `p2_core_lock`, ohne Warten auf ein aktives `PollingStream`)
- `:1051–1068` (`UnRegisterChn` setzt `source_channel = -1`, während ein `PollingStream` es noch für `ReleaseFrame` braucht)

**Ist-Zustand und Folgen**
1. Beim Zerstören von Sub bei laufendem Main (Main steckt in `PollingStream` mit einem Frame im Core) laufen `0x8014 = 0`, `0x8018`-Ack, `0x83f0 = 1,2,4` und `TurnOffGC`. Das tötet den Main-Frame. Main hängt danach in F1.
2. Der IRQ-Thread liest `g_tseries_irq_owner` ohne Lock (`:5137`). Destroy leert den Owner erst am Ende und gibt dann `irq_mutex` und `enc` frei. Ein bereits gelesener Owner führt zu Lock bzw. Callback auf freigegebenem Speicher oder auf unmapped CL-Ringen (Crash). Das Fenster ist klein, weil vorher maskiert wird; betroffen sind nur schon im Kernel eingereihte IRQs.
3. `DestroyChn` prüft nur `raw_stream`/`source_frame`. Ein gerade laufendes `PollingStream` desselben Kanals hat diese noch nicht gesetzt. Folge: `AL_Codec_Encode_Destroy` parallel zu `Process`/`GetStream` → Use-after-free.
4. `UnRegisterChn` während eines laufenden Polls führt zu `IMP_FrameSource_ReleaseFrame(-1, …)`. Der VBM-Puffer wird nie zurückgegeben, der Capture-Pool schrumpft pro Reconfig.

**Stock**
- `AL_EncChannel_ScheduleDestruction` (HLIL L72230, `0x6c230`) markiert den Kanal nur. Die Zerstörung macht der Scheduler, wenn der Kanal idle ist (`IsChannelIdle`, L66330 ff.).
- Kein Core-Reset pro Kanal. [V für die Markierung; dass kein Reset erfolgt: [I]]

**Fix**
- T31-`Destroy` unter `g_t31_encode_core_lock`; zusätzlich in P2 `DestroyChn` für AVC `p2_core_lock` nehmen.
- Eigenes Pending abwarten oder per F1-Recovery abräumen.
- Reset/GC-off/Mask nur, wenn **kein** anderer initialisierter AVC-Kontext mehr existiert (Sessionzähler). Sonst nur eigene Callbacks und den Owner entfernen.
- Owner unter demselben Lock leeren, den der IRQ-Thread beim Dispatch hält (wie T41), **bevor** DMA und Mutex freigegeben werden.
- P2: Zähler `ch->in_poll` pro Kanal. `DestroyChn`/`UnRegisterChn`/`StopRecvPic` warten per Condvar auf 0. `source_channel` beim Poll-Eintritt lokal kopieren.

**Verifikation**
- Script: Main und Sub streamen, Sub im Sekundentakt zerstören und neu anlegen.
- Vorher: Aussetzer oder Freeze im Main-Stream, gelegentlich SIGSEGV, wachsende Zahl `in_userspace`-VBM-Puffer.
- Nachher: Main läuft durch.

---

### F5 – Verworfener Frame ohne IDR-Erzwingung; Referenz wird trotzdem befördert [V]

**Fundstellen**
- `src/t40/codec-t40.c:4038–4058` (`avpu_complete_frame`: `promote_reference` + `frames_encoded++` vor der Publish-Prüfung; bei `!queued` nur `dropped_completions++`)
- `:5586–5598` (T31 verweigert ungültige Größe)
- `:6709–6726` (verweigert reine Header)

**Ist-Zustand**
- Ein Frame, dessen Completion ungültig ist (Error-Fill, zu groß, nur Header), wird nicht veröffentlicht.
- Trotzdem wird die (möglicherweise korrupte) Rekonstruktion zur Referenz.
- `frame_num`/POC laufen weiter, `force_next_idr` wird nicht gesetzt.
- Der Decoder verliert ein P-Bild und zeigt Schlieren oder Grau bis zum nächsten periodischen IDR (GOP-Länge, oft 2–4 s).
- Die T31-Ratenregelung (`t31_rate_control.c`) wird bei `completed == 0` nicht gefüttert (`:4702–4706`). Ein wiederholter Überlauf in detailreichen Nachtszenen wird also nicht per QP gegengesteuert. [I]

**Fix**
- In `avpu_complete_frame` bei `!queued`: `reference_valid = 0` (sicherer als `force_next_idr`, weil die Rekonstruktion selbst ungültig sein kann).
- Bei Überlauf (`payload > capacity`) der RC einen Ersatzwert „Kapazität × 8“ melden oder direkt QP + n.

**Verifikation**
- Mit Fehlerinjektion (jeden n-ten Writeback ignorieren) muss unmittelbar danach `NALs 7 8 5` (IDR) folgen.
- VLC/ffprobe ohne Referenzfehler (`ffmpeg -v error -i rtsp://…`: keine „reference picture missing“).

---

### F6 – Software-JPEG blockiert den AVC-Core-Lock [V] (Dauer [?])

**Fundstellen**
- `src/t40/codec-t40.c:9871` (T31 `AL_Codec_Encode_Process` nimmt `g_t31_encode_core_lock` für **jeden** Codec-Typ)
- `:9812–9819` (JPEG → `t31_hwjpeg_encode` bzw. `HW_Encoder_Encode_Software` → `HW_Encoder_Encode_NV12_JPEG`, `src/hw_encoder.c:1199–1200`)

**Ist-Zustand**
- P2 lässt JPEG absichtlich außerhalb von `p2_core_lock` laufen (Kommentar `openimp_p2_encoder.c:1226–1232`).
- Die Codec-Schicht serialisiert es dann doch mit dem AVC-Submit und dem 2-s-Completion-Wait.
- Eine 1080p-CPU-JPEG-Kodierung blockiert Main und Sub. Folge: Ruckler oder verpasste Frames bei jedem Snapshot bzw. MJPEG-Frame.

**Fix**
- In `AL_Codec_Encode_Process` (T31) den Lock nur nehmen, wenn `use_hardware == 2` (AVPU-AVC) oder ein HW-JPEG-Job startet.
- Software-JPEG ohne Lock.

**Verifikation**
- `curl /image.jpg` in einer Schleife bei laufendem RTSP.
- Vorher sichtbare Lücken (Frame-Intervall-Histogramm per `ffprobe -show_frames`), nachher keine.

---

### F7 – Init-Sequenz pro Session statt einmal pro Gerät [V]

**Fundstellen**
- `src/t40/codec-t40.c:9063–9135` (bei `!session_ready`: `0x8010 = 0x1000`, `0x83f0 = 1,2,4`, `0x8018 = 0xffffff`, `0x8054 = 0x80`)
- Stock-Trace: genau **einmal** `WR 0x8010`/`0x8018`/`0x8054` (`docs/re/stock_logs.txt:69, 81, 84`) bei 34 CL-Pushes für Main + Sub.

**Ist-Zustand**
- Der zweite AVC-Kanal setzt beim ersten Frame den Core zurück und löscht alle IRQ-Bits, auch das JPEG-Core-Bit 4.
- Unter dem Lock ist das meist harmlos. Es verliert aber einen gerade anstehenden JPEG-Completion-IRQ bei `OPENIMP_T31_HW_JPEG=1`. Außerdem gibt es beim Destroy symmetrisch einen Reset (F4).

**Fix**
- Statisches Flag „Core initialisiert“ unter `g_t31_encode_core_lock`. Init nur beim ersten Kontext, analog Stock `AL_EncCore_Init` (einmal pro Core/Scheduler).

**Verifikation**
- Registertrace (patched `avpu.ko`) mit Main + Sub: `0x8018 = 0xffffff` darf nur einmal erscheinen.

---

### F8 – IDR-Anforderung geht verloren [V]

**Fundstelle**
- `src/t40/codec-t40.c:9041` (`force_idr = __sync_lock_test_and_set(&enc->force_next_idr, 0)` ganz am Anfang)
- Frühe Returns: `:9249` (Busy-Skip), `:9277–9285` (kein freier Puffer), `:9346–9353` (RC-Prepare), `:9472–9477` (Submit-Slot fehlt), `:9679–9686` (Tracking)

**Ist-Zustand**
- Scheitert der Submit, ist die Anforderung verbraucht. Neue RTSP-Clients warten dann bis zum periodischen IDR.

**Fix**
- Bei jedem Return ohne `submitted` und `force_idr != 0` das Flag wieder setzen.

**Verifikation**
- `IMP_Encoder_RequestIDR` bei erschöpftem Stream-Pool aufrufen (ReleaseStream künstlich verzögern). Das nächste veröffentlichte Bild muss IDR sein.

---

### F9 – Schwere Arbeit im IRQ-Thread, Polling, Log-Druck [V] (Einfluss auf Stabilität [I])

**Fundstellen**
- `src/t40/codec-t40.c:5609` (`usleep(2000)` in jeder Completion)
- `:5733–5808` (EBSP-Kopie Byte für Byte aus `uncached_map`, der Stream-Alias über `/dev/mem`, `:8488`)
- `:5641–5649` (die Invalidierung des *cached* Alias ist dann wirkungslos)
- `:9888–9897` (1-ms-Polling im `Process`-Wait)
- `openimp_p2_encoder.c:1213–1217` (1-ms-Polling auf `GetFrame`)
- `src/kernel_interface.c` `VBMGetFrame` (ungedrosseltes `fprintf(stderr, "[VBM] GetFrame…")` pro Frame)
- `avpu_write_reg`/`avpu_read_reg` (`:572–630`, je 1 KiB `posix_memalign`/`free` pro Registerzugriff)
- `avpu/t31/avpu_ip.c:119` (`printk(KERN_INFO …)` im Hardirq pro IRQ; lokale Treiberkopie, ob thingino-`avpu.ko` dies enthält: [?])

**Warum relevant**
- Auf dem Single-Core-T31 verlängert das die Completion-zu-Publish-Latenz. Bei IDRs aus uncached RAM sind das mehrere ms.
- Es vergrößert das Zeitfenster für die Rennen aus F2 (Sticky Recovery gegen IRQ-Thread).
- Es erklärt einen Teil der in `prudynt-cpu-profile.md` gemessenen ca. 70 % sys-Zeit.
- Hängt `stderr` an einer Logger-Pipe, kann das `fprintf` pro Frame den Encoder-Thread blockieren.

**Fix**
- Payload über die gecachte Abbildung nach einer Invalidierung auf `payload_end` lesen (ist schon vorhanden). Wortweise nach `00 00 0x` suchen und `memcpy` für Blöcke ohne Escape verwenden.
- `usleep(2000)` durch ein Kriterium ersetzen: Writeback `+0x104` stabil zweimal gelesen.
- Warten per Condvar.
- `VBMGetFrame`-Log drosseln.
- Einen statischen Registerpuffer pro Thread verwenden.

**Verifikation**
- `/proc/<pid>/task/*/stat` vorher und nachher: Anteil des IRQ-Threads.
- Completion→GetStream-Latenz im Profil (`OPENIMP_PROFILE_*`).

---

### F10–F14 (niedrig)

- **F10 [V]** `src/fifo.c:148–157, 216–225` sowie das statische `fifo_dequeue` `:4399–4406`: timed waits auf `CLOCK_REALTIME`. Ein NTP- oder Uhrzeitsprung verkürzt oder verlängert die GetStream- und Queue-Timeouts.
  - Fix: `pthread_condattr_setclock(CLOCK_MONOTONIC)` (wie bereits bei `t31_hwjpeg_init_once` `:7579`). Semaphore-Timeouts relativ nachbilden.
- **F11 [V]** Keine Prüfung, ob Frame-`width`/`height`/`size` (`frame+0x08/0x0c/0x14`) zu `ctx->enc_w/enc_h` passen. `fill_cmd_regs_enc1` rechnet die Chroma-Adresse aus der Encoder-Konfiguration (`:3111–3125`). Bei FrameSource-Reconfig liest der AVPU über das Pufferende hinaus bzw. mit falscher Chroma-Lage.
  - Fix: Frame verwerfen, wenn die Größe nicht passt oder `size < nv12_size(enc_w, enc_h)`.
- **F12 [V]** `avpu_prewrite_stream_headers` kann 0 liefern (`:2522–2536`). `Process` submittet trotzdem. `stream_header_offset_by_buf[buf]` behält dann den Wert eines früheren Frames, und der veröffentlichte AU trägt einen falschen Slice-Header.
  - Fix: bei 0 den Puffer freigeben und `EIO` zurückgeben.
- **F13 [V]** Jede Bitraten-, fps-, GOP- oder QP-Grenzen-Änderung setzt den T31-RC komplett zurück (`avpu_t31_prepare_picture` `:2398–2421`). QP springt auf `iInitialQP`. `openimp_t31_rate_controller_set_bitrate` existiert, wird für T31-AVPU aber nicht benutzt.
  - Fix: bei reiner Bitratenänderung `set_bitrate` verwenden.
- **F14 [V]**
  - `AL_Codec_Encode_SetStreamBufferCount` ruft `Fifo_Init` erneut auf einer bereits initialisierten FIFO auf (`:6957–6958`): Leck plus Doppel-Init von Mutex, Cond und Semaphore bei jedem `CreateChn`.
  - `VBMReleaseFrame` mit ungültigem Kanal nach `UnRegisterChn` (F4.4).
- **[?]** Die VBM-Ready-Queue liefert den **ältesten** Frame (`kernel_interface.c:1589 ff.`). Ist die Kanal-fps kleiner als die Sensor-fps, wachsen Latenz und Anzahl der im Userspace gehaltenen Puffer (die ISP-Queue leert sich). Ob Stock „neueste gewinnt“ macht, ist offen.

---

## 2. Geprüft und unauffällig

- **Stream-Puffer-Lebenszyklus** [V]
  - Übergänge `FREE → IN_FLIGHT` (acquire) `→ READY` (pop) `→ FREE` (ReleaseStream per Phys-Match), alle unter `stream_queue_mutex` (`:3913–4007`, `:10018–10070`).
  - Deskriptor pro Slot aus dem festen Pool (`avpu_stream_descriptors[16]`).
  - Die Stream-FIFO hat Kapazität `stream_buf_count ≥` Anzahl `READY`-Puffer. `Fifo_Queue(-1)` im IRQ-Thread kann im Normalbetrieb also nicht blockieren.
- **CL-Ringe** [V]
  - Pro Kanal eigene Readback- und Submit-Ringe mit 19 × 0x200. Auf T31 uncached über `/dev/mem` (`:8543–8565`).
  - Jeder Eintrag wird pro Frame komplett neu geschrieben, das Status-Wort `+0x104` ist vor dem Push also 0. Es gibt keine Wiederverwendung während eines Jobs (seriell, ein Job pro Kanal).
- **Cache-Handoffs** [V]
  - Header-Präfix 0x220: cached schreiben, danach WBACK (`:9383–9396`). 0x220 ist auf 32 Byte ausgerichtet, teilt also keine Cachezeile mit dem Payload.
  - Payload: Lesen über den uncached Alias, danach Kopie in einen CPU-eigenen Puffer. Im DMA-Puffer wird nicht mehr kompaktiert.
  - Quell-Frame: Invalidierung vor dem Submit (`:8040–8069`).
  - EP1/EP2/EP3: einmal bei der Init geschrieben und per WBACK zurückgeschrieben (`:8698`, `:8783–8795`). Auf T31 schreibt die CPU sie pro Frame nicht neu.
- **Register-Parität pro Frame** [V], gegen `docs/re/stock_logs.txt:100–122` (Main) und `:266–288` (Sub):
  - `0x83f4` TurnOnGC (RMW) → `0x83f0 = 1,2,4` → `0x8014 |=` Enc1 (RMW) → `0x83e0` → `0x83e4 = 2`.
  - Nach der Completion: DisableEnc1 und `0x83f4 = …0000` TurnOffGC (`:314–319`).
  - OpenIMP entspricht dem bis auf die bekannten Punkte (absolutes `0x8014 = 0` statt RMW `&~1`; Bit 4 nicht gesetzt; Companion-Stage) und F7.
  - Kein IRQ-Ack aus dem Userspace pro Frame, ebenfalls wie Stock. Der Kernel quittiert im Hardirq (`avpu_ip.c:121`).
- **Source-Frame-Besitz auf T31** [V]: Freigabe erst nach der Completion (`P2_CAPTURE_RELEASE_AFTER_COMPLETION`, `openimp_p2_encoder.c:56–64, 1276–1282`). Einzige Ausnahme ist der Fehlerpfad (F1.4).
- **Referenzbeförderung vor dem Publish** [V] (`:4028–4041`). Im Normalpfad gibt es kein Rennen zwischen Rec/Ref-Tausch und dem nächsten Submit, weil Process auf die Completion wartet.
- **Timestamps** [V]: Aus dem DQBUF-Zeitstempel. open-tx-isp T31 liefert `CLOCK_MONOTONIC` (`driver/t31/tx_isp_module.c:1638`), normalisiert über `OpenIMP_P0_NormalizeMonotonicTimeStamp`. Durch die FIFO-Reihenfolge monoton. Keine Rückwärtssprünge gefunden.
- **Rate-Control-Arithmetik** [V]: Sättigende Addition, Division nur durch geprüfte Nicht-Null-Werte, QP-Klammerung (`t31_rate_control.c`).
- **Destroy-Reihenfolge im Einkanalfall** [V]: Quiesce → Waiter joinen → alle VMAs unmappen → fd schließen (`:7183–7322`). Das ist korrekt für den Kernel-Einzelkanal (`avpu_ip.c:36`).

---

## 3. Empfohlene Reihenfolge der Fixes

1. **F3** (Waiter-Singleton plus EINTR-Schutz): kleiner Eingriff, beseitigt totale Hänger nach einer Reconfig.
2. **F2** (Completion nur mit Writeback-Beleg, `irq_armed`, Sticky Recovery entfernen).
3. **F1** (Timeout-Recovery, P2-Deadline).
4. **F4** (Destroy serialisieren, P2 `in_poll`).
5. **F5 und F8** (IDR-Robustheit).
6. **F6, F7, F9** (Latenz und Parität).

Messgrößen für den Gerätetest mit Main + Sub + JPEG über 12 h, zusätzlich Reconfig-Script alle 60 s:

| Messgröße | Soll |
|---|---|
| `serialized completion timeout` | 0 |
| `invalid T31 entropy size` | 0 |
| `completion without pending stream` | 0 |
| `ENODATA`-Rückgaben | 0 |
| `no free AVPU stream buffer` | 0 |
| Thread-CPU des IRQ-Waiters | < 5 % |
| RSS | stabil |
| RTSP | ohne Referenzfehler (`ffmpeg -v error`) |
