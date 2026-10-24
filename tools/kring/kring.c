/* kring: persistent-ish log for cams without pstore/serial.
 * Copies /dev/kmsg records + 1 s stats (meminfo, /tmp, irq counts) into a RAM ring
 * in nmem (default 0x7ff0000, 64 KB; survives a WDT reset, not a power cycle) and to stdout.
 * kring run [base]  |  kring dump [base] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/statvfs.h>
#include <signal.h>
#define RSZ 0x10000
#define MAGIC 0x4b52494eu
struct hdr { unsigned magic, pos, wraps, gen; };
static volatile struct hdr *h; static volatile char *d; static unsigned dsz;
static void put(const char *s, int n)
{
    int i;
    for (i = 0; i < n; i++) { d[h->pos] = s[i]; if (++h->pos >= dsz) { h->pos = 0; h->wraps++; } }
    fwrite(s, 1, n, stdout); fflush(stdout);
}
static long mi(const char *buf, const char *k) { const char *p = strstr(buf, k); return p ? atol(p + strlen(k)) : -1; }
static void stats(void)
{
    char b[2048], l[512], o[600]; int fd, n; long m0 = 0, avpu = 0, w02 = 0, ld = 0; struct statvfs sv; FILE *f;
    double up = 0;
    fd = open("/proc/meminfo", O_RDONLY); n = fd >= 0 ? read(fd, b, sizeof(b) - 1) : 0; if (fd >= 0) close(fd); b[n > 0 ? n : 0] = 0;
    f = fopen("/proc/interrupts", "r");
    while (f && fgets(l, sizeof(l), f)) {
        long a = 0, c = 0; char *e = strstr(l, "isp-m0") ? "m0" : strstr(l, "avpu") ? "av" : strstr(l, "isp-w02") ? "w2" : 0;
        if (!e) continue;
        sscanf(l, "%*s %ld %ld", &a, &c);
        if (e[0] == 'm') m0 = a + c; else if (e[0] == 'a') avpu = a + c; else w02 = a + c;
    }
    if (f) fclose(f);
    f = fopen("/proc/uptime", "r"); if (f) { fscanf(f, "%lf", &up); fclose(f); }
    f = fopen("/proc/loadavg", "r"); if (f) { double x; fscanf(f, "%lf", &x); ld = (long)(x * 100); fclose(f); }
    if (statvfs("/tmp", &sv)) memset(&sv, 0, sizeof(sv));
    n = snprintf(o, sizeof(o), "S %.1f free=%ld avail=%ld shm=%ld cache=%ld slab=%ld tmpused=%lu m0=%ld w02=%ld avpu=%ld ld=%ld\n", up,
                 mi(b, "MemFree:"), mi(b, "MemAvailable:"), mi(b, "Shmem:"), mi(b, "Cached:"), mi(b, "Slab:"),
                 (unsigned long)((sv.f_blocks - sv.f_bfree) * sv.f_frsize / 1024), m0, w02, avpu, ld);
    put(o, n);
}
int main(int argc, char **argv)
{
    unsigned long base = argc > 2 ? strtoul(argv[2], 0, 0) : 0x7ff0000; int mfd = open("/dev/mem", O_RDWR | O_SYNC);
    void *m; signal(SIGPIPE, SIG_IGN); signal(SIGHUP, SIG_IGN);
    if (mfd < 0) { perror("mem"); return 1; }
    m = mmap(0, RSZ, PROT_READ | PROT_WRITE, MAP_SHARED, mfd, base); if (m == MAP_FAILED) { perror("mmap"); return 1; }
    h = m; d = (char *)m + sizeof(struct hdr); dsz = RSZ - sizeof(struct hdr);
    if (argc > 1 && !strcmp(argv[1], "dump")) {
        unsigned p, i; if (h->magic != MAGIC) { printf("no ring (magic %08x)\n", h->magic); return 1; }
        p = h->pos; if (p >= dsz) p = 0;
        printf("ring gen=%u wraps=%u pos=%u\n", h->gen, h->wraps, p);
        if (h->wraps) for (i = p; i < dsz; i++) putchar(d[i]);
        for (i = 0; i < p; i++) putchar(d[i]);
        return 0;
    }
    if (h->magic == MAGIC) h->gen++; else { h->gen = 0; }
    h->magic = MAGIC; h->pos = 0; h->wraps = 0;
    {
        int kfd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK); char r[1024]; time_t last = 0;
        if (kfd >= 0) lseek(kfd, 0, SEEK_END);
        put("kring start\n", 12);
        for (;;) {
            struct pollfd p = { kfd, POLLIN, 0 }; int n;
            poll(&p, 1, 250);
            while ((n = read(kfd, r, sizeof(r) - 1)) > 0) {
                char *s = strchr(r, ';'), o[1100]; int k;
                r[n] = 0; s = s ? s + 1 : r;
                k = snprintf(o, sizeof(o), "K %s", s); if (k > (int)sizeof(o)) k = sizeof(o) - 1;
                if (strstr(s, "ISP frame complete") || strstr(s, "frame-done work") || strstr(s, "shadow commit")) continue;
                put(o, k);
            }
            if (time(0) != last) { last = time(0); stats(); }
        }
    }
}
