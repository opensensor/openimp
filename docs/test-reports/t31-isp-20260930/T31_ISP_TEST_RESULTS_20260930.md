# T31 ISP: Geräteergebnisse vom 2026-09-30 (cam-A, T31X, SC4336P)

Vollständiger Bericht mit allen Messdaten: `Lu-Fi/openimp`, Branch `claude/t31-test-results-20260930b`,
`test-results/T31_TEST_RESULTS.md` (Abschnitt „Phase 2"). Hier nur die Kernaussagen für open-tx-isp.

Getestet wurde `claude/t31-isp-all` (`97cccfa2`) mit den thingino-Patches `0001`–`0006` als
installiertes Modul (thingino `ciao` mit `BR2_PACKAGE_THINGINO_ISP_OPEN=y`, komplettes OTA), libimp
OpenIMP `dfbd90f` bzw. deren Fix-Branches, Streamer timps.

| Test | `claude/t31-isp-all` | `claude/t31-isp-fixes` (dieser Branch) |
|---|---|---|
| T1 Start/Stopp ×20, T3 `kill -9` ×10, T4 Snapshots, T5 zweites `open()`, T6 Leerlauf-Kick, T7 Speicher | bestanden | – |
| T2 Tag/Nacht ×20 bei Stream (drop 0) | bestanden (altes Modul fror einmal 4,5 s ein) | – |
| L2 Latenz bei 24 Tag/Nacht-Wechseln | max. 660 µs, 0 s > 1 ms (altes Modul 1313 µs, 2 s > 1 ms) | – |
| L4 ctxt/s | 2618 (alt 2736) | – |
| Tuning 5.1–5.14 | bestanden bis auf 5.6 (Marker durch `print_level=1` unterdrückt); `it_max`/SetAe_IT_MAX wirken | – |
| L1 `rmmod tx_isp_t31` | **Oops** (`tx_isp_exit` → `tx_isp_vic_remove` → `tx_isp_vic_stop`, uninitialisierter Mutex) | **bestanden**: 3 Zyklen, kein Oops, IRQs/Regionen frei, Neuladen ok |
| T2/L5 mit `isp_day_night_switch_drop_frame_num=6` | nicht testbar (rmmod) | **bestanden** |

Commits auf diesem Branch (gegenüber `97cccfa2`):
- `1d85e525`, `4d83ac64`, `c7c2e7d2` (`claude/t31-isp-rmmod-fix`): VIC-Mutex init, Exit-Reihenfolge,
  doppeltes `kfree` der Pad-Arrays in `tx_isp_subdev_deinit`, `misc_deregister` der Frame-Kanäle,
  Regionen/IRQs mit korrekter dev_id, `release()` für die statischen Platform-Devices.
- `f70c5276`, `8b727c31`, `0e1c5773` (`claude/t31-isp-csi-mutex`): CSI-Lock ohne Überlappung der
  OEM-Rohslots (+`BUILD_BUG_ON`-Layoutprüfung), VIC-Frame-End-Completions und Custom-AE-Completion
  initialisiert.

Offen: das bekannte Leck von ~160 KiB pro Ladezyklus (`tisp_deinit_free`), `clk_put` der CSI-Clocks;
rmmod bei laufendem Stream ist nicht abgesichert. Die INFO-Marker der Lebenszyklus-Anleitung
(`stopped channel`, `ISP core quiet after …`) sind mit `print_level=1` + Patch 0006 nicht sichtbar.
