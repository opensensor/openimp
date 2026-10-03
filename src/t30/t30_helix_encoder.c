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
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "dma_alloc.h"
#include "imp_log_int.h"
#include "t30/h264enc/common.h"
#include "t30/t30_annexb.h"
#include "t30/t30_h264_level.h"
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

#if defined(HELIX_T21_SYNTAX) && !defined(PLATFORM_T23)
/* T21: one bitstream buffer for all channels and JPEG, like the stock
 * "vpuBs" (src/t30/helix_bitstream.h). */
#define HELIX_SHARED_BITSTREAM 1
#include "t30/helix_bitstream.h"
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
/* The longest command list (T21 P slice) is 2060 words, about 8 KiB. */
#define T30_DESCRIPTOR_WINDOW (1u << 14)
#if defined(HELIX_SMALL_WINDOWS)
/* T20's window; T21 and T23 size theirs per picture
 * (OpenIMP_T30_HelixCreate) */
#define T30_BITSTREAM_WINDOW  (1u << 20)
#endif
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
/* A completed job reads 0x301 (ENDFLAG plus the per-unit done bits 8 and
 * 9).  The kernel's interrupt handler clears the SDE and deblocker done
 * flags, which drops ENDFLAG and bit 9 from SCH_STAT; when the interrupt
 * handler runs a second time for the same job before the encoding thread
 * wakes (the soc_vpu handler re-reads SCH_STAT and takes its "error" branch
 * for a status without ENDFLAG), it overwrites the saved status with that
 * residue, 0x100, and zeroes the length, which vpu_wait_complete then
 * re-reads from REG_SDE_CFG9.  The ioctl only returns success after the
 * job's ENDFLAG interrupt has completed it, so such a result is a finished
 * picture.  The OEM encoder ignores the status word altogether and uses
 * output_len. */
#define T23_SCH_STAT_LATE    (1u << 8)
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

#if defined(PLATFORM_T23)
/* The Helix rate-control extras of the application's IMPEncoderAttrRcMode
 * (HWEncoderParams with HW_RC_FLAG_APP), mapped onto the native GOP-level
 * controller.  All zero - no extras, FIXQP, or values outside the OEM
 * ranges - is the historic native behaviour, unchanged.
 *
 * Vendor-verified (T23 1.3.0 libimp: IMP_Encoder_YuvInit,
 * i264e_param_default, i264e_ratecontrol_init): the accepted ranges
 * (staticTime 1..60 s, changePos 50..100, qualityLvl 0..7, iBiasLvl -3..3,
 * SMART -10..10), frmQPStep -> u8MaxPPQpDelta (P to P), gopQPStep ->
 * u8MaxIPQpDelta (I to P), iBiasLvl -> s8IQpBias, SMART configured like VBR
 * (same JZ_VPU_RC fields), adaptiveMode/gopRelation not part of the rate
 * control configuration.  How the OEM controller uses them inside is not
 * reverse engineered; the mapping below follows the SDK header text:
 *   iBiasLvl    I-picture QP = P QP + bias (negative: more bits for I)
 *   frmQPStep   max QP change from one P picture to the next
 *   gopQPStep   max QP change of an IDR against the last P picture
 *   changePos   VBR/SMART: the controller targets changePos% of
 *               maxBitRate and raises QP above it
 *   qualityLvl  VBR: lower QP again below maxBitRate * (80 - 10 * lvl)%
 *               (the header's minBitRate); SMART 0..6, higher = better:
 *               below maxBitRate * (20 + 10 * lvl)%
 *   staticTime  VBR/SMART: the bitrate must stay over (under) the band for
 *               staticTime seconds of GOPs (twice that) before QP moves */
typedef struct {
    uint32_t target_bitrate;    /* controller target, bit/s */
    uint32_t lower_percent;     /* controller band, 0: built-in */
    uint32_t raise_percent;
    uint32_t over_gops;
    uint32_t under_gops;
    uint32_t frm_step;          /* 0: unlimited */
    uint32_t gop_step;
    int32_t bias;
    int smart;
    int app;
} T23RcConfig;

typedef struct {
    uint32_t seconds;           /* report interval, 0: off */
    uint32_t frames;
    uint32_t idr_frames;
    uint32_t qp_min[2];         /* [0] P, [1] IDR */
    uint32_t qp_max[2];
    uint64_t qp_sum[2];
    uint64_t bytes;
} T23RcStats;
#endif

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
    uint32_t max_output_len;
    uint32_t gop_position;
    uint32_t idr_pic_id;
    unsigned int reference_index;
    int have_reference;
    int force_idr;
    OpenIMPT31RateController rate_control;
    int rate_control_enabled;
#if defined(PLATFORM_T20)
    int t10;                    /* T10 NVPU: T10 command list, padded refs */
#endif
#if defined(HELIX_T21_SYNTAX)
    uint32_t scratch_offset[4]; /* EMC per-macroblock buffer layout */
    uint32_t scratch_size;
    uint32_t bitstream_kib;     /* EMC bitstream window (0x30040) */
#endif
#if defined(PLATFORM_T23)
    uint32_t failures;          /* consecutive failed pictures */
    uint32_t input_size;        /* NV12 bytes the VPU reads per frame */
    uint32_t retries;           /* jobs repeated after an odd result */
    uint32_t late_status;       /* completed jobs with status 0x100 */
    int strict_status;          /* OPENIMP_T23_HELIX_STRICT_STATUS=1 */
    int bsf_stop;               /* core pauses at the window (t23_bsf_stop) */
    T23RcConfig rc;             /* the application's RC extras, mapped */
    uint32_t last_p_qp;         /* QP of the last P picture */
    int have_p_qp;
    T23RcStats stats;           /* OPENIMP_T23_RC_STATS window */
    /* Bitstream-window overflow recovery (t23_overflow_*): the QP added
     * to P [0] and IDR [1] pictures after an overflow, and the run of
     * small pictures since the last step back down. */
    uint32_t ovf_boost[2];
    uint32_t ovf_quiet[2];
    uint32_t overflows;         /* pictures dropped on overflow */
    uint32_t canary_hits;       /* overflows that wrote past the window */
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
    /* Long-lived encoder buffers (kept from the first picture or channel
     * creation to DestroyChn) go to the top of the reserved arena, away
     * from the FrameSource pools that are freed and re-created from the
     * bottom whenever a channel idles; mixed in between them, a pool's
     * hole would be cut up and its re-creation could fail. */
    if (size > INT32_MAX ||
        DMA_AllocDescriptorTop(dma, (int)size, tag) != 0 ||
        !dma->phys_addr || !dma->virt_addr) {
        LOG_CODEC("T30 Helix: DMA allocation failed tag=%s size=%u", tag,
                  size);
        return -1;
    }
    memset((void *)(uintptr_t)dma->virt_addr, 0, size);
    /* /dev/rmem is a cached mapping.  Write the zeroed lines back now: a
     * dirty line evicted later would overwrite whatever the VPU has written
     * there meanwhile.  Afterwards the CPU never writes the VPU-owned
     * buffers, so no per-picture cleaning of them is needed. */
    if (DMA_RmemFlushCache((void *)(uintptr_t)dma->virt_addr, size, 1) != 0) {
        LOG_CODEC("T30 Helix: DMA writeback failed tag=%s size=%u", tag,
                  size);
        t30_dma_release(dma);
        return -1;
    }
    return 0;
}

#if defined(HELIX_SHARED_BITSTREAM)
/* Shared-buffer bytes one picture needs: the CPU-written slice header area
 * and the window behind it, with a page of slack so even a full window
 * stays inside the buffer. */
static uint32_t t30_bitstream_bytes(const T30HelixEncoder *encoder)
{
    return (encoder->bitstream_kib << 10) + 4096u;
}
#endif

static void t30_init_parameter_sets(T30HelixEncoder *encoder)
{
    h264_sps_t *sps = &encoder->sps;
    h264_pps_t *pps = &encoder->pps;
    uint32_t aligned_width = (encoder->params.width + 15u) & ~15u;
    uint32_t aligned_height = (encoder->params.height + 15u) & ~15u;

    memset(sps, 0, sizeof(*sps));
    sps->i_id = 0;
    sps->i_profile_idc = PROFILE_HIGH;
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
    sps->i_level_idc = (int)t30_h264_level(
        encoder->params.width, encoder->params.height,
        encoder->params.fps_num, encoder->params.fps_den,
        encoder->params.bitrate, (uint32_t)sps->i_num_ref_frames);
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
    T30AnnexBWriter writer;

    if (t30_annexb_begin(&writer, destination, capacity, type,
                         priority) != 0 ||
        t30_annexb_append(&writer, bits->p_start,
                          (uint32_t)bs_pos(bits) / 8u) != 0)
        return -1;
    return (int)(writer.output - destination);
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

#if defined(PLATFORM_T20)
/* The stock T10/T20 libimp is one binary that picks its T10 or T20 slice
 * programming from the SoC id (get_cpu_id: 0x1300002c, family 1, id 5 is
 * T10, id 0x2000 is T20).  OPENIMP_HELIX_SOC=t10|t20 overrides the probe. */
static int t30_soc_is_t10(void)
{
    const char *forced = getenv("OPENIMP_HELIX_SOC");
    volatile uint32_t *regs;
    uint32_t soc_id;
    long page;
    int fd;

    if (forced && (forced[0] == 't' || forced[0] == 'T')) {
        if (!strcmp(forced + 1, "10"))
            return 1;
        if (!strcmp(forced + 1, "20"))
            return 0;
    }
    page = sysconf(_SC_PAGESIZE);
    if (page <= 0)
        page = 4096;
    fd = open("/dev/mem", O_RDONLY | O_SYNC | O_CLOEXEC);
    if (fd < 0)
        return 0;
    regs = mmap(NULL, (size_t)page, PROT_READ, MAP_SHARED, fd,
                (off_t)(0x1300002cu & ~((uint32_t)page - 1u)));
    close(fd);
    if (regs == MAP_FAILED)
        return 0;
    soc_id = regs[(0x1300002cu & ((uint32_t)page - 1u)) / 4u];
    munmap((void *)regs, (size_t)page);
    return (soc_id >> 28) == 1u && ((soc_id >> 12) & 0xffffu) == 5u;
}
#endif

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
    memcpy(slice->scratch_offset, encoder->scratch_offset,
           sizeof(slice->scratch_offset));
    slice->bitstream_kib = encoder->bitstream_kib;
#if defined(PLATFORM_T23)
    slice->bsf_stop = (uint8_t)(encoder->bsf_stop != 0);
#endif
#else
    /* SDK 1.0.5 selects the alternate DCS threshold for its substream. */
    slice->dcs_oth = encoder->params.width <= 640u ? 1u : 0u;
#endif
}

/* Fill unset or out-of-range rate-control fields from `fallback`, then from
 * the encoder defaults. */
static void t30_normalize_params(HWEncoderParams *params,
                                 const HWEncoderParams *fallback)
{
    uint32_t swap;

    if (!params->fps_num || !params->fps_den) {
        params->fps_num = fallback ? fallback->fps_num : 0u;
        params->fps_den = fallback ? fallback->fps_den : 0u;
    }
    if (!params->gop_length)
        params->gop_length = fallback ? fallback->gop_length : 25u;
    if (!params->bitrate && fallback)
        params->bitrate = fallback->bitrate;
    if (!params->qp || params->qp > 51u)
        params->qp = fallback ? fallback->qp : 28u;
    if (!params->min_qp || params->min_qp > 51u)
        params->min_qp = fallback ? fallback->min_qp : 18u;
    if (!params->max_qp || params->max_qp > 51u)
        params->max_qp = fallback ? fallback->max_qp : 45u;
    if (params->min_qp > params->max_qp) {
        swap = params->min_qp;
        params->min_qp = params->max_qp;
        params->max_qp = swap;
    }
}

#if defined(PLATFORM_T23)
static void t23_rc_config(const HWEncoderParams *params, T23RcConfig *rc)
{
    uint32_t change_pos = 0;
    uint32_t floor_percent = 0;
    int32_t bias_limit;

    memset(rc, 0, sizeof(*rc));
    rc->target_bitrate = params->bitrate;
    if (!(params->rc_flags & HW_RC_FLAG_APP) ||
        (params->rc_mode != HW_RC_MODE_CBR &&
         params->rc_mode != HW_RC_MODE_VBR))
        return;
    rc->app = 1;
    rc->smart = params->rc_mode == HW_RC_MODE_VBR &&
                (params->rc_flags & HW_RC_FLAG_SMART);
    rc->frm_step = params->frm_qp_step > 51u ? 51u : params->frm_qp_step;
    rc->gop_step = params->gop_qp_step > 51u ? 51u : params->gop_qp_step;
    bias_limit = rc->smart ? 10 : 3;
    if (params->bias_level >= -bias_limit && params->bias_level <= bias_limit)
        rc->bias = params->bias_level;
    if (params->rc_mode != HW_RC_MODE_VBR)
        return;

    if (params->change_pos >= 50u && params->change_pos <= 100u) {
        change_pos = params->change_pos;
        rc->target_bitrate = (uint32_t)(((uint64_t)params->bitrate *
                                         change_pos) / 100u);
        rc->raise_percent = 100u;
    }
    if (rc->smart ? params->quality_level <= 6u
                  : params->quality_level <= 7u)
        floor_percent = rc->smart ? 20u + 10u * params->quality_level
                                  : 80u - 10u * params->quality_level;
    if (floor_percent) {
        uint32_t raise = rc->raise_percent ? rc->raise_percent : 110u;
        uint32_t lower = floor_percent * 100u /
                         (change_pos ? change_pos : 100u);

        /* keep a dead band between lowering and raising QP */
        if (lower + 10u > raise)
            lower = raise - 10u;
        if (lower < 5u)
            lower = 5u;
        rc->lower_percent = lower;
    }
    if (params->static_time >= 1u && params->static_time <= 60u &&
        params->fps_num && params->fps_den && params->gop_length) {
        uint64_t frames = ((uint64_t)params->static_time *
                               params->fps_num + params->fps_den / 2u) /
                          params->fps_den;
        uint64_t gops = (frames + params->gop_length / 2u) /
                        params->gop_length;

        if (gops < 1u)
            gops = 1u;
        if (gops > 10u)
            gops = 10u;
        rc->over_gops = (uint32_t)gops;
        rc->under_gops = 2u * (uint32_t)gops;
    }
}

static void t23_rc_apply_band(T30HelixEncoder *encoder)
{
    if (encoder->rate_control_enabled)
        (void)openimp_t31_rate_controller_set_band(
            &encoder->rate_control, encoder->rc.lower_percent,
            encoder->rc.raise_percent, encoder->rc.over_gops,
            encoder->rc.under_gops);
}

static const char *t23_rc_mode_name(const T30HelixEncoder *encoder)
{
    switch (encoder->params.rc_mode) {
    case HW_RC_MODE_FIXQP:
        return "FIXQP";
    case HW_RC_MODE_CBR:
        return "CBR";
    default:
        return encoder->rc.smart ? "SMART" : "VBR";
    }
}

/* One line with the rate control in effect, at creation and whenever the
 * rate-control parameters change. */
static void t23_rc_log(const T30HelixEncoder *encoder, const char *what)
{
    const HWEncoderParams *p = &encoder->params;
    const T23RcConfig *rc = &encoder->rc;

    IMP_LOG_INFO("Encoder", "T23 Helix rc %s: %s max=%u target=%u bit/s "
                 "qp=[%u,%u] band=%u-%u%% persist=%u/%u GOPs "
                 "frmQPStep=%u gopQPStep=%u iBias=%d loop=%d app=%d "
                 "staticTime=%u changePos=%u qualityLvl=%u (adaptive=%u "
                 "gopRelation=%u: no effect)", what, t23_rc_mode_name(encoder),
                 p->bitrate, rc->target_bitrate, p->min_qp, p->max_qp,
                 rc->lower_percent ? rc->lower_percent : 80u,
                 rc->raise_percent ? rc->raise_percent : 110u,
                 rc->over_gops ? rc->over_gops : 3u,
                 rc->under_gops ? rc->under_gops : 6u,
                 rc->frm_step, rc->gop_step, rc->bias,
                 encoder->rate_control_enabled, rc->app,
                 p->static_time, p->change_pos, p->quality_level,
                 (p->rc_flags & HW_RC_FLAG_ADAPTIVE) != 0,
                 (p->rc_flags & HW_RC_FLAG_GOP_RELATION) != 0);
}

/* The last word of the bitstream allocation (behind the window and most of
 * its slack page).  The window size (0x30040) does not stop the core: with
 * the BSFULL interrupt off (soc_vpu enables it only for the ISP-direct
 * mode) it finishes the picture (status 0x301) and writes the bitstream
 * linearly past the window, into the allocation above it - on vorne
 * (sc2336 1080p) up to 1.79 MB for a 1 MiB window, i.e. 0.75 MB into the
 * second reference buffer.  The canary tells such a spill apart when the
 * length cannot be trusted. */
#define T23_BS_CANARY 0x5a17c0deu

/* Kernel marker of thingino patch 0098: the Helix driver then reports a
 * BSFULL-only stop with this job's length and resets the core before the
 * VPU is handed back.  Older kernels report the previous job's length and
 * leave the core paused mid-picture. */
#define T23_BSF_KERNEL_MARKER "/sys/module/helix/parameters/bsf_stop"

/* Whether to let the core stop at the window end (BSFULL interrupt,
 * 0x30000 bit 19) instead of spilling past it.  OPENIMP_T23_HELIX_BSF:
 * unset or 0 = off (default), 1 = on if the kernel has the marker,
 * "force" = on without the marker (device tests only). */
static int t23_bsf_stop(void)
{
    const char *value = getenv("OPENIMP_T23_HELIX_BSF");
    char marker[4] = {0};
    int fd;
    ssize_t got;

    if (!value || strcmp(value, "1") != 0)
        return value && strcmp(value, "force") == 0;
    fd = open(T23_BSF_KERNEL_MARKER, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        IMP_LOG_WARN("Encoder", "T23 Helix: OPENIMP_T23_HELIX_BSF=1 ignored, "
                     "kernel without %s", T23_BSF_KERNEL_MARKER);
        return 0;
    }
    got = read(fd, marker, sizeof(marker) - 1u);
    close(fd);
    if (got <= 0 || marker[0] != '1') {
        IMP_LOG_WARN("Encoder", "T23 Helix: OPENIMP_T23_HELIX_BSF=1 ignored, "
                     "%s is not 1", T23_BSF_KERNEL_MARKER);
        return 0;
    }
    return 1;
}

static volatile uint32_t *t23_canary(const T30HelixEncoder *encoder)
{
    return (volatile uint32_t *)(uintptr_t)(encoder->temporary.virt_addr +
                                            encoder->temporary.size - 4u);
}

static void t23_canary_arm(T30HelixEncoder *encoder)
{
    *t23_canary(encoder) = T23_BS_CANARY;
    (void)DMA_RmemFlushCache((void *)t23_canary(encoder), 4u, 1);
}

/* Overflow recovery steps, in QP: +4 per overflowed picture (about 0.6x
 * the bits), back down by one after a run of pictures that used less than
 * half the window - every such IDR, every 8th such P picture - so a scene
 * that keeps the encoder near the limit does not oscillate. */
#define T23_OVF_STEP        4u
#define T23_OVF_QUIET_P     8u

/* The window is full when the core says so (BSFULL: the length is then
 * whatever the kernel had left over - the current soc_vpu reports the
 * previous job's - and is never used) or when a finished picture reports
 * a length that reaches the window end. */
static int t23_overflowed(const T30HelixEncoder *encoder, uint32_t status,
                          uint32_t length)
{
    if (status & T23_SCH_STAT_BSFULL)
        return 1;
    return (status & (T23_SCH_STAT_ENDFLAG | T23_SCH_STAT_LATE)) &&
           length >= (encoder->bitstream_kib << 10);
}

/* The picture's QP with the overflow boost added, at most 51. */
static uint32_t t23_overflow_qp(const T30HelixEncoder *encoder, uint32_t qp,
                                int idr)
{
    qp += encoder->ovf_boost[idr ? 1 : 0];
    return qp > 51u ? 51u : qp;
}

/* A finished picture: step the boost back down after a quiet run. */
static void t23_overflow_settle(T30HelixEncoder *encoder, int idr,
                                uint32_t length)
{
    unsigned int k = idr ? 1u : 0u;

    if (!encoder->ovf_boost[k])
        return;
    if (length >= (encoder->bitstream_kib << 9)) {
        encoder->ovf_quiet[k] = 0;
        return;
    }
    if (++encoder->ovf_quiet[k] < (idr ? 1u : T23_OVF_QUIET_P))
        return;
    encoder->ovf_quiet[k] = 0;
    encoder->ovf_boost[k]--;
    if (!encoder->ovf_boost[k])
        IMP_LOG_INFO("Encoder", "T23 Helix: %ux%u %s pictures back at "
                     "the configured QP after bitstream overflows",
                     encoder->params.width, encoder->params.height,
                     idr ? "IDR" : "P");
}

/* Whether the bitstream of an overflowed picture reached the reference the
 * next P picture predicts from: the spill is [allocation end, window start
 * + length), the whole rest of reserved memory when the length is unknown
 * (BSFULL) and the canary was hit. */
static int t23_overflow_hit_reference(const T30HelixEncoder *encoder,
                                      uint32_t status, uint32_t length,
                                      int past)
{
    const IMPDMABufferInfo *ref;
    uint64_t spill_start = (uint64_t)encoder->temporary.phys_addr +
                           encoder->temporary.size;
    uint64_t spill_end;

    if (!encoder->have_reference || !past)
        return 0;
    ref = &encoder->reference[encoder->reference_index].dma;
    if (status & T23_SCH_STAT_BSFULL)
        spill_end = UINT64_MAX;
    else
        spill_end = (uint64_t)encoder->temporary.phys_addr +
                    T30_SLICE_OFFSET + length;
    return spill_end > ref->phys_addr &&
           spill_start < (uint64_t)ref->phys_addr + ref->size;
}

/* An overflowed picture is dropped, like a picture that never reached the
 * encoder: nothing is committed, so a P picture leaves the reference chain
 * and frame_num intact (the next P predicts from the same reference, no
 * IDR, which would be the largest picture of all), and an IDR stays due.
 * Only when the spill (see T23_BS_CANARY) overwrote that reference does
 * the GOP restart with an IDR.
 * The next picture of that type is encoded with a higher QP.  Returns 1
 * when the boost could still rise (the drop is recoverable), 0 at QP 51. */
static int t23_overflow_drop(T30HelixEncoder *encoder, int idr, uint32_t qp,
                             uint32_t status, uint32_t length)
{
    unsigned int k = idr ? 1u : 0u;
    int recoverable = qp < 51u;
    int past;
    int hit;

    (void)DMA_RmemFlushCache((void *)t23_canary(encoder), 4u, 2);
    past = *t23_canary(encoder) != T23_BS_CANARY;
    if (past) {
        encoder->canary_hits++;
        t23_canary_arm(encoder);
    }
    encoder->overflows++;
    encoder->ovf_quiet[k] = 0;
    if (recoverable) {
        encoder->ovf_boost[k] += T23_OVF_STEP;
        if (encoder->ovf_boost[k] > 51u)
            encoder->ovf_boost[k] = 51u;
    }
    hit = t23_overflow_hit_reference(encoder, status, length, past);
    if (hit) {
        /* the IDR that replaces the reference starts at least as high
         * as the P pictures that overflowed */
        encoder->force_idr = 1;
        encoder->have_reference = 0;
        if (encoder->ovf_boost[1] < encoder->ovf_boost[0])
            encoder->ovf_boost[1] = encoder->ovf_boost[0];
    }
    if (encoder->overflows <= 20u || encoder->overflows % 100u == 0u)
        IMP_LOG_WARN("Encoder", "T23 Helix: %ux%u frame=%u %s qp=%u "
                     "overflowed the %uK bitstream window (status=0x%08x "
                     "len=%u%s%s%s), dropped; next %s qp +%u (%u "
                     "overflows)",
                     encoder->params.width, encoder->params.height,
                     encoder->frame_number, idr ? "IDR" : "P", qp,
                     encoder->bitstream_kib, status, length,
                     (status & T23_SCH_STAT_BSFULL) ? " BSFULL, length not "
                         "trusted" : "",
                     past ? ", written past the window" : "",
                     hit ? " into the reference: IDR next" : "",
                     hit ? "IDR" : idr ? "IDR" : "P",
                     encoder->ovf_boost[hit ? 1 : k],
                     encoder->overflows);
    return recoverable;
}

/* The picture's QP from the controller's GOP QP: the I bias, then the
 * application's QP step limits against the last P picture.  Without
 * extras this returns `qp` unchanged. */
static uint32_t t23_rc_picture_qp(const T30HelixEncoder *encoder,
                                  uint32_t qp, int idr)
{
    const T23RcConfig *rc = &encoder->rc;
    int32_t value = (int32_t)qp;
    uint32_t step;

    if (idr)
        value += rc->bias;
    step = idr ? rc->gop_step : rc->frm_step;
    if (step && encoder->have_p_qp) {
        int32_t last = (int32_t)encoder->last_p_qp;

        if (value > last + (int32_t)step)
            value = last + (int32_t)step;
        if (value < last - (int32_t)step)
            value = last - (int32_t)step;
    }
    if (value == (int32_t)qp)
        return qp;
    if (value < (int32_t)encoder->params.min_qp)
        value = (int32_t)encoder->params.min_qp;
    if (value > (int32_t)encoder->params.max_qp)
        value = (int32_t)encoder->params.max_qp;
    return (uint32_t)value;
}

/* OPENIMP_T23_RC_STATS=<seconds>: per window, the delivered bitrate and the
 * QPs used for IDR and P pictures. */
static void t23_rc_stats(T30HelixEncoder *encoder, uint32_t qp, int idr,
                         uint32_t bytes)
{
    T23RcStats *st = &encoder->stats;
    unsigned int k = idr ? 1u : 0u;
    uint64_t kbps;

    if (!st->seconds || !encoder->params.fps_den)
        return;
    if (!st->frames) {
        st->qp_min[0] = st->qp_min[1] = 51u;
        st->qp_max[0] = st->qp_max[1] = 0u;
        st->qp_sum[0] = st->qp_sum[1] = 0u;
        st->idr_frames = 0u;
        st->bytes = 0u;
    }
    st->frames++;
    st->idr_frames += k;
    st->bytes += bytes;
    st->qp_sum[k] += qp;
    if (qp < st->qp_min[k])
        st->qp_min[k] = qp;
    if (qp > st->qp_max[k])
        st->qp_max[k] = qp;
    if ((uint64_t)st->frames * encoder->params.fps_den <
        (uint64_t)st->seconds * encoder->params.fps_num)
        return;
    kbps = st->bytes * 8u * encoder->params.fps_num /
           ((uint64_t)st->frames * encoder->params.fps_den * 1000u);
    IMP_LOG_INFO("Encoder", "T23 Helix rc stats: %ux%u %s %u frames "
                 "%llu kbit/s (max %u, target %u) gop_qp=%u "
                 "P qp avg %u [%u,%u] IDR %u qp avg %u [%u,%u]",
                 encoder->params.width, encoder->params.height,
                 t23_rc_mode_name(encoder), st->frames,
                 (unsigned long long)kbps, encoder->params.bitrate / 1000u,
                 encoder->rc.target_bitrate / 1000u,
                 encoder->rate_control_enabled
                     ? openimp_t31_rate_controller_qp(&encoder->rate_control)
                     : encoder->params.qp,
                 st->frames > st->idr_frames
                     ? (uint32_t)(st->qp_sum[0] /
                                  (st->frames - st->idr_frames)) : 0u,
                 st->frames > st->idr_frames ? st->qp_min[0] : 0u,
                 st->qp_max[0], st->idr_frames,
                 st->idr_frames
                     ? (uint32_t)(st->qp_sum[1] / st->idr_frames) : 0u,
                 st->idr_frames ? st->qp_min[1] : 0u, st->qp_max[1]);
    st->frames = 0u;
}
#endif

/* The controller's bitrate target: on T23 the application's VBR changePos
 * share of maxBitRate (t23_rc_config), else the bitrate itself. */
#if defined(PLATFORM_T23)
#define T30_RC_TARGET(encoder) ((encoder)->rc.target_bitrate)
#else
#define T30_RC_TARGET(encoder) ((encoder)->params.bitrate)
#endif

static void t30_start_rate_control(T30HelixEncoder *encoder,
                                   uint32_t initial_qp)
{
    encoder->rate_control_enabled =
        encoder->params.rc_mode != HW_RC_MODE_FIXQP &&
        T30_RC_TARGET(encoder) && encoder->params.fps_num &&
        encoder->params.fps_den &&
        openimp_t31_rate_controller_init(
            &encoder->rate_control, T30_RC_TARGET(encoder),
            encoder->params.fps_num, encoder->params.fps_den,
            encoder->params.gop_length, encoder->params.min_qp,
            encoder->params.max_qp, initial_qp) == 0;
#if defined(PLATFORM_T23)
    t23_rc_apply_band(encoder);
#endif
}

/* One /dev/soc_vpu descriptor for all native encoders, opened on first use
 * and kept until the process exits.  close() runs the kernel's
 * release-on-close (soc_vpu.c soc_channel_vpu_release), which picks the VPUs
 * to release by the closing thread's id without a lock: it can hand back and
 * power down the VPU while Helix JPEG or another encoder of this process has
 * a job on it, and that job's own release then puts the VPU on the free list
 * a second time (a self-linked list and a hard hang in soc_vpu_request).
 * The descriptor carries no state; channels are requested and released per
 * encoder. */
static pthread_mutex_t t30_vpu_fd_lock = PTHREAD_MUTEX_INITIALIZER;
static int t30_vpu_fd_shared = -1;

static int t30_vpu_fd(void)
{
    int fd;

    pthread_mutex_lock(&t30_vpu_fd_lock);
    if (t30_vpu_fd_shared < 0)
        t30_vpu_fd_shared = open("/dev/soc_vpu", O_RDWR | O_CLOEXEC);
    fd = t30_vpu_fd_shared;
    pthread_mutex_unlock(&t30_vpu_fd_lock);
    return fd;
}

int OpenIMP_T30_HelixCreate(T30HelixEncoder **encoder_out,
                            const HWEncoderParams *params)
{
    T30HelixEncoder *encoder;
    uint64_t frame_size;
    uint64_t aligned_luma_size;
    uint64_t reference_size;
    unsigned int i;
#if defined(PLATFORM_T23)
    int window_forced = 0;   /* OPENIMP_T23_HELIX_BS_KIB set */
#endif

    /* The descriptors carry macroblock dimensions in eight bits.  The
     * slice programs the luma stride as the picture width but places the
     * chroma plane at mb_width * 16 * mb_height * 16, and the bottom
     * padding lays it out at width * aligned_height: both only agree when
     * the width is a whole number of macroblocks. */
    if (!encoder_out || !params || !params->width || !params->height ||
        (params->width & 15u) ||
        params->width > 255u * 16u || params->height > 255u * 16u)
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
    t30_normalize_params(&encoder->params, NULL);
#if defined(PLATFORM_T23)
    t23_rc_config(&encoder->params, &encoder->rc);
    {
        const char *stats = getenv("OPENIMP_T23_RC_STATS");
        unsigned long seconds = stats ? strtoul(stats, NULL, 0) : 0ul;

        if (seconds <= 3600ul)
            encoder->stats.seconds = (uint32_t)seconds;
    }
#endif

#if defined(PLATFORM_T23)
    encoder->input_size = (uint32_t)(aligned_luma_size +
                                     aligned_luma_size / 2u);
#endif
    encoder->fd = t30_vpu_fd();
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
    {
        /* =1: also require ENDFLAG (retry once, then drop and restart the
         * GOP), for comparing against the default on a device */
        const char *strict = getenv("OPENIMP_T23_HELIX_STRICT_STATUS");

        encoder->strict_status = strict && strict[0] == '1';
    }
    encoder->bsf_stop = t23_bsf_stop();
#endif
    encoder->channel.thread_id = -1;
    if (ioctl(encoder->fd, T30_CHANNEL_REQUEST, &encoder->channel) != 0)
        goto fail;
#if defined(HELIX_T21_SYNTAX)
    /* Size the per-channel buffers for the picture, as the stock encoder
     * sizes its per-channel buffer pool from the channel's resolution:
     * EMC per-macroblock scratch (the captured 2 MiB layout at 1080p, 0.26
     * MiB at 360p), and a bitstream window of one raw picture (an
     * all-I_PCM picture fits; at least 256 KiB, at most the 1 MiB of the
     * T21 layout). */
    encoder->scratch_size = T23_HelixScratchLayout(
        (params->width + 15u) / 16u, (params->height + 15u) / 16u,
        encoder->scratch_offset);
    {
        uint64_t window = (reference_size + 0xffffu) &
                          ~(uint64_t)0xffffu;

        if (window < (256u << 10))
            window = 256u << 10;
#if defined(PLATFORM_T23)
        /* T23: up to 2 MiB (1080p); the allocation below falls back to
         * the 1 MiB T21 window when reserved memory is short */
        if (window > (2u << 20))
            window = 2u << 20;
#else
        if (window > (1u << 20))
            window = 1u << 20;
#endif
#if defined(PLATFORM_T23)
        {
            /* OPENIMP_T23_HELIX_BS_KIB=<KiB>: a larger (or smaller)
             * window, 256 KiB .. 4 MiB in 64 KiB steps, for testing the
             * core's window limit against reserved memory on a device */
            const char *kib = getenv("OPENIMP_T23_HELIX_BS_KIB");
            unsigned long value = kib ? strtoul(kib, NULL, 0) : 0ul;

            if (value >= 256ul && value <= 4096ul) {
                window = ((uint64_t)value << 10) & ~(uint64_t)0xffffu;
                window_forced = 1;
            }
        }
#endif
#if defined(HELIX_SHARED_BITSTREAM)
        /* the window lies in the shared buffer: keep it inside the pool
         * size (IMP_Encoder_SetPoolSize), down to the 256 KiB minimum */
        {
            uint32_t pool = OpenIMP_HelixBitstream_PoolSize();

            if (pool > 4096u && window + 4096u > pool) {
                window = (pool - 4096u) & ~0xffffu;
                if (window < (256u << 10))
                    window = 256u << 10;
            }
        }
#endif
        encoder->bitstream_kib = (uint32_t)(window >> 10);
    }
#endif
#if defined(PLATFORM_T23)
    for (i = 0; i < 2u; i++) {
        if (t30_dma_allocate(&encoder->reference[i].dma,
                             (uint32_t)reference_size,
                             "t23-helix-ref") != 0)
            goto fail;
        encoder->reference[i].y = encoder->reference[i].dma.phys_addr;
        encoder->reference[i].c = encoder->reference[i].y +
                                  (uint32_t)aligned_luma_size;
    }
    /* The core does not stop at the window end (1080p: up to 1.79 MB
     * written for a 1 MiB window).  The arena is allocated top-down, so
     * the EMC scratch goes between the references and the bitstream
     * buffer: a spill upwards runs into the scratch of the picture that
     * is being dropped anyway, not into the reference the next P picture
     * predicts from (the scratch is 2 MiB at 1080p, larger than the
     * spills seen).  The window starts at the 128-byte aligned slice
     * data; a page of slack keeps even a full window inside the
     * allocation.  Short of reserved memory: the 1 MiB T21 window. */
    if (t30_dma_allocate(&encoder->emc, encoder->scratch_size,
                         "t23-helix-emc") != 0)
        goto fail;
    if (t30_dma_allocate(&encoder->temporary,
                         (encoder->bitstream_kib << 10) + 4096u,
                         "t23-helix-bs") != 0) {
        if (window_forced || encoder->bitstream_kib <= 1024u ||
            t30_dma_allocate(&encoder->temporary, (1u << 20) + 4096u,
                             "t23-helix-bs") != 0)
            goto fail;
        encoder->bitstream_kib = 1024u;
    }
    if (t30_dma_allocate(&encoder->descriptor, T30_DESCRIPTOR_WINDOW,
                         "t23-helix-desc") != 0)
        goto fail;
    t23_canary_arm(encoder);
#else
    if (t30_dma_allocate(&encoder->descriptor, T30_DESCRIPTOR_WINDOW,
                         "t30-helix-desc") != 0)
        goto fail;
#if defined(HELIX_SHARED_BITSTREAM)
    /* Only the T21 command list points the VPU at an EMC scratch area.
     * The bitstream goes to the shared buffer, taken per picture. */
    if (t30_dma_allocate(&encoder->emc, encoder->scratch_size,
                         "t30-helix-emc") != 0 ||
        OpenIMP_HelixBitstream_Reserve(t30_bitstream_bytes(encoder)) != 0)
#elif defined(PLATFORM_T21)
    if (t30_dma_allocate(&encoder->temporary, T30_BITSTREAM_WINDOW,
                         "t30-helix-bs") != 0)
#else
    if (t30_dma_allocate(&encoder->temporary, (uint32_t)frame_size * 2u,
                         "t30-helix-bs") != 0)
#endif
        goto fail;
#if defined(PLATFORM_T20)
    encoder->t10 = t30_soc_is_t10();
    if (encoder->t10) {
        /* T10 reconstructions keep a one-macroblock border around both
         * planes; y/c are the padded plane bases (the deblocker writes
         * there, T10_H264_BuildDescriptor adds the border offset for the
         * MCE reference read). */
        uint8_t mbw = (uint8_t)(((uint32_t)params->width + 15u) / 16u);
        uint8_t mbh = (uint8_t)(((uint32_t)params->height + 15u) / 16u);
        uint32_t luma_plane = (uint32_t)T10_H264_ReferencePlaneSize(mbw, mbh, 0);
        uint32_t chroma_plane = (uint32_t)T10_H264_ReferencePlaneSize(mbw, mbh, 1);

        for (i = 0; i < 2u; i++) {
            if (t30_dma_allocate(&encoder->reference[i].dma,
                                 luma_plane + chroma_plane,
                                 "t10-nvpu-ref") != 0)
                goto fail;
            encoder->reference[i].y = encoder->reference[i].dma.phys_addr;
            encoder->reference[i].c = encoder->reference[i].dma.phys_addr +
                luma_plane;
        }
    } else
#endif
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
    t30_start_rate_control(encoder, encoder->params.qp);
    encoder->force_idr = 1;
    *encoder_out = encoder;
    LOG_CODEC("T30 Helix: native encoder ready channel=%u %ux%u desc=0x%08x",
              encoder->channel.channel_id, params->width, params->height,
              encoder->descriptor.phys_addr);
#if defined(HELIX_T21_SYNTAX) && !defined(PLATFORM_T23)
    IMP_LOG_INFO("Encoder", "T21 Helix: encoder ready channel=%u %ux%u "
                 "rmem desc=0x%08x emc=0x%08x/%uK ref=0x%08x/0x%08x %uK "
                 "each, bitstream window %uK in the shared %uK buffer",
                 encoder->channel.channel_id, params->width, params->height,
                 encoder->descriptor.phys_addr, encoder->emc.phys_addr,
                 encoder->scratch_size >> 10, encoder->reference[0].y,
                 encoder->reference[1].y, (uint32_t)(reference_size >> 10),
                 encoder->bitstream_kib,
                 OpenIMP_HelixBitstream_Size() >> 10);
#endif
#if defined(PLATFORM_T20)
    if (encoder->t10)
        IMP_LOG_INFO("Encoder", "T10 NVPU: %ux%u uses the T10 command list "
                     "(padded references)", params->width, params->height);
#endif
#if defined(PLATFORM_T23)
    IMP_LOG_INFO("Encoder", "T23 Helix: native encoder ready channel=%u "
                 "%ux%u rc=%u bitrate=%u fps=%u/%u gop=%u qp=%u [%u,%u] "
                 "desc=0x%08x emc=0x%08x/%uK bs=0x%08x/%uK "
                 "ref=0x%08x/0x%08x timeout=%ums bsf-stop=%s",
                 encoder->channel.channel_id,
                 params->width, params->height, encoder->params.rc_mode,
                 encoder->params.bitrate, encoder->params.fps_num,
                 encoder->params.fps_den, encoder->params.gop_length,
                 encoder->params.qp, encoder->params.min_qp,
                 encoder->params.max_qp, encoder->descriptor.phys_addr,
                 encoder->emc.phys_addr, encoder->scratch_size >> 10,
                 encoder->temporary.phys_addr, encoder->bitstream_kib,
                 encoder->reference[0].y, encoder->reference[1].y,
                 encoder->channel.mdelay, encoder->bsf_stop ? "on" : "off");
    t23_rc_log(encoder, "ready");
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

#if !defined(PLATFORM_T23)
/* The VPU encodes whole macroblocks, so a 1080-line picture is coded as
 * 1088 lines and the decoder crops the last eight.  The T20/T21/T30 ISP
 * frame channel lays NV12 out with the chroma plane at the
 * macroblock-aligned height but only writes the visible lines: luma rows
 * 1080..1087 and chroma rows 540..543 keep whatever the VBM buffer held
 * (zeros or another frame).  Zero chroma is saturated green, and the
 * encoder carries the padding into the visible rows of the last macroblock
 * row (chroma DC transform across the 8x8 block, deblocking, P_Skip and
 * motion compensation from the padded reference), which shows as a
 * flickering green stripe at the bottom.  Replicate the last visible luma
 * and chroma rows into the padding, as the H.264 cropping model expects. */
static void t30_pad_input_rows(const T30HelixEncoder *encoder,
                               const IMPFrameInfo *frame)
{
    uint32_t width = encoder->params.width;
    uint32_t height = encoder->params.height;
    uint32_t aligned_height = (uint32_t)encoder->sps.i_mb_height * 16u;
    uint32_t chroma_height = (height + 1u) / 2u;
    uint8_t *luma;
    uint8_t *chroma;
    uint32_t row;

    if (height >= aligned_height || !frame->virAddr ||
        (uint64_t)frame->size <
            (uint64_t)width * aligned_height * 3u / 2u)
        return;
    luma = (uint8_t *)(uintptr_t)frame->virAddr;
    chroma = luma + width * aligned_height;
    /* the source rows were written by DMA; drop stale cache lines
     * (bidirectional, as the IVS path does: write back + invalidate is
     * safe whatever the line state is) */
    (void)DMA_RmemFlushCache(luma + width * (height - 1u), width, 2);
    (void)DMA_RmemFlushCache(chroma + width * (chroma_height - 1u), width, 2);
    for (row = height; row < aligned_height; row++)
        memcpy(luma + width * row, luma + width * (height - 1u), width);
    for (row = chroma_height; row < aligned_height / 2u; row++)
        memcpy(chroma + width * row, chroma + width * (chroma_height - 1u),
               width);
    (void)DMA_RmemFlushCache(luma + width * height,
                             width * (aligned_height - height), 1);
    (void)DMA_RmemFlushCache(chroma + width * chroma_height,
                             width * (aligned_height / 2u - chroma_height),
                             1);
}
#endif

static int t30_helix_encode_job(T30HelixEncoder *encoder,
                                const IMPFrameInfo *frame,
                                HWStreamBuffer **stream_out);

int OpenIMP_T30_HelixEncode(T30HelixEncoder *encoder,
                            const IMPFrameInfo *frame,
                            HWStreamBuffer **stream_out)
{
#if defined(HELIX_SHARED_BITSTREAM)
    int ret;

    if (!encoder || !frame || !stream_out || !frame->phyAddr)
        return -1;
    /* The picture's bitstream goes to the shared buffer: hold it from the
     * slice header to the copy into the access unit. */
    if (OpenIMP_HelixBitstream_Lock(t30_bitstream_bytes(encoder),
                                    &encoder->temporary) != 0) {
        LOG_CODEC("T30 Helix: no shared bitstream buffer");
        return -1;
    }
    /* this picture's part of the shared buffer: the slice header area and
     * the window, so cache maintenance and the length check cover the
     * window and not the whole (2 MB) buffer */
    encoder->temporary.size = t30_bitstream_bytes(encoder);
    ret = t30_helix_encode_job(encoder, frame, stream_out);
    memset(&encoder->temporary, 0, sizeof(encoder->temporary));
    OpenIMP_HelixBitstream_Unlock();
    return ret;
#else
    return t30_helix_encode_job(encoder, frame, stream_out);
#endif
}

static int t30_helix_encode_job(T30HelixEncoder *encoder,
                                const IMPFrameInfo *frame,
                                HWStreamBuffer **stream_out)
{
    uint8_t *temporary;
    uint8_t *output;
    HWStreamBuffer *stream;
    T30AnnexBWriter writer;
    bs_t bits;
    uint32_t capacity;
    uint32_t header_length;
    uint32_t offset = 0;
    uint32_t qp;
    unsigned int output_index;
    int idr;
    size_t descriptor_pairs;

    if (!encoder || !frame || !stream_out || !frame->phyAddr)
        return -1;
    /* Nothing below commits encoder state until the access unit exists: a
     * failed picture leaves a requested IDR, the GOP position and the
     * reference chain exactly as they were. */
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
#else
    t30_pad_input_rows(encoder, frame);
#endif
    idr = encoder->force_idr || !encoder->have_reference ||
          encoder->gop_position >= encoder->params.gop_length;
    qp = encoder->rate_control_enabled
        ? openimp_t31_rate_controller_qp(&encoder->rate_control)
        : encoder->params.qp;
#if defined(PLATFORM_T23)
    qp = t23_rc_picture_qp(encoder, qp, idr);
    qp = t23_overflow_qp(encoder, qp, idr);
#endif
    output_index = encoder->have_reference
        ? (encoder->reference_index ^ 1u) : 0u;

    h264e_slice_header_init(&encoder->slice_header, &encoder->sps,
                            &encoder->pps,
                            idr ? (int)(encoder->idr_pic_id & 1u) : -1,
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
    if (bits.i_left != 32)
        return -1;
    header_length = (uint32_t)(bits.p - bits.p_start);
    h264_cabac_context_init(&encoder->cabac,
                            encoder->slice_header.i_type,
                            (int)qp,
                            encoder->slice_header.i_cabac_init_idc);

    t30_fill_slice(encoder, frame, qp, idr, output_index);
#if defined(HELIX_T21_SYNTAX)
    if (T21_H264_BuildDescriptor(&encoder->slice,
                                 &descriptor_pairs) != 0) {
#else
    if (
#if defined(PLATFORM_T20)
        encoder->t10 ? T10_H264_BuildDescriptor(&encoder->slice,
                                                &descriptor_pairs) != 0 :
#endif
        T30_H264_BuildDescriptor(&encoder->slice,
                                 &descriptor_pairs) != 0) {
#endif
        LOG_CODEC("T30 Helix: descriptor build failed: %s",
                  strerror(errno));
        return -1;
    }
    /* Publish the CPU-built command list before the VPU fetches it.  The
     * VPU-written buffers hold no dirty lines: their allocation zeroes were
     * written back at create, and the CPU only ever reads the bitstream
     * window (invalidated after RUN below), so the whole-window and
     * whole-reference invalidations per picture are unnecessary. */
    if (DMA_RmemFlushCache(
            (void *)(uintptr_t)encoder->descriptor.virt_addr,
            (uint32_t)(descriptor_pairs * 2u * sizeof(uint32_t)), 1) != 0
#if defined(PLATFORM_T23)
        /* T23 keeps the per-picture invalidation of the bitstream window
         * and the output reference it was device-tested with
         * (claude/t23-native-helix) */
        || DMA_RmemFlushCache(temporary + T30_SLICE_OFFSET,
                              encoder->temporary.size - T30_SLICE_OFFSET,
                              2) != 0
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
    {
        int attempt;
        int run_ret = -1;
        int late = 0;
        int overflow = 0;

        for (attempt = 0; attempt < 2; attempt++) {
            uint32_t status;

            encoder->channel.status = 0;
            encoder->channel.output_len = 0;
            encoder->channel.frame_type = 0;
            encoder->channel.time = 0;
            errno = 0;
            run_ret = ioctl(encoder->fd, T30_CHANNEL_RUN, &encoder->channel);
            status = encoder->channel.status;
            late = run_ret == 0 && !(status & T23_SCH_STAT_ENDFLAG) &&
                   (status & T23_SCH_STAT_LATE) && !encoder->strict_status;
            if (run_ret == 0 && !(status & T23_SCH_STAT_ERRORS) &&
                ((status & T23_SCH_STAT_ENDFLAG) || late) &&
                encoder->channel.output_len &&
                encoder->channel.output_len <=
                    (encoder->bitstream_kib << 10))
                break;
            /* A full window is decided by the status (and a length at the
             * window end), never retried: the same job overflows again. */
            if (run_ret == 0 &&
                t23_overflowed(encoder, status, encoder->channel.output_len)) {
                overflow = 1;
                break;
            }
            /* Retry an unexplained result once with the identical job;
             * a timeout (the kernel already waited and reset the core)
             * or an error the core reported would only repeat. */
            if (run_ret != 0 || (status & T23_SCH_STAT_ERRORS) ||
                attempt == 1) {
                attempt = 2;
                break;
            }
            IMP_LOG_WARN("Encoder", "T23 Helix: %ux%u frame=%u %s "
                         "status=0x%08x len=%u, retrying the job",
                         encoder->params.width, encoder->params.height,
                         encoder->frame_number, idr ? "IDR" : "P",
                         status, encoder->channel.output_len);
            encoder->retries++;
            (void)DMA_RmemFlushCache(temporary + T30_SLICE_OFFSET,
                                     encoder->temporary.size -
                                         T30_SLICE_OFFSET, 2);
        }
        if (overflow) {
            if (!t23_overflow_drop(encoder, idr, qp, encoder->channel.status,
                                   encoder->channel.output_len))
                encoder->failures++;
            return -1;
        }
        if (attempt >= 2) {
            /* A timed-out, errored or bitstream-full picture leaves no
             * usable reconstruction: restart the GOP.  soc_vpu has already
             * reset the core on a timeout and resets it again before the
             * next job. */
            IMP_LOG_ERR("Encoder", "T23 Helix: %ux%u run failed frame=%u "
                        "%s qp=%u ret=%d errno=%d status=0x%08x len=%u",
                        encoder->params.width, encoder->params.height,
                        encoder->frame_number, idr ? "IDR" : "P", qp,
                        run_ret, errno, encoder->channel.status,
                        encoder->channel.output_len);
            encoder->force_idr = 1;
            encoder->have_reference = 0;
            encoder->failures++;
            return -1;
        }
        if (late) {
            /* See T23_SCH_STAT_LATE: the job completed; the length was
             * read back from the bitstream engine. */
            encoder->late_status++;
            if (encoder->late_status <= 3u ||
                encoder->late_status % 100u == 0u)
                IMP_LOG_INFO("Encoder", "T23 Helix: %ux%u frame=%u %s "
                             "completed with late interrupt status "
                             "0x%08x len=%u (%u so far)",
                             encoder->params.width, encoder->params.height,
                             encoder->frame_number, idr ? "IDR" : "P",
                             encoder->channel.status,
                             encoder->channel.output_len,
                             encoder->late_status);
        }
    }
    encoder->failures = 0;
#else
    /* Never let a RUN that returns without reporting a length republish the
     * previous picture's size over stale bitstream bytes. */
    encoder->channel.output_len = 0;
    if (ioctl(encoder->fd, T30_CHANNEL_RUN, &encoder->channel) != 0 ||
        !encoder->channel.output_len ||
        encoder->channel.output_len > encoder->temporary.size - T30_SLICE_OFFSET) {
        LOG_CODEC("T30 Helix: run failed frame=%u errno=%d status=0x%08x len=%u",
                  encoder->frame_number, errno, encoder->channel.status,
                  encoder->channel.output_len);
        return -1;
    }
#endif
    if (DMA_RmemFlushCache(temporary + T30_SLICE_OFFSET,
                           encoder->channel.output_len, 2) != 0) {
        LOG_CODEC("T30 Helix: bitstream invalidate failed: %s",
                  strerror(errno));
        return -1;
    }

    /* Emit the CPU-written slice header and the VPU's CABAC payload as one
     * escaped NAL straight from the DMA window: no staging copy, and the
     * allocation follows the picture rather than the window size (which
     * also keeps T23's access units small). */
    capacity = (idr ? encoder->headers_size : 0u) +
               t30_annexb_bound(header_length + encoder->channel.output_len);
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
    if (t30_annexb_begin(&writer, output + offset, capacity - offset,
                         idr ? NAL_SLICE_IDR : NAL_SLICE,
                         idr ? NAL_PRIORITY_HIGHEST :
                               NAL_PRIORITY_HIGH) != 0 ||
        t30_annexb_append(&writer, temporary, header_length) != 0 ||
        t30_annexb_append(&writer, temporary + T30_SLICE_OFFSET,
                          encoder->channel.output_len) != 0) {
        free(output);
        free(stream);
        return -1;
    }
    stream->virt_addr = (uint32_t)(uintptr_t)output;
    stream->length = (uint32_t)(writer.output - output);
    stream->timestamp = frame->timeStamp;
    stream->frame_type = idr ? HW_FRAME_TYPE_I : HW_FRAME_TYPE_P;
    stream->slice_type = stream->frame_type;
    *stream_out = stream;
    if (idr) {
        encoder->force_idr = 0;
        encoder->gop_position = 0;
        encoder->idr_pic_id++;
    }
    if (encoder->channel.output_len > encoder->max_output_len)
        encoder->max_output_len = encoder->channel.output_len;
    encoder->reference_index = output_index;
    encoder->have_reference = 1;
    encoder->gop_position++;
    encoder->frame_number++;
    if (encoder->rate_control_enabled)
        (void)openimp_t31_rate_controller_complete(
            &encoder->rate_control, stream->length * 8u, qp, idr);
#if defined(PLATFORM_T23)
    if (!idr) {
        encoder->last_p_qp = qp;
        encoder->have_p_qp = 1;
    }
    t23_rc_stats(encoder, qp, idr, stream->length);
    t23_overflow_settle(encoder, idr, encoder->channel.output_len);
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
        /* hw_max against the window sizes the bitstream allocation. */
        LOG_CODEC("T30 Helix: frame=%u %s bytes=%u hw=%u hw_max=%u/%u status=0x%08x desc=%u",
                  encoder->frame_number, idr ? "IDR" : "P",
                  stream->length, encoder->channel.output_len,
                  encoder->max_output_len,
                  encoder->temporary.size - T30_SLICE_OFFSET,
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
        encoder->rc.target_bitrate && encoder->params.fps_num &&
        encoder->params.fps_den &&
        openimp_t31_rate_controller_init(
            &encoder->rate_control, encoder->rc.target_bitrate,
            encoder->params.fps_num, encoder->params.fps_den,
            encoder->params.gop_length, encoder->params.min_qp,
            encoder->params.max_qp, qp) == 0)
        encoder->rate_control_enabled = 1;
    t23_rc_apply_band(encoder);
    return 0;
}

int OpenIMP_T30_HelixReconfigure(T30HelixEncoder *encoder,
                                 const HWEncoderParams *params)
{
    HWEncoderParams next;
    T23RcConfig rc;
    uint32_t level;
    int frame_rate_changed;
    int extras_changed;
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
    /* the rate-control extras are always set as a whole (CreateChn,
     * SetChnAttrRcMode) */
    next.static_time = params->static_time;
    next.change_pos = params->change_pos;
    next.quality_level = params->quality_level;
    next.frm_qp_step = params->frm_qp_step;
    next.gop_qp_step = params->gop_qp_step;
    next.bias_level = params->bias_level;
    next.rc_flags = params->rc_flags;
    extras_changed =
        next.static_time != encoder->params.static_time ||
        next.change_pos != encoder->params.change_pos ||
        next.quality_level != encoder->params.quality_level ||
        next.frm_qp_step != encoder->params.frm_qp_step ||
        next.gop_qp_step != encoder->params.gop_qp_step ||
        next.bias_level != encoder->params.bias_level ||
        next.rc_flags != encoder->params.rc_flags;

    if (!extras_changed &&
        next.gop_length == encoder->params.gop_length &&
        next.fps_num == encoder->params.fps_num &&
        next.fps_den == encoder->params.fps_den &&
        next.rc_mode == encoder->params.rc_mode &&
        next.bitrate == encoder->params.bitrate &&
        next.qp == encoder->params.qp &&
        next.min_qp == encoder->params.min_qp &&
        next.max_qp == encoder->params.max_qp)
        return 0;

    t23_rc_config(&next, &rc);
    if (next.bitrate != encoder->params.bitrate && !extras_changed &&
        next.gop_length == encoder->params.gop_length &&
        next.fps_num == encoder->params.fps_num &&
        next.fps_den == encoder->params.fps_den &&
        next.rc_mode == encoder->params.rc_mode &&
        next.min_qp == encoder->params.min_qp &&
        next.max_qp == encoder->params.max_qp &&
        encoder->rate_control_enabled) {
        /* bitrate only: keep the scene model */
        if (openimp_t31_rate_controller_set_bitrate(&encoder->rate_control,
                                                    rc.target_bitrate) != 0)
            return -1;
    } else {
        restart = 1;
    }
    IMP_LOG_INFO("Encoder", "T23 Helix: %ux%u reconfigured rc=%u bitrate=%u fps=%u/%u "
              "gop=%u qp=%u [%u,%u]", encoder->params.width,
              encoder->params.height, next.rc_mode, next.bitrate,
              next.fps_num, next.fps_den, next.gop_length, next.qp,
              next.min_qp, next.max_qp);
    frame_rate_changed = next.fps_num != encoder->params.fps_num ||
                         next.fps_den != encoder->params.fps_den;
    encoder->params.fps_num = next.fps_num;
    encoder->params.fps_den = next.fps_den;
    encoder->params.gop_length = next.gop_length;
    encoder->params.rc_mode = next.rc_mode;
    encoder->params.bitrate = next.bitrate;
    encoder->params.qp = next.qp;
    encoder->params.min_qp = next.min_qp;
    encoder->params.max_qp = next.max_qp;
    encoder->params.static_time = next.static_time;
    encoder->params.change_pos = next.change_pos;
    encoder->params.quality_level = next.quality_level;
    encoder->params.frm_qp_step = next.frm_qp_step;
    encoder->params.gop_qp_step = next.gop_qp_step;
    encoder->params.bias_level = next.bias_level;
    encoder->params.rc_flags = next.rc_flags;
    encoder->rc = rc;
    /* The SPS carries the frame rate (VUI timing) and the level, which
     * depends on frame rate and bitrate (MaxBR), as in UpdateParams: send
     * new parameter sets with the next picture, which must then be an
     * IDR. */
    level = t30_h264_level(next.width, next.height, next.fps_num,
                           next.fps_den, next.bitrate,
                           (uint32_t)encoder->sps.i_num_ref_frames);
    if (frame_rate_changed || level != (uint32_t)encoder->sps.i_level_idc) {
        t30_init_parameter_sets(encoder);
        if (t30_generate_headers(encoder) != 0)
            return -1;
        encoder->force_idr = 1;
    }
    if (restart)
        (void)t23_rate_control_restart(encoder);
    t23_rc_log(encoder, "reconfigured");
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

int OpenIMP_T30_HelixUpdateParams(T30HelixEncoder *encoder,
                                  const HWEncoderParams *requested)
{
    HWEncoderParams next;
    uint32_t level;
    uint32_t qp;
    int frame_rate_changed;
    int model_changed;

    if (!encoder || !requested)
        return -1;
    next = encoder->params;
    next.fps_num = requested->fps_num;
    next.fps_den = requested->fps_den;
    next.gop_length = requested->gop_length;
    next.rc_mode = requested->rc_mode;
    next.bitrate = requested->bitrate;
    next.qp = requested->qp;
    next.min_qp = requested->min_qp;
    next.max_qp = requested->max_qp;
    t30_normalize_params(&next, &encoder->params);
    if (!memcmp(&next, &encoder->params, sizeof(next)))
        return 0;

    frame_rate_changed = next.fps_num != encoder->params.fps_num ||
                         next.fps_den != encoder->params.fps_den;
    model_changed = frame_rate_changed ||
                    next.gop_length != encoder->params.gop_length ||
                    next.rc_mode != encoder->params.rc_mode ||
                    next.min_qp != encoder->params.min_qp ||
                    next.max_qp != encoder->params.max_qp;
    qp = encoder->rate_control_enabled
        ? openimp_t31_rate_controller_qp(&encoder->rate_control)
        : encoder->params.qp;
    if (qp < next.min_qp)
        qp = next.min_qp;
    if (qp > next.max_qp)
        qp = next.max_qp;

    if (next.bitrate != encoder->params.bitrate && !model_changed &&
        encoder->rate_control_enabled) {
        /* Retarget without discarding the scene model. */
        if (openimp_t31_rate_controller_set_bitrate(&encoder->rate_control,
                                                    next.bitrate) != 0)
            return -1;
        encoder->params = next;
    } else {
        encoder->params = next;
        if (model_changed || !encoder->rate_control_enabled)
            t30_start_rate_control(encoder, qp);
    }

    /* The SPS carries the level (frame rate and bitrate dependent) and, on
     * T21 and T23, VUI timing.  A changed SPS must start a new IDR. */
    level = t30_h264_level(next.width, next.height, next.fps_num,
                           next.fps_den, next.bitrate,
                           (uint32_t)encoder->sps.i_num_ref_frames);
    if (level != (uint32_t)encoder->sps.i_level_idc
#if defined(HELIX_T21_SYNTAX)
        || frame_rate_changed
#endif
       ) {
        t30_init_parameter_sets(encoder);
        if (t30_generate_headers(encoder) != 0)
            return -1;
        encoder->force_idr = 1;
    }
    LOG_CODEC("T30 Helix: params fps=%u/%u gop=%u rc=%u bitrate=%u qp=%u [%u,%u] rc_loop=%d",
              next.fps_num, next.fps_den, next.gop_length, next.rc_mode,
              next.bitrate, next.qp, next.min_qp, next.max_qp,
              encoder->rate_control_enabled);
    return 0;
}

void OpenIMP_T30_HelixDestroy(T30HelixEncoder *encoder)
{
    unsigned int i;
    if (!encoder)
        return;
    if (encoder->fd >= 0 && encoder->channel.clist) {
        /* never a VPU in vlist with CLOSE: that powers the VPU down */
        encoder->channel.vlist = 0;
        encoder->channel.workphase = T30_CHANNEL_CLOSE;
        (void)ioctl(encoder->fd, T30_CHANNEL_RELEASE, &encoder->channel);
    }
    /* the shared descriptor stays open, see t30_vpu_fd */
    for (i = 0; i < 2u; i++)
        t30_dma_release(&encoder->reference[i].dma);
    t30_dma_release(&encoder->temporary);
    t30_dma_release(&encoder->emc);
    t30_dma_release(&encoder->descriptor);
    free(encoder);
}
