/* save_guard.h - keep camera test tools from filling RAM through tmpfs.
 *
 * Camera /tmp is tmpfs: every byte a tool saves there is unswappable RAM. On a
 * 30 MB T41 (Cinnado .34) a 2 x 8000 kbit/s capture to /tmp eats ~1 MB/s,
 * MemAvailable reaches ~1.5 MB after ~10 s, WLAN/ssh stop and the hardware
 * watchdog resets the board ~60 s later (2026-10-24, three reboots).
 *
 * save_guard_ok(len) returns 0 once MemAvailable drops under the floor
 * (SAVE_MIN_AVAIL_KB, default 6144); the caller keeps counting bytes/frames
 * but stops writing. Checked every 256 KB written. SAVE_MIN_AVAIL_KB=0 disables.
 */
#ifndef SAVE_GUARD_H
#define SAVE_GUARD_H
#include <stdio.h>
#include <stdlib.h>

static long save_guard_floor = -1, save_guard_since;
static int save_guard_stopped;

static long save_guard_avail_kb(void)
{
    FILE *f = fopen("/proc/meminfo", "r");
    char l[128];
    long v = -1;
    while (f && fgets(l, sizeof(l), f))
        if (sscanf(l, "MemAvailable: %ld kB", &v) == 1) break;
    if (f) fclose(f);
    return v;
}

static int save_guard_ok(size_t len)
{
    if (save_guard_stopped) return 0;
    if (save_guard_floor < 0) {
        const char *e = getenv("SAVE_MIN_AVAIL_KB");
        save_guard_floor = e ? atol(e) : 6144;
    }
    if (!save_guard_floor) return 1;
    save_guard_since += (long)len;
    if (save_guard_since >= (256 << 10)) {
        long a = save_guard_avail_kb();
        save_guard_since = 0;
        if (a >= 0 && a < save_guard_floor) {
            save_guard_stopped = 1;
            fprintf(stderr, "[W] save stopped: MemAvailable %ld kB < %ld kB (output on tmpfs?); "
                            "stats continue, files are truncated\n", a, save_guard_floor);
            return 0;
        }
    }
    return 1;
}
#endif
