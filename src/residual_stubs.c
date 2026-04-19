/*
 * Residual stubs for the ported libimp.
 *
 * After the main T0..T97 reverse-engineering port landed, a ring of small
 * helpers remained undefined at link time — either because they live in
 * the stock libsysutils.so, or because they are leaf helpers that never
 * got their own task. This file provides minimal compatible definitions
 * so the shared library is self-sufficient.
 *
 * When one of these helpers is later ported bit-exactly from the binary,
 * delete the corresponding stub here and the port will override it.
 */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* ----- framesource kernel-interface thunks ------------------------------ */

int32_t fs_open_device(const char *path)
{
    return open(path ? path : "/dev/isp-m0", O_RDWR);
}

int32_t fs_close_device(int32_t fd)
{
    if (fd < 0) return -1;
    return close(fd);
}

int32_t fs_set_buffer_count(int32_t fd, int32_t count)
{
    (void)fd; (void)count;
    return 0;
}

int32_t fs_set_depth(int32_t fd, int32_t chan, int32_t depth)
{
    (void)fd; (void)chan; (void)depth;
    return 0;
}

int32_t fs_set_format(int32_t fd, void *attr)
{
    (void)fd; (void)attr;
    return 0;
}

int32_t fs_stream_on(int32_t fd)
{
    (void)fd;
    return 0;
}

int32_t fs_stream_off(int32_t fd)
{
    (void)fd;
    return 0;
}

int32_t fs_poll_frame(int32_t fd, int32_t timeout_ms)
{
    (void)fd; (void)timeout_ms;
    return 0;
}

/* ----- VBM variants (not in binary; convenience wrappers) --------------- */

/* Forward decls — provided by T71 core/vbm.c */
void *VBMLockFrameByVaddr(void *vaddr);
void VBMUnlockFrameByVaddr(void *vaddr);

void *VBMFrame_GetBuffer(void *frame)
{
    return frame;
}

int32_t VBMReleaseFrame(void *frame)
{
    if (frame == NULL) return -1;
    VBMUnlockFrameByVaddr(frame);
    return 0;
}

int32_t VBMKernelDequeue(int32_t fd, void **out_frame, int32_t timeout_ms)
{
    (void)fd; (void)timeout_ms;
    if (out_frame) *out_frame = NULL;
    return -1;
}

int32_t VBMPrimeKernelQueue(int32_t fd, int32_t count)
{
    (void)fd; (void)count;
    return 0;
}

/* ----- osd-style fifo helpers ------------------------------------------ */

typedef struct OsdFifo {
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
    int32_t depth;
    int32_t elem_size;
    int32_t head;
    int32_t tail;
    int32_t count;
    uint8_t *buf;
} OsdFifo;

int32_t fifo_alloc(int32_t depth, int32_t elem_size)
{
    if (depth <= 0 || elem_size <= 0) return 0;
    OsdFifo *f = (OsdFifo *)calloc(1, sizeof(*f));
    if (!f) return 0;
    f->buf = (uint8_t *)calloc((size_t)(depth * elem_size), 1);
    if (!f->buf) { free(f); return 0; }
    pthread_mutex_init(&f->lock, NULL);
    pthread_cond_init(&f->not_empty, NULL);
    f->depth = depth;
    f->elem_size = elem_size;
    return (int32_t)(uintptr_t)f;
}

int32_t fifo_free(int32_t handle)
{
    OsdFifo *f = (OsdFifo *)(uintptr_t)handle;
    if (!f) return -1;
    pthread_mutex_destroy(&f->lock);
    pthread_cond_destroy(&f->not_empty);
    free(f->buf);
    free(f);
    return 0;
}

int32_t fifo_put(int32_t handle, void *elem, int32_t block)
{
    OsdFifo *f = (OsdFifo *)(uintptr_t)handle;
    if (!f || !elem) return -1;
    (void)block;
    pthread_mutex_lock(&f->lock);
    if (f->count >= f->depth) { pthread_mutex_unlock(&f->lock); return -1; }
    memcpy(f->buf + (size_t)(f->tail * f->elem_size), elem, (size_t)f->elem_size);
    f->tail = (f->tail + 1) % f->depth;
    f->count++;
    pthread_cond_signal(&f->not_empty);
    pthread_mutex_unlock(&f->lock);
    return 0;
}

int32_t fifo_get(int32_t handle, int32_t block)
{
    OsdFifo *f = (OsdFifo *)(uintptr_t)handle;
    if (!f) return -1;
    pthread_mutex_lock(&f->lock);
    if (f->count == 0) {
        if (!block) { pthread_mutex_unlock(&f->lock); return -1; }
        pthread_cond_wait(&f->not_empty, &f->lock);
    }
    if (f->count == 0) { pthread_mutex_unlock(&f->lock); return -1; }
    int32_t idx = f->head;
    f->head = (f->head + 1) % f->depth;
    f->count--;
    pthread_mutex_unlock(&f->lock);
    return (int32_t)(uintptr_t)(f->buf + (size_t)(idx * f->elem_size));
}

int32_t fifo_clear(int32_t handle)
{
    OsdFifo *f = (OsdFifo *)(uintptr_t)handle;
    if (!f) return -1;
    pthread_mutex_lock(&f->lock);
    f->head = f->tail = f->count = 0;
    pthread_mutex_unlock(&f->lock);
    return 0;
}

int32_t fifo_pre_get_ptr(int32_t handle, int32_t index, void **out)
{
    OsdFifo *f = (OsdFifo *)(uintptr_t)handle;
    if (!f || !out) return -1;
    pthread_mutex_lock(&f->lock);
    if (index >= f->count) { pthread_mutex_unlock(&f->lock); return -1; }
    int32_t idx = (f->head + index) % f->depth;
    *out = f->buf + (size_t)(idx * f->elem_size);
    pthread_mutex_unlock(&f->lock);
    return 0;
}

/* ----- codec helper stubs (binary-internal leaf helpers) --------------- */

int32_t CalcSum(const uint8_t *buf, int32_t len)
{
    if (!buf || len <= 0) return 0;
    int32_t s = 0;
    for (int32_t i = 0; i < len; i++) s += buf[i];
    return s;
}

void CreateAvcNuts(void *nuts) { (void)nuts; }
void CreateHevcNuts(void *nuts) { (void)nuts; }

int32_t PicStructToFieldNumber(int32_t pic_struct) { return pic_struct & 1; }

void MorphRowFilter(const uint8_t *src, uint8_t *dst, int32_t width, int32_t ksize, int32_t op)
{
    (void)src; (void)dst; (void)width; (void)ksize; (void)op;
}
void MorphColumnFilter(const uint8_t *src, uint8_t *dst, int32_t width, int32_t height, int32_t ksize, int32_t op)
{
    (void)src; (void)dst; (void)width; (void)height; (void)ksize; (void)op;
}

int32_t Ioii(int32_t a, int32_t b) { return a + b; }
int32_t loii(int32_t a, int32_t b) { return a - b; }

const char *GetH26xBufferName_constprop_59(int32_t type)
{
    switch (type) {
    case 0:  return "H264";
    case 1:  return "HEVC";
    default: return "?";
    }
}

int32_t UpdateCommand(void *arg1, void *arg2)
{
    (void)arg1; (void)arg2;
    return 0;
}

/* ----- tracing stubs ---------------------------------------------------- */

void TraceBufMV_constprop_51(void *a, void *b, int32_t c) { (void)a; (void)b; (void)c; }
void TraceBufRec_isra_49_constprop_64(void *a, void *b, int32_t c) { (void)a; (void)b; (void)c; }
void TraceBufSrc_8bits_constprop_57(void *a, void *b, int32_t c) { (void)a; (void)b; (void)c; }
void TraceHwRc_constprop_54(void *a, void *b, int32_t c) { (void)a; (void)b; (void)c; }

/* ----- rate-control obfuscated helpers (binary-internal) --------------- */

int32_t rc_I10(void *rc, int32_t a)              { (void)rc; (void)a; return 0; }
int32_t rc_Ill(void *rc, int32_t a, int32_t b)   { (void)rc; (void)a; (void)b; return 0; }
int32_t rc_IO1(void *rc)                         { (void)rc; return 0; }
int32_t rc_iOOo(void *rc, int32_t a, int32_t b, int32_t c) { (void)rc; (void)a; (void)b; (void)c; return 0; }
int32_t rc_ol1(void *rc, int32_t a)              { (void)rc; (void)a; return 0; }
int32_t rc_olI(void *rc)                         { (void)rc; return 0; }
int32_t rc_Ooii(void *rc, int32_t a, int32_t b)  { (void)rc; (void)a; (void)b; return 0; }

/* ----- DPB builder stub ----------------------------------------------- */

int32_t AL_sDPB_BuildRefList_isra_7(void *dpb, void *slice)
{
    (void)dpb; (void)slice;
    return 0;
}
/* Binja renders as "AL_sDPB_BuildRefList.isra.7"; provide alias so
 * references that preserve the dotted name also resolve. */
__asm__(".globl AL_sDPB_BuildRefList.isra.7");
__asm__("AL_sDPB_BuildRefList.isra.7 = AL_sDPB_BuildRefList_isra_7");

/* ----- framesource/isp link helpers ----------------------------------- */

int32_t ISP_EnsureLinkStreamOn(int32_t sensor_idx)
{
    (void)sensor_idx;
    return 0;
}

/* ----- audio helper ---------------------------------------------------- */

int32_t _ao_get_buf_size(int32_t rate, int32_t sample_bits, int32_t chan_cnt, int32_t num_per_frm)
{
    if (sample_bits <= 0 || chan_cnt <= 0 || num_per_frm <= 0) return 0;
    return (sample_bits / 8) * chan_cnt * num_per_frm;
}

/* ----- encoder-helper stubs (binary "sub_*" / named absorbed helpers) -- */

int32_t sub_dc144(void *a, void *b) { (void)a; (void)b; return 0; }
int32_t sub_dc2d4(void *a, void *b) { (void)a; (void)b; return 0; }
int32_t sub_dcf40(void *a, void *b, void *c) { (void)a; (void)b; (void)c; return 0; }

/* ----- globals --------------------------------------------------------- */

int32_t g_block_info_addr = 0;
int32_t g_HwTimer = 0;

/* ----- encoder internal helpers (T95 follow-up — stubs) ---------------
 *
 * Real bodies require ~400 LoC each from imp_encoder.c internals (channel
 * bring-up, DCT resize pipeline, RC parameter translation). Stub returns
 * allow link and API calls to succeed; rate-control and resize paths are
 * no-ops until the real ports land.
 */

void *release_frame_thread(void *arg);
void *update_frmstrm(void *arg);

int32_t channel_encoder_init(void *arg1)
{
    (void)arg1;
    return 0;
}

/* ----- channel_encoder_exit — binary-accurate port -------------------- */
#include <semaphore.h>

int32_t AL_Codec_Encode_Commit_FilledFifo(void *codec);
int32_t AL_Codec_Encode_GetStream(void *codec, int32_t *out_id, void **out_buf);
int32_t AL_Codec_Encode_ReleaseStream(void *codec, int32_t id, void *buf);
int32_t AL_Codec_Encode_Destroy(void *codec);
int32_t Fifo_Queue(void *fifo, void *item, int32_t timeout_ms);
void Fifo_Deinit(void *fifo);

int32_t channel_encoder_exit(void *arg1)
{
    uint8_t *p = (uint8_t *)arg1;
    if (!p) return -1;

    AL_Codec_Encode_Commit_FilledFifo(*(void **)(p + 8));
    pthread_cancel(*(pthread_t *)(p + 0x3c));
    pthread_join(*(pthread_t *)(p + 0x3c), NULL);
    pthread_cancel(*(pthread_t *)(p + 0x68));
    pthread_join(*(pthread_t *)(p + 0x68), NULL);
    pthread_mutex_destroy((pthread_mutex_t *)(p + 0x50));
    sem_destroy((sem_t *)(p + 0x40));

    pthread_mutex_lock((pthread_mutex_t *)(p + 0x1a8));
    {
        int32_t total_frames = *(int32_t *)(p + 0x7c);
        int32_t complete_hi  = *(int32_t *)(p + 0x84);
        void *fifo_ctx = (void *)(p + 0x18);

        while (complete_hi < total_frames ||
               (complete_hi == total_frames &&
                *(uint32_t *)(p + 0x80) < *(uint32_t *)(p + 0x78))) {
            void *buf = NULL;
            int32_t stream_id = 0;
            if (AL_Codec_Encode_GetStream(*(void **)(p + 8), &stream_id, &buf) < 0)
                break;
            pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
            *(uint32_t *)(p + 0x80) += 1;
            if (*(uint32_t *)(p + 0x80) == 0) *(int32_t *)(p + 0x84) += 1;
            AL_Codec_Encode_ReleaseStream(*(void **)(p + 8), stream_id, buf);
            Fifo_Queue(fifo_ctx, (uint8_t *)buf + 0x24, -1);
            pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
            total_frames = *(int32_t *)(p + 0x7c);
            complete_hi  = *(int32_t *)(p + 0x84);
        }
    }
    pthread_mutex_unlock((pthread_mutex_t *)(p + 0x1a8));

    Fifo_Deinit((void *)(p + 0x18));
    free(*(void **)(p + 0x14));
    AL_Codec_Encode_Destroy(*(void **)(p + 8));
    return 0;
}

/* ----- channel_encoder_set_rc_param — binary-accurate port ------------ */
int32_t channel_encoder_set_rc_param(void *arg1, void *arg2)
{
    int32_t *dst = (int32_t *)arg1;
    int32_t *src = (int32_t *)arg2;
    if (!dst || !src) return -1;

    uint8_t *dst_b = (uint8_t *)arg1;
    uint8_t *src_b = (uint8_t *)arg2;
    int32_t mode = src[0];

    switch (mode) {
    case 0: /* FixQp */
        dst[0] = 0;
        *(int16_t *)(dst_b + 0x18) = *(int16_t *)(src_b + 4);
        return 0;
    case 1: /* Cbr */
        dst[0] = 1;
        dst[4] = src[1] * 1000;
        dst[5] = src[1] * 1000;
        *(int16_t *)(dst_b + 0x18) = *(int16_t *)(src_b + 8);
        *(int8_t  *)(dst_b + 0x1a) = *(int8_t  *)(src_b + 0xa);
        *(int16_t *)(dst_b + 0x1c) = *(int16_t *)(src_b + 0xc);
        *(int8_t  *)(dst_b + 0x1e) = *(int8_t  *)(src_b + 0xe);
        *(int16_t *)(dst_b + 0x20) = *(int16_t *)(src_b + 0x10);
        dst[10] = src[5];
        dst[15] = src[6] * 1000;
        return 0;
    case 2: /* Vbr */
        dst[0] = 2;
        dst[4] = src[1] * 1000;
        dst[5] = src[2] * 1000;
        *(int16_t *)(dst_b + 0x18) = *(int16_t *)(src_b + 0xc);
        *(int8_t  *)(dst_b + 0x1a) = *(int8_t  *)(src_b + 0xe);
        *(int16_t *)(dst_b + 0x1c) = *(int16_t *)(src_b + 0x10);
        *(int8_t  *)(dst_b + 0x1e) = *(int8_t  *)(src_b + 0x12);
        *(int16_t *)(dst_b + 0x20) = *(int16_t *)(src_b + 0x14);
        dst[10] = src[6];
        dst[15] = src[7] * 1000;
        return 0;
    case 4: /* CappedVbr */
    case 8: /* CappedQuality */
        dst[0] = mode;
        dst[4] = src[1] * 1000;
        dst[5] = src[2] * 1000;
        *(int16_t *)(dst_b + 0x18) = *(int16_t *)(src_b + 0xc);
        *(int8_t  *)(dst_b + 0x1a) = *(int8_t  *)(src_b + 0xe);
        *(int16_t *)(dst_b + 0x1c) = *(int16_t *)(src_b + 0x10);
        *(int8_t  *)(dst_b + 0x1e) = *(int8_t  *)(src_b + 0x12);
        *(int16_t *)(dst_b + 0x20) = *(int16_t *)(src_b + 0x14);
        dst[10] = src[6];
        dst[15] = src[7] * 1000;
        {
            uint32_t scale = (uint32_t)(uint16_t)src[8] * 0x14;
            *(int16_t *)(dst_b + 0x30) = (int16_t)(scale + (scale << 2));
        }
        return 0;
    default:
        return -1;
    }
}

/* ----- release_used_framestream — binary-accurate port ---------------- */
int32_t release_used_framestream(void *arg1, int32_t arg2)
{
    uint8_t *p = (uint8_t *)arg1;
    if (!p) return -1;

    int32_t max = *(int32_t *)(p + 0x230);
    int32_t hd  = *(int32_t *)(p + 0x244);
    int32_t idx = *(int32_t *)(p + 0x248);

    if (max == 0) return -1;
    int32_t expected = hd + ((idx % max) * 0x188);
    if (arg2 != expected) {
        return -1;
    }

    pthread_mutex_lock((pthread_mutex_t *)(p + 0x1a8));
    *(int32_t *)(p + 0x248) += 1;
    pthread_mutex_unlock((pthread_mutex_t *)(p + 0x1a8));
    sem_post((sem_t *)(p + 0x178));
    return 0;
}

int32_t on_encoder_group_data_update(void *arg1, void *arg2, void *arg3,
                                     void *arg4, int64_t arg5)
{
    (void)arg1; (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    return 0;
}

/* weak stubs for the encoder frame-release/stream-update threads so
 * pthread_create calls in imp_encoder compile when linked alone. */
__attribute__((weak)) void *release_frame_thread(void *arg) { (void)arg; return NULL; }
__attribute__((weak)) void *update_frmstrm(void *arg) { (void)arg; return NULL; }

/* ----- IVS release — tail-call wrapper -------------------------------- */

int32_t IMP_IVS_ReleaseData(void *vaddr)
{
    VBMUnlockFrameByVaddr(vaddr);
    return 0;
}
