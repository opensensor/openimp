/*
 * T30 H.265 encoder on the Radix block, see t30_radix_encoder.h.
 *
 * Vendor flow (libimp 1.0.5, nm addresses of the uclibc 5.4.0 build):
 *   i265e_encode: decTypeAndRef (I/P, frames since the IDR = POC),
 *     rcStart (JZ_VPU_RC_FRAME_START_T30: picture QP, the parameter block's
 *     coding fields, h265_get_lcu_qp: the CU QP map), sliceInit (NAL type,
 *     RPS, slice fields);
 *   i265e_slice_write -> icodecEncode: resetEntropy (CABAC states for the
 *     slice type and QP), codec_enc 0x6cf4c (ROI, strides, frame_cqp_offset,
 *     state pointer, dqp fields), hwicodec_pf_h265e_t30_enc 0x4d858
 *     (addresses, H265E_T30_SliceInit, RUN with vpu_id 0x4000001, the 19
 *     statistics registers), rcReEncode (FRAME_REPEATE_JUDGE: code again);
 *   rcEnd (FRAME_END), sliceEncap 0x6c3c0 (VPS/SPS/PPS on an IDR, slice
 *     header, byte alignment, the slice data, emulation prevention).
 * Buffers as hwicodec_pf_h265e_init_bpool: the reconstruction/reference
 * pictures are NV12 with a 16-sample border (descriptor 256, 1024, 16, 16,
 * 16, 16 at .rodata 0x146880 of the 4.7.2 build), one 20 KiB command list
 * buffer.  The vendor's bitstream goes to the shared VPU bitstream buffer;
 * here each encoder has its own window of one raw picture.
 */
#include "t30/radix/t30_radix_encoder.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "dma_alloc.h"
#include "eprc/eprc.h"
#include "imp_log_int.h"
#include "t30/radix/t30_radix_cabac.h"
#include "t30/radix/t30_radix_cmdlist.h"
#include "t30/radix/t30_radix_hevc.h"
#include "t30/radix/t30_radix_sliceinfo.h"

#define RADIX_CHANNEL_REQUEST 0xc0386300u
#define RADIX_CHANNEL_RELEASE 0xc0386301u
#define RADIX_CHANNEL_RUN     0xc0386302u
#define RADIX_REG_READ        0xc0386307u
#define RADIX_VPU_ID          0x04000001u   /* VPU_RADIX_ID | 1 << 0 */
#define RADIX_CODEC_H265_ENC  0x10u         /* HWH265ENC */
#define RADIX_CHANNEL_CLOSE   2u
#define RADIX_DELAY_MS        20000u
#define RADIX_REG_BASE        0x13600000u
#define RADIX_CMDLIST_BYTES   20480u
#define RADIX_HEADER_BYTES    1024u
#define RADIX_MAX_REENCODES   3u

/* struct channel_node of soc_vpu (56 bytes, as T30ChannelNode) */
typedef struct {
    uint32_t clist;
    uint32_t vlist;
    uint32_t mdelay;
    uint32_t channel_id;
    int32_t vpu_id;
    uint32_t codecdir;
    uint32_t workphase;
    uint32_t status;
    uint32_t output_len;
    uint32_t dma_addr;
    int32_t thread_id;
    uint32_t cmpx;
    uint32_t n_flag;
    uint32_t ncu_addr;
} RadixChannelNode;

_Static_assert(sizeof(RadixChannelNode) == 56, "soc_vpu channel ABI");

typedef struct {
    IMPDMABufferInfo dma;
    uint32_t y, c;
} RadixPicture;

struct T30RadixEncoder {
    HWEncoderParams params;
    uint32_t width, height;         /* coded size (16 x 8 aligned) */
    int fd;
    RadixChannelNode channel;
    IMPDMABufferInfo cmdlist, bitstream;
    RadixPicture pic[2];            /* reconstruction / reference */
    unsigned int recon;             /* index of the next reconstruction */
    int have_reference;
    uint8_t block[T30_RADIX_SI_SIZE];
    uint8_t *qpmap;
    uint8_t state[T30_RADIX_CABAC_CONTEXTS];
    Eprc30 rc;
    uint32_t regs[EPRC30_STAT_REGS];
    T30HevcVps vps;
    T30HevcSps sps;
    T30HevcPps pps;
    uint32_t frames_since_idr;
    int force_idr;
    /* run-time changes (OpenIMP_T30_RadixUpdateParams), latched until the
     * next IDR as I265eReconfig does */
    HWEncoderParams pending;
    int pending_set;
    uint32_t failures;
    uint32_t reg_errors;
};

/* Radix serves every H.265 channel: the statistics registers belong to the
 * last run (the vendor holds bsbufsem around RUN and the reads). */
static pthread_mutex_t radix_run_lock = PTHREAD_MUTEX_INITIALIZER;

/* the 19 registers of I265eRatectrl::rcEnd, in its order */
static const uint32_t radix_stat_regs[19] = {
    0x40088, 0x4008c, 0x400a0, 0x400a4, 0x400b0, 0x400b8, 0x40028, 0x4002c,
    0x200a0, 0x200a4, 0x200a8, 0x200ac, 0x200b0, 0x200b4, 0x200b8, 0x200bc,
    0x20128, 0x2012c, 0x20130,
};

static void radix_dma_release(IMPDMABufferInfo *dma)
{
    if (dma->phys_addr) {
        DMA_FreePhys(dma->phys_addr);
        memset(dma, 0, sizeof(*dma));
    }
}

static int radix_dma_allocate(IMPDMABufferInfo *dma, uint32_t size,
                              const char *tag)
{
    if (size > INT32_MAX ||
        DMA_AllocDescriptorTop(dma, (int)size, tag) != 0 ||
        !dma->phys_addr || !dma->virt_addr) {
        LOG_CODEC("T30 Radix: DMA allocation failed tag=%s size=%u", tag, size);
        return -1;
    }
    memset((void *)(uintptr_t)dma->virt_addr, 0, size);
    /* the CPU does not write these buffers again (the command list is
     * written back per picture): write the zeroed lines back now */
    if (DMA_RmemFlushCache((void *)(uintptr_t)dma->virt_addr, size, 1) != 0) {
        radix_dma_release(dma);
        return -1;
    }
    return 0;
}

/* hwicodec_cal_bufsize for the picture descriptor (256, 1024, pad 16 left
 * and right, 16 + 16 + 16 rows): luma origin and chroma plane as
 * hwicodec_init_soc_bpool places them */
static uint32_t radix_picture_bytes(uint32_t w16, uint32_t h16)
{
    return (h16 + 48u) * (w16 + 32u) + 1280u;
}

static int radix_alloc_picture(RadixPicture *p, uint32_t w16, uint32_t h16)
{
    uint32_t plane = radix_picture_bytes(w16, h16);

    if (radix_dma_allocate(&p->dma, plane + plane / 2u + 2048u,
                           "t30-radix-ref") != 0)
        return -1;
    p->y = ((p->dma.phys_addr + 255u) & ~255u) + (w16 + 48u) * 16u;
    p->c = p->dma.phys_addr + plane;
    return 0;
}

static void radix_eprc_params(const HWEncoderParams *hp, uint32_t w,
                              uint32_t h, EprcParams *p)
{
    int app = (hp->rc_flags & HW_RC_FLAG_APP) != 0;
    uint32_t max_qp = hp->max_qp ? hp->max_qp : 51u;

    memset(p, 0, sizeof(*p));
    p->codec = 1;
    p->width = w;
    p->height = h;
    p->rc_mode = hp->rc_mode == HW_RC_MODE_CBR ? EPRC_MODE_CBR
               : hp->rc_mode == HW_RC_MODE_VBR
                   ? ((hp->rc_flags & HW_RC_FLAG_SMART) ? EPRC_MODE_SMART : EPRC_MODE_VBR)
                   : EPRC_MODE_CQP;
    p->gop = hp->gop_length;
    p->fps_num = hp->fps_num;
    p->fps_den = hp->fps_den;
    p->max_qp = max_qp > 51u ? 51u : max_qp;
    p->min_qp = hp->min_qp > p->max_qp ? p->max_qp : hp->min_qp;
    p->bitrate = p->max_bitrate = hp->bitrate / 1000u;
    p->cqp = p->init_qp = (int32_t)(hp->qp ? hp->qp : 35u);
    p->ip_ratio = 1.399999976158142;   /* i265e_param_default +0x60 */
    if (app) {
        p->i_bias = hp->bias_level;
        p->frm_qp_step = hp->frm_qp_step;
        p->gop_qp_step = hp->gop_qp_step;
        p->static_time = hp->static_time;
        p->change_pos = hp->change_pos;
        p->quality = hp->quality_level;
    } else {
        /* i265e_param_default */
        p->frm_qp_step = 3u;
        p->gop_qp_step = 15u;
        p->static_time = 2u;
        p->change_pos = 80u;
        p->quality = 4u;
    }
}

/* the IMP channel attributes that reach i265e_param_t (IMP_Encoder_CreateChn,
 * I265eReconfig::ReconfigIdrFrame) */
static void radix_channel(const HWEncoderParams *hp, T30HevcChannel *ch)
{
    memset(ch, 0, sizeof(*ch));
    ch->width = hp->width;
    ch->height = hp->height;
    ch->fps_num = hp->fps_num;
    ch->fps_den = hp->fps_den;
    ch->rc_mode = hp->rc_mode == HW_RC_MODE_CBR ? 1u :
                  hp->rc_mode == HW_RC_MODE_VBR
                      ? ((hp->rc_flags & HW_RC_FLAG_SMART) ? 3u : 2u) : 0u;
    ch->bitrate = hp->bitrate / 1000u;
}

int OpenIMP_T30_RadixCreate(T30RadixEncoder **encoder_out,
                            const HWEncoderParams *params)
{
    T30RadixEncoder *e;
    T30HevcChannel ch;
    EprcParams ep;
    uint32_t w16, h16, i;

    if (!encoder_out || !params || !params->width || !params->height ||
        params->width > 4096u || params->height > 2304u)
        return -1;
    e = calloc(1, sizeof(*e));
    if (!e)
        return -1;
    e->fd = -1;
    e->params = *params;
    if (!e->params.fps_num || !e->params.fps_den) {
        e->params.fps_num = 25u;
        e->params.fps_den = 1u;
    }
    if (!e->params.gop_length)
        e->params.gop_length = 25u;

    radix_channel(&e->params, &ch);
    t30_hevc_channel_params(&ch, &e->vps, &e->sps, &e->pps);
    e->width = e->sps.width;
    e->height = e->sps.height;
    w16 = (e->width + 15u) & ~15u;
    h16 = (e->height + 15u) & ~15u;

    radix_eprc_params(&e->params, e->width, e->height, &ep);
    if (EPRC30_Init(&e->rc, &ep, e->block) != 0)
        goto fail;
    e->qpmap = calloc(1, EPRC30_QpMapSize(&e->rc));
    if (!e->qpmap)
        goto fail;
    EPRC30_SetQpMap(&e->rc, e->qpmap, (uint32_t)(uintptr_t)e->qpmap);

    e->fd = open("/dev/soc_vpu", O_RDWR | O_SYNC | O_CLOEXEC);
    if (e->fd < 0) {
        LOG_CODEC("T30 Radix: /dev/soc_vpu: %s", strerror(errno));
        goto fail;
    }
    e->channel.mdelay = RADIX_DELAY_MS;
    e->channel.codecdir = RADIX_CODEC_H265_ENC;
    e->channel.thread_id = -1;
    if (ioctl(e->fd, RADIX_CHANNEL_REQUEST, &e->channel) != 0) {
        LOG_CODEC("T30 Radix: channel request failed: %s", strerror(errno));
        goto fail;
    }
    if (radix_dma_allocate(&e->cmdlist, RADIX_CMDLIST_BYTES, "t30-radix-cmd") != 0 ||
        radix_dma_allocate(&e->bitstream,
                           w16 * h16 * 3u / 2u < (256u << 10) ? (256u << 10)
                                                              : w16 * h16 * 3u / 2u,
                           "t30-radix-bs") != 0)
        goto fail;
    for (i = 0; i < 2u; i++)
        if (radix_alloc_picture(&e->pic[i], w16, h16) != 0)
            goto fail;
    e->force_idr = 1;
    IMP_LOG_INFO("Encoder", "T30 Radix: H.265 %ux%u (coded %ux%u) %u/%u fps gop %u "
                 "rc %u, level %u.%u%s", params->width, params->height, e->width,
                 e->height, e->params.fps_num, e->params.fps_den,
                 e->params.gop_length, ep.rc_mode, e->vps.ptl.level_idc / 30u,
                 e->vps.ptl.level_idc % 30u / 3u, e->vps.ptl.tier_flag ? " high tier" : "");
    *encoder_out = e;
    return 0;
fail:
    OpenIMP_T30_RadixDestroy(e);
    return -1;
}

void OpenIMP_T30_RadixDestroy(T30RadixEncoder *e)
{
    unsigned int i;

    if (!e)
        return;
    if (e->fd >= 0) {
        if (e->channel.clist) {
            e->channel.vlist = 0;
            e->channel.workphase = RADIX_CHANNEL_CLOSE;
            (void)ioctl(e->fd, RADIX_CHANNEL_RELEASE, &e->channel);
        }
        close(e->fd);
    }
    for (i = 0; i < 2u; i++)
        radix_dma_release(&e->pic[i].dma);
    radix_dma_release(&e->cmdlist);
    radix_dma_release(&e->bitstream);
    if (e->rc.blk)
        EPRC30_Free(&e->rc);
    free(e->qpmap);
    free(e);
}

int OpenIMP_T30_RadixRequestIDR(T30RadixEncoder *e)
{
    if (!e)
        return -1;
    e->force_idr = 1;
    return 0;
}

/* The rate-control fields of a run-time change: frame rate, GOP, RC mode,
 * bit rate, QPs and the IMPEncoderAttrRcMode extras.  Size, profile and
 * the rest stay as created. */
static void radix_rc_fields(HWEncoderParams *dst, const HWEncoderParams *src)
{
    dst->fps_num = src->fps_num;
    dst->fps_den = src->fps_den;
    dst->gop_length = src->gop_length;
    dst->rc_mode = src->rc_mode;
    dst->rc_flags = src->rc_flags;
    dst->bitrate = src->bitrate;
    dst->qp = src->qp;
    dst->min_qp = src->min_qp;
    dst->max_qp = src->max_qp;
    dst->static_time = src->static_time;
    dst->change_pos = src->change_pos;
    dst->quality_level = src->quality_level;
    dst->frm_qp_step = src->frm_qp_step;
    dst->gop_qp_step = src->gop_qp_step;
    dst->bias_level = src->bias_level;
}

int OpenIMP_T30_RadixUpdateParams(T30RadixEncoder *e,
                                  const HWEncoderParams *requested)
{
    const HWEncoderParams *cur;
    HWEncoderParams next;

    if (!e || !requested)
        return -1;
    cur = e->pending_set ? &e->pending : &e->params;
    next = *cur;
    radix_rc_fields(&next, requested);
    if (!next.fps_num || !next.fps_den) {
        next.fps_num = cur->fps_num;
        next.fps_den = cur->fps_den;
    }
    if (!next.gop_length)
        next.gop_length = cur->gop_length;
    if (!memcmp(&next, cur, sizeof(next)))
        return 0;
    /* encoder_get_fps_mask: a new frame rate or GOP goes to i265e
     * (set_param 5 / 7) together with an IDR request; an RC change
     * (SetChnAttrRcMode, set_param 3) waits for the next IDR */
    if (next.fps_num != cur->fps_num || next.fps_den != cur->fps_den ||
        next.gop_length != cur->gop_length)
        e->force_idr = 1;
    e->pending = next;
    e->pending_set = 1;
    return 0;
}

/* I265eReconfig::ReconfigIdrFrame + I265e::reset at an IDR: the parameter
 * sets for the new values (configure, levels, initVPS/SPS/PPS) and rcInit on
 * the running controller (EPRC30_Reinit). */
static void radix_apply_pending(T30RadixEncoder *e)
{
    T30HevcChannel ch;
    T30HevcVps vps;
    T30HevcSps sps;
    T30HevcPps pps;
    EprcParams ep;
    HWEncoderParams next = e->params;

    e->pending_set = 0;
    radix_rc_fields(&next, &e->pending);
    radix_channel(&next, &ch);
    t30_hevc_channel_params(&ch, &vps, &sps, &pps);
    radix_eprc_params(&next, e->width, e->height, &ep);
    if (sps.width != e->width || sps.height != e->height ||
        EPRC30_Reinit(&e->rc, &ep) != 0) {
        LOG_CODEC("T30 Radix: run-time change refused (fps %u/%u gop %u rc %u)",
                  next.fps_num, next.fps_den, next.gop_length, next.rc_mode);
        return;
    }
    e->params = next;
    e->vps = vps;
    e->sps = sps;
    e->pps = pps;
    LOG_CODEC("T30 Radix: params fps=%u/%u gop=%u rc=%u bitrate=%u qp=%u [%u,%u]",
              next.fps_num, next.fps_den, next.gop_length, next.rc_mode,
              next.bitrate, next.qp, next.min_qp, next.max_qp);
}

uint32_t OpenIMP_T30_RadixFailures(const T30RadixEncoder *e)
{
    return e ? e->failures : 0u;
}

/* the 19 statistics registers (hwicodec_pf_h265e_t30_enc: ioctl
 * 0xc0386307, node word 0 = 0x13600000 + register, word 1 the value) */
static void radix_statistics(T30RadixEncoder *e)
{
    unsigned int i;

    for (i = 0; i < 19u; i++) {
        uint32_t node[14];

        memset(node, 0, sizeof(node));
        node[0] = RADIX_REG_BASE + radix_stat_regs[i];
        if (ioctl(e->fd, RADIX_REG_READ, node) != 0) {
            if (e->reg_errors++ == 0)
                IMP_LOG_WARN("Encoder", "T30 Radix: reading 0x%08x failed (%s)",
                             node[0], strerror(errno));
            return;
        }
        e->regs[i] = node[1];
    }
}

/* codec_enc and hwicodec_pf_h265e_t30_enc: the fields the rate controller
 * does not set, and the addresses */
static void radix_picture_fields(T30RadixEncoder *e, const IMPFrameInfo *frame,
                                 int idr, uint32_t qp)
{
    uint8_t *b = e->block;
    const RadixPicture *dst = &e->pic[e->recon];
    const RadixPicture *ref = &e->pic[e->recon ^ 1u];
    uint32_t w16 = (e->width + 15u) & ~15u, h16 = (e->height + 15u) & ~15u;
    uint32_t pitch = w16;

    /* ROI: the channel's table (none: zero), not the QP map's copy */
    memset(b + RSI_ROI_INFO, 0, 16u * RSI_ROI_STRIDE);
    rsi_set_u16(b, RSI_FRAME_Y_STRIDE, (uint16_t)pitch);
    rsi_set_u16(b, RSI_FRAME_C_STRIDE, (uint16_t)pitch);
    rsi_set_u16(b, RSI_DST_Y_STRIDE, (uint16_t)e->width);
    rsi_set_u16(b, RSI_DST_C_STRIDE, (uint16_t)(e->width >> 1));
    rsi_set_u16(b, RSI_REF_Y_STRIDE, (uint16_t)e->width);
    /* codec_enc: the reference chroma stride is the frame's +0x2c as is,
     * only the reconstruction's is halved */
    rsi_set_u16(b, RSI_REF_C_STRIDE, (uint16_t)e->width);
    rsi_set_u16(b, RSI_FRAME_WIDTH, (uint16_t)e->width);
    rsi_set_u16(b, RSI_FRAME_HEIGHT, (uint16_t)e->height);
    rsi_set_u8(b, RSI_CONTEXT_TYPE, idr ? 2u : 1u);
    rsi_set_u8(b, RSI_FRAME_CQP_OFFSET, (uint8_t)e->pps.cb_qp_offset);
    rsi_set_u8(b, RSI_USE_DQP_FLAG, e->pps.cu_qp_delta_enabled);
    rsi_set_u8(b, RSI_DQP_MAX_DEPTH, (uint8_t)e->pps.diff_cu_qp_delta_depth);
    if (!e->pps.diff_cu_qp_delta_depth)
        rsi_set_u8(b, RSI_QP_MODE, 0);
    rsi_set_u32(b, RSI_STATE, (uint32_t)(uintptr_t)e->state);
    rsi_set_u8(b, RSI_RAW_FORMAT, 0);   /* NV12 */
    rsi_set_u32(b, RSI_DES_VA, e->cmdlist.virt_addr);
    rsi_set_u32(b, RSI_DES_PA, e->cmdlist.phys_addr);
    rsi_set_u32(b, RSI_RAW_Y_PA, frame->phyAddr);
    rsi_set_u32(b, RSI_RAW_C_PA, frame->phyAddr + pitch * h16);
    rsi_set_u32(b, RSI_DST_Y_PA, dst->y);
    rsi_set_u32(b, RSI_DST_C_PA, dst->c);
    rsi_set_u32(b, RSI_REF_Y_PA, idr ? 0u : ref->y);
    rsi_set_u32(b, RSI_REF_C_PA, idr ? 0u : ref->c);
    rsi_set_u32(b, RSI_BITSTREAM_PA, e->bitstream.phys_addr);
    t30_radix_cabac_init(e->state, idr ? 2 : 1, (int)qp);
}

/* I265eSlice::sliceInit for the N1X GOP (one reference, the previous
 * picture) and Entropy::codeSliceHeader; the header ends with the byte
 * alignment of sliceEncap */
static int radix_slice_header(const T30RadixEncoder *e, int idr, uint32_t qp,
                              T30HevcBits *b)
{
    T30HevcSlice s;
    /* decTypeAndRef: the POC is the picture count since the IDR */
    int32_t poc = idr ? 0 : (int32_t)e->frames_since_idr + 1;

    memset(&s, 0, sizeof(s));
    s.nal_unit_type = idr ? 19u : 1u;
    s.slice_type = idr ? 2u : 1u;
    s.poc = poc;
    s.last_idr_poc = 0;
    s.rps_idx = -1;
    if (!idr) {
        s.num_negative = 1;
        s.delta_poc[0] = -1;
        s.used[0] = 1;
        s.num_ref_idx_l0 = 1;
    }
    s.collocated_from_l0 = 1;
    s.max_num_merge_cand = 2;           /* i265e_param_default +0x1fc */
    s.slice_qp = (int32_t)qp;
    s.cb_qp_offset = e->pps.cb_qp_offset;
    s.cr_qp_offset = e->pps.cr_qp_offset;
    /* the vendor's slice_loop_filter_across_slices_enabled_flag */
    s.loop_filter_across_slices =
        ((1u << ((uint32_t)(poc / 31 + poc) & 31u)) & 0x5f4e4a53u) != 0;
    if (t30_hevc_write_slice_header(b, &s, &e->sps, &e->pps) != 0)
        return -1;
    t30_hevc_trailing(b);               /* byte_alignment() */
    return 0;
}

static size_t radix_put_nal(uint8_t *out, size_t cap, uint32_t type,
                            T30HevcBits *b)
{
    return t30_hevc_nal(out, cap, type, b->buf, b->len, 1);
}

/* VPS, SPS and PPS as I265e::getStreamHeaders */
static size_t radix_stream_headers(const T30RadixEncoder *e, uint8_t *out,
                                   size_t cap)
{
    uint8_t rbsp[RADIX_HEADER_BYTES];
    T30HevcBits b;
    T30HevcVps vps = e->vps;
    size_t n = 0, k;
    uint32_t r1;

    r1 = (uint32_t)random();
    vps.extension_bits = t30_hevc_vps_extension_bits(r1, (uint32_t)random());
    t30_hevc_bits_init(&b, rbsp, sizeof(rbsp));
    t30_hevc_write_vps(&b, &vps);
    t30_hevc_trailing(&b);
    if (!(k = radix_put_nal(out + n, cap - n, 32, &b)))
        return 0;
    n += k;
    t30_hevc_bits_init(&b, rbsp, sizeof(rbsp));
    t30_hevc_write_sps(&b, &e->sps);
    t30_hevc_trailing(&b);
    if (!(k = radix_put_nal(out + n, cap - n, 33, &b)))
        return 0;
    n += k;
    t30_hevc_bits_init(&b, rbsp, sizeof(rbsp));
    t30_hevc_write_pps(&b, &e->pps);
    t30_hevc_trailing(&b);
    if (!(k = radix_put_nal(out + n, cap - n, 34, &b)))
        return 0;
    return n + k;
}

/* finish a picture the controller started but that was not coded */
static void radix_drop(T30RadixEncoder *e)
{
    EprcPicture pic;

    (void)EPRC30_FrameEnd(&e->rc, 0, e->regs, 0, &pic, 0);
}

int OpenIMP_T30_RadixEncode(T30RadixEncoder *e, const IMPFrameInfo *frame,
                            HWStreamBuffer **stream_out)
{
    EprcPicture pic;
    HWStreamBuffer *stream = NULL;
    uint8_t *out = NULL, *rbsp = NULL;
    uint32_t w16, h16, qp, len = 0, reencodes = 0;
    size_t cap, n = 0, words, k;
    T30HevcBits b;
    int idr;

    if (!e || !frame || !stream_out || !frame->phyAddr)
        return -1;
    w16 = (e->width + 15u) & ~15u;
    h16 = (e->height + 15u) & ~15u;
    if (frame->size && frame->size < w16 * e->height + w16 * e->height / 2u) {
        e->failures++;
        return -1;
    }
    /* decTypeAndRef: an IDR every GOP, or on request */
    idr = e->force_idr || !e->have_reference ||
          e->frames_since_idr + 1u >= e->params.gop_length;
    /* I265e::reconfig: latched changes apply on an IDR only */
    if (idr && e->pending_set)
        radix_apply_pending(e);
    if (EPRC30_FrameStart(&e->rc, idr, &pic) != 0) {
        e->failures++;
        return -1;
    }
    qp = e->block[RSI_FRAME_QP];        /* = pic.qp, the QP the block codes */
    if (frame->virAddr)
        (void)DMA_RmemFlushCache((void *)(uintptr_t)frame->virAddr,
                                 w16 * h16 * 3u / 2u, 1);
    for (;;) {
        radix_picture_fields(e, frame, idr, qp);
        words = t30_radix_build_cmdlist(e->block,
                                        (uint32_t *)(uintptr_t)e->cmdlist.virt_addr,
                                        e->state, (const uint32_t *)e->qpmap);
        if (words * 4u > RADIX_CMDLIST_BYTES ||
            DMA_RmemFlushCache((void *)(uintptr_t)e->cmdlist.virt_addr,
                               (uint32_t)(words * 4u), 1) != 0) {
            radix_drop(e);
            e->failures++;
            return -1;
        }
        e->channel.vpu_id = (int32_t)RADIX_VPU_ID;
        e->channel.codecdir = RADIX_CODEC_H265_ENC;
        e->channel.dma_addr = e->cmdlist.phys_addr;
        e->channel.thread_id = -1;
        e->channel.output_len = 0;
        pthread_mutex_lock(&radix_run_lock);
        if (ioctl(e->fd, RADIX_CHANNEL_RUN, &e->channel) != 0 ||
            !e->channel.output_len ||
            e->channel.output_len > e->bitstream.size) {
            pthread_mutex_unlock(&radix_run_lock);
            LOG_CODEC("T30 Radix: run failed errno=%d status=0x%08x len=%u",
                      errno, e->channel.status, e->channel.output_len);
            radix_drop(e);
            e->failures++;
            return -1;
        }
        radix_statistics(e);
        pthread_mutex_unlock(&radix_run_lock);
        len = e->channel.output_len;
        if (EPRC30_FrameEnd(&e->rc, len, e->regs, 0, &pic,
                            reencodes < RADIX_MAX_REENCODES) != 1)
            break;
        reencodes++;
        qp = e->block[RSI_FRAME_QP];
    }
    (void)DMA_RmemFlushCache((void *)(uintptr_t)e->bitstream.virt_addr, len, 2);

    cap = 3u * RADIX_HEADER_BYTES + 64u + (size_t)len * 3u / 2u + 16u;
    out = malloc(cap);
    rbsp = malloc(RADIX_HEADER_BYTES + (size_t)len);
    stream = calloc(1, sizeof(*stream));
    if (!out || !rbsp || !stream)
        goto fail;
    if (idr && !(n = radix_stream_headers(e, out, cap)))
        goto fail;
    t30_hevc_bits_init(&b, rbsp, RADIX_HEADER_BYTES);
    if (radix_slice_header(e, idr, qp, &b) != 0 || b.overflow)
        goto fail;
    memcpy(rbsp + b.len, (const void *)(uintptr_t)e->bitstream.virt_addr, len);
    if (!(k = t30_hevc_nal(out + n, cap - n, idr ? 19u : 1u, rbsp, b.len + len, 1)))
        goto fail;
    n += k;
    free(rbsp);
    stream->virt_addr = (uint32_t)(uintptr_t)out;
    stream->length = (uint32_t)n;
    stream->timestamp = frame->timeStamp;
    stream->frame_type = idr ? HW_FRAME_TYPE_I : HW_FRAME_TYPE_P;
    stream->slice_type = stream->frame_type;
    *stream_out = stream;
    /* the reconstruction is the next picture's reference */
    e->recon ^= 1u;
    e->have_reference = 1;
    e->frames_since_idr = idr ? 0u : e->frames_since_idr + 1u;
    e->force_idr = 0;
    e->failures = 0;
    return 0;
fail:
    free(out);
    free(rbsp);
    free(stream);
    e->failures++;
    return -1;
}
