/*
 * FrameSource / VBM lifecycle test for the T-series frame-channel path
 * (src/framesource/framesource_tseries.c + src/kernel_interface.c), built
 * for T20, T21 and T23.
 *
 * The frame channel is a fake driver behind --wrap=open,close,ioctl,select:
 * it keeps the REQBUFS/QBUF/DQBUF/STREAMON/STREAMOFF state per fd, completes
 * a queued buffer at once, and counts what a real driver would not survive
 * (an ioctl on an fd that is not open, a QBUF of memory that has been freed,
 * a buffer left queued to a stream when its fd is closed). The reserved
 * memory is anonymous memory below 4 GiB (the frame record keeps 32-bit
 * addresses). Failures can be injected into every step of EnableChn.
 *
 * Meant to be run under ASan/UBSan and TSan as well (make fs-sanitize).
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

#include "imp/imp_framesource.h"
#include "core/module.h"
#include "dma_alloc.h"
#include "../fake_rmem.h"

/* ------------------------------------------------------------ helpers -- */

static int failures;
/* The library under test logs to stderr; the results go here (fd 2 stays
 * the real stderr, for the sanitizers). */
static FILE *report;

#define CHECK(cond, ...) do {                                           \
        if (!(cond)) {                                                  \
            failures++;                                                 \
            fprintf(report, "FAIL %s:%d: ", __FILE__, __LINE__);        \
            fprintf(report, __VA_ARGS__);                               \
            fputc('\n', report);                                        \
        }                                                               \
    } while (0)

static void sleep_ms(unsigned int ms)
{
    struct timespec ts = { ms / 1000u, (long)(ms % 1000u) * 1000000L };

    while (nanosleep(&ts, &ts) != 0 && errno == EINTR)
        ;
}

static uint64_t now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

/* ---------------------------------------------- fake reserved memory -- */

#define FAKE_DMA_MAX 64

static pthread_mutex_t dma_lock = PTHREAD_MUTEX_INITIALIZER;
static struct { uint32_t addr, size; } dma_live[FAKE_DMA_MAX];
static int dma_fail;            /* fail the next allocations */
static int dma_bad_free;

static int dma_alloc(IMPDMABufferInfo *info, int size)
{
    void *p;
    int i;

    if (!info || size <= 0)
        return -1;
    pthread_mutex_lock(&dma_lock);
    if (dma_fail) {
        dma_fail--;
        pthread_mutex_unlock(&dma_lock);
        return -1;
    }
    p = fake_rmem_map((size_t)size);
    for (i = 0; i < FAKE_DMA_MAX && dma_live[i].addr; i++)
        ;
    if (i == FAKE_DMA_MAX)
        abort();
    dma_live[i].addr = (uint32_t)(uintptr_t)p;
    dma_live[i].size = (uint32_t)size;
    pthread_mutex_unlock(&dma_lock);
    memset(info, 0, sizeof(*info));
    info->virt_addr = info->phys_addr = (uint32_t)(uintptr_t)p;
    info->size = (uint32_t)size;
    return 0;
}

int DMA_AllocDescriptor(IMPDMABufferInfo *info, int size, const char *tag)
{
    (void)tag;
    return dma_alloc(info, size);
}

int DMA_PoolAllocDescriptor(int pool, IMPDMABufferInfo *info, int size,
                            const char *tag)
{
    (void)pool;
    (void)tag;
    return dma_alloc(info, size);
}

int DMA_FreePhys(uint32_t phys)
{
    int i;

    pthread_mutex_lock(&dma_lock);
    for (i = 0; i < FAKE_DMA_MAX; i++) {
        if (dma_live[i].addr == phys) {
            fake_rmem_unmap((void *)(uintptr_t)phys, dma_live[i].size);
            dma_live[i].addr = 0;
            pthread_mutex_unlock(&dma_lock);
            return 0;
        }
    }
    dma_bad_free++;
    pthread_mutex_unlock(&dma_lock);
    return -1;
}

static int dma_contains(uint32_t phys, uint32_t len)
{
    int i, found = 0;

    pthread_mutex_lock(&dma_lock);
    for (i = 0; i < FAKE_DMA_MAX; i++)
        if (dma_live[i].addr && phys >= dma_live[i].addr &&
            (uint64_t)phys + len <= (uint64_t)dma_live[i].addr + dma_live[i].size)
            found = 1;
    pthread_mutex_unlock(&dma_lock);
    return found;
}

static int dma_count(void)
{
    int i, n = 0;

    pthread_mutex_lock(&dma_lock);
    for (i = 0; i < FAKE_DMA_MAX; i++)
        n += dma_live[i].addr != 0;
    pthread_mutex_unlock(&dma_lock);
    return n;
}

/* --------------------------------------------- fake frame channel -- */

#define FAKE_FDS 1024
#define FAKE_BUFS 64

enum {
    INJ_NONE, INJ_SET_FMT, INJ_REQBUFS, INJ_SET_DEPTH, INJ_QBUF_ALL,
    INJ_STREAM_ON, INJ_DMA,
};

struct fake_chan {
    int open;
    int streaming;
    unsigned int reqbufs;
    int queue[FAKE_BUFS];
    uint32_t queue_addr[FAKE_BUFS];
    int queued;
};

static pthread_mutex_t fake_lock = PTHREAD_MUTEX_INITIALIZER;
static struct fake_chan fake[FAKE_FDS];
static int fake_open_count;
static int fake_bad_ioctl;      /* ioctl on an fd that is not an open channel */
static int fake_qbuf_freed;     /* QBUF of memory that is not allocated */
static int fake_closed_streaming; /* closed while streaming */
static int fake_inject;
static int fake_streamoffs;
static unsigned int fake_dqbuf_delay_us; /* time spent "in the driver" */
/* The stock frame channel (T20, and the open tx-isp-t30 that copies it):
 * select() is always ready and DQBUF sleeps while no buffer is queued,
 * O_NONBLOCK ignored.  Off: a vb2 driver (EAGAIN, a real poll). */
static int fake_stock_dq;
/* frames the driver may still complete (-1: any number) and the capture
 * timestamp of the last one (40 ms apart, from 40 ms) */
static int fake_frame_budget = -1;
static unsigned int fake_frames_done;

int __real_open(const char *path, int flags, ...);
int __real_close(int fd);
int __real_ioctl(int fd, unsigned long request, void *arg);
int __real_select(int n, fd_set *r, fd_set *w, fd_set *e, struct timeval *tv);

int __wrap_open(const char *path, int flags, ...)
{
    va_list ap;
    int mode, fd;

    va_start(ap, flags);
    mode = va_arg(ap, int);
    va_end(ap);
    if (strncmp(path, "/dev/framechan", 14) && strncmp(path, "/dev/video", 10))
        return __real_open(path, flags, mode);
    fd = __real_open("/dev/null", O_RDWR | O_CLOEXEC, 0);
    if (fd < 0 || fd >= FAKE_FDS)
        abort();
    pthread_mutex_lock(&fake_lock);
    memset(&fake[fd], 0, sizeof(fake[fd]));
    fake[fd].open = 1;
    fake_open_count++;
    pthread_mutex_unlock(&fake_lock);
    return fd;
}

int __wrap_close(int fd)
{
    pthread_mutex_lock(&fake_lock);
    if (fd >= 0 && fd < FAKE_FDS && fake[fd].open) {
        if (fake[fd].streaming)
            fake_closed_streaming++;
        fake[fd].open = 0;
        fake_open_count--;
    }
    pthread_mutex_unlock(&fake_lock);
    return __real_close(fd);
}

static int fake_fail(int error)
{
    errno = error;
    return -1;
}

#if defined(PLATFORM_T30)
/* T30 (Lu-Fi/openimp#178): the vendor libimp sends the format as the
 * 0x5c-byte object 0xc05c56c3 / 0x405c56c4; any other form of the format
 * ioctl (the old 0x4c bytes) is counted. */
static uint32_t fake_fmt_set[0x5c / 4];
static int fake_fmt_sets, fake_fmt_gets, fake_fmt_wrong;
#endif

int __wrap_ioctl(int fd, unsigned long request, void *arg)
{
    struct fake_chan *c;
    uint32_t *words = arg;
    int ret = 0;

    /* A DQBUF that sleeps in the driver: STREAMOFF can come meanwhile. */
    if ((uint32_t)request == 0xc0445611u && fake_dqbuf_delay_us)
        usleep(fake_dqbuf_delay_us);
    if ((uint32_t)request == 0xc0445611u && fake_stock_dq) {
        int waited, ready = 0;

        /* sleep until a buffer is queued (the test bounds it: 4 s) */
        for (waited = 0; waited < 4000 && !ready; waited++) {
            pthread_mutex_lock(&fake_lock);
            ready = fd < 0 || fd >= FAKE_FDS || !fake[fd].open ||
                    !fake[fd].streaming || fake[fd].queued;
            pthread_mutex_unlock(&fake_lock);
            if (!ready)
                usleep(1000);
        }
    }
    pthread_mutex_lock(&fake_lock);
    if (fd < 0 || fd >= FAKE_FDS || !fake[fd].open) {
        fake_bad_ioctl++;
        pthread_mutex_unlock(&fake_lock);
        return fake_fail(EBADF);
    }
    c = &fake[fd];
    switch ((uint32_t)request) {
    case 0xc0145608u:                           /* REQBUFS */
        if (fake_inject == INJ_REQBUFS) {
            ret = fake_fail(ENOMEM);
            break;
        }
        if (words[0] > FAKE_BUFS)
            words[0] = FAKE_BUFS;
        c->reqbufs = words[0];
        c->queued = 0;
        break;
    case 0x800456c5u:                           /* SET_DEPTH */
        if (fake_inject == INJ_SET_DEPTH)
            ret = fake_fail(EINVAL);
        break;
    case 0x80045612u:                           /* STREAMON */
        if (fake_inject == INJ_STREAM_ON)
            ret = fake_fail(EIO);
        else
            c->streaming = 1;
        break;
    case 0x80045613u:                           /* STREAMOFF */
        c->streaming = 0;
        c->queued = 0;
        fake_streamoffs++;
        break;
    case 0xc044560fu: {                         /* QBUF */
        uint32_t index = words[0];
        uint32_t addr = words[0x34 / 4];
        uint32_t length = words[0x38 / 4];
        int i;

        if (fake_inject == INJ_QBUF_ALL || index >= c->reqbufs) {
            ret = fake_fail(EINVAL);
            break;
        }
        for (i = 0; i < c->queued; i++)
            if (c->queue[i] == (int)index)
                break;
        if (i < c->queued) {
            ret = fake_fail(EBUSY);
            break;
        }
        if (!dma_contains(addr, length))
            fake_qbuf_freed++;
        c->queue_addr[c->queued] = addr;
        c->queue[c->queued++] = (int)index;
        break;
    }
    case 0xc0445611u:                           /* DQBUF */
        if (!c->streaming) {
            /* vb2 (T20/T21): EINVAL; open tx-isp T23: EPIPE */
#if defined(PLATFORM_T23)
            ret = fake_fail(EPIPE);
#else
            ret = fake_fail(EINVAL);
#endif
        } else if (!c->queued || fake_frame_budget == 0) {
            ret = fake_fail(EAGAIN);
        } else {
            uint64_t ts_us;

            if (fake_frame_budget > 0)
                fake_frame_budget--;
            ts_us = 40000ull * ++fake_frames_done;
            words[0] = (uint32_t)c->queue[0];
            words[0x14 / 4] = (uint32_t)(ts_us / 1000000u);
            words[0x18 / 4] = (uint32_t)(ts_us % 1000000u);
            memmove(&c->queue[0], &c->queue[1],
                    (size_t)(c->queued - 1) * sizeof(c->queue[0]));
            memmove(&c->queue_addr[0], &c->queue_addr[1],
                    (size_t)(c->queued - 1) * sizeof(c->queue_addr[0]));
            c->queued--;
        }
        break;
#if defined(PLATFORM_T30)
    case 0xc05c56c3u:                           /* SET_FMT, 0x5c bytes */
        if (fake_inject == INJ_SET_FMT) {
            ret = fake_fail(EINVAL);
            break;
        }
        memcpy(fake_fmt_set, arg, sizeof(fake_fmt_set));
        fake_fmt_sets++;
        break;
    case 0x405c56c4u:                           /* GET_FMT, 0x5c bytes */
        fake_fmt_gets++;
        break;
#endif
    default:
        /* format, crop and scaler ioctls */
#if defined(PLATFORM_T30)
        if ((request & 0xffffu) == 0x56c3u || (request & 0xffffu) == 0x56c4u)
            fake_fmt_wrong++;
#endif
        if (fake_inject == INJ_SET_FMT)
            ret = fake_fail(EINVAL);
        break;
    }
    pthread_mutex_unlock(&fake_lock);
    return ret;
}

/* Readable when a buffer can be dequeued; a stopped stream reports an
 * error (readable), as vb2's poll does. Otherwise wait like a real select,
 * in short steps, up to its timeout. */
int __wrap_select(int n, fd_set *r, fd_set *w, fd_set *e, struct timeval *tv)
{
    long left_us = tv ? tv->tv_sec * 1000000L + tv->tv_usec : 1000000L;
    int fd;

    (void)w;
    (void)e;
    if (fake_stock_dq) {
        usleep(200);
        return 1;               /* always ready, as a device without poll */
    }
    for (;;) {
        int ready = 0;

        pthread_mutex_lock(&fake_lock);
        for (fd = 0; fd < n && fd < FAKE_FDS; fd++)
            if (r && FD_ISSET(fd, r) &&
                (!fake[fd].open || !fake[fd].streaming ||
                 (fake[fd].queued && fake_frame_budget != 0)))
                ready = 1;
        pthread_mutex_unlock(&fake_lock);
        if (ready)
            return 1;
        if (left_us <= 0) {
            if (r)
                FD_ZERO(r);
            return 0;
        }
        sleep_ms(1);
        left_us -= 1000;
    }
}

/* ------------------------------------- stubs for the rest of libimp -- */

uint32_t _gp;
struct FrameSourceState *gFrameSource;
Module *g_modules[6][6];

int IMP_Log_Get_Option(void) { return 0; }
int imp_log_fun(int level, int option, int type, ...)
{
    (void)level; (void)option; (void)type;
    return 0;
}
void *alloc_device(const char *name, size_t size)
{
    (void)name;
    return calloc(1, size);
}
void free_device(void *dev) { free(dev); }
int is_has_simd128(void) { return 0; }
int get_cpu_id(void) { return 0; }
int ISP_EnsureLinkStreamOn(int sensor) { (void)sensor; return 0; }
int IMP_FrameSource_GetPool(int chn) { (void)chn; return -1; }
int64_t IMP_System_GetTimeStamp(void) { return (int64_t)now_ms() * 1000; }
int64_t OpenIMP_P0_NormalizeMonotonicTimeStamp(uint64_t t) { return (int64_t)t; }
void *VBMGetInstance(void) { return NULL; }
int VBMDumpPoolInfo(void) { return 0; }
int DMA_RmemStats(size_t *used, size_t *size, size_t *largest_free)
{
    (void)used; (void)size; (void)largest_free;
    return -1;
}
void DMA_LogRmem(const char *reason) { (void)reason; }
void DMA_RmemStreamStarted(void) {}
static int dma_flushes;
int DMA_RmemFlushCache(void *virt_addr, uint32_t size, int dir)
{
    (void)virt_addr; (void)size; (void)dir;
    __atomic_add_fetch(&dma_flushes, 1, __ATOMIC_RELAXED);
    return 0;
}
int remove_observer_from_module(void *src, void *dst)
{
    (void)src; (void)dst;
    return 0;
}
#if defined(PLATFORM_T23)
int openimp_t23_persist_enabled(void) { return 0; }
void openimp_t23_persist_write(const char *buffer, size_t size)
{
    (void)buffer; (void)size;
}
#endif

/* create_group publishes the channel's module in g_modules[0][chn]; the
 * byte offsets FrameSource writes are those of the 32-bit layout, so the
 * module is just a large zeroed block. */
Subject *create_group(int32_t dev, int32_t chn, char *name, void *cb)
{
    Subject *s = calloc(1, 0x40);

    (void)name; (void)cb;
    if (!s)
        abort();
    if (dev != 0 || chn < 0 || chn >= 6) {
        CHECK(0, "create_group(%d, %d)", dev, chn);
        free(s);
        return NULL;
    }
    g_modules[0][chn] = calloc(1, 0x400);
    *(void **)((char *)s + 0x20) = g_modules[0][chn];
    return s;
}

int32_t destroy_group(Subject *s, int32_t dev)
{
    Module *m = *(Module **)((char *)s + 0x20);
    int i;

    (void)dev;
    for (i = 0; i < 6; i++)
        if (g_modules[0][i] == m)
            g_modules[0][i] = NULL;
    free(m);
    free(s);
    return 0;
}

/* The capture worker hands every frame to the bound consumers. The test
 * consumer may hold a lock and sleep with it, as an encoder does. */
static pthread_mutex_t consumer_lock = PTHREAD_MUTEX_INITIALIZER;
static int consumer_hold_ms;
static int consumer_frames;
/* size of the last frame record the consumers saw (0x08/0x0c) */
static uint32_t consumer_width, consumer_height;

int32_t notify_observers(Module *module, void *frame)
{
    (void)module;
    if (frame) {
        uint32_t w, h;

        memcpy(&w, (const uint8_t *)frame + 0x08, 4);
        memcpy(&h, (const uint8_t *)frame + 0x0c, 4);
        __atomic_store_n(&consumer_width, w, __ATOMIC_RELAXED);
        __atomic_store_n(&consumer_height, h, __ATOMIC_RELAXED);
    }
    __atomic_add_fetch(&consumer_frames, 1, __ATOMIC_RELAXED);
    if (consumer_hold_ms) {
        pthread_mutex_lock(&consumer_lock);
        usleep((useconds_t)consumer_hold_ms * 1000u);  /* cancellation point */
        pthread_mutex_unlock(&consumer_lock);
    }
    return 0;
}

int IMP_FrameSource_SetFrameDepthCopyType(int chn, int no_copy);

/* ---------------------------------------------------------- tests -- */

static IMPFSChnAttr attr_for(int w, int h, int vbs)
{
    IMPFSChnAttr a;

    memset(&a, 0, sizeof(a));
    a.picWidth = w;
    a.picHeight = h;
    a.pixFmt = PIX_FMT_NV12;
    a.outFrmRateNum = 25;
    a.outFrmRateDen = 1;
    a.nrVBs = vbs;
    a.type = FS_PHY_CHANNEL;
    return a;
}

static int fake_streaming_count(void)
{
    int fd, n = 0;

    pthread_mutex_lock(&fake_lock);
    for (fd = 0; fd < FAKE_FDS; fd++)
        n += fake[fd].open && fake[fd].streaming;
    pthread_mutex_unlock(&fake_lock);
    return n;
}

extern unsigned int OpenIMP_FS_DebugState(void);
extern const char *OpenIMP_FS_DebugHolder(void);

static void check_clean(const char *what)
{
    /* no stop in progress and nobody holds the FrameSource lock (#198) */
    CHECK((OpenIMP_FS_DebugState() & 0x0f0f0f0fu) == 0,
          "%s: DisableChn stage state 0x%x", what, OpenIMP_FS_DebugState());
    CHECK(OpenIMP_FS_DebugHolder() == NULL, "%s: g_fs_lock held by %s", what,
          OpenIMP_FS_DebugHolder() ? OpenIMP_FS_DebugHolder() : "-");
    CHECK(fake_open_count == 0, "%s: %d frame-channel fds left open", what,
          fake_open_count);
    CHECK(dma_count() == 0, "%s: %d DMA buffers left allocated", what,
          dma_count());
    CHECK(fake_bad_ioctl == 0, "%s: %d ioctls on a closed fd", what,
          fake_bad_ioctl);
    CHECK(fake_qbuf_freed == 0, "%s: %d QBUFs of freed memory", what,
          fake_qbuf_freed);
    CHECK(fake_closed_streaming == 0, "%s: %d fds closed while streaming",
          what, fake_closed_streaming);
    CHECK(dma_bad_free == 0, "%s: %d frees of unknown DMA", what,
          dma_bad_free);
}

/* create / enable / disable / destroy, many times, two channels */
static void test_cycles(void)
{
    IMPFSChnAttr main_attr = attr_for(1920, 1080, 2);
    IMPFSChnAttr sub_attr = attr_for(640, 360, 2);
    int i;

    for (i = 0; i < 40; i++) {
        CHECK(IMP_FrameSource_CreateChn(0, &main_attr) == 0, "create 0");
        CHECK(IMP_FrameSource_CreateChn(1, &sub_attr) == 0, "create 1");
        CHECK(IMP_FrameSource_EnableChn(0) == 0, "enable 0 (cycle %d)", i);
        CHECK(IMP_FrameSource_EnableChn(1) == 0, "enable 1 (cycle %d)", i);
        CHECK(fake_streaming_count() == 2, "both channels stream");
        sleep_ms(3);
        CHECK(IMP_FrameSource_DisableChn(1) == 0, "disable 1");
        CHECK(IMP_FrameSource_DisableChn(0) == 0, "disable 0");
        CHECK(IMP_FrameSource_DestroyChn(1) == 0, "destroy 1");
        CHECK(IMP_FrameSource_DestroyChn(0) == 0, "destroy 0");
    }
    check_clean("cycles");
}

/* The single live DMA buffer (address and size), 0 if there is not
 * exactly one. */
static uint32_t dma_only(uint32_t *size)
{
    uint32_t addr = 0;
    int i, n = 0;

    pthread_mutex_lock(&dma_lock);
    for (i = 0; i < FAKE_DMA_MAX; i++)
        if (dma_live[i].addr) {
            addr = dma_live[i].addr;
            *size = dma_live[i].size;
            n++;
        }
    pthread_mutex_unlock(&dma_lock);
    return n == 1 ? addr : 0;
}

/* T21/T20/T23 keep a disabled channel's pool memory for its next pool of the
 * same size (rmem fragmentation on timps' idle/re-enable cycles); a pool of
 * another size and DestroyChn free it.  Other SoCs free it at once. */
static void test_park(void)
{
    IMPFSChnAttr main_attr = attr_for(1920, 1080, 2);
    IMPFSChnAttr small_attr = attr_for(640, 360, 2);
    uint32_t addr, size = 0, size2 = 0;
    int i;

    CHECK(IMP_FrameSource_CreateChn(0, &main_attr) == 0, "create");
    CHECK(IMP_FrameSource_EnableChn(0) == 0, "enable");
    addr = dma_only(&size);
    CHECK(addr != 0, "one pool buffer while enabled (%d)", dma_count());
    for (i = 0; i < 5; i++) {
        CHECK(IMP_FrameSource_DisableChn(0) == 0, "disable (cycle %d)", i);
#if defined(PLATFORM_T21) || defined(PLATFORM_T23)
        CHECK(dma_only(&size2) == addr && size2 == size,
              "disabled: pool block parked (cycle %d)", i);
#else
        CHECK(dma_count() == 0, "disabled: pool freed (cycle %d)", i);
#endif
        CHECK(IMP_FrameSource_EnableChn(0) == 0, "re-enable (cycle %d)", i);
#if defined(PLATFORM_T21) || defined(PLATFORM_T23)
        CHECK(dma_only(&size2) == addr && size2 == size,
              "re-enable reuses the parked block (cycle %d)", i);
#else
        CHECK(dma_count() == 1, "re-enabled: one pool (cycle %d)", i);
#endif
    }
    /* another size while disabled: the parked block goes, a new one comes */
    CHECK(IMP_FrameSource_DisableChn(0) == 0, "disable");
    CHECK(IMP_FrameSource_SetChnAttr(0, &small_attr) == 0, "set small attr");
    CHECK(IMP_FrameSource_EnableChn(0) == 0, "enable small");
    CHECK(dma_only(&size2) != 0 && size2 < size,
          "small pool: one buffer of %u bytes (was %u)", size2, size);
    CHECK(IMP_FrameSource_DisableChn(0) == 0, "disable small");
    CHECK(IMP_FrameSource_DestroyChn(0) == 0, "destroy");
    check_clean("park");
}

/* A failure in any EnableChn step leaves the channel created, nothing
 * open or allocated, and the next EnableChn works. */
static void test_enable_failures(void)
{
    static const struct { int inject; const char *name; } steps[] = {
        { INJ_SET_FMT, "set-format" },
        { INJ_REQBUFS, "reqbufs" },
#if !defined(PLATFORM_T21)
        { INJ_SET_DEPTH, "set-depth" },
#endif
        { INJ_STREAM_ON, "stream-on" },
        { INJ_QBUF_ALL, "qbuf (channel 0 with nothing queued)" },
        { INJ_DMA, "pool allocation" },
    };
    IMPFSChnAttr a = attr_for(640, 360, 2);
    size_t i;

    for (i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
        int state = -1;

        CHECK(IMP_FrameSource_CreateChn(0, &a) == 0, "create");
        if (steps[i].inject == INJ_DMA)
            dma_fail = 1;
        else
            fake_inject = steps[i].inject;
        CHECK(IMP_FrameSource_EnableChn(0) != 0, "enable with %s failure",
              steps[i].name);
        fake_inject = INJ_NONE;
        dma_fail = 0;
        IMP_FrameSource_ChnStatQuery(0, &state);
        CHECK(state == 1, "%s failure: state %d, want 1", steps[i].name,
              state);
        CHECK(fake_open_count == 0 && dma_count() == 0,
              "%s failure leaks %d fds, %d DMA buffers", steps[i].name,
              fake_open_count, dma_count());
        CHECK(IMP_FrameSource_EnableChn(0) == 0, "enable after %s failure",
              steps[i].name);
        CHECK(IMP_FrameSource_DisableChn(0) == 0, "disable");
        CHECK(IMP_FrameSource_DestroyChn(0) == 0, "destroy");
    }
    check_clean("enable failures");
}

/* CreateChn on a channel that is enabled must not lose its fd: the
 * following DisableChn has to stop the stream and close it before the pool
 * memory goes back. */
static void test_create_while_enabled(void)
{
    IMPFSChnAttr a = attr_for(640, 360, 2);

    CHECK(IMP_FrameSource_CreateChn(0, &a) == 0, "create");
    CHECK(IMP_FrameSource_EnableChn(0) == 0, "enable");
    (void)IMP_FrameSource_CreateChn(0, &a);
    CHECK(IMP_FrameSource_DisableChn(0) == 0, "disable");
    CHECK(fake_streaming_count() == 0, "stream still on after DisableChn");
    CHECK(fake_open_count == 0, "fd still open after DisableChn");
    CHECK(IMP_FrameSource_DestroyChn(0) == 0, "destroy");
    /* clean up behind a failure so the next tests start from scratch */
    pthread_mutex_lock(&fake_lock);
    for (int fd = 0; fd < FAKE_FDS; fd++)
        fake[fd].streaming = 0;
    fake_closed_streaming = 0;
    pthread_mutex_unlock(&fake_lock);
}

/* Out-of-range channel numbers are refused, not used as an index. */
static void test_bad_channels(void)
{
    IMPFSChnAttr a = attr_for(640, 360, 2), out;
    int depth = 0;

    CHECK(IMP_FrameSource_CreateChn(0, &a) == 0, "create");
    CHECK(IMP_FrameSource_CreateChn(-1, &a) != 0, "create -1");
    CHECK(IMP_FrameSource_DestroyChn(-1) != 0, "destroy -1");
    CHECK(IMP_FrameSource_SetChnAttr(-1, &a) != 0, "set attr -1");
    CHECK(IMP_FrameSource_GetChnAttr(-1, &out) != 0, "get attr -1");
    CHECK(IMP_FrameSource_SetFrameDepth(-1, 2) != 0, "set depth -1");
    CHECK(IMP_FrameSource_GetFrameDepth(-1, &depth) != 0, "get depth -1");
    CHECK(IMP_FrameSource_SetFrameDepthCopyType(-1, 1) != 0, "copy type -1");
    CHECK(IMP_FrameSource_DestroyChn(0) == 0, "destroy");
    /* as the vendor: no channel there, nothing to do */
    CHECK(IMP_FrameSource_DestroyChn(0) == 0, "destroy twice");
    CHECK(IMP_FrameSource_DestroyChn(3) == 0, "destroy never created");
}

/* A frame a consumer still holds when the channel is disabled (and the
 * channel enabled again) is refused on release instead of being looked up
 * in freed memory. */
static void test_stale_release(void)
{
    IMPFSChnAttr a = attr_for(640, 360, 2);
    void *frame = NULL;
    uint64_t deadline;

    CHECK(IMP_FrameSource_CreateChn(0, &a) == 0, "create");
    CHECK(IMP_FrameSource_EnableChn(0) == 0, "enable");
    deadline = now_ms() + 2000u;
    while (now_ms() < deadline &&
           (IMP_FrameSource_GetFrame(0, &frame) != 0 || !frame))
        sleep_ms(1);
    CHECK(frame != NULL, "no frame captured");
    CHECK(IMP_FrameSource_DisableChn(0) == 0, "disable");
    CHECK(IMP_FrameSource_EnableChn(0) == 0, "enable again");
    if (frame)
        CHECK(IMP_FrameSource_ReleaseFrame(0, frame) != 0,
              "release of a frame from the previous pool accepted");
    CHECK(IMP_FrameSource_DisableChn(0) == 0, "disable");
    CHECK(IMP_FrameSource_DestroyChn(0) == 0, "destroy");
    check_clean("stale release");
}

/* GetFrame waits for a frame (stock T20 3.12.0, T21 1.0.33, T30 1.0.5,
 * T31 1.1.6 and T23 1.3.0: up to 2 s on the depth list): the very first
 * call after EnableChn must return a frame without a retry loop. */
static void test_getframe_wait(void)
{
    IMPFSChnAttr a = attr_for(640, 360, 2);
    void *frame = NULL;

    CHECK(IMP_FrameSource_CreateChn(0, &a) == 0, "create");
    CHECK(IMP_FrameSource_EnableChn(0) == 0, "enable");
    CHECK(IMP_FrameSource_GetFrame(0, &frame) == 0 && frame != NULL,
          "first GetFrame after EnableChn waits for a frame");
    if (frame)
        CHECK(IMP_FrameSource_ReleaseFrame(0, frame) == 0, "release");
    CHECK(IMP_FrameSource_DisableChn(0) == 0, "disable");
    CHECK(IMP_FrameSource_GetFrame(0, &frame) != 0, "GetFrame on a stopped channel fails at once");
    CHECK(IMP_FrameSource_DestroyChn(0) == 0, "destroy");
    check_clean("getframe wait");
}

/* An application thread pulls and releases frames while the channel is
 * enabled and disabled under it. */
static int puller_stop;
static unsigned int puller_frames;

static void *puller(void *arg)
{
    (void)arg;
    while (!__atomic_load_n(&puller_stop, __ATOMIC_RELAXED)) {
        void *frame = NULL;

        if (IMP_FrameSource_GetFrame(0, &frame) == 0 && frame) {
            __atomic_add_fetch(&puller_frames, 1u, __ATOMIC_RELAXED);
            IMP_FrameSource_ReleaseFrame(0, frame);
        }
        sched_yield();
    }
    return NULL;
}

static void test_pull_during_disable(void)
{
    IMPFSChnAttr a = attr_for(640, 360, 2);
    pthread_t thread;
    int i;

    CHECK(IMP_FrameSource_CreateChn(0, &a) == 0, "create");
    __atomic_store_n(&puller_stop, 0, __ATOMIC_RELAXED);
    pthread_create(&thread, NULL, puller, NULL);
    for (i = 0; i < 60; i++) {
        CHECK(IMP_FrameSource_EnableChn(0) == 0, "enable");
        sleep_ms(2);
        CHECK(IMP_FrameSource_DisableChn(0) == 0, "disable");
    }
    __atomic_store_n(&puller_stop, 1, __ATOMIC_RELAXED);
    pthread_join(thread, NULL);
    CHECK(IMP_FrameSource_DestroyChn(0) == 0, "destroy");
    CHECK(puller_frames > 0, "puller never got a frame");
    check_clean("pull during disable");
}

/* The delay FIFO of SetMaxDelay/SetDelay/SetChnFifoAttr/GetTimedFrame:
 * nrVBs + maxdelay buffers, the newest `delay` frames held back, the one
 * nearest to a timestamp copied out. */
static unsigned int frames_done(void)
{
    unsigned int n;

    pthread_mutex_lock(&fake_lock);
    n = fake_frames_done;
    pthread_mutex_unlock(&fake_lock);
    return n;
}

/* the one open frame channel (-1 if not exactly one) */
static int open_chan_fd(int chn)
{
    int fd, found = -1, n = 0;

    (void)chn;
    pthread_mutex_lock(&fake_lock);
    for (fd = 0; fd < FAKE_FDS; fd++)
        if (fake[fd].open) {
            found = fd;
            n++;
        }
    pthread_mutex_unlock(&fake_lock);
    return n == 1 ? found : -1;
}

static void give_frames(int n)
{
    unsigned int want = frames_done() + (unsigned int)n;
    uint64_t until = now_ms() + 2000;

    pthread_mutex_lock(&fake_lock);
    fake_frame_budget = n;
    pthread_mutex_unlock(&fake_lock);
    while (frames_done() < want && now_ms() < until)
        sleep_ms(1);
    sleep_ms(5);            /* the worker files the last one */
}

static void test_delay_fifo(void)
{
    IMPFSChnAttr a = attr_for(64, 40, 2);
    IMPFSChnFifoAttr fifo = { 0 };
    IMPFrameInfo info;
    IMPFrameTimestamp ts = { 0, 0, 0 };
    static uint8_t data[64 * 40 * 3 / 2];
    int value = -1, fd;
    unsigned int base, plain_bufs;

    /* without SetMaxDelay nothing changes: two buffers, no FIFO */
    CHECK(IMP_FrameSource_CreateChn(0, &a) == 0, "create");
    CHECK(IMP_FrameSource_GetMaxDelay(0, &value) == 0 && value == 0,
          "default maxdelay %d", value);
    CHECK(IMP_FrameSource_SetDelay(0, 1) != 0, "delay above maxdelay 0");
    CHECK(IMP_FrameSource_EnableChn(0) == 0, "enable without FIFO");
    fd = open_chan_fd(0);
    /* nrVBs 2 (+ a frame depth an earlier test may have left) */
    plain_bufs = fd >= 0 ? fake[fd].reqbufs : 0;
    CHECK(plain_bufs >= 2 && plain_bufs <= 3, "%u buffers without FIFO",
          plain_bufs);
    CHECK(IMP_FrameSource_GetTimedFrame(0, &ts, 0, data, &info) == -1,
          "GetTimedFrame without FIFO");
    CHECK(IMP_FrameSource_SetMaxDelay(0, 2) != 0, "SetMaxDelay while enabled");
    CHECK(IMP_FrameSource_DisableChn(0) == 0, "disable");

    /* SetChnFifoAttr: DATA_PRIORITY refused, maxdepth 0 a no-op */
    fifo.maxdepth = 2;
    fifo.type = FIFO_DATA_PRIORITY;
    CHECK(IMP_FrameSource_SetChnFifoAttr(0, &fifo) != 0,
          "FIFO_DATA_PRIORITY accepted");
    fifo.maxdepth = 0;
    CHECK(IMP_FrameSource_SetChnFifoAttr(0, &fifo) == 0, "maxdepth 0");
    CHECK(IMP_FrameSource_SetMaxDelay(0, 101) != 0 &&
          IMP_FrameSource_SetMaxDelay(0, 32) != 0, "maxdelay out of range");
    /* FIFO_CACHE_PRIORITY maxdepth 3: maxdelay 3, delay 3; delay to 2 */
    fifo.maxdepth = 3;
    fifo.type = FIFO_CACHE_PRIORITY;
    CHECK(IMP_FrameSource_SetChnFifoAttr(0, &fifo) == 0, "SetChnFifoAttr");
    CHECK(IMP_FrameSource_GetMaxDelay(0, &value) == 0 && value == 3,
          "maxdelay %d", value);
    CHECK(IMP_FrameSource_GetDelay(0, &value) == 0 && value == 3,
          "delay %d", value);
    CHECK(IMP_FrameSource_SetDelay(0, 4) != 0 &&
          IMP_FrameSource_SetDelay(0, 2) == 0, "SetDelay");

    pthread_mutex_lock(&fake_lock);
    fake_frame_budget = 0;
    pthread_mutex_unlock(&fake_lock);
    CHECK(IMP_FrameSource_EnableChn(0) == 0, "enable with FIFO");
    fd = open_chan_fd(0);
    CHECK(fd >= 0 && fake[fd].reqbufs == plain_bufs + 3,
          "%u buffers, want %u + maxdelay 3", fd >= 0 ? fake[fd].reqbufs : 0,
          plain_bufs);
    base = frames_done();
    give_frames(5);
    CHECK(frames_done() == base + 5, "%u frames captured, want 5",
          frames_done() - base);
    /* held: the two newest (base+4, base+5); the readers got the rest */
    ts.ts = 40000ull * (base + 5) + 1000000ull;
    CHECK(IMP_FrameSource_GetTimedFrame(0, &ts, 0, data, &info) == -2,
          "frame from the future: not -2");
    ts.ts = 40000ull * (base + 3);
    CHECK(IMP_FrameSource_GetTimedFrame(0, &ts, 0, data, &info) == -1,
          "frame already handed on: not -1");
    ts.ts = 40000ull * (base + 4) + 10000;
    memset(&info, 0, sizeof(info));
    CHECK(IMP_FrameSource_GetTimedFrame(0, &ts, 0, data, &info) == 0 &&
          info.timeStamp == (int64_t)(40000ull * (base + 4)) &&
          info.width == 64 && info.height == 40 &&
          info.size == 64 * 40 * 3 / 2,
          "nearest held frame: ts %lld size %u", (long long)info.timeStamp,
          info.size);
    ts.ts = 40000ull * (base + 5);
    CHECK(IMP_FrameSource_GetTimedFrame(0, &ts, 0, NULL, &info) == 0 &&
          info.timeStamp == (int64_t)ts.ts, "exact newest");
    /* block: the frame comes within the wait */
    ts.ts = 40000ull * (base + 6);
    pthread_mutex_lock(&fake_lock);
    fake_frame_budget = 1;
    pthread_mutex_unlock(&fake_lock);
    CHECK(IMP_FrameSource_GetTimedFrame(0, &ts, 1, data, &info) == 0 &&
          info.timeStamp == (int64_t)ts.ts, "blocking GetTimedFrame");
    /* SetDelay while running */
    CHECK(IMP_FrameSource_SetDelay(0, 1) == 0, "SetDelay running");
    CHECK(IMP_FrameSource_DisableChn(0) == 0, "disable with FIFO");
    pthread_mutex_lock(&fake_lock);
    fake_frame_budget = -1;
    pthread_mutex_unlock(&fake_lock);
    CHECK(IMP_FrameSource_GetTimedFrame(0, &ts, 0, data, &info) == -1,
          "GetTimedFrame after disable");
    /* the FIFO keeps working over enable cycles with frames flowing */
    CHECK(IMP_FrameSource_EnableChn(0) == 0, "re-enable");
    sleep_ms(20);
    CHECK(IMP_FrameSource_DisableChn(0) == 0, "disable again");
    CHECK(IMP_FrameSource_DestroyChn(0) == 0, "destroy");
    check_clean("delay fifo");
}

/* SetFrameDepth from an API thread while the channel goes up and down
 * must never issue its ioctl on a closed (or reused) fd. */
static int depth_stop;

static void *depth_setter(void *arg)
{
    (void)arg;
    while (!__atomic_load_n(&depth_stop, __ATOMIC_RELAXED)) {
        IMP_FrameSource_SetFrameDepth(0, 1);
        sched_yield();
    }
    return NULL;
}

static void test_depth_during_disable(void)
{
    IMPFSChnAttr a = attr_for(640, 360, 2);
    pthread_t thread;
    int i;

    CHECK(IMP_FrameSource_CreateChn(0, &a) == 0, "create");
    __atomic_store_n(&depth_stop, 0, __ATOMIC_RELAXED);
    pthread_create(&thread, NULL, depth_setter, NULL);
    for (i = 0; i < 60; i++) {
        CHECK(IMP_FrameSource_EnableChn(0) == 0, "enable");
        CHECK(IMP_FrameSource_DisableChn(0) == 0, "disable");
    }
    __atomic_store_n(&depth_stop, 1, __ATOMIC_RELAXED);
    pthread_join(thread, NULL);
    /* The depth lists overlap the host-sized channel mutex in the 32-bit
     * channel layout: clear the depth before SetFrameDepth(0) walks them. */
    *(int32_t *)((char *)gFrameSource + 0x1cc) = 0;
    CHECK(IMP_FrameSource_DestroyChn(0) == 0, "destroy");
    check_clean("depth during disable");
}

/* DisableChn while the worker is inside a consumer that holds a lock and
 * sleeps: the worker must leave on its own, not be cancelled with the
 * consumer's lock held. */
static void test_disable_during_delivery(void)
{
    IMPFSChnAttr a = attr_for(640, 360, 2);
    pthread_t thread;
    int i, held = 0, delivered = 0;

    CHECK(IMP_FrameSource_CreateChn(0, &a) == 0, "create");
    consumer_hold_ms = 30;
    /* A reader that keeps pulling: without one the worker hands every
     * buffer straight back to the driver and notifies nobody. */
    __atomic_store_n(&puller_stop, 0, __ATOMIC_RELAXED);
    pthread_create(&thread, NULL, puller, NULL);
    for (i = 0; i < 5 && !held; i++) {
        int start = __atomic_load_n(&consumer_frames, __ATOMIC_RELAXED);
        uint64_t deadline = now_ms() + 2000u;

        CHECK(IMP_FrameSource_EnableChn(0) == 0, "enable");
        while (now_ms() < deadline &&
               __atomic_load_n(&consumer_frames, __ATOMIC_RELAXED) == start)
            sleep_ms(1);
        delivered += __atomic_load_n(&consumer_frames, __ATOMIC_RELAXED) != start;
        sleep_ms(5);                /* the worker is now in the consumer */
        CHECK(IMP_FrameSource_DisableChn(0) == 0, "disable");
        if (pthread_mutex_trylock(&consumer_lock) == 0)
            pthread_mutex_unlock(&consumer_lock);
        else
            held = 1;
    }
    __atomic_store_n(&puller_stop, 1, __ATOMIC_RELAXED);
    pthread_join(thread, NULL);
    consumer_hold_ms = 0;
    CHECK(delivered == i, "frames reached the consumer in %d of %d runs",
          delivered, i);
    CHECK(!held, "the worker was cancelled holding the consumer's lock");
    CHECK(IMP_FrameSource_DestroyChn(0) == 0, "destroy");
    check_clean("disable during delivery");
}

/* DisableChn while the worker sits in DQBUF: the driver ends that DQBUF
 * with EINVAL (vb2) or EPIPE (open tx-isp T23) once STREAMOFF has stopped
 * the queue. That is the normal end
 * of a stop, not an error to report (seen on the Wyze T20 when timps idles
 * channel 0: "DQBUF failed ... Invalid argument" + "DQBUF error ret=-1"). */
static void test_disable_during_dqbuf(void)
{
    IMPFSChnAttr a = attr_for(640, 360, 2);
    FILE *saved = stderr, *log = tmpfile();
    char line[512];
    int i, errors = 0, streamoffs;

    if (!log)
        return;
    CHECK(IMP_FrameSource_CreateChn(0, &a) == 0, "create");
    fake_dqbuf_delay_us = 3000;
    streamoffs = fake_streamoffs;
    for (i = 0; i < 20; i++) {
        CHECK(IMP_FrameSource_EnableChn(0) == 0, "enable");
        sleep_ms(10);
        stderr = log;
        CHECK(IMP_FrameSource_DisableChn(0) == 0, "disable");
        stderr = saved;
    }
    fake_dqbuf_delay_us = 0;
    CHECK(fake_streamoffs - streamoffs == 20, "STREAMOFF per disable");
    rewind(log);
    while (fgets(line, sizeof(line), log))
        if (strstr(line, "DQBUF failed") || strstr(line, "DQBUF error"))
            errors++;
    fclose(log);
    CHECK(errors == 0, "%d DQBUF error lines for DQBUFs ended by STREAMOFF",
          errors);
    CHECK(IMP_FrameSource_DestroyChn(0) == 0, "destroy");
    check_clean("disable during dqbuf");
}

#if !defined(PLATFORM_T23) && !defined(PLATFORM_T30)
#include "framesource/nv12_rotate.h"

void openimp_fs_rotate_capture(int chn, void *frame);

/* Reference rotation of an NV12 frame (pitch = width, chroma after
 * ALIGN16(height) luma lines; sizes multiples of 16 here), straight from
 * the definitions: CW (x,y) -> (h-1-y, x), CCW (x,y) -> (y, w-1-x). */
static void ref_rotate(const uint8_t *src, uint8_t *dst, uint32_t w,
                       uint32_t h, int cw)
{
    uint32_t p, x, y;

    for (p = 0; p < 2; p++) {
        uint32_t pw = p ? w / 2 : w, ph = p ? h / 2 : h, bpp = p ? 2 : 1;
        const uint8_t *s = src + (p ? (size_t)w * h : 0);
        uint8_t *d = dst + (p ? (size_t)w * h : 0);
        uint32_t dp = h;            /* output pitch: the source height */

        for (y = 0; y < ph; y++)
            for (x = 0; x < pw; x++) {
                uint32_t dx = cw ? ph - 1 - y : y;
                uint32_t dy = cw ? x : pw - 1 - x;

                memcpy(d + (size_t)dy * dp + dx * bpp,
                       s + (size_t)y * w + x * bpp, bpp);
            }
    }
}

/* T10/T20/T21 SetChnRotate: geometry gate (16-aligned, sub-stream pixel
 * cap), the per-frame rotate on a frame record against the reference,
 * and the size the consumers see on a running channel. */
static void test_rotate(void)
{
    static const struct { uint32_t w, h; int mode; } cases[] = {
        { 640, 368, NV12_ROT_90_CW }, { 640, 368, NV12_ROT_90_CCW },
        { 704, 576, NV12_ROT_90_CW }, { 352, 288, NV12_ROT_90_CCW },
        { 64, 32, NV12_ROT_90_CW },
    };
    IMPFSChnAttr sub_attr = attr_for(640, 368, 2);
    uint8_t record[0x428];
    unsigned int i;
    int waited;
    pthread_t thread;

    /* gate: the vendor values only, even, 16-aligned, <= 704x576 */
    CHECK(IMP_FrameSource_SetChnRotate(1, 2, 640, 360) == -1,
          "640x360 (height not 16-aligned) must be refused");
    CHECK(IMP_FrameSource_SetChnRotate(0, 1, 1920, 1080) == -1,
          "1080p main stream must be refused");
    CHECK(IMP_FrameSource_SetChnRotate(0, 1, 1280, 720) == -1,
          "720p main stream must be refused");
    CHECK(IMP_FrameSource_SetChnRotate(1, 7, 640, 368) == -1, "bad mode");
    CHECK(IMP_FrameSource_SetChnRotate(5, 1, 640, 368) == -1, "bad channel");
    CHECK(IMP_FrameSource_SetChnRotate(0, 0, 1920, 1080) == 0, "rot 0");

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        uint32_t w = cases[i].w, h = cases[i].h, size = w * h * 3 / 2;
        uint32_t fourcc = 0x3231564eu, ow = 0, oh = 0, virt;
        uint8_t *buf = mmap(NULL, size, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
        uint8_t *orig = malloc(size), *want = malloc(size);
        uint32_t k, seed = 0x1234567u + i;

        if (buf == MAP_FAILED || !orig || !want)
            abort();
        for (k = 0; k < size; k++) {
            seed = seed * 1103515245u + 12345u;
            orig[k] = (uint8_t)(seed >> 16);
        }
        memcpy(buf, orig, size);
        ref_rotate(orig, want, w, h, cases[i].mode == NV12_ROT_90_CW);
        memset(record, 0, sizeof(record));
        virt = (uint32_t)(uintptr_t)buf;
        memcpy(record + 0x08, &w, 4);
        memcpy(record + 0x0c, &h, 4);
        memcpy(record + 0x10, &fourcc, 4);
        memcpy(record + 0x14, &size, 4);
        memcpy(record + 0x18, &virt, 4);
        memcpy(record + 0x1c, &virt, 4);
        CHECK(IMP_FrameSource_SetChnRotate(2, cases[i].mode, (int)w,
                                           (int)h) == 0,
              "%ux%u mode %d accepted", w, h, cases[i].mode);
        openimp_fs_rotate_capture(2, record);
        memcpy(&ow, record + 0x08, 4);
        memcpy(&oh, record + 0x0c, 4);
        CHECK(ow == h && oh == w, "%ux%u: record %ux%u after rotate", w, h,
              ow, oh);
        CHECK(memcmp(buf, want, size) == 0,
              "%ux%u mode %d: picture differs from the reference", w, h,
              cases[i].mode);
        /* the record now has the rotated size: the next frame of the
         * buffer is rotated again (it holds a new capture on a device) */
        memcpy(buf, orig, size);
        openimp_fs_rotate_capture(2, record);
        CHECK(memcmp(buf, want, size) == 0, "%ux%u: second frame", w, h);
        /* a too small buffer is left alone */
        memcpy(buf, orig, size);
        memcpy(record + 0x08, &w, 4);
        memcpy(record + 0x0c, &h, 4);
        k = size - 1;
        memcpy(record + 0x14, &k, 4);
        openimp_fs_rotate_capture(2, record);
        CHECK(memcmp(buf, orig, size) == 0, "%ux%u: short buffer touched",
              w, h);
        munmap(buf, size);
        free(orig);
        free(want);
    }
    CHECK(IMP_FrameSource_SetChnRotate(2, 0, 0, 0) == 0, "rot off");
    openimp_fs_rotate_capture(2, record);   /* frees the scratch */

    /* running channel: the consumers get the portrait record (the
     * puller keeps channel 0 delivering) */
    CHECK(IMP_FrameSource_SetChnRotate(0, NV12_ROT_90_CW, 640, 368) == 0,
          "sub stream rotate");
    CHECK(IMP_FrameSource_CreateChn(0, &sub_attr) == 0, "create 0");
    __atomic_store_n(&consumer_width, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&puller_stop, 0, __ATOMIC_RELAXED);
    pthread_create(&thread, NULL, puller, NULL);
    CHECK(IMP_FrameSource_EnableChn(0) == 0, "enable 0");
    for (waited = 0; waited < 2000 &&
         !__atomic_load_n(&consumer_width, __ATOMIC_RELAXED); waited++)
        sleep_ms(1);
    CHECK(__atomic_load_n(&consumer_width, __ATOMIC_RELAXED) == 368 &&
          __atomic_load_n(&consumer_height, __ATOMIC_RELAXED) == 640,
          "consumer saw %ux%u, want 368x640",
          __atomic_load_n(&consumer_width, __ATOMIC_RELAXED),
          __atomic_load_n(&consumer_height, __ATOMIC_RELAXED));
    CHECK(IMP_FrameSource_DisableChn(0) == 0, "disable 0");
    __atomic_store_n(&puller_stop, 1, __ATOMIC_RELAXED);
    pthread_join(thread, NULL);
    CHECK(IMP_FrameSource_DestroyChn(0) == 0, "destroy 0");
    CHECK(IMP_FrameSource_SetChnRotate(0, 0, 0, 0) == 0, "rot off 0");
    check_clean("rotate");
}
#else
/* T23 rotates in the caller (unbound YuvEncode), the T30 vendor libimp has no SetChnRotate: it refuses. */
static void test_rotate(void)
{
    CHECK(IMP_FrameSource_SetChnRotate(1, 2, 640, 368) == -1, "T23 refuses");
    CHECK(IMP_FrameSource_SetChnRotate(1, 0, 640, 368) == 0, "T23 rot 0");
}
#endif

#if defined(PLATFORM_T30)
/* T30 format ioctl: 0xc05c56c3 with the 0x5c-byte object of the vendor
 * libimp (type, 12-word pix header, crop, scaler, rate); the extra pix
 * words stay zero. */
static void test_format_abi(void)
{
    IMPFSChnAttr a = attr_for(1920, 1080, 2);
    static const uint32_t zero[4];
    const uint32_t *w = fake_fmt_set;

    a.crop.enable = 1;
    a.crop.left = 16;
    a.crop.top = 8;
    a.crop.width = 1600;
    a.crop.height = 900;
    a.scaler.enable = 1;
    a.scaler.outwidth = 640;
    a.scaler.outheight = 360;
    fake_fmt_sets = fake_fmt_gets = fake_fmt_wrong = 0;
    memset(fake_fmt_set, 0xa5, sizeof(fake_fmt_set));
    CHECK(IMP_FrameSource_CreateChn(0, &a) == 0, "format abi: create");
    CHECK(IMP_FrameSource_EnableChn(0) == 0, "format abi: enable");
    CHECK(fake_fmt_sets == 1, "format abi: %d 0xc05c56c3 calls", fake_fmt_sets);
    CHECK(fake_fmt_wrong == 0, "format abi: %d format ioctls of another form",
          fake_fmt_wrong);
    CHECK(w[0] == 1, "type %u", w[0]);
    CHECK(w[3] == 0x3231564eu, "pixelformat 0x%x", w[3]);
    CHECK(w[4] == 0, "field %u", w[4]);
    CHECK(w[5] == 0 && w[6] == 0, "bytesperline/sizeimage %u/%u", w[5], w[6]);
    CHECK(w[7] == 8, "colorspace %u", w[7]);
    CHECK(w[8] == 0, "priv %u", w[8]);
    CHECK(memcmp(&w[9], zero, sizeof(zero)) == 0,
          "extra pix words %x %x %x %x", w[9], w[10], w[11], w[12]);
    CHECK(w[13] == 1 && w[14] == 8 && w[15] == 16 && w[16] == 1600 &&
          w[17] == 900, "crop %u top %u left %u %ux%u", w[13], w[14], w[15],
          w[16], w[17]);
    CHECK(w[18] == 1 && w[19] == 640 && w[20] == 360, "scaler %u %ux%u",
          w[18], w[19], w[20]);
    CHECK(w[21] == 0 && w[22] == 1, "rate bits %u mask %u", w[21], w[22]);
    CHECK(IMP_FrameSource_DisableChn(0) == 0, "format abi: disable");
    CHECK(IMP_FrameSource_DestroyChn(0) == 0, "format abi: destroy");
    check_clean("format abi");
}
#endif

#if defined(PLATFORM_T30) || defined(PLATFORM_T20)
/* IVS as the only consumer (no reader pulls the channel) on the stock
 * frame channel.  A reader pulled a moment ago, so the first buffers are
 * parked in the ready queue; the driver has none left and DQBUF sleeps.
 * The capture worker must hand the parked buffers back once the pull window
 * (1 s) is over instead of sleeping in DQBUF for good: IVS keeps getting
 * frames (T30 camera, apitest ivs: 3 results, then none). */
int VBMGetFrame(int chn, void **frame);
static unsigned int ivs_hook_frames;

void openimp_t31_ivs_capture(int fs_chn, const void *frame)
{
    (void)fs_chn;
    (void)frame;
    __atomic_add_fetch(&ivs_hook_frames, 1u, __ATOMIC_RELAXED);
}

static void test_ivs_only_stock_dq(void)
{
    IMPFSChnAttr a = attr_for(640, 360, 2);
    void *frame = NULL;
    unsigned int at_start, waited;

    fake_stock_dq = 1;
    fake_frame_budget = -1;
    __atomic_store_n(&ivs_hook_frames, 0, __ATOMIC_RELAXED);
    pthread_t thread;

    CHECK(IMP_FrameSource_CreateChn(0, &a) == 0, "ivs only: create");
    CHECK(IMP_FrameSource_EnableChn(0) == 0, "ivs only: enable");
    /* a reader (the encoder of an earlier test) pulls for a moment and
     * leaves: the pull window (1 s) is open, so the next frames wait for
     * a reader in the ready queue */
    __atomic_store_n(&puller_stop, 0, __ATOMIC_RELAXED);
    pthread_create(&thread, NULL, puller, NULL);
    sleep_ms(100);
    __atomic_store_n(&puller_stop, 1, __ATOMIC_RELAXED);
    pthread_join(thread, NULL);
    (void)VBMGetFrame(0, &frame);
    sleep_ms(300);
    at_start = __atomic_load_n(&ivs_hook_frames, __ATOMIC_RELAXED);
    CHECK(at_start >= 1, "ivs only: no frame at all");
    for (waited = 0; waited < 3500 &&
         __atomic_load_n(&ivs_hook_frames, __ATOMIC_RELAXED) < at_start + 10;
         waited += 10)
        sleep_ms(10);
    CHECK(__atomic_load_n(&ivs_hook_frames, __ATOMIC_RELAXED) >= at_start + 10,
          "ivs only: %u frames after the pull window (capture asleep in DQBUF)",
          __atomic_load_n(&ivs_hook_frames, __ATOMIC_RELAXED));
    fake_stock_dq = 0;                  /* wakes nothing: the worker has a buffer by now */
    CHECK(IMP_FrameSource_DisableChn(0) == 0, "ivs only: disable");
    CHECK(IMP_FrameSource_DestroyChn(0) == 0, "ivs only: destroy");
    check_clean("ivs only");
}
#endif

int main(void)
{
    report = fdopen(dup(2), "w");
    if (!report)
        return 1;
    setvbuf(report, NULL, _IOLBF, 0);
    if (!getenv("FS_TEST_VERBOSE")) {
        FILE *null = fopen("/dev/null", "w");

        if (!null)
            return 1;
        stdout = null;
        stderr = null;
    }
    /* FS_TEST_ONLY=name[,name...] runs a subset (e.g. against an old
     * library where an earlier test would crash) */
#define RUN(name) do {                                                  \
        const char *only = getenv("FS_TEST_ONLY");                      \
        if (!only || strstr(only, #name))                               \
            test_##name();                                              \
    } while (0)
    RUN(cycles);
    RUN(enable_failures);
    RUN(park);
    RUN(create_while_enabled);
    RUN(bad_channels);
    RUN(stale_release);
    RUN(getframe_wait);
    RUN(pull_during_disable);
    RUN(depth_during_disable);
    RUN(disable_during_delivery);
    RUN(disable_during_dqbuf);
    RUN(delay_fifo);
    RUN(rotate);
#if defined(PLATFORM_T30)
    RUN(format_abi);
#endif
#if defined(PLATFORM_T30) || defined(PLATFORM_T20)
    RUN(ivs_only_stock_dq);
#endif
    if (failures) {
        fprintf(report, "fs_lifecycle_test: %d failure(s)\n", failures);
        return 1;
    }
    fprintf(report, "fs_lifecycle_test: all passed\n");
    return 0;
}
