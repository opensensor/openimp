/*
 * Native T30 Helix H.264 encoder.
 *
 * The descriptor generator and AVC syntax helpers are derived from Ingenic's
 * GPL-2.0 Helix sources.  This wrapper deliberately uses the legacy T30
 * /dev/soc_vpu ABI directly; no V4L2 codec node or proprietary libimp is
 * involved.
 *
 * T21 and T23 share one Helix generation ("T21 family": the T21 command
 * list, fixed quantisation matrices and High-profile PPS).  T23 differs in
 * its Helix bus address (handled by the command-list builder), a longer
 * soc_vpu channel_node, the bitstream-full interrupt its kernel enables,
 * and a few safety checks that keep a bad frame from reaching the VPU.
 */

#include "t30_helix_encoder.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "dma_alloc.h"
#include "imp_log_int.h"
#include "t30/h264enc/common.h"
#if (defined(PLATFORM_T21) && !defined(PLATFORM_T20)) || \
    defined(PLATFORM_T23)
/* T21-family command list, PPS and SPS (T21, T23). */
#define HELIX_T21_SYNTAX 1
#endif
#if defined(PLATFORM_T21) || defined(PLATFORM_T23)
/* 16 KiB command list, fixed bitstream window, reconstruction invalidate
 * (T20, T21, T23). */
#define HELIX_SMALL_WINDOWS 1
#endif

#if defined(HELIX_T21_SYNTAX)
#include "t21/t21_h264_descriptor.h"
typedef T21H264SliceConfig PlatformH264SliceConfig;
#else
#include "t30/t30_h264_descriptor.h"
typedef T30H264SliceConfig PlatformH264SliceConfig;
#endif
#include "t40/t31_rate_control.h"

#if defined(PLATFORM_T23)
/* _IOWR('c', n, struct channel_node) with the 88-byte T23 node */
#define T30_CHANNEL_REQUEST 0xc0586300u
#define T30_CHANNEL_RELEASE 0xc0586301u
#define T30_CHANNEL_RUN     0xc0586302u
#else
#define T30_CHANNEL_REQUEST 0xc0386300u
#define T30_CHANNEL_RELEASE 0xc0386301u
#define T30_CHANNEL_RUN     0xc0386302u
#endif

#if defined(PLATFORM_T20)
/* Stock T20 libimp passes RANDOM_ID (-1) and lets soc_vpu select/open the
 * pre-Helix JZ NVPU.  Preserve that ABI exactly; the later Helix selector is
 * not registered on T20. */
#define T30_HELIX_H264_CORE 0xffffffffu
#else
#define T30_HELIX_H264_CORE 0x02000001u
#endif
#define T30_H264_ENCODE      0u
#define T30_CHANNEL_OPEN     0u
#define T30_CHANNEL_CLOSE    2u
#if defined(PLATFORM_T23)
/* soc_vpu uses mdelay for the channel wait, the VPU wait and the encode
 * completion wait; on a timeout it resets the Helix core.  A 1080p picture
 * takes tens of milliseconds, so fail (and reset) after 2 s rather than
 * the OEM's 20 s.  OPENIMP_T23_HELIX_TIMEOUT_MS overrides it. */
#define T30_CHANNEL_DELAY_MS 2000u
#else
#define T30_CHANNEL_DELAY_MS 20000u
#endif
#if defined(HELIX_SMALL_WINDOWS)
#define T30_DESCRIPTOR_WINDOW (1u << 14)
/* T23 sizes its bitstream window per picture (OpenIMP_T30_HelixCreate) */
#define T30_BITSTREAM_WINDOW  (1u << 20)
#else
#define T30_DESCRIPTOR_WINDOW (1u << 20)
#endif
#define T30_EMC_SIZE          (1u << 21)
#define T30_DBLK_SIZE         (1u << 20)
#define T30_RECON_SIZE        (1u << 18)
#define T30_MV_SIZE           (1u << 11)
#define T30_SE_SIZE           (1u << 11)
#define T30_QPT_SIZE          (1u << 14)
#define T30_RC_SIZE           (1u << 15)
#define T30_CPX_SIZE          (1u << 17)
#define T30_MOD_SIZE          (1u << 15)
#define T30_SAD_SIZE          (1u << 17)
#define T30_SLICE_OFFSET      256u
#define T30_HEADER_CAPACITY   4096u
#define T30_NV12_MODE         8u

#if defined(HELIX_T21_SYNTAX)
/* T21 Helix programs fixed 4x4/8x8 quantization matrices in its VDMA list.
 * Advertise those same matrices in the PPS so a decoder interprets the
 * transform flags and inverse quantization exactly as the hardware does. */
static const uint8_t t21_high_profile_pps[] = {
    0x00, 0x00, 0x00, 0x01, 0x68, 0xee, 0x3c, 0xe1,
    0x00, 0x42, 0x42, 0x00, 0x84, 0x84, 0x04, 0x4c,
    0x52, 0x1b, 0x93, 0xc5, 0x7c, 0x9f, 0x93, 0xf9,
    0x3f, 0x27, 0xc9, 0xe6, 0xe4, 0xc9, 0x24, 0x2c,
    0x22, 0x42, 0x90, 0x9c, 0x9e, 0x4f, 0xaf, 0xc9,
    0xfd, 0x7e, 0x4f, 0xaf, 0x27, 0x26, 0xa4, 0xc0,
};
#endif

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
#if defined(PLATFORM_T23)
    /* CONFIG_SOC_T23 extension.  frame_type 3 selects the ISP-VPU direct
     * connection; OpenIMP always encodes complete frames from memory and
     * leaves these zero. */
    uint32_t frame_type;
    uint32_t overflow_cnt;
    uint32_t ivdc_mem_line;
    uint32_t data_threshold;
    uint32_t max_bs_act;
    uint32_t reserved;
    uint64_t time;
#endif
} T30ChannelNode;

#if defined(PLATFORM_T23)
_Static_assert(sizeof(T30ChannelNode) == 88,
               "T23 soc_vpu channel ABI mismatch");
_Static_assert(__builtin_offsetof(T30ChannelNode, time) == 80,
               "T23 soc_vpu channel time offset");

/* Helix SCH_STAT bits (soc_vpu helix.h) */
#define T23_SCH_STAT_ENDFLAG (1u << 0)
#define T23_SCH_STAT_ACFGERR (1u << 2)
#define T23_SCH_STAT_BSERR   (1u << 7)
#define T23_SCH_STAT_ORESERR (1u << 10)
#define T23_SCH_STAT_BSFULL  (1u << 20)
#define T23_SCH_STAT_ERRORS  (T23_SCH_STAT_ACFGERR | T23_SCH_STAT_BSERR | \
                              T23_SCH_STAT_ORESERR | T23_SCH_STAT_BSFULL)
#define T23_NV12_FOURCC      0x3231564eu
#else
_Static_assert(sizeof(T30ChannelNode) == 56,
               "T30 soc_vpu channel ABI mismatch");
#endif

typedef struct {
    IMPDMABufferInfo dma;
    uint32_t y;
    uint32_t c;
} T30ReferenceFrame;

struct T30HelixEncoder {
    int fd;
    T30ChannelNode channel;
    HWEncoderParams params;
    IMPDMABufferInfo descriptor;
    IMPDMABufferInfo emc;
    IMPDMABufferInfo temporary;
    T30ReferenceFrame reference[2];
    PlatformH264SliceConfig slice;
    h264_sps_t sps;
    h264_pps_t pps;
    h264_slice_header_t slice_header;
    h264_cabac_t cabac;
    uint8_t headers[T30_HEADER_CAPACITY];
    uint32_t headers_size;
    uint32_t frame_number;
    uint32_t gop_position;
    uint32_t idr_pic_id;
    unsigned int reference_index;
    int have_reference;
    int force_idr;
    OpenIMPT31RateController rate_control;
    int rate_control_enabled;
#if defined(PLATFORM_T23)
    uint32_t failures;          /* consecutive failed pictures */
    uint32_t input_size;        /* NV12 bytes the VPU reads per frame */
    uint32_t scratch_offset[4]; /* EMC per-macroblock buffer layout */
    uint32_t scratch_size;
    uint32_t bitstream_kib;     /* EMC bitstream window (0x30040) */
#endif
};

static void t30_dma_release(IMPDMABufferInfo *dma)
{
    if (dma && dma->phys_addr) {
        DMA_FreePhys(dma->phys_addr);
        memset(dma, 0, sizeof(*dma));
    }
}

static int t30_dma_allocate(IMPDMABufferInfo *dma, uint32_t size,
                            const char *tag)
{
#if defined(PLATFORM_T23)
    /* Long-lived encoder buffers go to the top of the reserved arena, away
     * from the FrameSource pools that are freed and re-created from the
     * bottom whenever a channel idles. */
    if (size > INT32_MAX ||
        DMA_AllocDescriptorTop(dma, (int)size, tag) != 0 ||
#else
    if (size > INT32_MAX || DMA_AllocDescriptor(dma, (int)size, tag) != 0 ||
#endif
        !dma->phys_addr || !dma->virt_addr) {
        LOG_CODEC("T30 Helix: DMA allocation failed tag=%s size=%u", tag,
                  size);
        return -1;
    }
    memset((void *)(uintptr_t)dma->virt_addr, 0, size);
    return 0;
}

static uint32_t t30_level_for_size(uint32_t width, uint32_t height)
{
    uint64_t macroblocks = ((uint64_t)width + 15u) / 16u;

    macroblocks *= ((uint64_t)height + 15u) / 16u;
    return macroblocks > 3600u ? 40u : 31u;
}

static void t30_init_parameter_sets(T30HelixEncoder *encoder)
{
    h264_sps_t *sps = &encoder->sps;
    h264_pps_t *pps = &encoder->pps;
    uint32_t aligned_width = (encoder->params.width + 15u) & ~15u;
    uint32_t aligned_height = (encoder->params.height + 15u) & ~15u;

    memset(sps, 0, sizeof(*sps));
    sps->i_id = 0;
    sps->i_profile_idc = PROFILE_HIGH;
    sps->i_level_idc = (int)t30_level_for_size(encoder->params.width,
                                                encoder->params.height);
    sps->i_log2_max_frame_num = 10;
    sps->i_poc_type = 2;
#if defined(HELIX_T21_SYNTAX)
    sps->i_num_ref_frames = 2;
    sps->b_gaps_in_frame_num_value_allowed = 1;
    sps->b_vui = 1;
    sps->vui.b_timing_info_present = 1;
    sps->vui.i_num_units_in_tick = encoder->params.fps_den
        ? encoder->params.fps_den : 1u;
    sps->vui.i_time_scale = (encoder->params.fps_num
        ? encoder->params.fps_num : 25u) * 2u;
    sps->vui.b_fixed_frame_rate = 1;
#else
    sps->i_num_ref_frames = 1;
#endif
    sps->i_mb_width = (int)(aligned_width / 16u);
    sps->i_mb_height = (int)(aligned_height / 16u);
    sps->b_frame_mbs_only = 1;
    sps->b_direct8x8_inference = 1;
    sps->i_chroma_format_idc = CHROMA_420;
    if (aligned_width != encoder->params.width ||
        aligned_height != encoder->params.height) {
        sps->b_crop = 1;
        sps->crop.i_right = (int)(aligned_width - encoder->params.width);
        sps->crop.i_bottom = (int)(aligned_height - encoder->params.height);
    }

    memset(pps, 0, sizeof(*pps));
    pps->i_id = 0;
    pps->i_sps_id = 0;
    pps->b_cabac = 1;
    pps->i_num_slice_groups = 1;
    pps->i_num_ref_idx_l0_default_active = 1;
    pps->i_num_ref_idx_l1_default_active = 1;
    pps->i_pic_init_qp = 26;
    pps->i_pic_init_qs = 26;
    pps->b_deblocking_filter_control = 1;
#if defined(HELIX_T21_SYNTAX)
    pps->b_transform_8x8_mode = 1;
#endif
}

static int t30_annexb_nal(bs_t *bits, uint8_t *destination,
                          uint32_t capacity, int type, int priority)
{
    uint8_t *source = bits->p_start;
    uint8_t *end = source + (uint32_t)bs_pos(bits) / 8u;
    uint8_t *output = destination;

    if (capacity < 5u)
        return -1;
    *output++ = 0;
    *output++ = 0;
    *output++ = 0;
    *output++ = 1;
    *output++ = (uint8_t)((priority << 5) | type);
    while (source < end) {
        if ((uint32_t)(output - destination) >= capacity)
            return -1;
        if (source[0] <= 3u && output - destination >= 2 &&
            output[-2] == 0u && output[-1] == 0u) {
            if ((uint32_t)(output - destination) >= capacity)
                return -1;
            *output++ = 3u;
        }
        *output++ = *source++;
    }
    return (int)(output - destination);
}

static int t30_generate_headers(T30HelixEncoder *encoder)
{
    uint8_t temporary[512];
    bs_t bits;
    int length;

    memset(temporary, 0, sizeof(temporary));
    encoder->headers_size = 0;
    bs_init(&bits, temporary, sizeof(temporary));
    h264e_sps_write(&bits, &encoder->sps);
    length = t30_annexb_nal(&bits, encoder->headers,
                            sizeof(encoder->headers), NAL_SPS,
                            NAL_PRIORITY_HIGHEST);
    if (length < 0)
        return -1;
    encoder->headers_size = (uint32_t)length;

#if defined(HELIX_T21_SYNTAX)
    if (sizeof(t21_high_profile_pps) >
        sizeof(encoder->headers) - encoder->headers_size)
        return -1;
    memcpy(encoder->headers + encoder->headers_size,
           t21_high_profile_pps, sizeof(t21_high_profile_pps));
    encoder->headers_size += sizeof(t21_high_profile_pps);
#else
    memset(temporary, 0, sizeof(temporary));
    bs_init(&bits, temporary, sizeof(temporary));
    h264e_pps_write(&bits, &encoder->sps, &encoder->pps);
    length = t30_annexb_nal(&bits,
                            encoder->headers + encoder->headers_size,
                            sizeof(encoder->headers) - encoder->headers_size,
                            NAL_PPS, NAL_PRIORITY_HIGHEST);
    if (length < 0)
        return -1;
    encoder->headers_size += (uint32_t)length;
#endif
    return 0;
}

static void t30_fill_slice(T30HelixEncoder *encoder,
                           const IMPFrameInfo *frame, uint32_t qp,
                           int idr, unsigned int output_index)
{
    PlatformH264SliceConfig *slice = &encoder->slice;

    memset(slice, 0, sizeof(*slice));
    slice->slice_type = idr ? 0u : 1u;
    slice->mb_width = (uint8_t)encoder->sps.i_mb_width;
    slice->mb_height = (uint8_t)encoder->sps.i_mb_height;
    slice->first_mby = 0;
    slice->last_mby = (uint8_t)(slice->mb_height - 1u);
    slice->qp = (uint8_t)qp;
    slice->width = (uint16_t)encoder->params.width;
    slice->height = (uint16_t)encoder->params.height;
    slice->cabac_state = encoder->cabac.state;
    slice->raw_format = T30_NV12_MODE;
    slice->stride[0] = (int)encoder->params.width;
    slice->stride[1] = (int)encoder->params.width;
    slice->raw[0] = frame->phyAddr;
    /* T30 FrameSource lays NV12 chroma after the macroblock-aligned luma
     * plane, not immediately after the visible-height plane. */
    slice->raw[1] = frame->phyAddr +
                    (uint32_t)encoder->sps.i_mb_width * 16u *
                    (uint32_t)encoder->sps.i_mb_height * 16u;
    slice->raw[2] = 0;
    if (!idr) {
        slice->reference_y = encoder->reference[encoder->reference_index].y;
        slice->reference_c = encoder->reference[encoder->reference_index].c;
    }
#if defined(PLATFORM_T23)
    else {
        /* An IDR does not predict, but the T21-family list still programs
         * reference addresses into the VDMA/recon blocks.  Point them at
         * the idle reconstruction buffer instead of physical address 0. */
        slice->reference_y = encoder->reference[output_index ^ 1u].y;
        slice->reference_c = encoder->reference[output_index ^ 1u].c;
    }
#endif
    slice->output_y = encoder->reference[output_index].y;
    slice->output_c = encoder->reference[output_index].c;
    slice->bitstream = encoder->temporary.phys_addr + T30_SLICE_OFFSET;
    slice->descriptor = (uint32_t *)(uintptr_t)encoder->descriptor.virt_addr;
    slice->descriptor_words = encoder->descriptor.size / sizeof(uint32_t);
#if defined(HELIX_T21_SYNTAX)
    slice->scratch_base = encoder->emc.phys_addr;
#if defined(PLATFORM_T23)
    memcpy(slice->scratch_offset, encoder->scratch_offset,
           sizeof(slice->scratch_offset));
    slice->bitstream_kib = encoder->bitstream_kib;
#endif
#else
    /* SDK 1.0.5 selects the alternate DCS threshold for its substream. */
    slice->dcs_oth = encoder->params.width <= 640u ? 1u : 0u;
#endif
}

int OpenIMP_T30_HelixCreate(T30HelixEncoder **encoder_out,
                            const HWEncoderParams *params)
{
    T30HelixEncoder *encoder;
    uint64_t frame_size;
    uint64_t aligned_luma_size;
    uint64_t reference_size;
    unsigned int i;

    if (!encoder_out || !params || !params->width || !params->height)
        return -1;
    frame_size = (uint64_t)params->width * params->height;
    if (frame_size > UINT32_MAX / 2u)
        return -1;
    aligned_luma_size = (((uint64_t)params->width + 15u) & ~15ull) *
                        (((uint64_t)params->height + 15u) & ~15ull);
    reference_size = aligned_luma_size + aligned_luma_size / 2u;
    if (aligned_luma_size > UINT32_MAX || reference_size > UINT32_MAX)
        return -1;
    encoder = calloc(1, sizeof(*encoder));
    if (!encoder)
        return -1;
    encoder->fd = -1;
    encoder->params = *params;
    if (!encoder->params.gop_length)
        encoder->params.gop_length = 25;
    if (!encoder->params.qp || encoder->params.qp > 51u)
        encoder->params.qp = 28;
    if (!encoder->params.min_qp || encoder->params.min_qp > 51u)
        encoder->params.min_qp = 18;
    if (!encoder->params.max_qp || encoder->params.max_qp > 51u)
        encoder->params.max_qp = 45;

#if defined(PLATFORM_T23)
    encoder->input_size = (uint32_t)(aligned_luma_size +
                                     aligned_luma_size / 2u);
#endif
    encoder->fd = open("/dev/soc_vpu", O_RDWR | O_CLOEXEC);
    if (encoder->fd < 0)
        goto fail;
    memset(&encoder->channel, 0, sizeof(encoder->channel));
    encoder->channel.mdelay = T30_CHANNEL_DELAY_MS;
#if defined(PLATFORM_T23)
    {
        const char *timeout = getenv("OPENIMP_T23_HELIX_TIMEOUT_MS");
        unsigned long value = timeout ? strtoul(timeout, NULL, 0) : 0ul;

        if (value >= 100ul && value <= 20000ul)
            encoder->channel.mdelay = (uint32_t)value;
    }
#endif
    encoder->channel.thread_id = -1;
    if (ioctl(encoder->fd, T30_CHANNEL_REQUEST, &encoder->channel) != 0)
        goto fail;
#if defined(PLATFORM_T23)
    /* Size the per-channel buffers for the picture: EMC per-macroblock
     * scratch, and a bitstream window of one raw picture (an all-I_PCM
     * picture fits; at least 256 KiB, at most the 1 MiB of the T21
     * layout). */
    encoder->scratch_size = T23_HelixScratchLayout(
        (params->width + 15u) / 16u, (params->height + 15u) / 16u,
        encoder->scratch_offset);
    {
        uint64_t window = (reference_size + 0xffffu) &
                          ~(uint64_t)0xffffu;

        if (window < (256u << 10))
            window = 256u << 10;
        if (window > (1u << 20))
            window = 1u << 20;
        encoder->bitstream_kib = (uint32_t)(window >> 10);
    }
    for (i = 0; i < 2u; i++) {
        if (t30_dma_allocate(&encoder->reference[i].dma,
                             (uint32_t)reference_size,
                             "t23-helix-ref") != 0)
            goto fail;
        encoder->reference[i].y = encoder->reference[i].dma.phys_addr;
        encoder->reference[i].c = encoder->reference[i].y +
                                  (uint32_t)aligned_luma_size;
    }
    /* the window starts at the 128-byte aligned slice data; a page of
     * slack keeps even a full window inside the allocation */
    if (t30_dma_allocate(&encoder->temporary,
                         (encoder->bitstream_kib << 10) + 4096u,
                         "t23-helix-bs") != 0 ||
        t30_dma_allocate(&encoder->emc, encoder->scratch_size,
                         "t23-helix-emc") != 0 ||
        t30_dma_allocate(&encoder->descriptor, T30_DESCRIPTOR_WINDOW,
                         "t23-helix-desc") != 0)
        goto fail;
#else
    if (t30_dma_allocate(&encoder->descriptor, T30_DESCRIPTOR_WINDOW,
                         "t30-helix-desc") != 0 ||
        t30_dma_allocate(&encoder->emc, T30_EMC_SIZE,
                         "t30-helix-emc") != 0 ||
#if defined(HELIX_SMALL_WINDOWS)
        t30_dma_allocate(&encoder->temporary, T30_BITSTREAM_WINDOW,
#else
        t30_dma_allocate(&encoder->temporary, (uint32_t)frame_size * 2u,
#endif
                         "t30-helix-bs") != 0)
        goto fail;
    for (i = 0; i < 2u; i++) {
        if (t30_dma_allocate(&encoder->reference[i].dma,
                             (uint32_t)reference_size,
                             "t30-helix-ref") != 0)
            goto fail;
        encoder->reference[i].y = encoder->reference[i].dma.phys_addr;
        encoder->reference[i].c = encoder->reference[i].y +
                                  (uint32_t)aligned_luma_size;
    }
#endif
    t30_init_parameter_sets(encoder);
    h264_cabac_init();
    if (t30_generate_headers(encoder) != 0)
        goto fail;
    if (encoder->params.rc_mode != HW_RC_MODE_FIXQP &&
        encoder->params.bitrate && encoder->params.fps_num &&
        encoder->params.fps_den &&
        openimp_t31_rate_controller_init(
            &encoder->rate_control, encoder->params.bitrate,
            encoder->params.fps_num, encoder->params.fps_den,
            encoder->params.gop_length, encoder->params.min_qp,
            encoder->params.max_qp, encoder->params.qp) == 0)
        encoder->rate_control_enabled = 1;
    encoder->force_idr = 1;
    *encoder_out = encoder;
    LOG_CODEC("T30 Helix: native encoder ready channel=%u %ux%u desc=0x%08x",
              encoder->channel.channel_id, params->width, params->height,
              encoder->descriptor.phys_addr);
#if defined(PLATFORM_T23)
    IMP_LOG_INFO("Encoder", "T23 Helix: native encoder ready channel=%u "
                 "%ux%u rc=%u bitrate=%u fps=%u/%u gop=%u qp=%u [%u,%u] "
                 "desc=0x%08x emc=0x%08x/%uK bs=0x%08x/%uK "
                 "ref=0x%08x/0x%08x timeout=%ums",
                 encoder->channel.channel_id,
                 params->width, params->height, encoder->params.rc_mode,
                 encoder->params.bitrate, encoder->params.fps_num,
                 encoder->params.fps_den, encoder->params.gop_length,
                 encoder->params.qp, encoder->params.min_qp,
                 encoder->params.max_qp, encoder->descriptor.phys_addr,
                 encoder->emc.phys_addr, encoder->scratch_size >> 10,
                 encoder->temporary.phys_addr, encoder->bitstream_kib,
                 encoder->reference[0].y, encoder->reference[1].y,
                 encoder->channel.mdelay);
#endif
    return 0;

fail:
    LOG_CODEC("T30 Helix: create failed: %s", strerror(errno));
#if defined(PLATFORM_T23)
    IMP_LOG_ERR("Encoder", "T23 Helix: native encoder create failed for "
                "%ux%u: %s", params->width, params->height, strerror(errno));
#endif
    OpenIMP_T30_HelixDestroy(encoder);
    return -1;
}

int OpenIMP_T30_HelixEncode(T30HelixEncoder *encoder,
                            const IMPFrameInfo *frame,
                            HWStreamBuffer **stream_out)
{
    uint8_t *temporary;
    uint8_t *output;
    HWStreamBuffer *stream;
    bs_t bits;
    uint32_t capacity;
    uint32_t offset = 0;
    uint32_t qp;
    unsigned int output_index;
    int idr;
    int nal_length;
    size_t descriptor_pairs;

    if (!encoder || !frame || !stream_out || !frame->phyAddr)
        return -1;
#if defined(PLATFORM_T23)
    /* The VPU reads input by physical address.  Only hand it a frame whose
     * address is the reserved-memory translation of its mapping and that
     * holds a complete macroblock-aligned NV12 picture; anything else is
     * dropped here instead of becoming a DMA from arbitrary memory. */
    if (!frame->virAddr || frame->size < encoder->input_size ||
        DMA_VirtToPhys((const void *)(uintptr_t)frame->virAddr) !=
            frame->phyAddr) {
        IMP_LOG_ERR("Encoder", "T23 Helix: rejecting frame phys=0x%08x virt=0x%08x "
                  "size=%u (need %u in reserved memory)", frame->phyAddr,
                  frame->virAddr, frame->size, encoder->input_size);
        return -1;
    }
    if (frame->pixfmt && frame->pixfmt != T23_NV12_FOURCC &&
        encoder->frame_number == 0u)
        IMP_LOG_WARN("Encoder", "T23 Helix: frame format 0x%08x is not NV12; encoding "
                  "it as NV12", frame->pixfmt);
    /* Write back what the CPU drew into the frame (OSD) before DMA. */
    if (DMA_RmemFlushCache((void *)(uintptr_t)frame->virAddr,
                           encoder->input_size, 1) != 0) {
        IMP_LOG_ERR("Encoder", "T23 Helix: input flush failed");
        return -1;
    }
#endif
    idr = encoder->force_idr || !encoder->have_reference ||
          encoder->gop_position >= encoder->params.gop_length;
    encoder->force_idr = 0;
    if (idr)
        encoder->gop_position = 0;
    qp = encoder->rate_control_enabled
        ? openimp_t31_rate_controller_qp(&encoder->rate_control)
        : encoder->params.qp;
    output_index = encoder->have_reference
        ? (encoder->reference_index ^ 1u) : 0u;

    h264e_slice_header_init(&encoder->slice_header, &encoder->sps,
                            &encoder->pps,
                            idr ? (int)(encoder->idr_pic_id++ & 1u) : -1,
                            idr ? 0 : (int)(encoder->gop_position & 1023u),
                            (int)qp);
    encoder->slice_header.i_type = idr ? SLICE_TYPE_I : SLICE_TYPE_P;
    encoder->slice_header.i_disable_deblocking_filter_idc = 0;
    temporary = (uint8_t *)(uintptr_t)encoder->temporary.virt_addr;
    memset(temporary, 0, T30_SLICE_OFFSET);
    bs_init(&bits, temporary, T30_SLICE_OFFSET);
    h264e_slice_header_write(&bits, &encoder->slice_header,
                             idr ? NAL_PRIORITY_HIGHEST : NAL_PRIORITY_HIGH);
    bs_align_1(&bits);
    h264_cabac_context_init(&encoder->cabac,
                            encoder->slice_header.i_type,
                            (int)qp,
                            encoder->slice_header.i_cabac_init_idc);

    t30_fill_slice(encoder, frame, qp, idr, output_index);
#if defined(HELIX_T21_SYNTAX)
    if (T21_H264_BuildDescriptor(&encoder->slice,
                                 &descriptor_pairs) != 0) {
#else
    if (T30_H264_BuildDescriptor(&encoder->slice,
                                 &descriptor_pairs) != 0) {
#endif
        LOG_CODEC("T30 Helix: descriptor build failed: %s",
                  strerror(errno));
        return -1;
    }
    /* /dev/rmem is a cached mapping.  Publish the CPU-built command list
     * before the VPU fetches it, and discard the allocator's initial dirty
     * zero lines from the hardware-output window before DMA begins.  Doing
     * the latter only after RUN is too late: an intervening cache eviction
     * can overwrite freshly encoded CABAC bytes with stale zeros. */
    if (DMA_RmemFlushCache(
            (void *)(uintptr_t)encoder->descriptor.virt_addr,
            (uint32_t)(descriptor_pairs * 2u * sizeof(uint32_t)), 1) != 0 ||
        DMA_RmemFlushCache(temporary + T30_SLICE_OFFSET,
                           encoder->temporary.size - T30_SLICE_OFFSET,
                           2) != 0
#if defined(HELIX_SMALL_WINDOWS)
        || DMA_RmemFlushCache(
               (void *)(uintptr_t)encoder->reference[output_index].dma.virt_addr,
               encoder->reference[output_index].dma.size, 2) != 0
#endif
       ) {
        LOG_CODEC("T30 Helix: DMA prepare failed: %s", strerror(errno));
        return -1;
    }
    encoder->channel.vpu_id = (int32_t)T30_HELIX_H264_CORE;
    encoder->channel.codecdir = T30_H264_ENCODE;
    encoder->channel.dma_addr = encoder->descriptor.phys_addr;
    encoder->channel.thread_id = -1;
#if defined(PLATFORM_T23)
    encoder->channel.status = 0;
    encoder->channel.output_len = 0;
    encoder->channel.frame_type = 0;
    encoder->channel.time = 0;
    if (ioctl(encoder->fd, T30_CHANNEL_RUN, &encoder->channel) != 0 ||
        !(encoder->channel.status & T23_SCH_STAT_ENDFLAG) ||
        (encoder->channel.status & T23_SCH_STAT_ERRORS) ||
        !encoder->channel.output_len ||
        encoder->channel.output_len > (encoder->bitstream_kib << 10)) {
        /* A timed-out, errored or bitstream-full picture leaves no usable
         * reconstruction: restart the GOP.  soc_vpu has already reset the
         * core on a timeout and resets it again before the next job. */
        IMP_LOG_ERR("Encoder", "T23 Helix: run failed frame=%u errno=%d status=0x%08x "
                  "len=%u", encoder->frame_number, errno,
                  encoder->channel.status, encoder->channel.output_len);
        encoder->force_idr = 1;
        encoder->have_reference = 0;
        encoder->failures++;
        return -1;
    }
    encoder->failures = 0;
#else
    if (ioctl(encoder->fd, T30_CHANNEL_RUN, &encoder->channel) != 0 ||
        !encoder->channel.output_len ||
        encoder->channel.output_len > encoder->temporary.size - T30_SLICE_OFFSET) {
        LOG_CODEC("T30 Helix: run failed frame=%u errno=%d status=0x%08x len=%u",
                  encoder->frame_number, errno, encoder->channel.status,
                  encoder->channel.output_len);
        return -1;
    }
#endif
    DMA_RmemFlushCache(temporary + T30_SLICE_OFFSET,
                       encoder->channel.output_len, 2);
    if (bits.i_left != 32)
        return -1;
    memcpy(bits.p, temporary + T30_SLICE_OFFSET,
           encoder->channel.output_len);
    bits.p += encoder->channel.output_len;

#if defined(PLATFORM_T23)
    /* Emulation prevention adds at most one byte per two payload bytes;
     * size the access unit for that instead of twice the window (RAM is
     * scarce on T23). */
    capacity = T30_HEADER_CAPACITY + 16u +
               ((uint32_t)bs_pos(&bits) / 8u) * 3u / 2u;
#else
    capacity = encoder->temporary.size * 2u + T30_HEADER_CAPACITY;
#endif
    output = malloc(capacity);
    stream = calloc(1, sizeof(*stream));
    if (!output || !stream) {
        free(output);
        free(stream);
        return -1;
    }
    if (idr) {
        memcpy(output, encoder->headers, encoder->headers_size);
        offset = encoder->headers_size;
    }
    nal_length = t30_annexb_nal(&bits, output + offset, capacity - offset,
                                idr ? NAL_SLICE_IDR : NAL_SLICE,
                                idr ? NAL_PRIORITY_HIGHEST :
                                      NAL_PRIORITY_HIGH);
    if (nal_length < 0) {
        free(output);
        free(stream);
        return -1;
    }
    stream->virt_addr = (uint32_t)(uintptr_t)output;
    stream->length = offset + (uint32_t)nal_length;
    stream->timestamp = frame->timeStamp;
    stream->frame_type = idr ? HW_FRAME_TYPE_I : HW_FRAME_TYPE_P;
    stream->slice_type = stream->frame_type;
    *stream_out = stream;
    encoder->reference_index = output_index;
    encoder->have_reference = 1;
    encoder->gop_position++;
    encoder->frame_number++;
    if (encoder->rate_control_enabled)
        (void)openimp_t31_rate_controller_complete(
            &encoder->rate_control, stream->length * 8u, qp, idr);
#if defined(PLATFORM_T23)
    if (encoder->frame_number <= 3u)
        IMP_LOG_INFO("Encoder", "T23 Helix: %ux%u frame=%u %s bytes=%u "
                     "qp=%u status=0x%08x pairs=%u", encoder->params.width,
                     encoder->params.height, encoder->frame_number,
                     idr ? "IDR" : "P", stream->length, qp,
                     encoder->channel.status,
                     (unsigned int)descriptor_pairs);
#endif
    if (encoder->frame_number <= 4u ||
        (encoder->frame_number % 100u) == 0u)
        LOG_CODEC("T30 Helix: frame=%u %s bytes=%u hw=%u status=0x%08x desc=%u",
                  encoder->frame_number, idr ? "IDR" : "P",
                  stream->length, encoder->channel.output_len,
                  encoder->channel.status, (unsigned int)descriptor_pairs);
    return 0;
}

#if defined(PLATFORM_T23)
static int t23_rate_control_restart(T30HelixEncoder *encoder)
{
    uint32_t qp = encoder->rate_control_enabled
        ? openimp_t31_rate_controller_qp(&encoder->rate_control)
        : encoder->params.qp;

    encoder->rate_control_enabled = 0;
    if (qp < encoder->params.min_qp)
        qp = encoder->params.min_qp;
    if (qp > encoder->params.max_qp)
        qp = encoder->params.max_qp;
    if (encoder->params.rc_mode != HW_RC_MODE_FIXQP &&
        encoder->params.bitrate && encoder->params.fps_num &&
        encoder->params.fps_den &&
        openimp_t31_rate_controller_init(
            &encoder->rate_control, encoder->params.bitrate,
            encoder->params.fps_num, encoder->params.fps_den,
            encoder->params.gop_length, encoder->params.min_qp,
            encoder->params.max_qp, qp) == 0)
        encoder->rate_control_enabled = 1;
    return 0;
}

int OpenIMP_T30_HelixReconfigure(T30HelixEncoder *encoder,
                                 const HWEncoderParams *params)
{
    HWEncoderParams next;
    int restart = 0;

    if (!encoder || !params)
        return -1;
    next = encoder->params;
    if (params->gop_length)
        next.gop_length = params->gop_length;
    if (params->fps_num && params->fps_den) {
        next.fps_num = params->fps_num;
        next.fps_den = params->fps_den;
    }
    if (params->rc_mode <= HW_RC_MODE_VBR)
        next.rc_mode = params->rc_mode;
    if (params->bitrate)
        next.bitrate = params->bitrate;
    if (params->qp && params->qp <= 51u)
        next.qp = params->qp;
    if (params->min_qp && params->min_qp <= 51u)
        next.min_qp = params->min_qp;
    if (params->max_qp && params->max_qp <= 51u)
        next.max_qp = params->max_qp;
    if (next.min_qp > next.max_qp)
        next.min_qp = next.max_qp;

    if (next.gop_length == encoder->params.gop_length &&
        next.fps_num == encoder->params.fps_num &&
        next.fps_den == encoder->params.fps_den &&
        next.rc_mode == encoder->params.rc_mode &&
        next.bitrate == encoder->params.bitrate &&
        next.qp == encoder->params.qp &&
        next.min_qp == encoder->params.min_qp &&
        next.max_qp == encoder->params.max_qp)
        return 0;

    if (next.bitrate != encoder->params.bitrate &&
        next.gop_length == encoder->params.gop_length &&
        next.fps_num == encoder->params.fps_num &&
        next.fps_den == encoder->params.fps_den &&
        next.rc_mode == encoder->params.rc_mode &&
        next.min_qp == encoder->params.min_qp &&
        next.max_qp == encoder->params.max_qp &&
        encoder->rate_control_enabled) {
        /* bitrate only: keep the scene model */
        if (openimp_t31_rate_controller_set_bitrate(&encoder->rate_control,
                                                    next.bitrate) != 0)
            return -1;
    } else {
        restart = 1;
    }
    IMP_LOG_INFO("Encoder", "T23 Helix: %ux%u reconfigured rc=%u bitrate=%u fps=%u/%u "
              "gop=%u qp=%u [%u,%u]", encoder->params.width,
              encoder->params.height, next.rc_mode, next.bitrate,
              next.fps_num, next.fps_den, next.gop_length, next.qp,
              next.min_qp, next.max_qp);
    if (next.fps_num != encoder->params.fps_num ||
        next.fps_den != encoder->params.fps_den) {
        /* The SPS carries the frame rate (VUI timing): send new parameter
         * sets with the next picture, which must then be an IDR. */
        encoder->params.fps_num = next.fps_num;
        encoder->params.fps_den = next.fps_den;
        t30_init_parameter_sets(encoder);
        if (t30_generate_headers(encoder) != 0)
            return -1;
        encoder->force_idr = 1;
    }
    encoder->params.gop_length = next.gop_length;
    encoder->params.rc_mode = next.rc_mode;
    encoder->params.bitrate = next.bitrate;
    encoder->params.qp = next.qp;
    encoder->params.min_qp = next.min_qp;
    encoder->params.max_qp = next.max_qp;
    if (restart)
        return t23_rate_control_restart(encoder);
    return 0;
}

uint32_t OpenIMP_T30_HelixFailures(const T30HelixEncoder *encoder)
{
    return encoder ? encoder->failures : 0u;
}
#endif

int OpenIMP_T30_HelixRequestIDR(T30HelixEncoder *encoder)
{
    if (!encoder)
        return -1;
    encoder->force_idr = 1;
    return 0;
}

int OpenIMP_T30_HelixSetBitrate(T30HelixEncoder *encoder,
                                uint32_t bitrate)
{
    if (!encoder || !bitrate)
        return -1;

    if (encoder->params.rc_mode != HW_RC_MODE_FIXQP) {
        if (encoder->rate_control_enabled) {
            if (openimp_t31_rate_controller_set_bitrate(
                    &encoder->rate_control, bitrate) != 0)
                return -1;
        } else if (encoder->params.fps_num && encoder->params.fps_den &&
                   openimp_t31_rate_controller_init(
                       &encoder->rate_control, bitrate,
                       encoder->params.fps_num, encoder->params.fps_den,
                       encoder->params.gop_length, encoder->params.min_qp,
                       encoder->params.max_qp, encoder->params.qp) == 0) {
            encoder->rate_control_enabled = 1;
        } else {
            return -1;
        }
    }

    encoder->params.bitrate = bitrate;
    return 0;
}

void OpenIMP_T30_HelixDestroy(T30HelixEncoder *encoder)
{
    unsigned int i;
    if (!encoder)
        return;
    if (encoder->fd >= 0 && encoder->channel.clist) {
        encoder->channel.workphase = T30_CHANNEL_CLOSE;
        (void)ioctl(encoder->fd, T30_CHANNEL_RELEASE, &encoder->channel);
    }
    if (encoder->fd >= 0)
        close(encoder->fd);
    for (i = 0; i < 2u; i++)
        t30_dma_release(&encoder->reference[i].dma);
    t30_dma_release(&encoder->temporary);
    t30_dma_release(&encoder->emc);
    t30_dma_release(&encoder->descriptor);
    free(encoder);
}
