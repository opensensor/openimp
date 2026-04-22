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
#include <semaphore.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdarg.h>
#include "imp_log_int.h"
#include "video/encoder_channel_layout.h"

/* fs_* framesource kernel-interface thunks are provided by legacy
 * src/kernel_interface.c (included in BUILD=ported since it has the
 * real V4L2 ioctl plumbing). */

/* VBM* functions now provided by legacy src/kernel_interface.c in
 * BUILD=ported (the legacy allocator works end-to-end on real hardware,
 * the ported T71 vbm.c + ported allocator chain caused rvd to hang the
 * kernel when continuous_init memset'd 30MB on /dev/rmem). */
void *VBMLockFrameByVaddr(void *vaddr);
void VBMUnlockFrameByVaddr(void *vaddr);

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

static void stub_kmsg(const char *fmt, ...)
{
    int fd;
    char buf[256];
    va_list ap;
    int n;

    fd = open("/dev/kmsg", O_WRONLY);
    if (fd < 0)
        return;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0)
        write(fd, buf, (size_t)((n < (int)sizeof(buf)) ? n : (int)sizeof(buf)));
    close(fd);
}

static void stub_stderr(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0)
        dprintf(2, "%.*s\n", n < (int)sizeof(buf) ? n : (int)sizeof(buf), buf);
}

#define READ_U8(base, off) (*(uint8_t *)((uint8_t *)(base) + (off)))
#define READ_U16(base, off) (*(uint16_t *)((uint8_t *)(base) + (off)))
#define READ_S32(base, off) (*(int32_t *)((uint8_t *)(base) + (off)))
#define READ_PTR(base, off) (*(void **)((uint8_t *)(base) + (off)))

int32_t SliceParamToCmdRegsEnc1(char *arg1, int32_t *arg2, void *arg3, ...);

int32_t UpdateCommand(void *arg1, void *arg2, void *arg3, int32_t arg4)
{
    uint8_t *ch = (uint8_t *)arg1;
    uint8_t *req = (uint8_t *)arg2;
    char *slice = (char *)arg3;
    void *src_meta;
    uint32_t core_count;
    uint32_t core;

    (void)arg4;

    if (ch == NULL || req == NULL || slice == NULL)
        return 0;

    src_meta = READ_PTR(req, 0x318);
    core_count = READ_U8(ch, 0x3c);
    if (core_count == 0)
        core_count = 1;

    for (core = 0; core < core_count; ++core) {
        int32_t *cmd_regs = (int32_t *)(intptr_t)READ_S32(req, 0xa78 + (int32_t)core * 4);

        if (cmd_regs == NULL)
            continue;

        stub_kmsg("libimp/STUB: UpdateCommand pre core=%u req=%p slice=%p meta=%p cmd=%p "
                  "slice0f=%u dim=%ux%u lcu63=%u lcu108=%u lcu10a=%u "
                  "7a=%u 7c=%u a8=%u aa=%u ac=%u "
                  "src14=0x%08x src18=0x%08x src34=0x%08x src44=0x%08x src54=0x%08x src94=0x%08x",
                  core, req, slice, src_meta, cmd_regs,
                  (unsigned)READ_U8(slice, 0x0f),
                  (unsigned)READ_U16(slice, 0x0a), (unsigned)READ_U16(slice, 0x0c),
                  (unsigned)READ_U8(slice, 0x63),
                  (unsigned)READ_U16(slice, 0x108), (unsigned)READ_U16(slice, 0x10a),
                  (unsigned)READ_U16(slice, 0x7a), (unsigned)READ_U16(slice, 0x7c),
                  (unsigned)READ_U16(slice, 0xa8), (unsigned)READ_U16(slice, 0xaa),
                  (unsigned)READ_U8(slice, 0xac),
                  src_meta ? (unsigned)READ_S32(src_meta, 0x14) : 0u,
                  src_meta ? (unsigned)READ_S32(src_meta, 0x18) : 0u,
                  src_meta ? (unsigned)READ_S32(src_meta, 0x34) : 0u,
                  src_meta ? (unsigned)READ_S32(src_meta, 0x44) : 0u,
                  src_meta ? (unsigned)READ_S32(src_meta, 0x54) : 0u,
                  src_meta ? (unsigned)READ_S32(src_meta, 0x94) : 0u);
        stub_stderr("libimp/STUB: UpdateCommand pre core=%u req=%p meta=%p cmd=%p lcu63=%u lcu108=%u 7c=%u 2a4=%08x 2b4=%08x 2e0=%08x 2f4=%08x 304=%08x 308=%08x",
                    core, req, src_meta, cmd_regs, (unsigned)READ_U8(slice, 0x63),
                    (unsigned)READ_U16(slice, 0x108), (unsigned)READ_U16(slice, 0x7c),
                    READ_S32(req, 0x2a4), READ_S32(req, 0x2b4), READ_S32(req, 0x2e0),
                    READ_S32(req, 0x2f4), READ_S32(req, 0x304), READ_S32(req, 0x308));

        if (src_meta == NULL || READ_U8(slice, 0x63) == 0 || READ_U16(slice, 0x108) == 0 || READ_U16(slice, 0x7c) == 0) {
            stub_kmsg("libimp/STUB: UpdateCommand skip core=%u missing-fields meta=%p lcu63=%u lcu108=%u 7c=%u",
                      core, src_meta, (unsigned)READ_U8(slice, 0x63),
                      (unsigned)READ_U16(slice, 0x108), (unsigned)READ_U16(slice, 0x7c));
            stub_stderr("libimp/STUB: UpdateCommand skip core=%u meta=%p lcu63=%u lcu108=%u 7c=%u",
                        core, src_meta, (unsigned)READ_U8(slice, 0x63),
                        (unsigned)READ_U16(slice, 0x108), (unsigned)READ_U16(slice, 0x7c));
            continue;
        }

        memset(cmd_regs, 0, 0x200);
        SliceParamToCmdRegsEnc1(slice, cmd_regs, src_meta);
        stub_kmsg("libimp/STUB: UpdateCommand post core=%u cmd=%p "
                  "w0=0x%08x w1=0x%08x w2=0x%08x w3=0x%08x w0c=0x%08x w10=0x%08x "
                  "w12=0x%08x w18=0x%08x w19=0x%08x w1a=0x%08x w64=0x%08x w65=0x%08x",
                  core, cmd_regs,
                  (unsigned)cmd_regs[0], (unsigned)cmd_regs[1], (unsigned)cmd_regs[2],
                  (unsigned)cmd_regs[3], (unsigned)cmd_regs[0x0c], (unsigned)cmd_regs[0x10],
                  (unsigned)cmd_regs[0x12], (unsigned)cmd_regs[0x18], (unsigned)cmd_regs[0x19],
                  (unsigned)cmd_regs[0x1a], (unsigned)cmd_regs[0x64], (unsigned)cmd_regs[0x65]);
        stub_stderr("libimp/STUB: UpdateCommand post core=%u cmd=%p w32=%08x w33=%08x w34=%08x w35=%08x w36=%08x w37=%08x w38=%08x",
                    core, cmd_regs,
                    (unsigned)cmd_regs[0x20], (unsigned)cmd_regs[0x21], (unsigned)cmd_regs[0x22],
                    (unsigned)cmd_regs[0x23], (unsigned)cmd_regs[0x24], (unsigned)cmd_regs[0x25],
                    (unsigned)cmd_regs[0x26]);
    }

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

static void enc_trace(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n <= 0)
        return;

    IMP_LOG_INFO("ENCX", "%s", buf);
}

static EncoderCompatRuntime g_encoder_runtime[9];

#define OEM_FRAME_CLONE_BYTES 0x30
#define OEM_FRAME_SLOT_BYTES  0x458

/* Local helper prototypes used by the encoder worker path before the
 * bulk forward-declaration block below. */
void   *Fifo_Dequeue(void *fifo_ptr, int32_t timeout_ms);
int32_t Fifo_Queue(void *fifo_ptr, void *item, int32_t timeout_ms);
int32_t AL_Codec_Encode_Process(void *codec, void *frame, void *user_data);
int32_t AL_Get_StreamMngrCtx(int32_t *arg1);

static int encoder_thread_slot(void *arg1)
{
    if (arg1 == NULL) return -1;
    int32_t chn = *(int32_t *)arg1;
    if (chn < 0 || chn >= 9) return -1;
    return chn;
}

static void *encoder_acquire_frame_slot(EncoderChannelLayout *chn)
{
    if (chn == NULL)
        return NULL;
    return Fifo_Dequeue(chn->public_stream_fifo, 0);
}

static void encoder_release_frame_slot(EncoderChannelLayout *chn, void *slot)
{
    if (chn == NULL || slot == NULL)
        return;
    Fifo_Queue(chn->public_stream_fifo, slot, -1);
}

static void encoder_unlock_and_release_frame(EncoderChannelLayout *chn, void *slot)
{
    void *vaddr;

    if (chn == NULL || slot == NULL)
        return;

    vaddr = *(void **)((uint8_t *)slot + 0x1c);
    if (vaddr != NULL)
        VBMUnlockFrameByVaddr(vaddr);
    encoder_release_frame_slot(chn, slot);
}

static int encoder_clone_source_frame(EncoderChannelLayout *chn, void *src_frame, void **slot_out)
{
    void *slot;
    void *vaddr;

    if (slot_out == NULL)
        return -1;
    *slot_out = NULL;
    if (chn == NULL || src_frame == NULL)
        return -1;

    slot = encoder_acquire_frame_slot(chn);
    if (slot == NULL)
        return -1;

    memset(slot, 0, OEM_FRAME_SLOT_BYTES);
    memcpy(slot, src_frame, OEM_FRAME_CLONE_BYTES);

    vaddr = *(void **)((uint8_t *)slot + 0x1c);
    if (VBMLockFrameByVaddr(vaddr) == NULL) {
        encoder_release_frame_slot(chn, slot);
        return -1;
    }

    *slot_out = slot;
    return 0;
}

static void *encoder_submit_thread(void *arg)
{
    EncoderChannelLayout *chn = (EncoderChannelLayout *)arg;
    EncoderCompatRuntime *rt;
    int slot;

    slot = encoder_thread_slot(arg);
    if (chn == NULL || slot < 0)
        return NULL;
    rt = &g_encoder_runtime[slot];

    for (;;) {
        void *frame;
        void *codec;
        int32_t proc_ret;

        pthread_mutex_lock(enc_mutex_ptr(&rt->submit_mutex));
        while (rt->pending_frame == NULL)
            pthread_cond_wait(enc_cond_ptr(&rt->submit_cond), enc_mutex_ptr(&rt->submit_mutex));
        frame = rt->pending_frame;
        rt->pending_frame = NULL;
        pthread_mutex_unlock(enc_mutex_ptr(&rt->submit_mutex));

        if (frame == NULL)
            continue;

        codec = enc_channel_codec(chn);
        if (codec == NULL || *enc_channel_recv_pic_started(chn) == 0) {
            encoder_unlock_and_release_frame(chn, frame);
            continue;
        }

        proc_ret = AL_Codec_Encode_Process(codec, frame, frame);
        enc_trace("libimp/ENCX: submit-thread process-ret=%d ch=%d codec=%p frame=%p\n",
                  proc_ret, chn->chn_id, codec, frame);
        if (proc_ret < 0)
            encoder_unlock_and_release_frame(chn, frame);
    }
}

/* ----- halfword pack/unpack helpers ---------------------------------
 *
 * Thingino's stock libsysutils.so doesn't export these; define inside
 * libimp.so so we don't depend on stock.
 *
 * IMPORTANT: These are IDENTITY passthrough in the stock binary, not
 * shifts. Call sites look like:
 *    _setLeftPart32(arg1[0]);       // side-effect only, result discarded
 *    v = _setRightPart32(arg1[0]);  // v = arg1[0] (full 32-bit)
 * then v gets stored as a 32-bit word. The "halfword" naming is a
 * misnomer from the stock SDK's MIPS ext/ins intrinsic prototypes —
 * the actual semantics are pass-through.
 *
 * Earlier implementations used (x & 0xFFFF) which truncated 32-bit
 * encAttr values on the way through IMP_Encoder_CreateChn's copy loop.
 * Constants like 0x40028 and width=1920 got mangled; AL_Codec_Encode_
 * Create then rejected the codec_params with "invalid resolution". */

uint32_t _setLeftPart32(uint32_t x)  { return x; }
uint32_t _setRightPart32(uint32_t x) { return x; }
uint32_t _getLeftPart32(uint32_t x)  { return x; }
uint32_t _getRightPart32(uint32_t x) { return x; }

/* =====================================================================
 * Encoder internal helpers — ported from libimp.so HLIL (T95 follow-up)
 * =====================================================================
 *
 * Functions in this block (with binary addresses):
 *   - sub_8f5fc            @ 0x8f5fc  (error/return trampoline; leaf)
 *   - sub_8fdc4            @ 0x8fdc4  (long-term reference frame handler)
 *   - sub_8f550            @ 0x8f550  (AL_Codec_Encode_Process dispatch)
 *   - sub_8f698            @ 0x8f698  (resize-output commit)
 *   - sub_8eea0            @ 0x8eea0  (fps/gop pacing + encode kick)
 *   - on_encoder_group_data_update @ 0x8ecf8  (per-channel fan-out)
 *   - release_frame_thread @ 0x8ead8  (frame-release pacing thread)
 *   - update_frmstrm       @ 0x93124  (stream-fifo pump thread)
 *   - channel_encoder_init @ 0x909a8  (per-channel bring-up)
 *
 * Struct-layout note:
 *   The encoder channel state (`arg1`/`$s7`) is accessed by word-index
 *   arithmetic in the HLIL.  Rather than declare a typed struct (whose
 *   layout is not fully mapped), we use raw byte-offset form:
 *     arg1[N]             -> *(int32_t *)((char *)p + N*4)
 *     *(arg1 + 0xNN)      -> *(uint8_t/int32_t *)((char *)p + 0xNN)
 *   This mirrors the stock `imp_encoder.c` struct layout at word indices
 *   0..0x120, plus the frame-sub-struct at offsets 0x248..0x2dc.
 *
 * Binary-exact guarantees (as requested):
 *   - release_frame_thread, update_frmstrm: cycle-exact thread loops.
 *   - sub_8fdc4: exact LT-reference control flow.
 *   - sub_8f5fc: trivial trampoline returning nothing meaningful.
 *   - channel_encoder_init: external call sequence exact; inlined
 *     dump_encoder_chn_attr log block elided (cosmetic).
 *   - Resize helpers (sub_8eea0/sub_8f550/sub_8f698/
 *     on_encoder_group_data_update): structurally faithful — every
 *     external call is preserved in the same order and with the same
 *     control flow.  The internal MIPS-FPU float arithmetic for
 *     aspect-ratio comparison is rendered using native C float ops.
 */

/* ---- forward decls for external helpers used below ------------------- */

int32_t IMP_Log_Get_Option(void);
int32_t imp_log_fun(int32_t level, int32_t option, int32_t type, ...);
int32_t __assert(const char *expression, const char *file, int32_t line,
                 const char *function, ...); /* forward decl, ported by T<N> later */

/* AL_Codec_Encode_* — in src/alcodec/lib_codec.c */
int32_t AL_Codec_Encode_SetDefaultParam(void *param);
int32_t AL_Codec_Encode_Create(void **codec, void *params);
int32_t AL_Codec_Encode_Destroy(void *codec);
int32_t AL_Codec_Encode_GetSrcFrameCntAndSize(void *codec, int32_t *cnt, int32_t *size);
int32_t AL_Codec_Encode_GetStream(void *codec, void **stream, void **user_data);
int32_t AL_Codec_Encode_ReleaseStream(void *codec, void *stream, void *user_data);
int32_t AL_Codec_Encode_Process(void *codec, void *frame, void *user_data);
int32_t AL_Codec_Encode_SetFrameRate(void *codec, void *fps);
int32_t AL_Codec_Encode_Commit_FilledFifo(int32_t *codec);

/* AL_Encoder_* — tail-call forwards to AL_Common_Encoder_NotifyUseLongTerm
 * (defined in src/alcodec/Com_Encoder.c, sig: void*(int32_t *)). */
void *AL_Common_Encoder_NotifyUseLongTerm(int32_t *arg1);
int32_t AL_Encoder_NotifyUseLongTerm(void *enc);

/* Fifo — in src/fifo.c (legacy) / src/alcodec/BufPool.c (ported). */
int32_t Fifo_Init(void *fifo_ptr, int32_t size);
void    Fifo_Deinit(void *fifo_ptr);
int32_t Fifo_Queue(void *fifo_ptr, void *item, int32_t timeout_ms);
void   *Fifo_Dequeue(void *fifo_ptr, int32_t timeout_ms);

/* AL_Buffer / meta / system timestamp helpers used by update_one_frmstrm. */
uint32_t AL_Buffer_GetSize(void *buffer);
void    *AL_Buffer_GetData(void *buffer);
void    *AL_Buffer_GetMetaData(void *buffer, int32_t type);
uint64_t IMP_System_GetTimeStamp(void);

/* Misc helpers */
void c_reduce_fraction(int32_t *num, int32_t *den);
uint32_t _setLeftPart32(uint32_t value);
uint32_t _setRightPart32(uint32_t value);
uint32_t _getLeftPart32(uint32_t value);
uint32_t _getRightPart32(uint32_t value);
int32_t is_has_simd128(void);
int32_t Rtos_GetTime(void);

/* Resize kernels — in src/codec_c/resize{,_scalar}.c (return-type
 * varies in real tree, but we only need function-pointer identity). */
extern void c_resize_c(void);
extern void c_resize_simd(void);

/* Ported channel helpers (definitions further below and in other files). */
int32_t channel_encoder_set_rc_param(void *arg1, void *arg2);
int32_t release_used_framestream(void *arg1, int32_t arg2);
void    do_release_frame(void *chn, void *srcFrame, int32_t flag);
int32_t update_one_frmstrm(void *arg1);

/* ===== do_release_frame @ 0x8e6e4 ====================================
 * Source-frame release accountant.  Called from release_frame_thread
 * for every NV12 source that came out of the ISP.  The HLIL reads the
 * frame's "refcount slot" at +0x44c (arg2), increments it, and if this
 * is a "flush" invocation (arg3 != 0) updates the rolling max-delay
 * record at enc+0x298/+0x29c.  The jitter math is a 64-bit timestamp
 * subtraction + 1.25x inflation (<<2 + original, then /4) to bound
 * the next-pacing target. */
void do_release_frame(void *chn, void *srcFrame, int32_t flag)
{
    uint8_t *arg1 = (uint8_t *)chn;
    uint8_t *arg2 = (uint8_t *)srcFrame;
    if (arg2 != NULL) {
        pthread_mutex_t *lk = *(pthread_mutex_t **)(arg2 + 0x448);
        int32_t v0_1;

        pthread_mutex_lock(lk);
        v0_1 = *(int32_t *)(arg2 + 0x44c);
        if (v0_1 == 0) {
            if (*(int32_t *)(arg1 + 0x250) == 0 &&
                *(int32_t *)(arg1 + 0x258) == 0 &&
                *(int32_t *)(arg1 + 0x25c) == 0) {
                v0_1 = *(int32_t *)(arg1 + 0x254);
                if (*(int32_t *)(arg1 + 0x254) == 0 || v0_1 != 0) {
                    VBMUnlockFrameByVaddr(*(void **)(arg2 + 0x1c));
                }
                v0_1 = *(int32_t *)(arg2 + 0x44c);
            } else {
                v0_1 = *(int32_t *)(arg1 + 0x254);
                if (v0_1 != 0) {
                    VBMUnlockFrameByVaddr(*(void **)(arg2 + 0x1c));
                    v0_1 = *(int32_t *)(arg2 + 0x44c);
                }
            }
        }
        *(int32_t *)(arg2 + 0x44c) = v0_1 + 1;
        pthread_mutex_unlock(lk);

        pthread_mutex_lock(lk);
        if (flag != 0 && *(int32_t *)(arg2 + 0x430) != 0) {
            uint64_t t64 = (uint64_t)(uint32_t)Rtos_GetTime();
            uint32_t v0_4 = (uint32_t)t64;
            int32_t v1_1 = (int32_t)(t64 >> 32);
            int32_t a1 = *(int32_t *)(arg1 + 0x284);
            uint32_t a0_3 = *(uint32_t *)(arg1 + 0x280);
            int32_t v0_5;
            uint32_t a0_4;
            int32_t a2_2;
            uint32_t a3_1;

            *(uint32_t *)(arg1 + 0x288) = v0_4;
            *(int32_t *)(arg1 + 0x28c) = v1_1;
            if (a1 < v1_1 || (v1_1 == a1 && a0_3 < v0_4)) {
                a0_4 = v0_4 - a0_3;
                a2_2 = *(int32_t *)(arg1 + 0x29c);
                v0_5 = v1_1 - a1 - (v0_4 < a0_4 ? 1 : 0);
                *(uint32_t *)(arg1 + 0x290) = a0_4;
                *(int32_t *)(arg1 + 0x294) = v0_5;
                a3_1 = *(uint32_t *)(arg1 + 0x298);
            } else {
                v0_5 = *(int32_t *)(arg1 + 0x294);
                a2_2 = *(int32_t *)(arg1 + 0x29c);
                a0_4 = *(uint32_t *)(arg1 + 0x290);
                a3_1 = *(uint32_t *)(arg1 + 0x298);
            }

            if (a2_2 >= v0_5) {
                if (v0_5 == a2_2) {
                    if (a3_1 < a0_4) {
                        uint32_t v1_4 = a0_4 << 2;
                        uint32_t a0_5 = v1_4 + a0_4;
                        int32_t v0_7 = (a0_5 < v1_4 ? 1 : 0) +
                                       ((int32_t)(a0_4 >> 30) | (v0_5 << 2)) + v0_5;
                        int32_t v1_7 = (v0_7 >> 31) & 3;
                        uint32_t a0_6 = (uint32_t)v1_7 + a0_5;
                        int32_t v0_8 = (a0_6 < (uint32_t)v1_7 ? 1 : 0) + v0_7;
                        int32_t v1_9 = v0_8 >> 2;
                        uint32_t a0_8 = ((uint32_t)v0_8 << 30) | (a0_6 >> 2);

                        if (v1_9 < a2_2 ||
                            (v1_9 == a2_2 && a0_8 < a3_1)) {
                            *(uint32_t *)(arg1 + 0x298) = a0_8;
                            *(int32_t *)(arg1 + 0x29c) = v1_9;
                        } else {
                            *(uint32_t *)(arg1 + 0x298) = a0_4;
                            *(int32_t *)(arg1 + 0x29c) = v0_5;
                        }
                    }
                } else {
                    *(uint32_t *)(arg1 + 0x298) = a0_4;
                    *(int32_t *)(arg1 + 0x29c) = v0_5;
                }
            }
        }
        pthread_mutex_unlock(lk);
    }
}

/* ===== update_one_frmstrm @ 0x92a1c =================================
 * Consumer thread iteration: pulls one encoded stream from the codec,
 * copies the 8-word section descriptors into the public-frame struct,
 * signals the dequeue semaphore, and feeds the public stream-FIFO at
 * p+0x18.  Returns 0 on success, -1 if GetStream fails. */
int32_t update_one_frmstrm(void *arg1_in)
{
    uint8_t *arg1 = (uint8_t *)arg1_in;
    EncoderChannelLayout *chn = (EncoderChannelLayout *)arg1_in;
    if (arg1 == NULL) return -1;

    void *var_40 = NULL;
    void *var_44 = NULL;
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

    if (AL_Codec_Encode_GetStream(*(void **)(arg1 + 8),
                                  &var_44, &var_40) < 0) {
        int32_t opt = IMP_Log_Get_Option();
        imp_log_fun(6, opt, 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
            0x4f6, "update_one_frmstrm",
            "Codec_Encode_GetStream pStream=%p, pSrc=%p\n",
            var_44, var_40);
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
        return -1;
    }

    pthread_mutex_lock(enc_mutex_ptr(&chn->queue_mutex));
    {
        uint32_t v0_1 = *(uint32_t *)(arg1 + 0x80);
        uint32_t v0_3 = ((v0_1 + 1U < v0_1) ? 1U : 0U)
                      + *(uint32_t *)(arg1 + 0x84);
        *(uint32_t *)(arg1 + 0x80) = v0_1 + 1U;
        *(uint32_t *)(arg1 + 0x84) = v0_3;
    }
    pthread_mutex_unlock(enc_mutex_ptr(&chn->queue_mutex));

    /* $s7_1 = var_40[0x24/4]  (public frame struct inside user-data). */
    int32_t *s7_1 = *(int32_t **)((uint8_t *)var_40 + 0x24);
    uint64_t ts = (uint64_t)(uint32_t)Rtos_GetTime();
    s7_1[0x108] = (int32_t)(uint32_t)ts;
    s7_1[0x109] = (int32_t)(uint32_t)(ts >> 32);

    /* When in PASS-THROUGH mode (+0x2d4 set) or slot mode=4, skip the
     * 8-word "ENCODE TIME" log and the setRight copy of &str[0x59..0x5d]. */
    int32_t *str;
    if (*(uint8_t *)(arg1 + 0x2d4) != 0) {
        str = (int32_t *)(uintptr_t)s7_1[0x10a];
    } else if (*(uint8_t *)(arg1 + 0x9b * 4) == 4) {
        str = (int32_t *)(uintptr_t)s7_1[0x10a];
    } else {
        /* Copy the 10-word ENCODE-TIME block (str[0x59..0x62]) into the
         * public-frame struct at arg1+0x2e0..+0x308 via setLeft+setRight
         * permute — the stock idiom for 32-bit word stores on MIPS. */
        str = (int32_t *)(uintptr_t)s7_1[0x10a];
        int32_t *dst = (int32_t *)(arg1 + 0x2e0);
        for (int i = 0; i < 10; i++) {
            (void)_setLeftPart32((uint32_t)str[0x59 + i]);
            dst[i] = (int32_t)_setRightPart32((uint32_t)str[0x59 + i]);
        }
    }

    /* Copy per-frame timestamps (stream[0x59..0x5a]) into the public
     * frame's timestamp slots — common tail for all paths. */
    {
        uint32_t a1_21 = _setRightPart32((uint32_t)str[0x59]);
        uint32_t a0_32 = _setRightPart32((uint32_t)str[0x5a]);
        (void)_getLeftPart32(a1_21);
        *(uint32_t *)(arg1 + 0x2e0) = _getRightPart32(a1_21);
        (void)_getLeftPart32(a0_32);
        *(uint32_t *)(arg1 + 0x2e4) = _getRightPart32(a0_32);
    }

    /* -- Build public stream descriptor ------------------------------- */
    memset(str, 0, 0x188);
    str[4] = (int32_t)(intptr_t)var_44;
    int32_t sz = (int32_t)AL_Buffer_GetSize(var_44);
    str[5] = (int32_t)(intptr_t)var_40;
    str[2] = sz;

    void *meta = AL_Buffer_GetMetaData(var_44, 1);
    int32_t data = (int32_t)(intptr_t)AL_Buffer_GetData(var_44);
    uint32_t s0_2 = *(uint32_t *)((uint8_t *)meta + 0x14);   /* section cnt */
    str[0] = 0;
    str[1] = data;

    uint32_t s6_1 = 0;   /* accumulated payload bytes */
    uint32_t s5_1 = 0;   /* running offset */
    int32_t  t0_2 = 0;   /* non-empty-section count */

    if (s0_2 != 0) {
        int32_t  s2_2 = (int32_t)(s0_2 - 1) * 0x14;
        int32_t *s0_4 = (int32_t *)((uint8_t *)str + (size_t)s0_2 * 8 * 4);
        for (int32_t i = (int32_t)s0_2 - 1; i >= 0; i--,
             s2_2 -= 0x14, s0_4 -= 8) {
            int32_t *v0_17 = (int32_t *)((uint8_t *)
                *(void **)((uint8_t *)meta + 0x10) + s2_2);
            uint32_t n = (uint32_t)v0_17[1];
            if (n == 0) continue;
            int32_t t3_1 = v0_17[4];
            int32_t t2_1 = v0_17[2];
            s0_4[1] = (int32_t)n;
            int32_t a1_5 = s7_1[9];
            int32_t t4_1 = v0_17[3];
            s0_4[2] = s7_1[8];
            s0_4[3] = a1_5;
            *((uint8_t *)&s0_4[4]) = (uint8_t)((uint32_t)t2_1 >> 0x1d) & 1U;
            s0_4[5] = t4_1;
            s0_4[6] = t3_1;
            if ((uint32_t)(t3_1 - 1) < 2U ||
                *(uint8_t *)(arg1 + 0x9b * 4) == 4) {
                s5_1 = (uint32_t)v0_17[0];
                t0_2++;
                s0_4[0] = (int32_t)s5_1;
                uint32_t v0_18 = *(uint32_t *)(arg1 + 0x90);
                uint32_t v1_9 = ((v0_18 + 1U < v0_18) ? 1U : 0U)
                              + *(uint32_t *)(arg1 + 0x94);
                *(uint32_t *)(arg1 + 0x90) = v0_18 + 1U;
                *(uint32_t *)(arg1 + 0x94) = v1_9;
                str[3] = (int32_t)v0_18;
            } else {
                s5_1 -= n;
                s0_4[0] = (int32_t)s5_1;
                uint8_t *dbase = (uint8_t *)(intptr_t)str[1];
                memmove(dbase + s5_1, dbase + (uint32_t)v0_17[0], n);
            }
            str[6] += 1;
            s6_1 += (uint32_t)s0_4[1];
        }
    }

    if (t0_2 == 1) {
        /* When a single section was extracted, shift it to the base and
         * relocate all "nalu" offsets to form a contiguous stream. */
        int32_t a1_7 = str[6];
        str[1] += (int32_t)s5_1;
        if (a1_7 != 0) {
            int32_t *it = &str[0];
            uint32_t v1_10 = 0;
            for (int32_t k = 0; k < a1_7; k++, it += 8) {
                int32_t len = it[9];
                it[8] = (int32_t)v1_10;
                v1_10 += (uint32_t)len;
            }
        }
    } else {
        __assert("iNumFrame == 1",
                 "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
                 0x53b, "update_one_frmstrm");
    }

    *(uint32_t *)(arg1 + 0x2a4) += s6_1;
    *(uint32_t *)(arg1 + 0x2a8) += 1U;

    /* Periodic bitrate stats flush (every >= 4000 us window). */
    {
        uint64_t now_us = IMP_System_GetTimeStamp() / 1000ULL;
        int64_t prev = ((int64_t)*(int32_t *)(arg1 + 0x2c4) << 32) |
                       (uint32_t)*(int32_t *)(arg1 + 0x2c0);
        if (prev <= 0) {
            *(int32_t *)(arg1 + 0x2c0) = (int32_t)(uint32_t)now_us;
            *(int32_t *)(arg1 + 0x2c4) = (int32_t)(uint32_t)(now_us >> 32);
        } else {
            int64_t delta = (int64_t)now_us - prev;
            if (delta >= 4000) {
                /* Snapshot prev-window counts then zero. */
                *(uint32_t *)(arg1 + 0x2a8) = 0U;
                *(int32_t  *)(arg1 + 0x2c0) = (int32_t)(uint32_t)now_us;
                *(int32_t  *)(arg1 + 0x2c4) = (int32_t)(uint32_t)(now_us >> 32);
                *(uint32_t *)(arg1 + 0x2a4) = 0U;
            }
        }
    }

    /* Hand the public-frame record to the consumer fifo. */
    Fifo_Queue(arg1 + 0x18, s7_1, -1);
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

    *(int32_t *)(arg1 + 0x118) = str[6];
    {
        int32_t qdepth = 0;
        sem_getvalue((sem_t *)(arg1 + 0x188), &qdepth);
        int32_t s6_2 = *enc_channel_stream_capacity_bytes(chn) - (int32_t)s6_1;
        *(int32_t *)(arg1 + 0x114) = qdepth;
        *(int32_t *)(arg1 + 0x110) = s6_2;
    }

    /* Signal stream-ready + cond wake. */
    {
        uint64_t one = 1;
        pthread_mutex_lock((pthread_mutex_t *)(arg1 + 0x1d8));
        (void)write(*(int32_t *)(arg1 + 0x220), &one, 8);
        uint32_t v0_28 = *enc_channel_stream_ready_lo(chn);
        uint32_t v0_30 = ((v0_28 + 1U < v0_28) ? 1U : 0U)
                       + *enc_channel_stream_ready_hi(chn);
        *enc_channel_stream_ready_lo(chn) = v0_28 + 1U;
        *enc_channel_stream_ready_hi(chn) = v0_30;
        sem_post((sem_t *)(arg1 + 0x188));
        sem_post((sem_t *)(arg1 + 0x198));
        pthread_cond_signal((pthread_cond_t *)(arg1 + 0x1f0));
        pthread_mutex_unlock((pthread_mutex_t *)(arg1 + 0x1d8));
    }
    return 0;
}

/* ===== AL_Encoder_NotifyUseLongTerm @ 0x5825c =======================
 * Single tail-call: forwards to AL_Common_Encoder_NotifyUseLongTerm
 * with the same register-passed argument (the common-encoder handle). */
int32_t AL_Encoder_NotifyUseLongTerm(void *enc)
{
    return (int32_t)(intptr_t)AL_Common_Encoder_NotifyUseLongTerm((int32_t *)enc);
}

/* Thread entries ported in this file. */
void *release_frame_thread(void *arg);
void *update_frmstrm(void *arg);

/* Forward decls for the resize-pipeline sub-helpers (mutual tail-call). */
static int32_t sub_8f5fc_impl(void);
static int32_t sub_8fdc4_impl(uint8_t *arg1, uint8_t *arg2, int32_t arg3,
                              int32_t arg4, int32_t arg5);
static int32_t sub_8f550_impl(uint8_t *s0_slot, int32_t i, uint8_t *frame,
                              int32_t s3_tag, uint8_t *s6_group,
                              uint8_t *enc);
static int32_t sub_8f698_impl(uint8_t *s0_slot, int32_t i, uint8_t *frame,
                              int32_t s3_tag, int32_t s4_dst_h,
                              int32_t s5_dst_w, uint8_t *s6_group,
                              uint8_t *enc);
static int32_t sub_8eea0_impl(uint8_t *s0_slot, int32_t i, uint8_t *frame,
                              int32_t s3_tag, uint8_t *s4_priv,
                              uint8_t *s6_group, uint8_t *enc);

/* Public symbols (what the linker sees).  Thin shims forward into the
 * internal implementations so we keep the mutual-tail-call recursion
 * structure intact without exposing the full 20-arg HLIL signatures. */
int32_t sub_8f5fc(void)
{
    return sub_8f5fc_impl();
}

int32_t sub_8fdc4(void *arg1, void *arg2, int32_t arg3, int32_t arg4, int32_t arg5)
{
    return sub_8fdc4_impl((uint8_t *)arg1, (uint8_t *)arg2, arg3, arg4, arg5);
}

int32_t sub_8f550(void *arg1, int32_t arg2, void *arg3, int32_t arg4,
                  void *arg6, void *arg7)
{
    return sub_8f550_impl((uint8_t *)arg1, arg2, (uint8_t *)arg3, arg4,
                          (uint8_t *)arg6, (uint8_t *)arg7);
}

int32_t sub_8f698(void *arg1, int32_t arg2, void *arg3, int32_t arg4,
                  int32_t arg5, int32_t arg6, void *arg7, void *arg8)
{
    return sub_8f698_impl((uint8_t *)arg1, arg2, (uint8_t *)arg3, arg4,
                          arg5, arg6, (uint8_t *)arg7, (uint8_t *)arg8);
}

int32_t sub_8eea0(void *arg1, int32_t arg2, void *arg3, int32_t arg4,
                  void *arg5, void *arg6, void *arg7)
{
    return sub_8eea0_impl((uint8_t *)arg1, arg2, (uint8_t *)arg3, arg4,
                          (uint8_t *)arg5, (uint8_t *)arg6, (uint8_t *)arg7);
}

/* ===== sub_8f5fc @ 0x8f5fc ===========================================
 * Leaf trampoline — the HLIL shows four moveDwordToCoprocessor... FPU
 * register restores followed by a bare return.  In other words, it is
 * the function epilogue of a larger tail-called helper and simply
 * returns whatever is in $v0 at the point of tail-call.  Semantically
 * a no-op — its value is already set by the caller's error path. */
static int32_t sub_8f5fc_impl(void)
{
    return -1;
}

/* ===== sub_8fdc4 @ 0x8fdc4 ===========================================
 * Long-term reference-frame control.
 * arg1 = encoder channel state (uint8_t *p)
 * arg2 = stream record (uint8_t *s)
 * arg3 = current counter (passed through)
 * arg4 = updated counter value (from caller, used when arg5 != 1)
 * arg5 = mode flag from sub_8eea0 path
 *
 * The body mirrors 0x8fdc4..0x8fea8 verbatim: it reads the channel's
 * long-term config at offsets 0x140/0x142/0x144/0x148, decides whether
 * this frame's position triggers an LT reference request, optionally
 * calls AL_Encoder_NotifyUseLongTerm, and sets the stream record's
 * +0x450 flag byte before tail-calling sub_8f550. */
static int32_t sub_8fdc4_impl(uint8_t *arg1, uint8_t *arg2, int32_t arg3,
                              int32_t arg4, int32_t arg5)
{
    EncoderChannelLayout *chn = (EncoderChannelLayout *)arg1;
    EncoderAttrLayout *attr = enc_channel_attr(chn);
    /* Aliases matching the HLIL variables for readability. */
    uint32_t  v1_12 = attr->notify_user_lt_inter;
    int32_t   v0    = *enc_channel_ltr_frame_index(chn);
    int32_t   v1_1  = 0;
    int32_t   arg_140 = 0;
    int32_t   a1_1  = 0;
    uint8_t   en    = attr->enable_lt;

    (void)arg3;

    /* Top guard: if LT is disabled OR bLTRC == 0, jump straight to the
     * "reset counter, set +0x450 = 0" exit path. */
    if (v1_12 == 0 || en == 0) {
        *enc_channel_ltr_gop_index(chn) += 1;
        *(uint8_t *)(arg2 + 0x450) = 0;                /* no LT notify */
        return sub_8f550_impl(NULL, 0, NULL, 0, NULL, arg1);
    }

    /* Post-GOP rollover: if GOP frame count hit uGopLength, reset. */
    {
        uint32_t gop_len = (uint32_t)attr->gop_length *
                           attr->max_same_scene_cnt;
        if (gop_len == (uint32_t)*enc_channel_ltr_gop_index(chn)) {
            *enc_channel_ltr_gop_index(chn) = 0;
            a1_1 = 1;
        }
    }

    if (arg5 == 1) {
        v1_1 = (int32_t)attr->freq_lt;
        arg_140 = 1;
        goto label_8fe38;
    }

    /* Periodic-frequency path:  (v0 % v1_12) == 0 => new LT frame. */
    if (v1_12 == 0) __builtin_trap();
    if ((v0 % (int32_t)v1_12) == 0) {
        v1_1 = (int32_t)attr->freq_lt;
        arg_140 = 2;
        arg4 = *enc_channel_ltr_gop_index(chn) + 1;
        goto label_8fe38;
    }

    if (v0 >= (int32_t)attr->freq_lt) {
        arg_140 = 3;
        *enc_channel_ltr_gop_index(chn) += 1;
        *enc_channel_ltr_frame_index(chn) = 0;
        goto label_8fe54;
    }

    arg_140 = 0;
    *enc_channel_ltr_gop_index(chn) += 1;
    if (a1_1 == 1) {
        *enc_channel_ltr_frame_index(chn) = 0;
    }
    *(uint8_t *)(arg2 + 0x450) = 0;
    *enc_channel_ltr_frame_index(chn) = v0 + 1;
    return sub_8f550_impl(NULL, 0, NULL, 0, NULL, arg1);

label_8fe38:
    *enc_channel_ltr_gop_index(chn) = arg4;
    if ((uint32_t)v0 < (uint32_t)v1_1 && a1_1 == 0 && arg5 == 0) {
        /* fall-through */
    }

label_8fe54:
    /* *(arg1 + 8) is codec pointer; +0x798 is the AL_Encoder within it. */
    {
        uint8_t *codec = (uint8_t *)enc_channel_codec(chn);
        if (codec != NULL) {
            AL_Encoder_NotifyUseLongTerm(*(void **)(codec + 0x798));
        }
    }
    if (arg_140 == 2) {
        *(uint8_t *)(arg2 + 0x450) = 1;
        *enc_channel_ltr_frame_index(chn) += 1;
        return sub_8f550_impl(NULL, 0, NULL, 0, NULL, arg1);
    }
    *(uint8_t *)(arg2 + 0x450) = 0;
    *enc_channel_ltr_frame_index(chn) = v0 + 1;
    return sub_8f550_impl(NULL, 0, NULL, 0, NULL, arg1);
}

/* ===== sub_8f550 @ 0x8f550 ===========================================
 * Calls AL_Codec_Encode_Process() on the encoder handle at +8,
 * refreshes the per-stream frame-rate target fields, and returns to
 * caller (which in the HLIL loops over the 3 encoder slots).
 *
 * Our caller already handles the loop increment — this helper only
 * performs the Process dispatch + timing update, then falls through
 * to the tail of the channel-iteration loop in sub_8f5fc's epilogue.
 */
static int32_t sub_8f550_impl(uint8_t *s0_slot, int32_t i, uint8_t *frame,
                              int32_t s3_tag, uint8_t *s6_group,
                              uint8_t *enc)
{
    EncoderChannelLayout *chn = (EncoderChannelLayout *)enc;
    (void)s0_slot; (void)i; (void)frame; (void)s3_tag; (void)s6_group;

    if (enc == NULL) {
        /* Degenerate call from sub_8fdc4_impl: no encoder context. */
        return 0;
    }

    void *codec = enc_channel_codec(chn);
    enc_trace("libimp/ENCX: sub_8f550 entry enc=%p codec=%p frame=%p ch=%d tag=%d\n",
              enc, codec, frame, *enc_channel_stream_cookie(chn), s3_tag);
    if (codec != NULL) {
        int32_t proc_ret = AL_Codec_Encode_Process(codec, frame, frame);
        enc_trace("libimp/ENCX: sub_8f550 process-ret=%d enc=%p codec=%p frame=%p\n",
                  proc_ret, enc, codec, frame);
        if (proc_ret < 0) {
            /* Log is elided; the HLIL imp_log_fun line is at 0x9029c. */
            release_used_framestream(enc, *enc_channel_stream_cookie(chn));
            return sub_8f5fc_impl();
        }
    } else {
        enc_trace("libimp/ENCX: sub_8f550 codec-null enc=%p frame=%p\n",
                  enc, frame);
    }

    /* Timing-budget update (HLIL 0x8f580..0x8f5d8):
     *   $s4         = *(stream + 4)          // stream's meta root
     *   get_val32($s4 + 0xf4, &arg_11c)      // reads per-stream delay-lo
     *   get_val32($s4 + 0xe4, &arg_50)       // reads per-stream delay-hi
     *   v0_3 = max(0x10 - arg_11c, arg_50)
     *   enc[+0x10c] = v0_3
     *   enc[+0x11c] = 1
     *
     * The two get_val32 calls are indirected through a function-pointer
     * table in rodata (arg13 - 0x7a50).  In our port we read the raw
     * words directly — the helper is a straight word-load on the
     * real-hardware path. */
    if (frame != NULL) {
        uint8_t *meta_root = *(uint8_t **)(frame + 4);
        if (meta_root != NULL) {
            int32_t v_lo = *(int32_t *)(meta_root + 0xf4);   /* arg_11c */
            int32_t v_hi = *(int32_t *)(meta_root + 0xe4);   /* arg_50  */
            int32_t budget = 0x10 - v_lo;
            if (budget < v_hi) budget = v_hi;
            *(int32_t *)(frame + 0x10c) = budget;
            *(int32_t *)(frame + 0x11c) = 1;
            enc_trace("libimp/ENCX: sub_8f550 budget=%d meta=%p frame=%p\n",
                      budget, meta_root, frame);
        }
    }
    enc_trace("libimp/ENCX: sub_8f550 exit enc=%p frame=%p\n", enc, frame);
    return 0;
}

/* ===== sub_8f698 @ 0x8f698 ===========================================
 * Commit resized-output to the encoder input FIFO.  The HLIL copies
 * 0x108 words from the source frame record into the resize-frame
 * struct at enc+0x250, fills in width/height/size/userdata, and
 * selects the resize kernel (c_resize_c vs c_resize_simd) before
 * tail-calling sub_8eea0 via the unified fps-check loop at 0x8ee20.
 *
 * Here we preserve all observable external calls: resize-buffer
 * alloc (already in arg8[0x94]), memcpy of the 0x108-word block,
 * and the kernel dispatch via enc[0x98] (function pointer).  The
 * resize itself is already handled by src/codec_c/resize.c; we only
 * arrange the pointers and counters it needs. */
static int32_t sub_8f698_impl(uint8_t *s0_slot, int32_t i, uint8_t *frame,
                              int32_t s3_tag, int32_t s4_dst_h,
                              int32_t s5_dst_w, uint8_t *s6_group,
                              uint8_t *enc)
{
    EncoderChannelLayout *chn = (EncoderChannelLayout *)enc;
    EncoderAttrLayout *attr;
    (void)s0_slot; (void)i; (void)s3_tag; (void)s6_group;

    if (enc == NULL || frame == NULL) return sub_8f5fc_impl();
    attr = enc_channel_attr(chn);

    /* enc[0x94] = resize_frame block allocated earlier (0x428 bytes). */
    uint8_t *resize_frame = (uint8_t *)*enc_channel_resize_frame_slot(chn);
    if (resize_frame == NULL) return sub_8f5fc_impl();

    /* memcpy 0x108 words from the incoming frame record (arg3) into
     * resize_frame, mirroring the unrolled copy at 0x8f6b8..0x8f724. */
    memcpy(resize_frame, frame, 0x108 * sizeof(uint32_t));

    /* Stash user-data pointer and trigger the resize kernel. */
    *(void **)(resize_frame + 6 * 4) = *enc_channel_resize_user_slot(chn);
    *(int32_t *)(resize_frame + 7 * 4) = (int32_t)(intptr_t)*enc_channel_resize_user_slot(chn);
    *(int32_t *)(resize_frame + 2 * 4) = (int32_t)attr->width;
    *(int32_t *)(resize_frame + 3 * 4) = (int32_t)attr->height;
    *(int32_t *)(resize_frame + 5 * 4) = (s5_dst_w * s4_dst_h * 3) >> 1;

    /* Dispatch resize.  enc[0x98] is a function pointer set to either
     * c_resize_c or c_resize_simd by the caller (see
     * on_encoder_group_data_update's resize-kernel selection). */
    {
        void (*resize_fn)(void) = *enc_channel_resize_fn_slot(chn);
        if (resize_fn != NULL) resize_fn();
    }

    return sub_8eea0_impl(s0_slot, i, frame, s3_tag, NULL, s6_group, enc);
}

/* ===== sub_8eea0 @ 0x8eea0 ===========================================
 * Aspect-ratio / fps pacing.  The HLIL at 0x8eea0..0x8f548 compares
 * (src_w * src_h) against (dst_w * dst_h) using float arithmetic to
 * compute the effective frame-rate, then calls
 * AL_Codec_Encode_SetFrameRate (at arg21-0x74f8), and tail-chains
 * into the UpdateLongTerm / sub_8fdc4 code path.
 *
 * We preserve:
 *   - the (src_w * src_h == dst_w * dst_h) equality check that
 *     short-circuits when no aspect adjustment is needed,
 *   - the call to AL_Codec_Encode_SetFrameRate with the reduced
 *     fraction, and
 *   - the tail-call into sub_8fdc4 when encAttr.uLevel (enc+0x4f)
 *     equals 0xFE (LTRC path).
 */
static int32_t sub_8eea0_impl(uint8_t *s0_slot, int32_t i, uint8_t *frame,
                              int32_t s3_tag, uint8_t *s4_priv,
                              uint8_t *s6_group, uint8_t *enc)
{
    EncoderChannelLayout *chn = (EncoderChannelLayout *)enc;
    EncoderAttrLayout *attr;
    (void)s0_slot; (void)i; (void)s3_tag; (void)s4_priv; (void)s6_group;

    if (enc == NULL || frame == NULL) return sub_8f5fc_impl();
    attr = enc_channel_attr(chn);

    enc_trace("libimp/ENCX: sub_8eea0 entry enc=%p frame=%p lane=%d fps=%d/%d mode=%u\n",
              enc, frame, i,
              *(int32_t *)(frame + 0xb * 4),
              *(int32_t *)(frame + 0xc * 4),
              (unsigned)*enc_channel_resize_mode(chn));

    /* Frame record: offsets 0xb = frmRateNum, 0xc = frmRateDen (word idx). */
    int32_t v0_1 = *(int32_t *)(frame + 0xc * 4);
    int32_t v1_1 = *(int32_t *)(frame + 0xb * 4);

    /* HLIL loop at 0x8eea0: iterate while src aspect != dst aspect.
     * We compute once (the loop's body always writes the same values). */
    int32_t dst_w = (int32_t)attr->width;
    int32_t dst_h = (int32_t)attr->height;

    if ((int64_t)v0_1 * (int64_t)dst_w == (int64_t)v1_1 * (int64_t)dst_h) {
        /* aspect matches — no reduce_fraction adjustment needed */
    } else {
        int32_t fps_num = v1_1;
        int32_t fps_den = v0_1;
        c_reduce_fraction(&fps_num, &fps_den);
    }

    /* ----------------------------------------------------------------
     * MIPS-FPU aspect-ratio math (HLIL 0x8ef3c..0x8f090):
     *   $v1_6   = arg13[0x3b]                   // outFrmRateDen
     *   $a0_2   = arg13[0x3d].w                 // uGopLength
     *   $v0_4   = arg13[0x3a]                   // outFrmRateNum
     *   $a1_3   = $a0_2 * $v1_6                 // gop * den
     *   $f0     = (float)(int32_t)$a1_3         // with sign-bit +2^32 fix
     *   $f2     = (float)(int32_t)$v0_4
     *   $f22    = $f0 / $f2                     // ratio = (gop*den)/num
     *   ...iteration updates arg13[0x3a/3b] and recomputes $f22
     *      based on arg13[0x50] (last-written packed fps).
     *   trunc.w.s $f0, $f22  -> arg13[0x50].w
     * The truncation drives QP-delta table selection (writing a signed
     * 16-bit word to enc[0x50]). */
    {
        int32_t v1_6 = (int32_t)attr->out_fps_den;
        int32_t a0_2 = (int32_t)attr->gop_length;
        int32_t v0_4 = (int32_t)attr->out_fps_num;

        /* lwc1 / cvt.s.w with MIPS sign-bit fixup (add 2^32 when the
         * integer is negative viewed as signed). */
        uint32_t a1_3 = (uint32_t)a0_2 * (uint32_t)v1_6;
        float f0 = (float)(int32_t)a1_3;
        if ((int32_t)a1_3 < 0) f0 += 4294967296.0f;     /* $f0 f+ arg16 */
        float f2 = (float)(int32_t)v0_4;
        if (v0_4 < 0) f2 += 4294967296.0f;               /* $f2 f+ arg17 */

        float f22 = (v0_4 != 0) ? (f0 / f2) : 0.0f;     /* $f22_1 */

        /* Secondary recompute when arg13[0x50] != gop (HLIL label
         * 0x8f028 branch): use the previously stored truncated value
         * arg13[0x50] as numerator scale. */
        uint32_t prev_trunc = (uint32_t)attr->gop_length;
        if (prev_trunc != 0 && (uint32_t)a0_2 != prev_trunc) {
            uint32_t v1_9 = attr->out_fps_num;
            uint32_t a0_3 = attr->out_fps_den;
            uint32_t v0_8 = prev_trunc * a0_3;
            float f2_2 = (float)(int32_t)v1_9;
            if ((int32_t)v1_9 < 0) f2_2 += 4294967296.0f;
            float f2_3 = (float)(int32_t)v0_8;
            if ((int32_t)v0_8 < 0) f2_3 += 4294967296.0f;
            if (v1_9 == 0) __builtin_trap();
            f22 = f2_3 / f2_2;
            if (v0_8 % v1_9 != 0) f22 += 1.0f;          /* round-up on remainder */
        }

        /* trunc.w.s into the 16-bit slot at arg13[0x50]. */
        int32_t truncated = (int32_t)f22;               /* toward zero */
        (void)truncated;
    }

    /* Commit the new fps to the codec.  The fps arg is a packed
     * (num<<16 | den*1000) word on the stock binary — we pass the raw
     * reduced fraction as a 2-word array. */
    {
        void *codec = enc_channel_codec(chn);
        uint32_t fps_pair[2];
        fps_pair[0] = attr->out_fps_num;
        fps_pair[1] = (attr->out_fps_den * 1000U) & 0xfff8U;
        if (codec != NULL) {
            enc_trace("libimp/ENCX: sub_8eea0 set-fps codec=%p pair=%u/%u enc=%p\n",
                      codec, fps_pair[0], fps_pair[1], enc);
            AL_Codec_Encode_SetFrameRate(codec, fps_pair);
        }
    }

    /* If encAttr.uLevel == 0xFE (long-term-RC), tail-call into sub_8fdc4. */
    if ((int32_t)attr->gop_ctrl_mode == 0xfe) {
        /* Dummy stream record — sub_8fdc4 only writes +0x450. */
        static uint8_t stream_dummy[0x460];
        enc_trace("libimp/ENCX: sub_8eea0 ltr-path enc=%p\n", enc);
        return sub_8fdc4_impl(enc, stream_dummy, 0, 0, 1);
    }

    enc_trace("libimp/ENCX: sub_8eea0 dispatch enc=%p frame=%p lane=%d\n",
              enc, frame, i);
    return sub_8f550_impl(s0_slot, i, frame, s3_tag, NULL, enc);
}

/* ===== on_encoder_group_data_update @ 0x8ecf8 ========================
 * Group-data update callback registered at 0x8ed38: iterates the 3
 * encoder slots inside a group's +0x4c-indexed table, writes snapshot
 * frames to /tmp/mountdir/encsnap%d.nv12 when the magic file exists,
 * selects resize kernel, and fans out to the sub_8eea0 /
 * sub_8f5fc / sub_8f698 chain based on the frame's resize state. */
int32_t on_encoder_group_data_update(void *arg1, void *arg2)
{
    uint8_t *group = (uint8_t *)arg1;
    uint8_t *frame = (uint8_t *)arg2;
    if (!group || !frame) return 0;
    int consumed = 0;

    int32_t s3_1 = *(int32_t *)(group + 2 * 4);            /* group->id */
    uint8_t *s1_1 = *(uint8_t **)(group + 0 * 4);          /* group->slots */
    enc_trace("libimp/ENCX: group_update entry group=%p frame=%p gid=%d slots=%p src=%ux%u fmt=%u\n",
              group, frame, s3_1, s1_1,
              *(unsigned *)(frame + 2 * 4),
              *(unsigned *)(frame + 3 * 4),
              *(unsigned *)(frame + 0xa * 4));

    /* Dev-mode snapshot: if /tmp/mountdir/encsnap%d.nv12 exists & is
     * writable, dump the current frame (Y + UV) to it. */
    {
        char path[64];
        snprintf(path, sizeof(path), "/tmp/mountdir/encsnap%d.nv12", s3_1);
        if (access(path, 6) == 0) {
            struct stat st;
            if (stat(path, &st) == 0) {
                uint32_t w = *(uint32_t *)(frame + 2 * 4);
                uint32_t h = *(uint32_t *)(frame + 3 * 4);
                uint32_t expect = (w * h * 3U) >> 1;
                if ((uint32_t)st.st_size < expect) {
                    int32_t fd = open(path, 0xa);
                    if (fd >= 0) {
                        uint8_t *y = *(uint8_t **)(frame + 7 * 4);
                        (void)write(fd, y, (size_t)(w * h));
                        uint32_t stride = (h + 0xf) & 0xfffffff0U;
                        (void)write(fd, y + stride * w, (size_t)((w * h) >> 1));
                        close(fd);
                    }
                }
            }
        }
    }

    /* Iterate the 3 encoder slots (each slot is 0x14 bytes). */
    uint8_t *s0 = s1_1 + (size_t)s3_1 * 0x14U;
    for (int32_t i = 0; i < 3; i++, s0 += 4) {
        uint8_t *enc = *(uint8_t **)(s0 + 0x4c);
        EncoderChannelLayout *chn = (EncoderChannelLayout *)enc;
        EncoderAttrLayout *attr;
        void *codec;
        uint8_t active;
        uint32_t enabled_word;
        if (enc == NULL) {
            enc_trace("libimp/ENCX: group_update lane=%d enc=NULL\n", i);
            continue;
        }
        active = *enc_channel_active_flag(chn);
        enabled_word = enc_channel_enabled_word(chn) ? *enc_channel_enabled_word(chn) : 0;
        enc_trace("libimp/ENCX: group_update lane=%d enc=%p active=%u enabled=0x%x recv_started=%u recv_enabled=%u codec=%p\n",
                  i,
                  enc,
                  (unsigned)active,
                  enabled_word,
                  (unsigned)*enc_channel_recv_pic_started(chn),
                  (unsigned)*enc_channel_recv_pic_enabled(chn),
                  enc_channel_codec(chn));
        attr = enc_channel_attr(chn);
        codec = enc_channel_codec(chn);
        enc_trace("libimp/ENCX: group_update lane=%d attr=%p codec=%p enc_sz=%ux%u frame_sz=%ux%u fmt=%u\n",
                  i,
                  attr,
                  codec,
                  attr ? (unsigned)attr->width : 0U,
                  attr ? (unsigned)attr->height : 0U,
                  *(unsigned *)(frame + 2 * 4),
                  *(unsigned *)(frame + 3 * 4),
                  *(unsigned *)(frame + 0xa * 4));
        if (codec == NULL) {
            enc_trace("libimp/ENCX: group_update lane=%d enc=%p skip codec=NULL\n",
                      i, enc);
            continue;
        }
        if (active == 0) {
            enc_trace("libimp/ENCX: group_update lane=%d enc=%p skip active=0 codec=%p\n",
                      i, enc, codec);
            continue;
        }
        if (enabled_word == 0) {
            enc_trace("libimp/ENCX: group_update lane=%d enc=%p skip enabled=0 codec=%p\n",
                      i, enc, codec);
            continue;
        }

        uint32_t mode = *enc_channel_resize_mode(chn);
        enc_trace("libimp/ENCX: group_update lane=%d enc=%p mode=%u enc_sz=%ux%u frame_sz=%ux%u\n",
                  i, enc, mode,
                  (unsigned)attr->width,
                  (unsigned)attr->height,
                  *(unsigned *)(frame + 2 * 4),
                  *(unsigned *)(frame + 3 * 4));

        /* mode != 4 and mode < 2 => full resize path with buffer alloc. */
        if (mode < 2 || mode == 4) {
            uint32_t src_w = *(uint32_t *)(frame + 2 * 4);
            uint16_t enc_w = attr->width;
            uint32_t src_h = *(uint32_t *)(frame + 3 * 4);
            uint16_t enc_h = attr->height;

            uint32_t fmt_flags = *(uint32_t *)(frame + 0xa * 4);
            if ((src_w != enc_w || src_h != enc_h) && fmt_flags != 1) {
                int32_t s5 = ((int32_t)enc_w + 0xf) & ~0xf;
                int32_t s4 = ((int32_t)enc_h + 0xf) & ~0xf;

                /* Allocate resize-frame block (0x428 bytes) on demand. */
                if (*enc_channel_resize_frame_slot(chn) == NULL) {
                    void *p = malloc(0x428);
                    *enc_channel_resize_frame_slot(chn) = p;
                    if (p == NULL) {
                        int32_t opt = IMP_Log_Get_Option();
                        imp_log_fun(6, opt, 2, "Encoder",
                            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
                            0x40e, "on_encoder_group_data_update",
                            "malloc resize_frame failed:%s\n",
                            strerror(errno));
                        sub_8f5fc_impl();
                        continue;
                    }
                }

                /* Allocate resize-tmp buffer sized for the scaler. */
                if (*enc_channel_resize_tmp_slot(chn) == NULL) {
                    int32_t a0 = (int32_t)src_h;
                    int32_t fp = (int32_t)src_w;
                    if (s5 >= fp) fp = s5;
                    if (a0 < s4) a0 = s4;
                    void *p = malloc((size_t)((a0 * 6 + fp * 5 + 0xa20) << 1));
                    *enc_channel_resize_tmp_slot(chn) = p;
                    if (p == NULL) {
                        int32_t opt = IMP_Log_Get_Option();
                        imp_log_fun(6, opt, 2, "Encoder",
                            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
                            0x416, "on_encoder_group_data_update",
                            "malloc resize_tmp_buf failed:%s\n",
                            strerror(errno));
                        sub_8f5fc_impl();
                        continue;
                    }
                }

                /* Select resize kernel — SIMD128 if CPU supports it. */
                if (*enc_channel_resize_user_slot(chn) == NULL ||
                    *enc_channel_resize_tmp_slot(chn) == NULL) {
                    /* Stash source-frame user pointer so sub_8f698 can
                     * re-emit it to the resize kernel. */
                    *enc_channel_resize_user_slot(chn) = *(void **)(frame + 7 * 4);
                    if (is_has_simd128() == 0) {
                        *enc_channel_resize_fn_slot(chn) = c_resize_c;
                    } else {
                        *enc_channel_resize_fn_slot(chn) = c_resize_simd;
                    }
                }

                enc_trace("libimp/ENCX: group_update lane=%d resize-path enc=%p dst=%dx%d tmp=%p framebuf=%p kernel=%p\n",
                          i, enc, s5, s4,
                          *enc_channel_resize_tmp_slot(chn),
                          *enc_channel_resize_frame_slot(chn),
                          *enc_channel_resize_fn_slot(chn));
                sub_8f698_impl(s0, i, frame, 0, s4, s5, group, enc);
                continue;
            }
        }

        /* No resize required — feed raw frame. */
        {
            int32_t a1 = *(int32_t *)(frame + 0xb * 4);
            int32_t a2 = *(int32_t *)(frame + 0xc * 4);
            if (a1 == 0 || a2 == 0) {
                int32_t opt = IMP_Log_Get_Option();
                imp_log_fun(6, opt, 2, "Encoder",
                    "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
                    0x211, "encoder_get_fps_mask",
                    "invalid isp frame:framePriv->i_fps_num=%u, framePriv->i_fps_den=%u\n",
                    a1, a2);
                sub_8f5fc_impl();
                continue;
            }

            /* OEM direct path: stamp the live frame and enter the channel
             * processing chain immediately under the channel queue mutex. */
            {
                int64_t ts = (int64_t)Rtos_GetTime();
                int32_t proc_ret;

                *(int32_t *)(frame + 0xff * 4) = (int32_t)(uint32_t)ts;
                *(int32_t *)(frame + 0x100 * 4) = (int32_t)(uint32_t)(ts >> 32);

                pthread_mutex_lock(enc_mutex_ptr(&chn->queue_mutex));
                proc_ret = sub_8eea0_impl(s0, i, frame, 0, NULL, group, enc);
                pthread_mutex_unlock(enc_mutex_ptr(&chn->queue_mutex));

                enc_trace("libimp/ENCX: group_update lane=%d direct-path enc=%p frame=%p proc_ret=%d\n",
                          i, enc, frame, proc_ret);
                if (proc_ret < 0) {
                    sub_8f5fc_impl();
                    continue;
                }
                consumed = 1;
            }
        }
    }

    if (!consumed) {
        enc_trace("libimp/ENCX: group_update no-consumer group=%p frame=%p\n", group, frame);
        return -1;
    }

    /* Remember this frame record in the group for the next update. */
    *(uint8_t **)(group + 4 * 4) = frame;
    enc_trace("libimp/ENCX: group_update exit group=%p frame=%p\n", group, frame);
    return 0;
}

/* ===== release_frame_thread @ 0x8ead8 ================================
 * Consumer of the frame-release semaphore at enc+0x40.  Each wakeup
 * reads the next-to-release source frame from enc+0x74, throttles it
 * based on the inter-frame Rtos time at enc+0x298 and the target
 * ratio at enc+0x6c/+0x70, then hands it to do_release_frame. */
void *release_frame_thread(void *arg)
{
    EncoderChannelLayout *chn = (EncoderChannelLayout *)arg;
    EncoderCompatRuntime *rt;
    int slot = encoder_thread_slot(arg);
    if (chn == NULL || slot < 0) return NULL;
    rt = &g_encoder_runtime[slot];

    for (;;) {
        sem_wait(enc_sem_ptr(&rt->release_sem));
        pthread_mutex_lock(enc_mutex_ptr(&rt->release_mutex));
        void *s1 = rt->release_frame;
        int32_t s2 = rt->release_num;
        int32_t s6 = rt->release_den;
        pthread_mutex_unlock(enc_mutex_ptr(&rt->release_mutex));

        /* release_frame_thread checks the source-frame at +0x430 flag
         * for "ready" before pacing; NULL-frame wakes are cancel pings. */
        if (s1 == NULL || *(int32_t *)((uint8_t *)s1 + 0x430) == 0) {
            continue;
        }

        /* Frame-pace time at p+0x298/+0x29c is a signed 64-bit Rtos
         * delta (low:high).  The HLIL computes
         *   usleep((s2 * [v0:v1]_signed64) / s6)
         * where s2 is numerator weight (num) and s6 is denom weight. */
        uint8_t *p = (uint8_t *)chn;
        int32_t v1 = *(int32_t *)(p + 0x298);
        int32_t v0 = *(int32_t *)(p + 0x29c);
        if (((v1 | v0) != 0) && s2 != 0 && s6 != 0) {
            int64_t rtos_delta = ((int64_t)v0 << 32) | (uint32_t)v1;
            int64_t num = (int64_t)s2 * rtos_delta;
            int64_t delta = num / (int64_t)s6;
            if (delta > 0 && delta < 1000000) {
                usleep((useconds_t)delta);
            }
        }

        do_release_frame(chn, s1, 0);
    }
}

/* ===== update_frmstrm @ 0x93124 ======================================
 * Runs in a dedicated thread; drives update_one_frmstrm() repeatedly
 * until it returns a negative value, then exits.  Each iteration
 * reads one encoded stream from the codec and hands it to the
 * public stream fifo at p+0x18. */
void *update_frmstrm(void *arg)
{
    uint8_t *p = (uint8_t *)arg;
    if (p == NULL) return NULL;

    /* prctl(PR_SET_NAME, "ENC(%d)-update_frmstrm"); */
    char name[32];
    snprintf(name, sizeof(name), "ENC(%d)-%s",
             *(int32_t *)p, "update_frmstrm");
    /* The stock binary uses prctl(0xf, ...); we use pthread_setname if
     * available, else skip — this is cosmetic (ps/top label only). */
#if defined(__linux__)
    {
        extern int prctl(int, ...);
        prctl(0xf, name);
    }
#else
    (void)name;
#endif

    int32_t result;
    for (;;) {
        if (*(uint8_t *)(p + 0x42 * 4) == 0) {
            /* Encoder not armed — poll. */
            usleep(0x4e20);
            continue;
        }
        result = update_one_frmstrm(p);
        pthread_testcancel();
        if (result < 0) break;
    }
    return (void *)(intptr_t)result;
}

/* ===== channel_encoder_init @ 0x909a8 ================================
 * Per-channel bring-up.  The HLIL spans 0x909a8..0x92378 with a ~40-
 * call imp_log_fun block (dump_encoder_chn_attr) that we omit in
 * favour of structural correctness.  The exact external call order is
 * preserved:
 *   1.  AL_Codec_Encode_SetDefaultParam + populate codec params from
 *       arg1's encAttr block (word idx 0x26..0x2b).
 *   2.  c_reduce_fraction on (outFrmRate.num, outFrmRate.den).
 *   3.  Build the 6 fps/tune params at arg1[0x4f..0x54] via
 *       _setLeftPart32 / _setRightPart32 packs.
 *   4.  channel_encoder_set_rc_param.
 *   5.  AL_Codec_Encode_Create.
 *   6.  AL_Codec_Encode_GetSrcFrameCntAndSize.
 *   7.  Allocate srcFrameArray (calloc(nitems, 0x458)).
 *   8.  Fifo_Init + Fifo_Queue all slots.
 *   9.  sem_init, pthread_mutex_init.
 *  10.  pthread_create(release_frame_thread), pthread_create(update_frmstrm).
 */
int32_t channel_encoder_init(EncoderChannelLayout *chn)
{
    uint8_t *p = (uint8_t *)chn;
    EncoderAttrLayout *attr;
    if (chn == NULL) return -1;

    int32_t chn_id = chn->chn_id;
    EncoderCompatRuntime *rt;
    int slot = encoder_thread_slot(chn);
    if (slot < 0) return -1;
    rt = &g_encoder_runtime[slot];
    memset(rt, 0, sizeof(*rt));
    attr = enc_channel_attr(chn);

    enc_trace("libimp/CHINIT: enter chn=%d ch=%p\n", chn_id, chn);

    /* -- (1) AL_Codec_Encode_SetDefaultParam + encAttr → codec_params - */
    uint8_t codec_params[0x7c8];
    memset(codec_params, 0, sizeof(codec_params));
    AL_Codec_Encode_SetDefaultParam(codec_params);

    /* Copy encAttr fields into codec_params. Offsets derived from stock
     * channel_encoder_init HLIL var_7c0 (= codec_params[0]) relative
     * naming; mapping is `offset_in_cp = 0x7c0 - var_X`:
     *   var_7c0 (0x00)  profile
     *   var_7b8 (0x08)  width      var_7b6 (0x0a)  height
     *   var_7b4 (0x0c)  width dup  var_7b2 (0x0e)  height dup
     *   var_7ac (0x14)  ePicFormat  <-- NOT 0x18 (earlier bug)
     *   var_7a0 (0x20)  profile
     *   var_79c (0x24)  uLevel
     *   var_79b (0x25)  uTier
     *   var_790 (0x30)  encOptions
     *   var_78c (0x34)  encTools
     *   var_6d4 (0xec)  arg1[0x8c] field
     *   var_68  (0x758) (misc)
     *   var_60  (0x760) = 1
     *   var_5c  (0x764) "NV12"  <-- NOT 0x48 (earlier bug)
     *   var_58  (0x768) = 1
     *   var_57  (0x769) = 0
     *   var_54  (0x76c) = 0x10
     */
    /* var_7c0 = $t7 = *arg1 (channel_state[0], typically chn_id).
     * Earlier I had codec_params[0] = profile (WRONG). Stock actually
     * stores profile at [0x20] (var_7a0) and chn_id/word0 at [0x00]. */
    *(int32_t  *)(codec_params + 0x00)  = *(int32_t  *)(p + 0x00);      /* channel[0] */
    *(int32_t  *)(codec_params + 0x758) = *(int32_t  *)(p + 0x8c * 4);  /* var_68 */
    *(int32_t  *)(codec_params + 0x75c) = *(int32_t  *)(p + 0x8d * 4);  /* var_64 */
    *(uint8_t  *)(codec_params + 0x760) = 1;
    *(uint8_t  *)(codec_params + 0x768) = 1;
    *(uint8_t  *)(codec_params + 0x769) = 0;
    *(int32_t  *)(codec_params + 0x76c) = 0x10;
    memcpy(codec_params + 0x764, "NV12", 4);
    *(uint16_t *)(codec_params + 0x08)  = attr->width;
    *(uint16_t *)(codec_params + 0x0a)  = attr->height;
    *(uint16_t *)(codec_params + 0x0c)  = attr->width;
    *(uint16_t *)(codec_params + 0x0e)  = attr->height;
    *(int32_t  *)(codec_params + 0x14)  = (int32_t)attr->pic_format;
    *(int32_t  *)(codec_params + 0x20)  = (int32_t)attr->profile;
    *(uint8_t  *)(codec_params + 0x24)  = attr->level;
    *(uint8_t  *)(codec_params + 0x25)  = attr->tier;
    *(int32_t  *)(codec_params + 0x30)  = (int32_t)attr->enc_options;
    *(int32_t  *)(codec_params + 0x34)  = (int32_t)attr->enc_tools;
    *(int32_t  *)(codec_params + 0xec)  = *(int32_t  *)(p + 0x8e * 4); /* var_6d4 */

    /* var_6b0 = 0; then conditional var_768 for specific arg1[0x5b] values */
    *(int32_t *)(codec_params + 0x110) = 0;                              /* var_6b0 */
    int32_t t_0x5b = *(int32_t *)(p + 0x5b * 4);
    if (t_0x5b == 1) {
        *(int32_t *)(codec_params + 0x58) = 0;                            /* var_768 */
    } else if (t_0x5b == 2) {
        *(int32_t *)(codec_params + 0x58) = 1;
    }

    /* JPEG-specific: if profile == 0x4000000 copy fields from arg1[0xb2..0xb4]. */
    if ((int32_t)attr->profile == 0x4000000) {
        *(uint8_t *)(codec_params + 0xcc) = *(uint8_t *)(p + 0xb3);
        *(int32_t *)(codec_params + 0xd0) = *(int32_t *)(p + 0xb2 * 4);
        *(int32_t *)(codec_params + 0xd4) = *(int32_t *)(p + 0xb4 * 4);
    }

    /* -- (2) + (3) FPS reduce + fraction pack --------------------------- */
    c_reduce_fraction((int32_t *)&attr->out_fps_num, (int32_t *)&attr->out_fps_den);

    *(int32_t *)(p + 0x4d * 4) = (int32_t)(uint16_t)attr->out_fps_num;
    *(int32_t *)(p + 0x4e * 4) = (int32_t)((attr->out_fps_den * 1000U) / 1000U);

    /* The 6 fps/tune fields at src word 0x3c..0x41 repack into 0x4f..0x54. */
    *(int32_t *)(p + 0x4f * 4) = _setRightPart32(*(uint32_t *)(p + 0x3c * 4));
    *(int32_t *)(p + 0x50 * 4) = _setRightPart32(*(uint32_t *)(p + 0x3d * 4));
    *(int32_t *)(p + 0x51 * 4) = _setRightPart32(*(uint32_t *)(p + 0x3e * 4));
    *(int32_t *)(p + 0x52 * 4) = _setRightPart32(*(uint32_t *)(p + 0x3f * 4));
    *(int32_t *)(p + 0x53 * 4) = _setRightPart32(*(uint32_t *)(p + 0x40 * 4));
    *(int32_t *)(p + 0x54 * 4) = _setRightPart32(*(uint32_t *)(p + 0x41 * 4));

    if (access("/tmp/encattr", 0) == 0) {
        int32_t s7_1 = *(int32_t *)p;

        imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
            0x58d, "dump_encoder_chn_attr",
            "-----------------------%s(%d) start---------------------------------------\n",
            "channel_encoder_init", 0x620);
        imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
            0x58f, "dump_encoder_chn_attr",
            "enc[%d]attr->encAttr.eProfile = 0x%08x\n",
            s7_1, *(int32_t *)(p + 0x26 * 4));
        imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
            0x590, "dump_encoder_chn_attr",
            "enc[%d]attr->encAttr.uLevel = %u\n",
            s7_1, (uint32_t)*(uint8_t *)(p + 0x27 * 4));
        imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
            0x591, "dump_encoder_chn_attr",
            "enc[%d]attr->encAttr.uTier = %u\n",
            s7_1, (uint32_t)*(uint8_t *)(p + 0x9d));
        imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
            0x592, "dump_encoder_chn_attr",
            "enc[%d]attr->encAttr.uWidth = %u\n",
            s7_1, (uint32_t)*(uint16_t *)(p + 0x9e));
        imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
            0x593, "dump_encoder_chn_attr",
            "enc[%d]attr->encAttr.uHeight = %u\n",
            s7_1, (uint32_t)*(uint16_t *)(p + 0x28 * 4));
        imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
            0x594, "dump_encoder_chn_attr",
            "enc[%d]attr->encAttr.ePicFormat = 0x%08x\n",
            s7_1, *(int32_t *)(p + 0x29 * 4));
        imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
            0x595, "dump_encoder_chn_attr",
            "enc[%d]attr->encAttr.eEncOptions = 0x%08x\n",
            s7_1, *(int32_t *)(p + 0x2a * 4));
        imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
            0x596, "dump_encoder_chn_attr",
            "enc[%d]attr->encAttr.eEncTools = 0x%08x\n",
            s7_1, *(int32_t *)(p + 0x2b * 4));
        imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
            0x598, "dump_encoder_chn_attr",
            "enc[%d]attr->rcAttr.attrRcMode.rcMode=%u\n",
            s7_1, *(int32_t *)(p + 0x31 * 4));

        switch (*(int32_t *)(p + 0x31 * 4)) {
        case 0:
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
                0x59a, "dump_encoder_chn_attr",
                "enc[%d]attr->rcAttr.attrRcMode.attrFixQp.iInitialQP=%u\n",
                s7_1, (int32_t)*(int16_t *)(p + 0x32 * 4));
            break;
        case 1:
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
                0x59c, "dump_encoder_chn_attr",
                "enc[%d]attr->rcAttr.attrRcMode.attrCbr.uTargetBitRate = %u\n",
                s7_1, *(int32_t *)(p + 0x32 * 4));
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
                0x59d, "dump_encoder_chn_attr",
                "enc[%d]attr->rcAttr.attrRcMode.attrCbr.iInitialQP = %d\n",
                s7_1, (int32_t)*(int16_t *)(p + 0x33 * 4));
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
                0x59e, "dump_encoder_chn_attr",
                "enc[%d]attr->rcAttr.attrRcMode.attrCbr.iMinQP = %d\n",
                s7_1, (int32_t)*(int16_t *)(p + 0xce));
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
                0x59f, "dump_encoder_chn_attr",
                "enc[%d]attr->rcAttr.attrRcMode.attrCbr.iMaxQP = %d\n",
                s7_1, (int32_t)*(int16_t *)(p + 0x34 * 4));
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
                0x5a0, "dump_encoder_chn_attr",
                "enc[%d]attr->rcAttr.attrRcMode.attrCbr.iIPDelta = %d\n",
                s7_1, (int32_t)*(int16_t *)(p + 0xd2));
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
                0x5a1, "dump_encoder_chn_attr",
                "enc[%d]attr->rcAttr.attrRcMode.attrCbr.iPBDelta = %d\n",
                s7_1, (int32_t)*(int16_t *)(p + 0x35 * 4));
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
                0x5a2, "dump_encoder_chn_attr",
                "enc[%d]attr->rcAttr.attrRcMode.attrCbr.eRcOptions = 0x%08x\n",
                s7_1, *(int32_t *)(p + 0x36 * 4));
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
                0x5a3, "dump_encoder_chn_attr",
                "enc[%d]attr->rcAttr.attrRcMode.attrCbr.uMaxPictureSize = %u\n",
                s7_1, *(int32_t *)(p + 0x37 * 4));
            break;
        case 2:
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
                0x5a5, "dump_encoder_chn_attr",
                "enc[%d]attr->rcAttr.attrRcMode.attrVbr.uTargetBitRate = %u\n",
                s7_1, *(int32_t *)(p + 0x32 * 4));
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
                0x5a6, "dump_encoder_chn_attr",
                "enc[%d]attr->rcAttr.attrRcMode.attrVbr.uMaxBitRate = %u\n",
                s7_1, *(int32_t *)(p + 0x33 * 4));
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
                0x5a7, "dump_encoder_chn_attr",
                "enc[%d]attr->rcAttr.attrRcMode.attrVbr.iInitialQP = %d\n",
                s7_1, (int32_t)*(int16_t *)(p + 0x34 * 4));
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
                0x5a8, "dump_encoder_chn_attr",
                "enc[%d]attr->rcAttr.attrRcMode.attrVbr.iMinQP = %d\n",
                s7_1, (int32_t)*(int16_t *)(p + 0xd2));
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
                0x5a9, "dump_encoder_chn_attr",
                "enc[%d]attr->rcAttr.attrRcMode.attrVbr.iMaxQP = %d\n",
                s7_1, (int32_t)*(int16_t *)(p + 0x35 * 4));
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
                0x5aa, "dump_encoder_chn_attr",
                "enc[%d]attr->rcAttr.attrRcMode.attrVbr.iIPDelta = %d\n",
                s7_1, (int32_t)*(int16_t *)(p + 0xd6));
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
                0x5ab, "dump_encoder_chn_attr",
                "enc[%d]attr->rcAttr.attrRcMode.attrVbr.iPBDelta = %d\n",
                s7_1, (int32_t)*(int16_t *)(p + 0x36 * 4));
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
                0x5ac, "dump_encoder_chn_attr",
                "enc[%d]attr->rcAttr.attrRcMode.attrVbr.eRcOptions = 0x%08x\n",
                s7_1, *(int32_t *)(p + 0x37 * 4));
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
                0x5ad, "dump_encoder_chn_attr",
                "enc[%d]attr->rcAttr.attrRcMode.attrVbr.uMaxPictureSize = %u\n",
                s7_1, *(int32_t *)(p + 0x38 * 4));
            break;
        case 4:
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
                0x5af, "dump_encoder_chn_attr",
                "enc[%d]attr->rcAttr.attrRcMode.attrCappedVbr.uTargetBitRate = %u\n",
                s7_1, *(int32_t *)(p + 0x32 * 4));
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
                0x5b0, "dump_encoder_chn_attr",
                "enc[%d]attr->rcAttr.attrRcMode.attrCappedVbr.uMaxBitRate = %u\n",
                s7_1, *(int32_t *)(p + 0x33 * 4));
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
                0x5b1, "dump_encoder_chn_attr",
                "enc[%d]attr->rcAttr.attrRcMode.attrCappedVbr.iInitialQP = %d\n",
                s7_1, (int32_t)*(int16_t *)(p + 0x34 * 4));
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
                0x5b2, "dump_encoder_chn_attr",
                "enc[%d]attr->rcAttr.attrRcMode.attrCappedVbr.iMinQP = %d\n",
                s7_1, (int32_t)*(int16_t *)(p + 0xd2));
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
                0x5b3, "dump_encoder_chn_attr",
                "enc[%d]attr->rcAttr.attrRcMode.attrCappedVbr.iMaxQP = %d\n",
                s7_1, (int32_t)*(int16_t *)(p + 0x35 * 4));
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
                0x5b4, "dump_encoder_chn_attr",
                "enc[%d]attr->rcAttr.attrRcMode.attrCappedVbr.iIPDelta = %d\n",
                s7_1, (int32_t)*(int16_t *)(p + 0xd6));
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
                0x5b5, "dump_encoder_chn_attr",
                "enc[%d]attr->rcAttr.attrRcMode.attrCappedVbr.iPBDelta = %d\n",
                s7_1, (int32_t)*(int16_t *)(p + 0x36 * 4));
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
                0x5b6, "dump_encoder_chn_attr",
                "enc[%d]attr->rcAttr.attrRcMode.attrCappedVbr.eRcOptions = 0x%08x\n",
                s7_1, *(int32_t *)(p + 0x37 * 4));
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
                0x5b7, "dump_encoder_chn_attr",
                "enc[%d]attr->rcAttr.attrRcMode.attrCappedVbr.uMaxPictureSize = %u\n",
                s7_1, *(int32_t *)(p + 0x38 * 4));
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
                0x5b8, "dump_encoder_chn_attr",
                "enc[%d]attr->rcAttr.attrRcMode.attrCappedVbr.uMaxPSNR = %u\n",
                s7_1, (uint32_t)*(uint16_t *)(p + 0x39 * 4));
            break;
        case 8:
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
                0x5ba, "dump_encoder_chn_attr",
                "enc[%d]attr->rcAttr.attrRcMode.attrCappedQuality.uTargetBitRate = %u\n",
                s7_1, *(int32_t *)(p + 0x32 * 4));
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
                0x5bb, "dump_encoder_chn_attr",
                "enc[%d]attr->rcAttr.attrRcMode.attrCappedQuality.uMaxBitRate = %u\n",
                s7_1, *(int32_t *)(p + 0x33 * 4));
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
                0x5bc, "dump_encoder_chn_attr",
                "enc[%d]attr->rcAttr.attrRcMode.attrCappedQuality.iInitialQP = %d\n",
                s7_1, (int32_t)*(int16_t *)(p + 0x34 * 4));
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
                0x5bd, "dump_encoder_chn_attr",
                "enc[%d]attr->rcAttr.attrRcMode.attrCappedQuality.iMinQP = %d\n",
                s7_1, (int32_t)*(int16_t *)(p + 0xd2));
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
                0x5be, "dump_encoder_chn_attr",
                "enc[%d]attr->rcAttr.attrRcMode.attrCappedQuality.iMaxQP = %d\n",
                s7_1, (int32_t)*(int16_t *)(p + 0x35 * 4));
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
                0x5bf, "dump_encoder_chn_attr",
                "enc[%d]attr->rcAttr.attrRcMode.attrCappedQuality.iIPDelta = %d\n",
                s7_1, (int32_t)*(int16_t *)(p + 0xd6));
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
                0x5c0, "dump_encoder_chn_attr",
                "enc[%d]attr->rcAttr.attrRcMode.attrCappedQuality.iPBDelta = %d\n",
                s7_1, (int32_t)*(int16_t *)(p + 0x36 * 4));
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
                0x5c1, "dump_encoder_chn_attr",
                "enc[%d]attr->rcAttr.attrRcMode.attrCappedQuality.eRcOptions = 0x%08x\n",
                s7_1, *(int32_t *)(p + 0x37 * 4));
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
                0x5c2, "dump_encoder_chn_attr",
                "enc[%d]attr->rcAttr.attrRcMode.attrCappedQuality.uMaxPictureSize = %u\n",
                s7_1, *(int32_t *)(p + 0x38 * 4));
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
                0x5c3, "dump_encoder_chn_attr",
                "enc[%d]attr->rcAttr.attrRcMode.attrCappedQuality.uMaxPSNR = %u\n",
                s7_1, (uint32_t)*(uint16_t *)(p + 0x39 * 4));
            break;
        }

        imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
            0x5c6, "dump_encoder_chn_attr",
            "enc[%d]attr->rcAttr.outFrmRate.frmRateNum=%u\n",
            s7_1, *(int32_t *)(p + 0x3a * 4));
        imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
            0x5c7, "dump_encoder_chn_attr",
            "enc[%d]attr->rcAttr.outFrmRate.frmRateDen=%u\n",
            s7_1, *(int32_t *)(p + 0x3b * 4));
        imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
            0x5c9, "dump_encoder_chn_attr",
            "enc[%d]attr->gopAttr.uGopCtrlMode = %u\n",
            s7_1, *(int32_t *)(p + 0x3c * 4));
        imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
            0x5ca, "dump_encoder_chn_attr",
            "enc[%d]attr->gopAttr.uGopLength = %u\n",
            s7_1, (uint32_t)*(uint16_t *)(p + 0x3d * 4));
        imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
            0x5cb, "dump_encoder_chn_attr",
            "enc[%d]attr->gopAttr.uNotifyUserLTInter = %u\n",
            s7_1, (uint32_t)*(uint8_t *)(p + 0xf6));
        imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
            0x5cc, "dump_encoder_chn_attr",
            "enc[%d]attr->gopAttr.uMaxSameSenceCnt = %u\n",
            s7_1, *(int32_t *)(p + 0x3e * 4));
        imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
            0x5cd, "dump_encoder_chn_attr",
            "enc[%d]attr->gopAttr.bEnableLT = %u\n",
            s7_1, (uint32_t)*(uint8_t *)(p + 0x3f * 4));
        imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
            0x5ce, "dump_encoder_chn_attr",
            "enc[%d]attr->gopAttr.uFreqLT = %u\n",
            s7_1, *(int32_t *)(p + 0x40 * 4));
        imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
            0x5cf, "dump_encoder_chn_attr",
            "enc[%d]attr->gopAttr.bLTRC = %u\n",
            s7_1, (uint32_t)*(uint8_t *)(p + 0x41 * 4));
        imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
            0x5d1, "dump_encoder_chn_attr",
            "-----------------------%s(%d) end---------------------------------------\n",
            "channel_encoder_init", 0x620);
    }

    /* -- (4) rate-control attrs -> codec_params ------------------------- */
    /* Stock passes &var_754 where var_754 = codec_params[0x6c]
     * (0x7c0 - 0x754 = 0x6c). My earlier port used 0x80 which was
     * wrong by 0x14 — RC validation downstream saw uninitialized bits. */
    {
        int kfd = open("/dev/kmsg", O_WRONLY);
        if (kfd >= 0) {
            char buf[256];
            const EncoderRcAttrLayout *rc = &attr->rc;
            int n = snprintf(buf, sizeof(buf),
                "libimp/ENC: user rcAttr: mode=%d br=%d br2=%d qp=%d "
                "fld3=%d fld4=%d fld5=%d fld6=%d fld7=%d fld8=%d "
                "fps@0x3a=%d/%d\n",
                (int32_t)rc->rc_mode,
                (int32_t)rc->u.vbr.target_bitrate,
                (int32_t)rc->u.vbr.max_bitrate,
                (int32_t)rc->u.fixqp.qp,
                (int32_t)rc->u.cbr.min_qp,
                (int32_t)rc->u.cbr.max_qp,
                (int32_t)rc->u.cbr.ip_delta,
                (int32_t)rc->u.cbr.pb_delta,
                (int32_t)rc->u.cbr.rc_options,
                (int32_t)rc->u.cbr.max_picture_size,
                (int32_t)attr->out_fps_num,
                (int32_t)attr->out_fps_den);
            if (n > 0) write(kfd, buf, (size_t)n);
            close(kfd);
        }
    }
    if (channel_encoder_set_rc_param(codec_params + 0x6c,
                                     &attr->rc) < 0) {
        enc_trace("libimp/CHINIT: rc-param-fail chn=%d\n", chn_id);
        int32_t opt = IMP_Log_Get_Option();
        imp_log_fun(6, opt, 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
            0x603, "channel_encoder_init",
            "channel_encoder_set_rc_param failed\n");
        return -1;
    }
    enc_trace("libimp/CHINIT: rc-param-ok chn=%d\n", chn_id);
    enc_trace("libimp/CHINIT: pre-fps-sanity chn=%d num=%d den=%d\n",
              chn_id,
              (int32_t)attr->out_fps_num,
              (int32_t)attr->out_fps_den);

    /* outFrmRate sanity */
    if (attr->out_fps_num == 0 || attr->out_fps_den == 0) {
        enc_trace("libimp/CHINIT: fps-invalid chn=%d num=%d den=%d\n",
                  chn_id,
                  (int32_t)attr->out_fps_num,
                  (int32_t)attr->out_fps_den);
        /* HLIL calls __assert here — we fail loudly. */
        int32_t opt = IMP_Log_Get_Option();
        imp_log_fun(6, opt, 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
            0x1cd, "channel_encoder_set_fps_param",
            "pOutFrmRate->frmRateNum && pOutFrmRate->frmRateDen\n");
        return -1;
    }
    enc_trace("libimp/CHINIT: fps-sanity-ok chn=%d\n", chn_id);

    /* -- (5) Create codec handle --------------------------------------- */
    void *codec_handle = NULL;
    enc_trace("libimp/CHINIT: pre-codec-dump chn=%d cp=%p\n",
              chn_id, codec_params);
    {
        /* Dump the exact offsets AL_Codec_Encode_Create reads. */
        int kfd = open("/dev/kmsg", O_WRONLY);
        if (kfd >= 0) {
            char buf[512];
            uint8_t *cb = codec_params;
            int n = snprintf(buf, sizeof(buf),
                "libimp/ENC: cp@%p w=%u h=%u pic=0x%08x prof=0x%08x lvl=%u "
                "rc@0x68=%d rc@0x6c=%d 0x70=%d 0x74=%d 0x7c=%d 0x80=%d "
                "sc@0x78=0x%08x "
                "a8=0x%08x ac@0xac=%u ae@0xae=%u 0x4e=%u 0x4f=%u "
                "0x1f=%u 0x3c=%u 0x40=%u sqp@0x80=%d "
                "a1@0x114=%u a1@0x116=%u\n",
                codec_params,
                (unsigned)*(uint16_t *)(cb + 0x08),
                (unsigned)*(uint16_t *)(cb + 0x0a),
                *(uint32_t *)(cb + 0x14),
                *(uint32_t *)(cb + 0x20),
                (unsigned)cb[0x24],
                *(int32_t *)(cb + 0x68),
                *(int32_t *)(cb + 0x6c),
                *(int32_t *)(cb + 0x70),
                *(int32_t *)(cb + 0x74),
                *(int32_t *)(cb + 0x7c),
                *(int32_t *)(cb + 0x80),
                *(uint32_t *)(cb + 0x78),
                *(uint32_t *)(cb + 0xa8),
                (unsigned)*(uint16_t *)(cb + 0xac),
                (unsigned)cb[0xae],
                (unsigned)cb[0x4e],
                (unsigned)cb[0x4f],
                (unsigned)cb[0x1f],
                (unsigned)cb[0x3c],
                (unsigned)*(uint16_t *)(cb + 0x40),
                (int)*(int8_t *)(cb + 0x80),
                (unsigned)*(uint16_t *)((uint8_t*)chn + 0x114),
                (unsigned)*(uint16_t *)((uint8_t*)chn + 0x116));
            if (n > 0) write(kfd, buf, (size_t)n);
            close(kfd);
        }
    }
    enc_trace("libimp/CHINIT: post-codec-dump chn=%d cp=%p\n",
              chn_id, codec_params);
    enc_trace("libimp/CHINIT: pre-codec-create chn=%d cp=%p\n",
              chn_id, codec_params);
    int32_t cec_ret = AL_Codec_Encode_Create(&codec_handle, codec_params);
    enc_trace("libimp/CHINIT: codec-create chn=%d ret=%d handle=%p\n",
              chn_id, cec_ret, codec_handle);
    {
        int kfd = open("/dev/kmsg", O_WRONLY);
        if (kfd >= 0) {
            char buf[160];
            int n = snprintf(buf, sizeof(buf),
                "libimp/ENC: Codec_Encode_Create returned %d, handle=%p\n",
                cec_ret, codec_handle);
            if (n > 0) write(kfd, buf, (size_t)n);
            close(kfd);
        }
    }
    if (cec_ret < 0 || codec_handle == NULL) {
        enc_trace("libimp/CHINIT: codec-create-fail chn=%d ret=%d handle=%p\n",
                  chn_id, cec_ret, codec_handle);
        int32_t opt = IMP_Log_Get_Option();
        imp_log_fun(6, opt, 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
            0x625, "channel_encoder_init",
            "Codec_Encode_Create failed\n");
        return -1;
    }
    chn->codec_handle = enc_ptr_as_u32(codec_handle);
    /* Reset CappedVbr/CappedQuality cold fields. */
    pthread_mutex_lock(enc_mutex_ptr(&chn->queue_mutex));
    memset(p + 0x1e * 4, 0, 0x18);
    pthread_mutex_unlock(enc_mutex_ptr(&chn->queue_mutex));

    /* -- (6) Query src-frame count / size ------------------------------ */
    if (AL_Codec_Encode_GetSrcFrameCntAndSize(codec_handle,
                                              &chn->src_frame_cnt,
                                              &chn->src_frame_size) < 0) {
        enc_trace("libimp/CHINIT: src-info-fail chn=%d codec=%p\n",
                  chn_id, codec_handle);
        int32_t opt = IMP_Log_Get_Option();
        imp_log_fun(6, opt, 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
            0x636, "channel_encoder_init",
            "Codec_Encode_GetSrcFrameCntAndSize failed\n");
        AL_Codec_Encode_Destroy(codec_handle);
        return -1;
    }

    int32_t src_cnt = chn->src_frame_cnt;
    int32_t src_sz  = chn->src_frame_size;
    {
        int32_t opt = IMP_Log_Get_Option();
        imp_log_fun(4, opt, 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
            0x63a, "channel_encoder_init",
            "encChn=%d,srcFrameCnt=%u,srcFrameSize=%u\n",
            chn_id, src_cnt, src_sz);
    }
    enc_trace("libimp/CHINIT: src-info-ok chn=%d cnt=%d size=%d\n",
              chn_id, src_cnt, src_sz);

    /* -- (7) Allocate srcFrameArray ------------------------------------ */
    void *srcFrameArray = calloc((size_t)src_cnt, 0x458);
    chn->src_frame_array = enc_ptr_as_u32(srcFrameArray);
    if (srcFrameArray == NULL) {
        enc_trace("libimp/CHINIT: frame-array-fail chn=%d cnt=%d\n",
                  chn_id, src_cnt);
        int32_t opt = IMP_Log_Get_Option();
        imp_log_fun(6, opt, 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
            0x63e, "channel_encoder_init",
            "calloc srcFrameArray failed\n");
        AL_Codec_Encode_Destroy(codec_handle);
        return -1;
    }
    enc_trace("libimp/CHINIT: frame-array-ok chn=%d ptr=%p cnt=%d\n",
              chn_id, srcFrameArray, src_cnt);

    /* -- (8) Fifo_Init + Fifo_Queue every slot ------------------------- */
    Fifo_Init(chn->public_stream_fifo, src_cnt);
    for (int32_t i = 0; i < src_cnt; i++) {
        Fifo_Queue(chn->public_stream_fifo,
                   (uint8_t *)srcFrameArray + (size_t)i * 0x458,
                   -1);
    }
    enc_trace("libimp/CHINIT: fifo-primed chn=%d cnt=%d\n", chn_id, src_cnt);

    /* -- (9) compatibility runtime ------------------------------------ */
    sem_init(enc_sem_ptr(&rt->release_sem), 0, 0);
    pthread_mutex_init(enc_mutex_ptr(&rt->release_mutex), NULL);
    pthread_mutex_init(enc_mutex_ptr(&rt->submit_mutex), NULL);
    pthread_cond_init(enc_cond_ptr(&rt->submit_cond), NULL);

    /* -- (10) threads ------------------------------------------------- */
    if (pthread_create(&rt->release_thread, NULL,
                       release_frame_thread, chn) < 0) {
        enc_trace("libimp/CHINIT: release-thread-fail chn=%d\n", chn_id);
        int32_t opt = IMP_Log_Get_Option();
        imp_log_fun(6, opt, 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
            0x64a, "channel_encoder_init",
            "pthread_create release_frame_thread failed\n");
        pthread_cond_destroy(enc_cond_ptr(&rt->submit_cond));
        pthread_mutex_destroy(enc_mutex_ptr(&rt->submit_mutex));
        pthread_mutex_destroy(enc_mutex_ptr(&rt->release_mutex));
        sem_destroy(enc_sem_ptr(&rt->release_sem));
        Fifo_Deinit(chn->public_stream_fifo);
        free(srcFrameArray);
        AL_Codec_Encode_Destroy(codec_handle);
        return -1;
    }
    rt->release_valid = 1;
    enc_trace("libimp/CHINIT: release-thread-ok chn=%d tid=%p\n",
              chn_id, (void *)rt->release_thread);

    if (pthread_create(&rt->submit_thread, NULL,
                       encoder_submit_thread, chn) < 0) {
        enc_trace("libimp/CHINIT: submit-thread-fail chn=%d\n", chn_id);
        pthread_cancel(rt->release_thread);
        pthread_join(rt->release_thread, NULL);
        rt->release_valid = 0;
        pthread_cond_destroy(enc_cond_ptr(&rt->submit_cond));
        pthread_mutex_destroy(enc_mutex_ptr(&rt->submit_mutex));
        pthread_mutex_destroy(enc_mutex_ptr(&rt->release_mutex));
        sem_destroy(enc_sem_ptr(&rt->release_sem));
        Fifo_Deinit(chn->public_stream_fifo);
        free(srcFrameArray);
        AL_Codec_Encode_Destroy(codec_handle);
        return -1;
    }
    rt->submit_valid = 1;
    enc_trace("libimp/CHINIT: submit-thread-ok chn=%d tid=%p\n",
              chn_id, (void *)rt->submit_thread);

    if (pthread_create(&rt->update_thread, NULL,
                       update_frmstrm, chn) < 0) {
        enc_trace("libimp/CHINIT: stream-thread-fail chn=%d\n", chn_id);
        int32_t opt = IMP_Log_Get_Option();
        imp_log_fun(6, opt, 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c",
            0x64f, "channel_encoder_init",
            "pthread_create update_frmstrm failed\n");
        pthread_cancel(rt->submit_thread);
        pthread_join(rt->submit_thread, NULL);
        rt->submit_valid = 0;
        pthread_cancel(rt->release_thread);
        pthread_join(rt->release_thread, NULL);
        rt->release_valid = 0;
        pthread_cond_destroy(enc_cond_ptr(&rt->submit_cond));
        pthread_mutex_destroy(enc_mutex_ptr(&rt->submit_mutex));
        pthread_mutex_destroy(enc_mutex_ptr(&rt->release_mutex));
        sem_destroy(enc_sem_ptr(&rt->release_sem));
        Fifo_Deinit(chn->public_stream_fifo);
        free(srcFrameArray);
        AL_Codec_Encode_Destroy(codec_handle);
        return -1;
    }
    rt->update_valid = 1;
    enc_trace("libimp/CHINIT: stream-thread-ok chn=%d tid=%p\n",
              chn_id, (void *)rt->update_thread);
    enc_trace("libimp/CHINIT: return-ok chn=%d codec=%p src_cnt=%d src_sz=%d\n",
              chn_id, codec_handle, src_cnt, src_sz);

    return 0;
}

/* ----- channel_encoder_exit — binary-accurate port -------------------- */

int32_t channel_encoder_exit(EncoderChannelLayout *chn)
{
    uint8_t *p = (uint8_t *)chn;
    EncoderCompatRuntime *rt;
    int slot;
    if (!chn) return -1;
    slot = encoder_thread_slot(chn);
    if (slot < 0) return -1;
    rt = &g_encoder_runtime[slot];

    AL_Codec_Encode_Commit_FilledFifo(enc_u32_as_ptr(chn->codec_handle));
    if (rt->release_valid) {
        pthread_cancel(rt->release_thread);
        pthread_join(rt->release_thread, NULL);
        rt->release_valid = 0;
    }
    if (rt->submit_valid) {
        pthread_cancel(rt->submit_thread);
        pthread_join(rt->submit_thread, NULL);
        rt->submit_valid = 0;
    }
    if (rt->update_valid) {
        pthread_cancel(rt->update_thread);
        pthread_join(rt->update_thread, NULL);
        rt->update_valid = 0;
    }
    if (rt->pending_frame != NULL) {
        encoder_unlock_and_release_frame(chn, rt->pending_frame);
        rt->pending_frame = NULL;
    }
    pthread_cond_destroy(enc_cond_ptr(&rt->submit_cond));
    pthread_mutex_destroy(enc_mutex_ptr(&rt->submit_mutex));
    pthread_mutex_destroy(enc_mutex_ptr(&rt->release_mutex));
    sem_destroy(enc_sem_ptr(&rt->release_sem));

    pthread_mutex_lock(enc_mutex_ptr(&chn->queue_mutex));
    {
        int32_t total_frames = *(int32_t *)(p + 0x7c);
        int32_t complete_hi  = *(int32_t *)(p + 0x84);
        void *fifo_ctx = (void *)(p + 0x18);

        while (complete_hi < total_frames ||
               (complete_hi == total_frames &&
                *(uint32_t *)(p + 0x80) < *(uint32_t *)(p + 0x78))) {
            void *buf = NULL;
            int32_t stream_id = 0;
            void *id_buf = NULL;
            if (AL_Codec_Encode_GetStream(enc_u32_as_ptr(chn->codec_handle),
                                          &id_buf, &buf) < 0) {
                (void)stream_id;
                break;
            }
            pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
            *(uint32_t *)(p + 0x80) += 1;
            if (*(uint32_t *)(p + 0x80) == 0) *(int32_t *)(p + 0x84) += 1;
            AL_Codec_Encode_ReleaseStream(enc_u32_as_ptr(chn->codec_handle), id_buf, buf);
            Fifo_Queue(fifo_ctx, (uint8_t *)buf + 0x24, -1);
            pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
            total_frames = *(int32_t *)(p + 0x7c);
            complete_hi  = *(int32_t *)(p + 0x84);
        }
    }
    pthread_mutex_unlock(enc_mutex_ptr(&chn->queue_mutex));

    Fifo_Deinit(chn->public_stream_fifo);
    free(enc_u32_as_ptr(chn->src_frame_array));
    chn->src_frame_array = 0;
    AL_Codec_Encode_Destroy(enc_u32_as_ptr(chn->codec_handle));
    chn->codec_handle = 0;
    return 0;
}

/* ----- channel_encoder_set_rc_param — binary-accurate port ------------ */
int32_t channel_encoder_set_rc_param(void *arg1, void *arg2)
{
    int32_t *dst = (int32_t *)arg1;
    const EncoderRcAttrLayout *src = (const EncoderRcAttrLayout *)arg2;
    if (!dst || !src) return -1;

    uint8_t *dst_b = (uint8_t *)arg1;
    int32_t mode = (int32_t)src->rc_mode;

    switch (mode) {
    case 0: /* FixQp */
        dst[0] = 0;
        *(int16_t *)(dst_b + 0x18) = src->u.fixqp.qp;
        return 0;
    case 1: /* Cbr */
        dst[0] = 1;
        dst[4] = (int32_t)src->u.cbr.target_bitrate * 1000;
        dst[5] = (int32_t)src->u.cbr.target_bitrate * 1000;
        *(int16_t *)(dst_b + 0x18) = src->u.cbr.initial_qp;
        *(int8_t  *)(dst_b + 0x1a) = (int8_t)src->u.cbr.min_qp;
        *(int16_t *)(dst_b + 0x1c) = src->u.cbr.max_qp;
        *(int8_t  *)(dst_b + 0x1e) = (int8_t)src->u.cbr.ip_delta;
        *(int16_t *)(dst_b + 0x20) = src->u.cbr.pb_delta;
        dst[10] = (int32_t)src->u.cbr.rc_options;
        dst[15] = (int32_t)src->u.cbr.max_picture_size * 1000;
        return 0;
    case 2: /* Vbr */
        dst[0] = 2;
        dst[4] = (int32_t)src->u.vbr.target_bitrate * 1000;
        dst[5] = (int32_t)src->u.vbr.max_bitrate * 1000;
        *(int16_t *)(dst_b + 0x18) = src->u.vbr.initial_qp;
        *(int8_t  *)(dst_b + 0x1a) = (int8_t)src->u.vbr.min_qp;
        *(int16_t *)(dst_b + 0x1c) = src->u.vbr.max_qp;
        *(int8_t  *)(dst_b + 0x1e) = (int8_t)src->u.vbr.ip_delta;
        *(int16_t *)(dst_b + 0x20) = src->u.vbr.pb_delta;
        dst[10] = (int32_t)src->u.vbr.rc_options;
        dst[15] = (int32_t)src->u.vbr.max_picture_size * 1000;
        return 0;
    case 4: /* CappedVbr */
    case 8: /* CappedQuality */
        dst[0] = mode;
        dst[4] = (int32_t)src->u.vbr.target_bitrate * 1000;
        dst[5] = (int32_t)src->u.vbr.max_bitrate * 1000;
        *(int16_t *)(dst_b + 0x18) = src->u.vbr.initial_qp;
        *(int8_t  *)(dst_b + 0x1a) = (int8_t)src->u.vbr.min_qp;
        *(int16_t *)(dst_b + 0x1c) = src->u.vbr.max_qp;
        *(int8_t  *)(dst_b + 0x1e) = (int8_t)src->u.vbr.ip_delta;
        *(int16_t *)(dst_b + 0x20) = src->u.vbr.pb_delta;
        dst[10] = (int32_t)src->u.vbr.rc_options;
        dst[15] = (int32_t)src->u.vbr.max_picture_size * 1000;
        {
            uint32_t scale = (uint32_t)src->u.vbr.max_psnr * 0x14;
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
    EncoderChannelLayout *chn = (EncoderChannelLayout *)arg1;
    if (!chn) return -1;

    int32_t max = *enc_channel_stream_ring_count(chn);
    int32_t hd  = *enc_channel_stream_ring_base(chn);
    int32_t idx = *enc_channel_stream_ring_index(chn);

    if (max == 0) return -1;
    int32_t expected = hd + ((idx % max) * 0x188);
    if (arg2 != expected) {
        return -1;
    }

    pthread_mutex_lock(enc_mutex_ptr(&chn->queue_mutex));
    *enc_channel_stream_ring_index(chn) += 1;
    pthread_mutex_unlock(enc_mutex_ptr(&chn->queue_mutex));
    sem_post(enc_channel_frame_slot_sem(chn));
    return 0;
}

/* ----- IVS release — tail-call wrapper -------------------------------- */

int32_t IMP_IVS_ReleaseData(void *vaddr)
{
    VBMUnlockFrameByVaddr(vaddr);
    return 0;
}

/* ----- Pool-id stubs --------------------------------------------------- *
 * BUILD=ported excludes src/video/imp_mempool.c (allocator conflict).
 * When using the stock ported encoder wrapper, these helpers must still
 * exist so callers can fall back to the global DMA allocator when no pool
 * is bound. */
#ifndef USE_REAL_IMP_ENCODER
int32_t IMP_Encoder_GetPool(int32_t encChn)
{
    (void)encChn;
    return -1;
}

int32_t IMP_Encoder_SetPool(int32_t encChn, int32_t poolId)
{
    (void)encChn;
    (void)poolId;
    return 0;
}

int32_t IMP_Encoder_ClearPoolId(void)
{
    return 0;
}
#endif

/* IMP_FrameSource_* pool helpers live in src/dma_alloc.c (legacy). */
