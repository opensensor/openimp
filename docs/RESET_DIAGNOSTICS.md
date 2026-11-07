# Reset diagnostics: how to capture the cause of an unplanned reboot

Status 2026-10-31, update 2026-11-07: the unexplained resets #94 (T31L) and #108 (T23) were closed after a 3 h debug-kernel stress without a reset (rule of 2026-11-07; their causes stay unexplained and a recurrence re-opens them; later resets during power outages were power-on resets). The T23 reset of 2026-11-05 (#229, closed) was our own rotation test running the MXU2 selftest on a T23N (no MXU2); OpenIMP never probes MXU2 on T23 since 2026-11-06. Written so nobody has to repeat this research. Evidence: camera runs on T31L/T31X (Cinnado, Wuuk) and T23N (Galayou), the U-Boot/kernel sources of the firmware build, the Ingenic docs archive.

## The problem
An unplanned reset (watchdog) leaves no panic text: the kernel messages never reach the remote syslog (the forwarder is a user-space process; after a panic the machine restarts within seconds). The boot log only shows `CPU0 RESET ERROR PC:`, which is the CP0 ErrorEPC. `801A5Exx` / `801A8Exx` = `__delay` in the restart path = a planned reboot or a panic (panic=2), NOT a hang.

## What survives a watchdog reset (measured on cameras)
| Store | T31L / T31X | T23N | Notes |
|---|---|---|---|
| RTC alarm register RTCSAR (0x10003008) | **survives** | no RTC block (reads 0) | needs the RTC write-enable sequence (WENR 0x1000303c = 0xa55a); the old "skip if alarm enable set" gate must not be used |
| CPM scratch CPSPR (0x10000034, unlock 0x5a5a/0xa5a5 at 0x10000038) | reset to default 0x0101 (both halves) | same | not usable |
| TCSM 0x132c0000 | - | constant 0x1, not RAM | not usable |
| DRAM (ramoops/pstore) | unproven (see below) | unproven | |
| **ErrorEPC (CP0)** | survives | **survives** | the reset PC itself: basis of the "sled" |
| SD card (FAT) | survives (user space only, not in panic) | same | ring log of the last seconds |

## The "sled" (T23, works): record in the reset PC
In the panic/oops/restart notifier the CPU spins at an uncached address that encodes the record; the watchdog reset leaves that address in ErrorEPC, the boot script decodes it. About 23 bits: the exact kernel or module oops/lockup address, or reason + uptime class + two characters of the task name (hung task: the blocked task). Camera proof (T23N .28, image diag6b): reboot -f -> reason=reboot, selftest -> kernel_oops loc 0x80123468, oops -> inside diag_write, panic -> reason=panic. Decode table and test steps: audit notes `t23-sled-only.md` (kernel patch 0107 in the firmware tree). A non-sled PC (`__delay`, or a user-space address) is a stock restart or a watchdog bite = silent hang.
On T31 the RTCSAR channel gives the same record (`/proc/diag_lastpanic`, `resetcause` in the syslog).

## Why the memory layout would have to change (and why we did not do it)
To keep the FULL panic text we would put ramoops in DRAM, which only works if (a) the kernel never uses that window and (b) the boot chain does not wipe it:
- U-Boot 2013.07 zeroes its 32 MB malloc area at the TOP of the 64 MB on every boot (`memset` in `mem_malloc_init`, about physical 0x1f40000-0x3f50000). SPL does a full DDR init but no RAM clear. Everything above about 0x1e00000 is wiped; our early DRAM test patterns (0x29c8000, 0x29d8000) lay inside that area, so "DRAM does not survive a reset" is NOT proven, only "the wiped area is wiped".
- The kernel normally uses 0..0x2a00000 (`osmem=42M`), followed by the DMA area (`rmem`). A window that survives must lie below 0x1e00000, i.e. INSIDE the kernel's own memory. To keep the kernel away it needs a hole in its memory map: two ranges (`osmem=28M@0x0` + `mem=14016K@0x1c50000`, ramoops 256 KB at 0x1c00000). Shrinking the kernel to 28 MB instead would starve the drivers.
- Risk: booting with two `mem=` ranges is untested; a boot loop needs the SD recovery (below). That is why the sled-only image (diag6b, U-Boot env identical to the normal image) is the first step. The ramoops window is only needed if the address from the sled is not enough.
Untested next step (if needed): flash the image with the low ramoops window (diag6, build recipe in the firmware tree), write a pattern in the window with devmem, `reboot -f`, read back; only then trust it.

## Other options we evaluated and rejected
- Polled flash write from the panic notifier: no free flash sector (`backup` is used by sysupgrade), risk of bricking.
- Polled raw SD write at panic: needs the MMC driver in polled mode, not implemented.
- GPIO/PWM/TCU registers: a watchdog reset clears the whole chip.
- mtdoops/netconsole: not in the 3.10 kernel / `jz_sfc` has no panic_write.
- Serial console (USB-UART 115200 8N1 on the board's console pads): zero risk and complete, needs hardware work by hand.

## Lessons that cost time
- `CONFIG_KALLSYMS=y` changes `struct module` size (0x120 -> 0x140): prebuilt rootfs modules then misbehave at the first modprobe and the board boot-loops (panic_on_oops + panic=2). Rebuild all modules or keep KALLSYMS off; check `readelf` `.gnu.linkonce.this_module` of every `.ko`.
- The ISP boot guard (`S10isp-guard`) empties `/etc/modules.d/20-isp` and `30-sensor` after quick resets (boot not up 300 s with the streamer): after reset tests run `/etc/init.d/S10isp-guard clear; load` and check a snapshot.
- A full OTA wipes the overlay (settings, local init scripts): reinstall them.
- Recovery without UART: FAT32 SD with the full image as `autoupdate-full.bin` in the root, power cycle (proven on the T31L; U-Boot writes `autoupdate-full.done`). Not available on the Eufy T40.
- Reading a gated peripheral register can stall the bus: do not read ISP registers after `rmmod`.

## How to read a reset
1. Syslog host: kernel boot lines `RESET ERROR PC` and the `resetcause` lines (`PANIC`, `RESTART`, `SLED`, `NORECORD`).
2. On the camera: `dmesg | grep "RESET ERROR"`, `/proc/diag_lastpanic`.
3. SD ring log (if installed): `/mnt/mmcblk0p1/flashlog.*`, a line every 5 s (uptime, load, memory, ISP interrupts, last kernel line); it stops at the freeze.
4. Resolve a kernel address with `mipsel-linux-nm -n vmlinux` of the build (`thingino-out/<profile>-<tag>-<ip>/build/linux-3.10.14/`); module addresses: subtract the module load base.
