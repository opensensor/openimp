/* T31/T23/T21/T20/T30 IVS framework (IMP_IVS_*) and the move / base-move
 * interfaces.
 *
 * Semantics follow the T31 1.1.6 libimp; the T23 1.3.0 libimp has the same
 * framework (ivs.c), the same move / base-move code (the T31 scalar path:
 * T23 has no MXU2 branch) and differs only in the IMPFrameInfo layout, see
 * openimp_t31_ivs_abi.h. T20/T21/T30 build it with the legacy (0x28-byte)
 * IMPFrameInfo; the OEM T20 3.12.0 libimp exports the same IMP_IVS_* and
 * move / base-move entry points with that parameter layout.
 * PLATFORM_T23 builds this file for T23:
 *   - one IVS group (0), up to 64 channels, each with its own processing
 *     thread and three semaphores (process start = 0, process end = 1,
 *     result = 0);
 *   - every frame of the FrameSource channel bound to the group reaches
 *     each receiving channel in capture context; a channel still busy with
 *     the previous frame drops it;
 *   - SetParam copies into interface->param and takes effect on the next
 *     accepted frame; GetParam returns the handler's current copy;
 *   - move and base move keep six results in a ring; GetResult hands out a
 *     pointer into it without checking that a result is pending,
 *     ReleaseResult is a no-op.
 *
 * Frame hook: OpenIMP's IMP_System_Bind only records the bind, and the
 * T31 encoder pulls frames from the FrameSource ready queue. The capture
 * thread therefore calls openimp_t31_ivs_capture() for every dequeued
 * frame before it is queued for the encoder: the frame is stable (the
 * encoder cannot have released it or blended OSD into it yet) and every
 * IVS channel sees the full sensor-side frame rate, as with the vendor.
 * Only the copy happens there (move: the 2:1 decimation of frames that
 * take part in a comparison; base move: the luma of capture frames); the
 * comparison runs in the channel thread, outside any encoder lock.
 *
 * Interfaces created elsewhere (streamer-defined algorithms) get
 * preProcessSync on the capture frame and processAsync on a private NV12
 * copy, since OpenIMP cannot hold a capture buffer for them. */

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <time.h>
#include <unistd.h>

#include "dma_alloc.h"
#include "imp_log_int.h"
#include "imp/imp_common.h"

#include "openimp_t31_ivs.h"
#include "openimp_t31_ivs_abi.h"
#include "openimp_t31_ivs_move.h"

#define T31_IVS_GROUPS   1
#define T31_IVS_CHANNELS 64
#define T31_IVS_RESULTS  6

extern int IMP_System_GetBindbyDest(IMPCell *destination, IMPCell *source);

/* Public prototypes (the generic include/imp/imp_ivs*.h do not match the
 * T31 ABI and are not included here). */
IMPIVSInterface *IMP_IVS_CreateMoveInterface(IMP_IVS_MoveParam *param);
void IMP_IVS_DestroyMoveInterface(IMPIVSInterface *moveInterface);
IMPIVSInterface *IMP_IVS_CreateBaseMoveInterface(IMP_IVS_BaseMoveParam *param);
void IMP_IVS_DestroyBaseMoveInterface(IMPIVSInterface *moveInterface);
int IMP_IVS_CreateGroup(int group);
int IMP_IVS_DestroyGroup(int group);
int IMP_IVS_CreateChn(int channel, IMPIVSInterface *handler);
int IMP_IVS_DestroyChn(int channel);
int IMP_IVS_RegisterChn(int group, int channel);
int IMP_IVS_UnRegisterChn(int channel);
int IMP_IVS_StartRecvPic(int channel);
int IMP_IVS_StopRecvPic(int channel);
int IMP_IVS_PollingResult(int channel, int timeout_ms);
int IMP_IVS_GetResult(int channel, void **result);
int IMP_IVS_ReleaseResult(int channel, void *result);
int IMP_IVS_ReleaseData(void *address);
int IMP_IVS_GetParam(int channel, void *param);
int IMP_IVS_SetParam(int channel, void *param);

/* ============================ helpers ============================ */

/* OpenIMP's FrameSource (T31/T23/T21/T20/T30) hands out NV12 with the luma rows packed
 * (stride == width, the UV plane follows at width * ALIGN16(height)); the
 * frame record carries no separate stride. Every copy below goes row by
 * row through this value, so a different stride only needs changing here. */
static uint32_t ivs_frame_stride(const T31IVSFrameInfo *frame)
{
    return frame->width;
}

/* The capture buffers are cached rmem written by ISP DMA: drop stale lines
 * before the CPU reads the range. */
static void ivs_invalidate(uint32_t virt, uint32_t size)
{
    if (virt && size)
        (void)DMA_RmemFlushCache((void *)(uintptr_t)virt, size, 2);
}

/* Frame must match the size the instance was created for; the vendor
 * copies width * height of the frame into a buffer sized from the
 * parameters and overflows when they differ. */
static int ivs_frame_ok(const T31IVSFrameInfo *frame, uint32_t width,
                        uint32_t height, int *warned, const char *what)
{
    uint32_t stride = ivs_frame_stride(frame);

    if (frame->virAddr && frame->width == width && frame->height == height &&
        (!frame->size || (uint64_t)stride * height <= frame->size))
        return 1;
    if (!*warned) {
        *warned = 1;
        IMP_LOG_ERR("IVS", "%s: frame %ux%u (size %u) does not match the "
                    "configured %ux%u, frames dropped", what,
                    frame->width, frame->height, frame->size, width, height);
    }
    return 0;
}

/* ======================= move interface ======================= */

struct t31_move_iface {
    IMPIVSInterface inf;
    IMP_IVS_MoveParam param;        /* inf.param, written by SetParam */
};

struct t31_move_priv {
    T31IvsMove *alg;
    int pending;
    int warned;
    int rd, wr;
    IMP_IVS_MoveOutput ring[T31_IVS_RESULTS];
};

static int move_init(IMPIVSInterface *inf)
{
    struct t31_move_priv *p;

    if (inf->priv)
        return 0;
    p = calloc(1, sizeof(*p));
    if (!p)
        return -1;
    p->alg = t31_ivs_move_create((const IMP_IVS_MoveParam *)inf->param);
    if (!p->alg) {
        IMP_LOG_ERR("IVS", "move: invalid parameters or out of memory");
        free(p);
        return -1;
    }
    inf->priv = p;
    return 0;
}

static void move_exit(IMPIVSInterface *inf)
{
    struct t31_move_priv *p = inf->priv;

    if (!p)
        return;
    t31_ivs_move_destroy(p->alg);
    free(p);
    inf->priv = NULL;
}

static int move_preprocess(IMPIVSInterface *inf, T31IVSFrameInfo *frame)
{
    struct t31_move_priv *p = inf->priv;
    uint32_t width, height, stride;
    int need;

    if (!p || !frame)
        return -1;
    p->pending = 0;
    t31_ivs_move_frame_size(p->alg, &width, &height);
    if (!ivs_frame_ok(frame, width, height, &p->warned, "move"))
        return -1;
    stride = ivs_frame_stride(frame);
    need = t31_ivs_move_needs_luma(p->alg);
    if (need)
        ivs_invalidate(frame->virAddr, stride * height);
    t31_ivs_move_feed(p->alg,
                      need ? (const uint8_t *)(uintptr_t)frame->virAddr : NULL,
                      stride);
    p->pending = 1;
    return 0;
}

static int move_process(IMPIVSInterface *inf, T31IVSFrameInfo *frame)
{
    struct t31_move_priv *p = inf->priv;
    int ret;

    (void)frame;
    if (!p || !p->pending)
        return 1;
    p->pending = 0;
    ret = t31_ivs_move_run(p->alg, p->ring[p->wr].retRoi);
    if (ret == 0)
        p->wr = (p->wr + 1) % T31_IVS_RESULTS;
    return ret;
}

static int move_get_result(IMPIVSInterface *inf, void **result)
{
    struct t31_move_priv *p = inf->priv;

    if (!p || !result)
        return -1;
    *result = &p->ring[p->rd];
    p->rd = (p->rd + 1) % T31_IVS_RESULTS;
    return 0;
}

static int move_release_result(IMPIVSInterface *inf, void *result)
{
    (void)inf;
    (void)result;
    return 0;
}

static int move_get_param(IMPIVSInterface *inf, void *param)
{
    struct t31_move_priv *p = inf->priv;

    if (!p || !param)
        return -1;
    t31_ivs_move_get_param(p->alg, param);
    return 0;
}

static int move_set_param(IMPIVSInterface *inf, void *param)
{
    struct t31_move_priv *p = inf->priv;

    if (!p || !param)
        return -1;
    if (t31_ivs_move_set_param(p->alg, param) < 0) {
        IMP_LOG_ERR("IVS", "move: sense out of range 0..8, parameters kept");
        return -1;
    }
    return 0;
}

static int move_flush(IMPIVSInterface *inf)
{
    (void)inf;
    return 0;
}

IMPIVSInterface *IMP_IVS_CreateMoveInterface(IMP_IVS_MoveParam *param)
{
    struct t31_move_iface *m;

    if (!param) {
        errno = EINVAL;
        return NULL;
    }
    m = calloc(1, sizeof(*m));
    if (!m)
        return NULL;
    m->param = *param;
    m->inf.param = &m->param;
    m->inf.paramSize = (int)sizeof(m->param);
    m->inf.pixfmt = T31_IVS_PIX_NV12;
    m->inf.init = move_init;
    m->inf.exit = move_exit;
    m->inf.preProcessSync = move_preprocess;
    m->inf.processAsync = move_process;
    m->inf.getResult = move_get_result;
    m->inf.releaseResult = move_release_result;
    m->inf.getParam = move_get_param;
    m->inf.setParam = move_set_param;
    m->inf.flushFrame = move_flush;
    return &m->inf;
}

/* ===================== base move interface ===================== */

/* Result slot: the public IMP_IVS_BaseMoveOutput followed by the frame
 * timestamp, as in the vendor ring. */
struct t31_base_slot {
    int ret;
    uint8_t *data;
    int datalen;
    int reserved;
    int64_t timeStamp;
};

_Static_assert(offsetof(struct t31_base_slot, datalen) ==
               offsetof(IMP_IVS_BaseMoveOutput, datalen),
               "base move result slot must start with IMP_IVS_BaseMoveOutput");
#if OPENIMP_IVS_BASE_MOVE_TIMESTAMP
_Static_assert(offsetof(struct t31_base_slot, timeStamp) ==
               offsetof(IMP_IVS_BaseMoveOutput, timeStamp),
               "T23/T21/T30 publish the base move timestamp in IMP_IVS_BaseMoveOutput");
#endif

struct t31_base_iface {
    IMPIVSInterface inf;
    IMP_IVS_BaseMoveParam param;
};

struct t31_base_priv {
    T31IvsBaseMove *alg;
    int pending;
    int warned;
    int rd, wr;
    struct t31_base_slot ring[T31_IVS_RESULTS];
};

static void base_exit(IMPIVSInterface *inf)
{
    struct t31_base_priv *p = inf->priv;
    int i;

    if (!p)
        return;
    for (i = 0; i < T31_IVS_RESULTS; i++)
        free(p->ring[i].data);
    t31_ivs_base_move_destroy(p->alg);
    free(p);
    inf->priv = NULL;
}

static int base_init(IMPIVSInterface *inf)
{
    struct t31_base_priv *p;
    int i, len;

    if (inf->priv)
        return 0;
    p = calloc(1, sizeof(*p));
    if (!p)
        return -1;
    inf->priv = p;
    p->alg = t31_ivs_base_move_create((const IMP_IVS_BaseMoveParam *)inf->param);
    if (!p->alg) {
        IMP_LOG_ERR("IVS", "base move: skipFrameCnt/referenceNum/sadMode "
                    "out of range or out of memory");
        base_exit(inf);
        return -1;
    }
    len = t31_ivs_base_move_datalen(p->alg);
    for (i = 0; i < T31_IVS_RESULTS; i++) {
        p->ring[i].data = calloc((size_t)(len > 0 ? len : 1), 1);
        if (!p->ring[i].data) {
            base_exit(inf);
            return -1;
        }
    }
    return 0;
}

static int base_preprocess(IMPIVSInterface *inf, T31IVSFrameInfo *frame)
{
    struct t31_base_priv *p = inf->priv;
    uint32_t width, height, stride;
    int need;

    if (!p || !frame)
        return -1;
    p->pending = 0;
    t31_ivs_base_move_frame_size(p->alg, &width, &height);
    if (!ivs_frame_ok(frame, width, height, &p->warned, "base move"))
        return -1;
    stride = ivs_frame_stride(frame);
    need = t31_ivs_base_move_needs_luma(p->alg);
    if (need)
        ivs_invalidate(frame->virAddr, stride * height);
    t31_ivs_base_move_feed(p->alg,
                           need ? (const uint8_t *)(uintptr_t)frame->virAddr : NULL,
                           stride);
    p->pending = 1;
    return 0;
}

static int base_process(IMPIVSInterface *inf, T31IVSFrameInfo *frame)
{
    struct t31_base_priv *p = inf->priv;
    struct t31_base_slot *slot;

    if (!p || !p->pending)
        return 1;
    p->pending = 0;
    slot = &p->ring[p->wr];
    t31_ivs_base_move_run(p->alg, slot->data, &slot->ret);
    slot->datalen = t31_ivs_base_move_datalen(p->alg);
    slot->timeStamp = frame ? frame->timeStamp : 0;
    p->wr = (p->wr + 1) % T31_IVS_RESULTS;
    return 0;                       /* a result on every frame */
}

static int base_get_result(IMPIVSInterface *inf, void **result)
{
    struct t31_base_priv *p = inf->priv;

    if (!p || !result)
        return -1;
    *result = &p->ring[p->rd];
    p->rd = (p->rd + 1) % T31_IVS_RESULTS;
    return 0;
}

static int base_get_param(IMPIVSInterface *inf, void *param)
{
    struct t31_base_priv *p = inf->priv;

    if (!p || !param)
        return -1;
    t31_ivs_base_move_get_param(p->alg, param);
    return 0;
}

static int base_set_param(IMPIVSInterface *inf, void *param)
{
    struct t31_base_priv *p = inf->priv;

    if (!p || !param)
        return -1;
    return t31_ivs_base_move_set_param(p->alg, param);
}

IMPIVSInterface *IMP_IVS_CreateBaseMoveInterface(IMP_IVS_BaseMoveParam *param)
{
    struct t31_base_iface *b;

    if (!param) {
        errno = EINVAL;
        return NULL;
    }
    b = calloc(1, sizeof(*b));
    if (!b)
        return NULL;
    b->param = *param;
    b->inf.param = &b->param;
    b->inf.paramSize = (int)sizeof(b->param);
    b->inf.pixfmt = T31_IVS_PIX_NV12;
    b->inf.init = base_init;
    b->inf.exit = base_exit;
    b->inf.preProcessSync = base_preprocess;
    b->inf.processAsync = base_process;
    b->inf.getResult = base_get_result;
    b->inf.releaseResult = move_release_result;
    b->inf.getParam = base_get_param;
    b->inf.setParam = base_set_param;
    b->inf.flushFrame = move_flush;
    return &b->inf;
}

/* ============================ framework ============================ */

enum { IVS_CHN_FREE, IVS_CHN_ACTIVE, IVS_CHN_BUSY };

struct t31_ivs_channel {
    int state;                      /* IVS_CHN_*, under ivs_lock */
    int group;                      /* -1: not registered */
    int enabled;                    /* StartRecvPic */
    unsigned int gen;               /* bumped on create and destroy */
    int users;                      /* callers waiting outside ivs_lock */
    int param_changed;
    int own;                        /* move / base move of this file */
    IMPIVSInterface *inf;
    sem_t sem_start, sem_end, sem_result;
    pthread_t thread;
    int quit;                       /* atomic */
    int number;
    T31IVSFrameInfo work;           /* frame handed to processAsync */
    uint8_t *copy;                  /* NV12 copy for foreign interfaces */
    size_t copy_size;
    int proc_error_logged;          /* IVS thread */
    int pre_error_logged;           /* capture context */
    struct {                        /* OPENIMP_T31_IVS_STATS=1 */
        unsigned int frames, dropped;
        uint64_t copy_ns;
        uint32_t copy_max_ns;
        int64_t last_ms;
        /* written by the IVS thread, atomics */
        uint32_t results, proc_us, proc_max_us;
    } stats;
};

static pthread_mutex_t ivs_lock = PTHREAD_MUTEX_INITIALIZER;
static int ivs_groups[T31_IVS_GROUPS];
static struct t31_ivs_channel ivs_channels[T31_IVS_CHANNELS];
static int ivs_receiving;           /* channels with enabled set */

static uint64_t ivs_now_ns(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

/* OPENIMP_T31_IVS_STATS=1: one line per channel every 10 s with frame,
 * drop and result counts and the time spent copying (capture thread) and
 * processing (IVS thread). */
static int ivs_stats_enabled(void)
{
    static int enabled = -1;

    if (enabled < 0) {
        const char *value = getenv("OPENIMP_T31_IVS_STATS");

        enabled = value && value[0] == '1';
    }
    return enabled;
}

static void ivs_stats_report(struct t31_ivs_channel *c)
{
    int64_t now = (int64_t)(ivs_now_ns() / 1000000u);
    unsigned int accepted = c->stats.frames - c->stats.dropped;
    uint32_t results, proc_us, proc_max_us;

    if (!c->stats.last_ms)
        c->stats.last_ms = now;
    if (now - c->stats.last_ms < 10000)
        return;
    results = __atomic_exchange_n(&c->stats.results, 0u, __ATOMIC_RELAXED);
    proc_us = __atomic_exchange_n(&c->stats.proc_us, 0u, __ATOMIC_RELAXED);
    proc_max_us = __atomic_exchange_n(&c->stats.proc_max_us, 0u, __ATOMIC_RELAXED);
    IMP_LOG_INFO("IVS", "chn%d: %u frames, %u dropped busy, %u results; "
                 "copy avg %u us max %u us; process avg %u us max %u us",
                 c->number, c->stats.frames, c->stats.dropped, results,
                 accepted ? (unsigned int)(c->stats.copy_ns / accepted / 1000u) : 0u,
                 c->stats.copy_max_ns / 1000u,
                 accepted ? proc_us / accepted : 0u, proc_max_us);
    c->stats.frames = 0;
    c->stats.dropped = 0;
    c->stats.copy_ns = 0;
    c->stats.copy_max_ns = 0;
    c->stats.last_ms = now;
}

static int ivs_fail(int error)
{
    errno = error;
    return -1;
}

static int ivs_valid_group(int group)
{
    return group >= 0 && group < T31_IVS_GROUPS;
}

static int ivs_valid_channel(int channel)
{
    return channel >= 0 && channel < T31_IVS_CHANNELS;
}

static int ivs_own_interface(const IMPIVSInterface *inf)
{
    return inf->preProcessSync == move_preprocess ||
           inf->preProcessSync == base_preprocess;
}

static void ivs_set_enabled(struct t31_ivs_channel *c, int enabled)
{
    if (c->enabled == enabled)
        return;
    c->enabled = enabled;
    __atomic_add_fetch(&ivs_receiving, enabled ? 1 : -1, __ATOMIC_RELAXED);
}

/* FrameSource channel feeding an IVS group, following binds backwards
 * (normally FS -> IVS directly). */
static int ivs_group_source(int group)
{
    IMPCell cursor = { DEV_ID_IVS, group, 0 };
    IMPCell source;
    int depth;

    for (depth = 0; depth < 4; depth++) {
        if (IMP_System_GetBindbyDest(&cursor, &source) != 0)
            return -1;
        if (source.deviceID == DEV_ID_FS)
            return source.groupID;
        cursor = source;
        cursor.outputID = 0;
    }
    return -1;
}

static void *ivs_thread(void *arg)
{
    struct t31_ivs_channel *c = arg;
    char name[16];

    snprintf(name, sizeof(name), "ivs_chn%d", c->number);
    prctl(PR_SET_NAME, name);
    for (;;) {
        IMPIVSInterface *inf = c->inf;
        int ret;

        while (sem_wait(&c->sem_start) != 0 && errno == EINTR)
            ;
        if (__atomic_load_n(&c->quit, __ATOMIC_ACQUIRE))
            break;
        if (ivs_stats_enabled()) {
            uint64_t t0 = ivs_now_ns();
            uint32_t us;

            ret = inf->processAsync ? inf->processAsync(inf, &c->work) : 1;
            us = (uint32_t)((ivs_now_ns() - t0) / 1000u);
            __atomic_add_fetch(&c->stats.proc_us, us, __ATOMIC_RELAXED);
            if (us > __atomic_load_n(&c->stats.proc_max_us, __ATOMIC_RELAXED))
                __atomic_store_n(&c->stats.proc_max_us, us, __ATOMIC_RELAXED);
            if (ret == 0)
                __atomic_add_fetch(&c->stats.results, 1u, __ATOMIC_RELAXED);
        } else {
            ret = inf->processAsync ? inf->processAsync(inf, &c->work) : 1;
        }
        if (ret == 0) {
            sem_post(&c->sem_result);
        } else if (ret < 0 && !c->proc_error_logged) {
            /* The vendor ends the thread here; keep serving frames. */
            c->proc_error_logged = 1;
            IMP_LOG_ERR("IVS", "channel %d: ivs process failed (%d)",
                        c->number, ret);
        }
        sem_post(&c->sem_end);
    }
    return NULL;
}

/* Capture context, ivs_lock held, sem_end taken. */
static void ivs_deliver(struct t31_ivs_channel *c, const T31IVSFrameInfo *frame)
{
    IMPIVSInterface *inf = c->inf;

    if (c->param_changed) {
        c->param_changed = 0;
        if (inf->setParam && inf->param)
            (void)inf->setParam(inf, inf->param);
    }
    c->work = *frame;
    if (c->own) {
        (void)inf->preProcessSync(inf, &c->work);
    } else {
        uint32_t size = frame->size ? frame->size
                                    : frame->width * frame->height * 3u / 2u;

        ivs_invalidate(frame->virAddr, size);
        if (inf->preProcessSync && inf->preProcessSync(inf, &c->work) < 0 &&
            !c->pre_error_logged) {
            c->pre_error_logged = 1;
            IMP_LOG_ERR("IVS", "channel %d: preProcessSync failed", c->number);
        }
        if (inf->processAsync && frame->virAddr && size) {
            if (c->copy_size < size) {
                uint8_t *copy = realloc(c->copy, size);

                if (!copy) {
                    sem_post(&c->sem_end);
                    return;
                }
                c->copy = copy;
                c->copy_size = size;
            }
            memcpy(c->copy, (const void *)(uintptr_t)frame->virAddr, size);
            c->work = *frame;
            c->work.virAddr = (uint32_t)(uintptr_t)c->copy;
            c->work.phyAddr = 0;
        }
    }
    sem_post(&c->sem_start);
}

void openimp_t31_ivs_capture(int fs_chn, const void *frame)
{
    T31IVSFrameInfo info;
    int source = -2;
    int i;

    if (!frame || !__atomic_load_n(&ivs_receiving, __ATOMIC_RELAXED))
        return;
    memset(&info, 0, sizeof(info));
    memcpy(&info, frame, T31_IVS_FRAME_RECORD_BYTES);
    pthread_mutex_lock(&ivs_lock);
    for (i = 0; i < T31_IVS_CHANNELS; i++) {
        struct t31_ivs_channel *c = &ivs_channels[i];

        if (c->state != IVS_CHN_ACTIVE || !c->enabled || c->group < 0)
            continue;
        if (source == -2)
            source = ivs_group_source(0);
        if (source != fs_chn)
            break;                  /* one group: nothing else to feed */
        c->stats.frames++;
        if (sem_trywait(&c->sem_end) != 0) {
            c->stats.dropped++;     /* still busy: drop, as the vendor */
        } else if (ivs_stats_enabled()) {
            uint64_t t0 = ivs_now_ns(), dt;

            ivs_deliver(c, &info);
            dt = ivs_now_ns() - t0;
            c->stats.copy_ns += dt;
            if (dt > c->stats.copy_max_ns)
                c->stats.copy_max_ns = (uint32_t)dt;
        } else {
            ivs_deliver(c, &info);
        }
        if (ivs_stats_enabled())
            ivs_stats_report(c);
    }
    pthread_mutex_unlock(&ivs_lock);
}

static int ivs_interface_in_use(const IMPIVSInterface *inf)
{
    int i, used = 0;

    pthread_mutex_lock(&ivs_lock);
    for (i = 0; i < T31_IVS_CHANNELS; i++)
        if (ivs_channels[i].state != IVS_CHN_FREE && ivs_channels[i].inf == inf)
            used = 1;
    pthread_mutex_unlock(&ivs_lock);
    return used;
}

static void ivs_destroy_interface(IMPIVSInterface *inf,
                                  int (*init)(IMPIVSInterface *))
{
    if (!inf || inf->init != init)
        return;
    if (ivs_interface_in_use(inf)) {
        IMP_LOG_ERR("IVS", "interface %p still used by a channel, not freed",
                    (void *)inf);
        return;
    }
    if (inf->priv && inf->exit)
        inf->exit(inf);
    free(inf);
}

void IMP_IVS_DestroyMoveInterface(IMPIVSInterface *moveInterface)
{
    ivs_destroy_interface(moveInterface, move_init);
}

void IMP_IVS_DestroyBaseMoveInterface(IMPIVSInterface *moveInterface)
{
    ivs_destroy_interface(moveInterface, base_init);
}

int IMP_IVS_CreateGroup(int group)
{
    if (!ivs_valid_group(group))
        return ivs_fail(EINVAL);
    pthread_mutex_lock(&ivs_lock);
    ivs_groups[group] = 1;
    pthread_mutex_unlock(&ivs_lock);
    return 0;
}

int IMP_IVS_DestroyGroup(int group)
{
    int i;

    if (!ivs_valid_group(group))
        return ivs_fail(EINVAL);
    pthread_mutex_lock(&ivs_lock);
    if (!ivs_groups[group]) {
        pthread_mutex_unlock(&ivs_lock);
        return ivs_fail(ENOENT);
    }
    for (i = 0; i < T31_IVS_CHANNELS; i++) {
        if (ivs_channels[i].state == IVS_CHN_ACTIVE &&
            ivs_channels[i].group == group) {
            pthread_mutex_unlock(&ivs_lock);
            return ivs_fail(EBUSY);
        }
    }
    ivs_groups[group] = 0;
    pthread_mutex_unlock(&ivs_lock);
    return 0;
}

int IMP_IVS_CreateChn(int channel, IMPIVSInterface *handler)
{
    struct t31_ivs_channel *c;

    if (!ivs_valid_channel(channel) || !handler ||
        (!handler->preProcessSync && !handler->processAsync) ||
        (handler->param == NULL) != (handler->paramSize <= 0))
        return ivs_fail(EINVAL);
    c = &ivs_channels[channel];
    pthread_mutex_lock(&ivs_lock);
    if (c->state != IVS_CHN_FREE) {
        pthread_mutex_unlock(&ivs_lock);
        return ivs_fail(EEXIST);
    }
    c->state = IVS_CHN_BUSY;        /* reserved while it is set up */
    pthread_mutex_unlock(&ivs_lock);

    c->inf = handler;
    c->own = ivs_own_interface(handler);
    c->number = channel;
    c->group = -1;
    c->enabled = 0;
    c->param_changed = 0;
    c->quit = 0;
    c->proc_error_logged = 0;
    c->pre_error_logged = 0;
    memset(&c->stats, 0, sizeof(c->stats));
    sem_init(&c->sem_start, 0, 0);
    sem_init(&c->sem_end, 0, 1);
    sem_init(&c->sem_result, 0, 0);
    if (handler->init && handler->init(handler) < 0)
        goto fail;
    if (pthread_create(&c->thread, NULL, ivs_thread, c) != 0) {
        if (handler->exit)
            handler->exit(handler);
        goto fail;
    }
    pthread_mutex_lock(&ivs_lock);
    c->gen++;
    c->state = IVS_CHN_ACTIVE;
    pthread_mutex_unlock(&ivs_lock);
    return 0;

fail:
    sem_destroy(&c->sem_start);
    sem_destroy(&c->sem_end);
    sem_destroy(&c->sem_result);
    pthread_mutex_lock(&ivs_lock);
    c->inf = NULL;
    c->state = IVS_CHN_FREE;
    pthread_mutex_unlock(&ivs_lock);
    return -1;
}

/* Waits, without ivs_lock, until no API call is inside the channel. */
static void ivs_wait_users(struct t31_ivs_channel *c)
{
    for (;;) {
        int users;

        pthread_mutex_lock(&ivs_lock);
        users = c->users;
        pthread_mutex_unlock(&ivs_lock);
        if (!users)
            return;
        usleep(2000);
    }
}

int IMP_IVS_DestroyChn(int channel)
{
    struct t31_ivs_channel *c;
    IMPIVSInterface *inf;

    if (!ivs_valid_channel(channel))
        return ivs_fail(EINVAL);
    c = &ivs_channels[channel];
    pthread_mutex_lock(&ivs_lock);
    if (c->state != IVS_CHN_ACTIVE) {
        pthread_mutex_unlock(&ivs_lock);
        return ivs_fail(ENOENT);
    }
    /* Stop and unregister implicitly rather than leak the thread. */
    ivs_set_enabled(c, 0);
    c->group = -1;
    c->state = IVS_CHN_BUSY;
    c->gen++;
    inf = c->inf;
    pthread_mutex_unlock(&ivs_lock);

    ivs_wait_users(c);
    __atomic_store_n(&c->quit, 1, __ATOMIC_RELEASE);
    sem_post(&c->sem_start);
    pthread_join(c->thread, NULL);
    if (inf->exit)
        inf->exit(inf);
    sem_destroy(&c->sem_start);
    sem_destroy(&c->sem_end);
    sem_destroy(&c->sem_result);
    free(c->copy);

    pthread_mutex_lock(&ivs_lock);
    c->copy = NULL;
    c->copy_size = 0;
    c->inf = NULL;
    c->state = IVS_CHN_FREE;
    pthread_mutex_unlock(&ivs_lock);
    return 0;
}

int IMP_IVS_RegisterChn(int group, int channel)
{
    struct t31_ivs_channel *c;

    if (!ivs_valid_group(group) || !ivs_valid_channel(channel))
        return ivs_fail(EINVAL);
    c = &ivs_channels[channel];
    pthread_mutex_lock(&ivs_lock);
    if (!ivs_groups[group] || c->state != IVS_CHN_ACTIVE) {
        pthread_mutex_unlock(&ivs_lock);
        return ivs_fail(ENOENT);
    }
    if (c->group >= 0) {
        pthread_mutex_unlock(&ivs_lock);
        return ivs_fail(EBUSY);
    }
    c->group = group;
    pthread_mutex_unlock(&ivs_lock);
    return 0;
}

int IMP_IVS_UnRegisterChn(int channel)
{
    struct t31_ivs_channel *c;

    if (!ivs_valid_channel(channel))
        return ivs_fail(EINVAL);
    c = &ivs_channels[channel];
    pthread_mutex_lock(&ivs_lock);
    if (c->state != IVS_CHN_ACTIVE || c->group < 0) {
        pthread_mutex_unlock(&ivs_lock);
        return ivs_fail(ENOENT);
    }
    if (c->enabled) {
        pthread_mutex_unlock(&ivs_lock);
        return ivs_fail(EBUSY);
    }
    c->group = -1;
    pthread_mutex_unlock(&ivs_lock);
    return 0;
}

int IMP_IVS_StartRecvPic(int channel)
{
    struct t31_ivs_channel *c;

    if (!ivs_valid_channel(channel))
        return ivs_fail(EINVAL);
    c = &ivs_channels[channel];
    pthread_mutex_lock(&ivs_lock);
    if (c->state != IVS_CHN_ACTIVE || c->group < 0) {
        pthread_mutex_unlock(&ivs_lock);
        return ivs_fail(ENOENT);
    }
    ivs_set_enabled(c, 1);
    pthread_mutex_unlock(&ivs_lock);
    return 0;
}

int IMP_IVS_StopRecvPic(int channel)
{
    struct t31_ivs_channel *c;
    int i;

    if (!ivs_valid_channel(channel))
        return ivs_fail(EINVAL);
    c = &ivs_channels[channel];
    pthread_mutex_lock(&ivs_lock);
    if (c->state != IVS_CHN_ACTIVE) {
        pthread_mutex_unlock(&ivs_lock);
        return ivs_fail(ENOENT);
    }
    ivs_set_enabled(c, 0);
    c->users++;
    pthread_mutex_unlock(&ivs_lock);

    /* Let a frame in flight finish (vendor: up to 100 x 10 ms). */
    for (i = 0; i < 100; i++) {
        if (sem_trywait(&c->sem_end) == 0) {
            sem_post(&c->sem_end);
            break;
        }
        usleep(10000);
    }
    pthread_mutex_lock(&ivs_lock);
    if (c->inf && c->inf->flushFrame)
        (void)c->inf->flushFrame(c->inf);
    c->users--;
    pthread_mutex_unlock(&ivs_lock);
    return 0;
}

static void ivs_realtime_in(struct timespec *ts, long ms)
{
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec += ms / 1000;
    ts->tv_nsec += (ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec++;
        ts->tv_nsec -= 1000000000L;
    }
}

static int64_t ivs_monotonic_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int IMP_IVS_PollingResult(int channel, int timeout_ms)
{
    struct t31_ivs_channel *c;
    unsigned int gen;
    int64_t deadline;
    int got = 0, alive;

    if (!ivs_valid_channel(channel))
        return ivs_fail(EINVAL);
    c = &ivs_channels[channel];
    pthread_mutex_lock(&ivs_lock);
    if (c->state != IVS_CHN_ACTIVE) {
        pthread_mutex_unlock(&ivs_lock);
        return ivs_fail(ENOENT);
    }
    c->users++;
    gen = c->gen;
    pthread_mutex_unlock(&ivs_lock);

    /* 0: poll, < 0 (IMP_IVS_DEFAULT_TIMEOUTMS): 10 s, as the vendor. Waits
     * in slices on the monotonic clock so a wall-clock step (NTP) neither
     * shortens nor stretches the timeout and DestroyChn is not held up. */
    deadline = ivs_monotonic_ms() + (timeout_ms < 0 ? 10000 : timeout_ms);
    for (;;) {
        struct timespec ts;
        int64_t left;

        if (sem_trywait(&c->sem_result) == 0) {
            got = 1;
            break;
        }
        left = deadline - ivs_monotonic_ms();
        if (!timeout_ms || left <= 0)
            break;
        pthread_mutex_lock(&ivs_lock);
        alive = c->state == IVS_CHN_ACTIVE && c->gen == gen;
        pthread_mutex_unlock(&ivs_lock);
        if (!alive)
            break;
        ivs_realtime_in(&ts, left < 100 ? (long)left : 100L);
        if (sem_timedwait(&c->sem_result, &ts) == 0) {
            got = 1;
            break;
        }
    }

    pthread_mutex_lock(&ivs_lock);
    c->users--;
    if (c->state != IVS_CHN_ACTIVE || c->gen != gen)
        got = 0;
    pthread_mutex_unlock(&ivs_lock);
    return got ? 0 : ivs_fail(ETIMEDOUT);
}

int IMP_IVS_GetResult(int channel, void **result)
{
    struct t31_ivs_channel *c;
    int ret;

    if (!ivs_valid_channel(channel) || !result)
        return ivs_fail(EINVAL);
    c = &ivs_channels[channel];
    pthread_mutex_lock(&ivs_lock);
    if (c->state != IVS_CHN_ACTIVE || !c->inf->getResult) {
        pthread_mutex_unlock(&ivs_lock);
        return ivs_fail(ENOENT);
    }
    ret = c->inf->getResult(c->inf, result);
    pthread_mutex_unlock(&ivs_lock);
    return ret;
}

int IMP_IVS_ReleaseResult(int channel, void *result)
{
    struct t31_ivs_channel *c;
    int ret = 0;

    if (!ivs_valid_channel(channel) || !result)
        return ivs_fail(EINVAL);
    c = &ivs_channels[channel];
    pthread_mutex_lock(&ivs_lock);
    if (c->state != IVS_CHN_ACTIVE) {
        pthread_mutex_unlock(&ivs_lock);
        return ivs_fail(ENOENT);
    }
    if (c->inf->releaseResult)
        ret = c->inf->releaseResult(c->inf, result);
    pthread_mutex_unlock(&ivs_lock);
    return ret;
}

/* Capture frames are never locked for IVS (see the top), so there is
 * nothing to release. */
int IMP_IVS_ReleaseData(void *address)
{
    if (!address)
        return ivs_fail(EINVAL);
    return 0;
}

int IMP_IVS_GetParam(int channel, void *param)
{
    struct t31_ivs_channel *c;
    int ret;

    if (!ivs_valid_channel(channel) || !param)
        return ivs_fail(EINVAL);
    c = &ivs_channels[channel];
    pthread_mutex_lock(&ivs_lock);
    if (c->state != IVS_CHN_ACTIVE || !c->inf->getParam) {
        pthread_mutex_unlock(&ivs_lock);
        return ivs_fail(ENOENT);
    }
    ret = c->inf->getParam(c->inf, param);
    pthread_mutex_unlock(&ivs_lock);
    return ret;
}

int IMP_IVS_SetParam(int channel, void *param)
{
    struct t31_ivs_channel *c;

    if (!ivs_valid_channel(channel) || !param)
        return ivs_fail(EINVAL);
    c = &ivs_channels[channel];
    pthread_mutex_lock(&ivs_lock);
    if (c->state != IVS_CHN_ACTIVE || !c->inf->param ||
        c->inf->paramSize <= 0) {
        pthread_mutex_unlock(&ivs_lock);
        return ivs_fail(ENOENT);
    }
    /* Applied with the next accepted frame, as the vendor. */
    memcpy(c->inf->param, param, (size_t)c->inf->paramSize);
    c->param_changed = 1;
    pthread_mutex_unlock(&ivs_lock);
    return 0;
}
