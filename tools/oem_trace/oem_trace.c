/*
 * oem_trace - LD_PRELOAD tracer for the stock Ingenic libimp (T31 focus).
 *
 * Logs the ioctls a streamer's libimp issues and captures the DMA data the
 * hardware consumes or produces, as ground truth for OpenIMP:
 *
 *   /dev/avpu  register writes/reads, WAIT_IRQ slots, DMA allocations;
 *              on a JPEG start (write 0x85E4) the EP1 table buffer at the
 *              address last written to 0x8418 (0x790 bytes); on the JPEG
 *              length read (0x8434) the encoded bytes at 0x841C + 0x8424.
 *   /dev/ipu   IOCTL_IPU_START with all 38 words of struct ipu_param and
 *              the OSD bitmaps its channels point to; cache flushes.
 *   tx-isp     tuning ioctls (0xc00c56c6 / 0xc01056c6): cmd, CID, value and
 *              the first 0x80 bytes behind a pointer argument.
 *   /dev/rmem  cache flush ioctls.
 *
 * Physical buffers are read through /dev/mem (root, uncached mapping).
 *
 * Environment:
 *   OEM_TRACE_DIR    output directory (default /tmp/oemtrace); trace.log + dumps
 *   OEM_TRACE_DUMPS  max dumps per kind (default 4, 0 = log only)
 *   OEM_TRACE_ALL    1 = also log ioctls on other devices
 *
 * Build with the camera's toolchain (a shared library, so the camera libc):
 *   mipsel-linux-gcc -O2 -fPIC -shared -o liboem_trace.so oem_trace.c -ldl
 * Use:
 *   LD_PRELOAD=/tmp/liboem_trace.so <streamer command line>
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#if defined(__GLIBC__) || defined(__UCLIBC__)
typedef unsigned long ioctl_req_t;
#else
typedef int ioctl_req_t;                      /* musl */
#endif

/* _IOWR('q', n, size) etc., MIPS encoding */
#define AVPU_WRITE_REG   0xc008710aUL
#define AVPU_READ_REG    0xc008710bUL
#define AVPU_WAIT_IRQ    0xc004710cUL
#define AVPU_GET_DMA_FD  0xc00c710dUL
#define AVPU_GET_DMA_PHY 0xc00c7112UL
#define AVPU_GET_DMA_MMAP 0xc00c711aUL
#define IPU_START        0x2000496aUL
#define IPU_FLUSH_CACHE  0x20004976UL
#define ISP_TUNING       0xc00c56c6UL
#define ISP_TUNING_T23   0xc01056c6UL
#define RMEM_FLUSH       0xc00c7200UL

enum { DEV_OTHER, DEV_AVPU, DEV_IPU, DEV_ISP, DEV_RMEM };

typedef int (*ioctl_fn)(int, ioctl_req_t, ...);
static ioctl_fn real_ioctl;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static FILE *g_log;
static char g_dir[128] = "/tmp/oemtrace";
static int g_max_dumps = 4, g_all;
static int g_devmem = -2;
static int g_pipe[2] = {-1, -1};
static uint8_t g_fdtype[1024];            /* 0 unknown, else DEV_* + 1 */
static uint32_t g_reg[0x2000 / 4];        /* shadow of 0x8000..0x9fff writes */
static int g_dumps_ep1, g_dumps_jpeg, g_dumps_osd, g_jpeg_pending;

static void init(void)
{
    static int done;
    const char *s;
    char path[192];

    if (done)
        return;
    done = 1;
    real_ioctl = (ioctl_fn)dlsym(RTLD_NEXT, "ioctl");
    if ((s = getenv("OEM_TRACE_DIR")) && *s)
        snprintf(g_dir, sizeof g_dir, "%s", s);
    if ((s = getenv("OEM_TRACE_DUMPS")))
        g_max_dumps = atoi(s);
    g_all = (s = getenv("OEM_TRACE_ALL")) && *s == '1';
    mkdir(g_dir, 0755);
    snprintf(path, sizeof path, "%s/trace.log", g_dir);
    g_log = fopen(path, "a");
    if (g_log)
        setvbuf(g_log, NULL, _IOLBF, 0);
    if (pipe(g_pipe) != 0)
        g_pipe[0] = g_pipe[1] = -1;
}

static void tlog(const char *fmt, ...)
{
    struct timespec t;
    va_list ap;

    if (!g_log)
        return;
    clock_gettime(CLOCK_MONOTONIC, &t);
    fprintf(g_log, "%ld.%06ld t%ld ", (long)t.tv_sec, t.tv_nsec / 1000,
            (long)syscall(SYS_gettid));
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
}

static int fd_type(int fd)
{
    char link[64], target[128];
    ssize_t n;
    int type = DEV_OTHER;

    if (fd >= 0 && fd < (int)sizeof g_fdtype && g_fdtype[fd])
        return g_fdtype[fd] - 1;
    snprintf(link, sizeof link, "/proc/self/fd/%d", fd);
    n = readlink(link, target, sizeof target - 1);
    if (n > 0) {
        target[n] = 0;
        if (strstr(target, "avpu"))
            type = DEV_AVPU;
        else if (!strcmp(target, "/dev/ipu"))
            type = DEV_IPU;
        else if (strstr(target, "isp") || strstr(target, "tx-isp") || strstr(target, "video"))
            type = DEV_ISP;
        else if (strstr(target, "rmem"))
            type = DEV_RMEM;
    }
    if (fd >= 0 && fd < (int)sizeof g_fdtype)
        g_fdtype[fd] = (uint8_t)(type + 1);
    return type;
}

/* Copy user memory without faulting on a bad pointer (EFAULT instead). */
static int safe_copy(void *dst, const void *src, size_t n)
{
    if (g_pipe[1] < 0 || write(g_pipe[1], src, n) != (ssize_t)n)
        return -1;
    return read(g_pipe[0], dst, n) == (ssize_t)n ? 0 : -1;
}

static void hexline(char *out, size_t cap, const uint8_t *p, size_t n)
{
    size_t used = 0;

    for (size_t i = 0; i < n && used + 3 < cap; i++)
        used += (size_t)snprintf(out + used, cap - used, "%02x", p[i]);
}

/* Dump a physical range through /dev/mem into DIR/<name>. */
static void dump_phys(const char *name, uint32_t phys, uint32_t len)
{
    char path[256];
    uint32_t base, off;
    void *map;
    FILE *f;

    if (!phys || !len || len > 16u << 20)
        return;
    if (g_devmem == -2)
        g_devmem = open("/dev/mem", O_RDONLY | O_SYNC);
    if (g_devmem < 0) {
        tlog("dump %s: /dev/mem unavailable (%s)", name, strerror(errno));
        return;
    }
    base = phys & ~0xfffu;
    off = phys - base;
    map = mmap(NULL, off + len, PROT_READ, MAP_SHARED, g_devmem, (off_t)base);
    if (map == MAP_FAILED) {
        tlog("dump %s: mmap 0x%08x+%u failed (%s)", name, phys, len, strerror(errno));
        return;
    }
    snprintf(path, sizeof path, "%s/%s", g_dir, name);
    f = fopen(path, "wb");
    if (f) {
        fwrite((uint8_t *)map + off, 1, len, f);
        fclose(f);
    }
    tlog("dump %s phys=0x%08x len=%u", name, phys, len);
    munmap(map, off + len);
}

static uint32_t reg(uint32_t id)
{
    return id >= 0x8000 && id < 0xa000 ? g_reg[(id - 0x8000) / 4] : 0;
}

static void trace_avpu(ioctl_req_t req, void *arg, int ret)
{
    uint32_t v[3] = {0, 0, 0};
    char name[64];

    if (req == AVPU_WRITE_REG || req == AVPU_READ_REG) {
        if (safe_copy(v, arg, 8))
            return;
        tlog("avpu %s 0x%04x 0x%08x ret=%d", req == AVPU_WRITE_REG ? "W" : "R", v[0], v[1], ret);
        if (req == AVPU_WRITE_REG && v[0] >= 0x8000 && v[0] < 0xa000)
            g_reg[(v[0] - 0x8000) / 4] = v[1];
        if (req == AVPU_WRITE_REG && v[0] == 0x85e4 && v[1] == 1) {
            tlog("jpeg start: cmd %08x %08x %08x %08x src %08x/%08x ep1 %08x strm %08x size %08x off %08x avail %08x",
                 reg(0x8400), reg(0x8404), reg(0x8408), reg(0x840c), reg(0x8410), reg(0x8414),
                 reg(0x8418), reg(0x841c), reg(0x8420), reg(0x8424), reg(0x8428));
            if (g_dumps_ep1 < g_max_dumps) {
                snprintf(name, sizeof name, "ep1-%03d.bin", g_dumps_ep1++);
                dump_phys(name, reg(0x8418), 0x790);
            }
            g_jpeg_pending = 1;
        }
        if (req == AVPU_READ_REG && v[0] == 0x8434 && g_jpeg_pending && ret == 0) {
            g_jpeg_pending = 0;
            if (g_dumps_jpeg < g_max_dumps && v[1] && v[1] <= reg(0x8428)) {
                snprintf(name, sizeof name, "jpeg-%03d.bin", g_dumps_jpeg++);
                dump_phys(name, reg(0x841c) + reg(0x8424), v[1]);
            }
        }
    } else if (req == AVPU_WAIT_IRQ) {
        if (!safe_copy(v, arg, 4))
            tlog("avpu WAIT_IRQ -> slot %d ret=%d", (int)v[0], ret);
    } else if (req == AVPU_GET_DMA_MMAP || req == AVPU_GET_DMA_FD || req == AVPU_GET_DMA_PHY) {
        if (!safe_copy(v, arg, 12))
            tlog("avpu DMA req=0x%08lx fd/off=0x%x size=%u phys=0x%08x ret=%d",
                 (unsigned long)req, v[0], v[1], v[2], ret);
    } else {
        tlog("avpu ioctl 0x%08lx ret=%d", (unsigned long)req, ret);
    }
}

static void trace_ipu(ioctl_req_t req, void *arg, int ret)
{
    uint32_t p[38];
    char line[512], name[64];
    size_t used = 0;

    if (req == IPU_FLUSH_CACHE) {
        if (!safe_copy(p, arg, 8))
            tlog("ipu FLUSH_CACHE addr=0x%08x size=%u ret=%d", p[0], p[1], ret);
        return;
    }
    if (req != IPU_START || safe_copy(p, arg, sizeof p)) {
        tlog("ipu ioctl 0x%08lx ret=%d", (unsigned long)req, ret);
        return;
    }
    for (int i = 0; i < 38; i++)
        used += (size_t)snprintf(line + used, sizeof line - used, " %08x", p[i]);
    tlog("ipu START ret=%d words:%s", ret, line);
    for (int ch = 0; ch < 4; ch++) {
        const uint32_t *c = p + 6 + 8 * ch;         /* fmt para bak pos_x pos_y w h buf */
        uint32_t bpp4 = c[0] == 6 || c[0] == 0x1a ? 8 : c[0] == 0x18 || c[0] == 0x19 ? 6 : 16;

        if (!(p[0] >> ch & 1))
            continue;
        tlog("ipu  ch%d fmt=0x%x para=0x%08x bak_argb=0x%08x pos=(%u,%u) %ux%u buf=0x%08x",
             ch, c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7]);
        if (c[7] && g_dumps_osd < g_max_dumps) {
            snprintf(name, sizeof name, "osd-%03d-ch%d-%ux%u-fmt%x.bin", g_dumps_osd, ch,
                     c[5], c[6], c[0]);
            dump_phys(name, c[7], c[5] * c[6] * bpp4 / 4);
        }
    }
    if (g_dumps_osd < g_max_dumps)
        g_dumps_osd++;
}

static void trace_isp(ioctl_req_t req, void *arg, int ret)
{
    uint32_t q[4] = {0, 0, 0, 0};
    uint8_t blob[0x80];
    char hex[0x80 * 2 + 1] = "";

    if ((req != ISP_TUNING && req != ISP_TUNING_T23) ||
        safe_copy(q, arg, req == ISP_TUNING ? 12 : 16)) {
        if (g_all)
            tlog("isp ioctl 0x%08lx ret=%d", (unsigned long)req, ret);
        return;
    }
    if (q[2] >= 0x400000u && !safe_copy(blob, (void *)(uintptr_t)q[2], sizeof blob))
        hexline(hex, sizeof hex, blob, sizeof blob);
    tlog("isp TUNING cmd=%u cid=0x%08x val/ptr=0x%08x ret=%d%s%s", q[0], q[1], q[2], ret,
         *hex ? " ptr[0..0x7f]=" : "", hex);
}

int ioctl(int fd, ioctl_req_t req, ...)
{
    va_list ap;
    void *arg;
    int ret, type, saved;

    va_start(ap, req);
    arg = va_arg(ap, void *);
    va_end(ap);

    pthread_mutex_lock(&g_lock);
    init();
    pthread_mutex_unlock(&g_lock);
    if (!real_ioctl)
        return -1;
    ret = real_ioctl(fd, req, arg);
    saved = errno;

    pthread_mutex_lock(&g_lock);
    type = fd_type(fd);
    if (type == DEV_AVPU)
        trace_avpu(req, arg, ret);
    else if (type == DEV_IPU)
        trace_ipu(req, arg, ret);
    else if (type == DEV_ISP)
        trace_isp(req, arg, ret);
    else if (type == DEV_RMEM && req == RMEM_FLUSH) {
        uint32_t f[3];
        if (!safe_copy(f, arg, sizeof f))
            tlog("rmem FLUSH addr=0x%08x size=%u dir=%u ret=%d", f[0], f[1], f[2], ret);
    } else if (g_all) {
        tlog("fd%d ioctl 0x%08lx ret=%d", fd, (unsigned long)req, ret);
    }
    pthread_mutex_unlock(&g_lock);
    errno = saved;
    return ret;
}
