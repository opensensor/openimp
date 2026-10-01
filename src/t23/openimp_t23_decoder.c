/* T23 IMP_Decoder: the OEM hardware JPEG decoder.
 *
 * The OEM libimp 1.3.0 decodes JPEG only (ijpegd on the Helix VPU), on
 * channel 0, into NV12, NV21 or YUV420P; H.264 and every other payload are
 * refused at CreateChn.  OpenIMP has no JPEG decoder of its own, so - like
 * the H.264 encoder - the OEM decoder runs in openimp-t23-helixd: a JPEG is
 * copied into the worker's shared window, decoded there, and the decoded
 * picture stays in the worker's VBM (rmem) until ReleaseFrame.  The caller
 * reads it through OpenIMP's own rmem mapping of the same physical buffer
 * (no copy); if that mapping is not available the picture is copied.
 *
 * The worker exists only between CreateChn and DestroyChn. */

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <imp/imp_common.h>

#include "dma_alloc.h"
#include "imp_log_int.h"
#include "t23/openimp_t23_helix_bridge.h"

/* vendor imp_decoder.h */
typedef struct {
    int decType;                    /* IMPPayloadType: PT_JPEG only */
    uint32_t maxWidth;
    uint32_t maxHeight;
    int pixelFormat;                /* PIX_FMT_YUV420P, NV12 or NV21 */
    uint32_t nrKeepStream;
    uint32_t frmRateNum;
    uint32_t frmRateDen;
} T23DecoderChnAttr;

typedef struct {
    int i_payload;
    uint8_t *p_payload;
    int64_t timeStamp;
} T23DecoderStream;

_Static_assert(sizeof(T23DecoderChnAttr) == 28, "IMPDecoderCHNAttr");
_Static_assert(sizeof(T23DecoderStream) == 16, "IMPDecoderStream");

#define T23_DEC_PIX_YUV420P 0
#define T23_DEC_PIX_NV12    10
#define T23_DEC_PIX_NV21    11
#define T23_DEC_SLOTS       8

enum { SLOT_FREE, SLOT_READY, SLOT_HELD };

typedef struct {
    int state;
    uint64_t order;
    uint32_t handle;
    IMPFrameInfo frame;
    void *copy;                     /* fallback copy, else NULL */
} T23DecSlot;

static struct {
    int created;
    int receiving;
    T23DecoderChnAttr attr;
    T23HelixBridge bridge;
    T23DecSlot slots[T23_DEC_SLOTS];
    int depth;
    uint64_t order;
    pthread_mutex_t lock;
    pthread_cond_t cond;
} dec = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER,
};

static void deadline_after(struct timespec *ts, uint32_t ms)
{
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec += ms / 1000u;
    ts->tv_nsec += (long)(ms % 1000u) * 1000000l;
    if (ts->tv_nsec >= 1000000000l) {
        ts->tv_sec++;
        ts->tv_nsec -= 1000000000l;
    }
}

int IMP_Decoder_CreateChn(int chn, const T23DecoderChnAttr *attr)
{
    int ret;

    if (chn != 0 || !attr)
        return -1;
    /* OEM: JPEG into YUV420P/NV12/NV21 only */
    if (attr->decType != PT_JPEG || !attr->maxWidth || !attr->maxHeight ||
        (attr->pixelFormat != T23_DEC_PIX_YUV420P &&
         attr->pixelFormat != T23_DEC_PIX_NV12 &&
         attr->pixelFormat != T23_DEC_PIX_NV21)) {
        IMP_LOG_ERR("Decoder", "only JPEG to YUV420P/NV12/NV21 is supported");
        return -1;
    }
    pthread_mutex_lock(&dec.lock);
    if (dec.created) {
        pthread_mutex_unlock(&dec.lock);
        return -1;
    }
    memset(&dec.bridge, 0, sizeof(dec.bridge));
    memset(dec.slots, 0, sizeof(dec.slots));
    dec.attr = *attr;
    dec.depth = attr->nrKeepStream ? (int)attr->nrKeepStream : 1;
    if (dec.depth > T23_DEC_SLOTS)
        dec.depth = T23_DEC_SLOTS;
    ret = OpenIMP_T23_HelixDecoderOpen(&dec.bridge, attr, sizeof(*attr),
                                       attr->maxWidth, attr->maxHeight);
    if (ret == 0)
        dec.created = 1;
    else
        OpenIMP_T23_HelixExit(&dec.bridge);
    pthread_mutex_unlock(&dec.lock);
    return ret == 0 ? 0 : -1;
}

int IMP_Decoder_DestroyChn(int chn)
{
    if (chn != 0)
        return -1;
    pthread_mutex_lock(&dec.lock);
    if (!dec.created) {
        pthread_mutex_unlock(&dec.lock);
        return -1;
    }
    dec.created = 0;
    dec.receiving = 0;
    pthread_cond_broadcast(&dec.cond);
    /* the worker returns every frame it still holds when it exits */
    OpenIMP_T23_HelixExit(&dec.bridge);
    for (int i = 0; i < T23_DEC_SLOTS; i++)
        free(dec.slots[i].copy);
    memset(dec.slots, 0, sizeof(dec.slots));
    pthread_mutex_unlock(&dec.lock);
    return 0;
}

int IMP_Decoder_StartRecvPic(int chn)
{
    if (chn != 0)
        return -1;
    pthread_mutex_lock(&dec.lock);
    if (!dec.created) {
        pthread_mutex_unlock(&dec.lock);
        return -1;
    }
    dec.receiving = 1;
    pthread_mutex_unlock(&dec.lock);
    return 0;
}

int IMP_Decoder_StopRecvPic(int chn)
{
    if (chn != 0)
        return -1;
    pthread_mutex_lock(&dec.lock);
    if (!dec.created) {
        pthread_mutex_unlock(&dec.lock);
        return -1;
    }
    dec.receiving = 0;
    pthread_cond_broadcast(&dec.cond);
    pthread_mutex_unlock(&dec.lock);
    return 0;
}

/* Make the decoded picture readable here: map the worker's VBM buffer
 * through OpenIMP's rmem mapping, or fall back to a copy. */
static int expose_frame(T23DecSlot *slot)
{
    IMPFrameInfo *f = &slot->frame;
    void *virt = f->phyAddr ? DMA_PhysToVirt(f->phyAddr) : NULL;

    if (virt) {
        DMA_RmemFlushCache(virt, f->size, 2 /* invalidate */);
        f->virAddr = (uint32_t)(uintptr_t)virt;
        return 0;
    }
    slot->copy = malloc(f->size);
    if (!slot->copy ||
        OpenIMP_T23_HelixDecoderCopy(&dec.bridge, slot->handle, slot->copy,
                                     f->size) != 0) {
        free(slot->copy);
        slot->copy = NULL;
        return -1;
    }
    f->virAddr = (uint32_t)(uintptr_t)slot->copy;
    f->phyAddr = 0;
    return 0;
}

static T23DecSlot *free_slot(void)
{
    int used = 0;
    T23DecSlot *free_one = NULL;

    for (int i = 0; i < T23_DEC_SLOTS; i++) {
        if (dec.slots[i].state != SLOT_FREE)
            used++;
        else if (!free_one)
            free_one = &dec.slots[i];
    }
    return used < dec.depth ? free_one : NULL;
}

int IMP_Decoder_SendStreamTimeout(int chn, T23DecoderStream *stream,
                                  uint32_t timeout_ms)
{
    struct timespec deadline;
    T23DecSlot *slot;
    IMPFrameInfo frame;
    uint32_t handle;
    int wait = 0;

    if (chn != 0 || !stream || !stream->p_payload || stream->i_payload <= 0)
        return -1;
    deadline_after(&deadline, timeout_ms);
    pthread_mutex_lock(&dec.lock);
    /* the OEM waits up to the timeout for a free picture buffer */
    while (dec.created && !(slot = free_slot()) && wait == 0)
        wait = pthread_cond_timedwait(&dec.cond, &dec.lock, &deadline);
    if (!dec.created || !(slot = free_slot())) {
        pthread_mutex_unlock(&dec.lock);
        return -1;
    }
    slot->state = SLOT_HELD;        /* reserved while decoding */
    pthread_mutex_unlock(&dec.lock);

    memset(&frame, 0, sizeof(frame));
    handle = 0;
    if (OpenIMP_T23_HelixDecode(&dec.bridge, stream->p_payload,
                                (uint32_t)stream->i_payload,
                                stream->timeStamp,
                                timeout_ms ? timeout_ms : 1000u, &frame,
                                &handle) != 0) {
        pthread_mutex_lock(&dec.lock);
        slot->state = SLOT_FREE;
        pthread_cond_broadcast(&dec.cond);
        pthread_mutex_unlock(&dec.lock);
        return -1;
    }

    pthread_mutex_lock(&dec.lock);
    slot->handle = handle;
    slot->frame = frame;
    slot->frame.timeStamp = stream->timeStamp;
    slot->copy = NULL;
    if (expose_frame(slot) != 0) {
        (void)OpenIMP_T23_HelixDecoderRelease(&dec.bridge, handle);
        slot->state = SLOT_FREE;
        pthread_cond_broadcast(&dec.cond);
        pthread_mutex_unlock(&dec.lock);
        return -1;
    }
    slot->order = ++dec.order;
    slot->state = SLOT_READY;
    pthread_cond_broadcast(&dec.cond);
    pthread_mutex_unlock(&dec.lock);
    return 0;
}

static T23DecSlot *oldest_ready(void)
{
    T23DecSlot *best = NULL;

    for (int i = 0; i < T23_DEC_SLOTS; i++) {
        T23DecSlot *slot = &dec.slots[i];

        if (slot->state == SLOT_READY && (!best || slot->order < best->order))
            best = slot;
    }
    return best;
}

int IMP_Decoder_PollingFrame(int chn, uint32_t timeout_ms)
{
    struct timespec deadline;
    int wait = 0, ret;

    if (chn != 0)
        return -1;
    deadline_after(&deadline, timeout_ms);
    pthread_mutex_lock(&dec.lock);
    while (dec.created && !oldest_ready() && wait == 0)
        wait = pthread_cond_timedwait(&dec.cond, &dec.lock, &deadline);
    ret = dec.created && oldest_ready() ? 0 : -1;
    pthread_mutex_unlock(&dec.lock);
    return ret;
}

int IMP_Decoder_GetFrame(int chn, IMPFrameInfo **frame)
{
    T23DecSlot *slot;

    if (chn != 0 || !frame)
        return -1;
    pthread_mutex_lock(&dec.lock);
    slot = dec.created ? oldest_ready() : NULL;
    if (!slot) {
        pthread_mutex_unlock(&dec.lock);
        return -1;
    }
    slot->state = SLOT_HELD;
    *frame = &slot->frame;
    pthread_mutex_unlock(&dec.lock);
    return 0;
}

int IMP_Decoder_ReleaseFrame(int chn, IMPFrameInfo *frame)
{
    T23DecSlot *slot = NULL;

    if (chn != 0 || !frame)
        return -1;
    pthread_mutex_lock(&dec.lock);
    for (int i = 0; dec.created && i < T23_DEC_SLOTS; i++) {
        if (&dec.slots[i].frame == frame &&
            dec.slots[i].state == SLOT_HELD) {
            slot = &dec.slots[i];
            break;
        }
    }
    if (!slot) {
        pthread_mutex_unlock(&dec.lock);
        return -1;
    }
    (void)OpenIMP_T23_HelixDecoderRelease(&dec.bridge, slot->handle);
    free(slot->copy);
    memset(slot, 0, sizeof(*slot));
    pthread_cond_broadcast(&dec.cond);
    pthread_mutex_unlock(&dec.lock);
    return 0;
}
