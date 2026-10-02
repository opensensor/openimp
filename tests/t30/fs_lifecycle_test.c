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
    p = mmap(NULL, (size_t)size, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (p == MAP_FAILED) {
        pthread_mutex_unlock(&dma_lock);
        return -1;
    }
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
            munmap((void *)(uintptr_t)phys, dma_live[i].size);
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

int __wrap_ioctl(int fd, unsigned long request, void *arg)
{
    struct fake_chan *c;
    uint32_t *words = arg;
    int ret = 0;

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
            ret = fake_fail(EINVAL);
        } else if (!c->queued) {
            ret = fake_fail(EAGAIN);
        } else {
            words[0] = (uint32_t)c->queue[0];
            words[0x14 / 4] = 0;
            words[0x18 / 4] = 0;
            memmove(&c->queue[0], &c->queue[1],
                    (size_t)(c->queued - 1) * sizeof(c->queue[0]));
            memmove(&c->queue_addr[0], &c->queue_addr[1],
                    (size_t)(c->queued - 1) * sizeof(c->queue_addr[0]));
            c->queued--;
        }
        break;
    default:
        /* format, crop and scaler ioctls */
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
    for (;;) {
        int ready = 0;

        pthread_mutex_lock(&fake_lock);
        for (fd = 0; fd < n && fd < FAKE_FDS; fd++)
            if (r && FD_ISSET(fd, r) &&
                (!fake[fd].open || !fake[fd].streaming || fake[fd].queued))
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
void imp_log_fun(int level, int option, int type, ...)
{
    (void)level; (void)option; (void)type;
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

int32_t notify_observers(Module *module, void *frame)
{
    (void)module; (void)frame;
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

static void check_clean(const char *what)
{
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
    RUN(create_while_enabled);
    RUN(bad_channels);
    RUN(stale_release);
    RUN(pull_during_disable);
    RUN(depth_during_disable);
    RUN(disable_during_delivery);
    if (failures) {
        fprintf(report, "fs_lifecycle_test: %d failure(s)\n", failures);
        return 1;
    }
    fprintf(report, "fs_lifecycle_test: all passed\n");
    return 0;
}
