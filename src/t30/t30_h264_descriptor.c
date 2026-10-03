/*
 * Ingenic T30 Helix H.264 descriptor builder.
 *
 * Register definitions and programming order are based on the GPL-2.0 T30
 * SDK 1.0.5 kernel (thingino-linux commit dc2e24d03f) and the GPL-2.0 Ingenic
 * Helix encoder sources.  The T30-specific ordering and values were checked
 * against the stock SDK 1.0.5 encoder on real T30X hardware.
 */

#include "t30_h264_descriptor.h"

#include <errno.h>

#define T30_VDMA_VALID 0x80000000u
#define T30_VDMA_TERM  0x40000000u

#define T30_TCSM_FLUSH 0xc0000u

#define T30_VRAM_TOPPA 0x132c4000u
#define T30_VRAM_TOPMV 0x132c4800u
#define T30_VRAM_MAU   0x132c4c00u
#define T30_VRAM_DBLK  0x132c5000u
#define T30_VRAM_ME    0x132c5400u
#define T30_VRAM_SDE   0x132c5600u
#define T30_VRAM_RAW   0x132f0000u
#define T30_VRAM_DUMMY 0x132ffffcu

typedef struct {
    uint32_t *cursor;
    uint32_t *end;
} T30DescriptorWriter;

static const uint32_t t30_lps_range[64] = {
    0xeeceaefc, 0xe1c3a5fc, 0xd6b99cfc, 0xcbb094f2,
    0xc1a78ce4, 0xb79e85da, 0xad967ece, 0xa48e78c4,
    0x9c8772ba, 0x94806cb0, 0x8c7966a6, 0x8573619e,
    0x7e6d5c96, 0x7867578e, 0x72625386, 0x6c5d4e80,
    0x66584a78, 0x61544672, 0x5c4f436c, 0x574b3f66,
    0x53473c62, 0x4e43395c, 0x4a403658, 0x463d3352,
    0x4339304e, 0x3f362e4a, 0x3c342b46, 0x39312942,
    0x362e273e, 0x332c253c, 0x30292338, 0x2e272136,
    0x2b251f32, 0x29231d30, 0x27211c2c, 0x251f1a2a,
    0x231e1928, 0x211c1826, 0x1f1b1624, 0x1d191522,
    0x1c181420, 0x1a17131e, 0x1915121c, 0x1714111a,
    0x16131018, 0x15120f18, 0x14110e16, 0x13100d14,
    0x120f0c14, 0x110e0c12, 0x100d0b12, 0x0f0d0a10,
    0x0e0c0a10, 0x0d0b090e, 0x0c0a090e, 0x0c0a080c,
    0x0b09070c, 0x0a09070a, 0x0a08070a, 0x0908060a,
    0x09070608, 0x08070508, 0x07060508, 0x00000000
};

static int t30_emit(T30DescriptorWriter *writer, uint32_t reg,
                    uint32_t value)
{
    if ((size_t)(writer->end - writer->cursor) < 2u) {
        errno = ENOSPC;
        return -1;
    }
    *writer->cursor++ = value;
    *writer->cursor++ = T30_VDMA_VALID | (reg & 0xffffcu);
    return 0;
}

static int t30_emit_final(T30DescriptorWriter *writer, uint32_t reg,
                          uint32_t value)
{
    if ((size_t)(writer->end - writer->cursor) < 2u) {
        errno = ENOSPC;
        return -1;
    }
    *writer->cursor++ = value;
    *writer->cursor++ = T30_VDMA_VALID | T30_VDMA_TERM |
                        (reg & 0xffffcu);
    return 0;
}

/* Packed from the GPL-2.0 H264_QPEL interpolation table in the T30 Helix
 * kernel header.  The generated register values were checked against SDK
 * 1.0.5 on T30X hardware. */
static const uint32_t t30_h264_luma_interpolation[16][6] = {
    {0x00000000u, 0x00000000u, 0x00000001u, 0x00000000u, 0x00000000u, 0x00000000u},
    {0x80100506u, 0x00000000u, 0x1414fb01u, 0x000001fbu, 0x00000000u, 0x00000000u},
    {0x80100500u, 0x00000000u, 0x1414fb01u, 0x000001fbu, 0x00000000u, 0x00000000u},
    {0x80100507u, 0x00000000u, 0x1414fb01u, 0x000001fbu, 0x00000000u, 0x00000000u},
    {0x81100506u, 0x00000000u, 0x1414fb01u, 0x000001fbu, 0x00000000u, 0x00000000u},
    {0x8c100500u, 0x8d100506u, 0x1414fb01u, 0x000001fbu, 0x1414fb01u, 0x000001fbu},
    {0x80100500u, 0x81000a06u, 0x1414fb01u, 0x000001fbu, 0x1414fb01u, 0x000001fbu},
    {0x8c100501u, 0x8d100506u, 0x1414fb01u, 0x000001fbu, 0x1414fb01u, 0x000001fbu},
    {0x81100500u, 0x00000000u, 0x1414fb01u, 0x000001fbu, 0x00000000u, 0x00000000u},
    {0x81100500u, 0x80000a06u, 0x1414fb01u, 0x000001fbu, 0x1414fb01u, 0x000001fbu},
    {0x80100500u, 0x81000a00u, 0x1414fb01u, 0x000001fbu, 0x1414fb01u, 0x000001fbu},
    {0x81100500u, 0x80000a07u, 0x1414fb01u, 0x000001fbu, 0x1414fb01u, 0x000001fbu},
    {0x81100507u, 0x00000000u, 0x1414fb01u, 0x000001fbu, 0x00000000u, 0x00000000u},
    {0x8c100500u, 0x8d100507u, 0x1414fb01u, 0x000001fbu, 0x1414fb01u, 0x000001fbu},
    {0x80100500u, 0x81000a07u, 0x1414fb01u, 0x000001fbu, 0x1414fb01u, 0x000001fbu},
    {0x8c100501u, 0x8d100507u, 0x1414fb01u, 0x000001fbu, 0x1414fb01u, 0x000001fbu},
};

static const uint32_t t30_h264_chroma_interpolation[16][4] = {
    {0x00000000u, 0x00000000u, 0x00000001u, 0x00000000u},
    {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u},
    {0x80040300u, 0x00000000u, 0x00000000u, 0x00000000u},
    {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u},
    {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u},
    {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u},
    {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u},
    {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u},
    {0x81040300u, 0x00000000u, 0x00000000u, 0x00000000u},
    {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u},
    {0x80000000u, 0x81200600u, 0x00000000u, 0x00000000u},
    {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u},
    {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u},
    {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u},
    {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u},
    {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u},
};

static unsigned int t30_vmau_context_index(uint8_t slice_type,
                                            unsigned int index)
{
    if (index < 10u)
        return index + (slice_type ? 14u : 3u);
    if (index < 22u)
        return index + 63u;
    if (index < 28u)
        return index + 42u;
    return index + 12u;
}

static int t30_emit_motion_estimation(T30DescriptorWriter *writer,
                                      const T30H264SliceConfig *config)
{
    static const uint32_t luma_register_offset[6] = {
        0x50900, 0x50904, 0x50a00, 0x50a04, 0x50a08, 0x50a0c
    };
    static const uint32_t chroma_register_offset[4] = {
        0x50980, 0x50984, 0x50b00, 0x50b08
    };
    unsigned int i;
    unsigned int j;

#define MOTION_EMIT(reg, value)                                              \
    do {                                                                      \
        if (t30_emit(writer, (reg), (value)) != 0)                           \
            return -1;                                                        \
    } while (0)

    MOTION_EMIT(0x5010c, T30_VRAM_ME);
    MOTION_EMIT(0x50104, T30_VRAM_TOPMV);
    MOTION_EMIT(0x50108, 0x13200070u);
    MOTION_EMIT(0x50060, ((uint32_t)config->height - 1u) << 16 |
                         ((uint32_t)config->width - 1u));
    MOTION_EMIT(0x50064, (config->stride[1] << 16) |
                         config->stride[0]);
    MOTION_EMIT(0x50010, 0x800a8021u);
    MOTION_EMIT(0x50040, 0x873f5008u);
    MOTION_EMIT(0x50044, 0);
    MOTION_EMIT(0x50048, 0x07d007d0u);
    MOTION_EMIT(0x50800, config->reference_y);
    MOTION_EMIT(0x50804, config->reference_c);
    for (i = 0; i < 16u; i++) {
        for (j = 0; j < 6u; j++)
            MOTION_EMIT(luma_register_offset[j] +
                        (j < 2u ? i * 8u : i * 16u),
                        t30_h264_luma_interpolation[i][j]);
    }
    for (i = 0; i < 16u; i++) {
        for (j = 0; j < 4u; j++)
            MOTION_EMIT(chroma_register_offset[j] +
                        (j < 2u ? i * 8u : i * 16u),
                        t30_h264_chroma_interpolation[i][j]);
    }
    MOTION_EMIT(0x50068, 0);
    /* Disable the optional frame-size-control padding: OpenIMP owns tightly
     * aligned reconstruction planes and supplies their addresses directly. */
    MOTION_EMIT(0x5004c, 0);
    MOTION_EMIT(0x50000, 0x11u);

#undef MOTION_EMIT
    return 0;
}

int T30_H264_BuildDescriptor(const T30H264SliceConfig *config,
                             size_t *pair_count)
{
    static const uint32_t roi_position_registers[16] = {
        0x4004c, 0x40050, 0x40054, 0x40058,
        0x4005c, 0x40060, 0x40064, 0x40068,
        0x40138, 0x4013c, 0x40140, 0x40144,
        0x40148, 0x4014c, 0x40150, 0x40154
    };
#if !defined(PLATFORM_T20)
    static const uint32_t qpg_zero_registers[13] = {
        0x4007c, 0x40080, 0x40084, 0x40088,
        0x4008c, 0x40090, 0x40078, 0x400a4,
        0x400a8, 0x400ac, 0x400b0, 0x400b4,
        0x400c8
    };
#endif
    static const uint32_t scheduler_zero_registers[8] = {
        0x00060, 0x00064, 0x00068, 0x0006c,
        0x00070, 0x00074, 0x00078, 0x0007c
    };
    T30DescriptorWriter writer;
    uint32_t aligned_width;
    uint32_t aligned_height;
#if !defined(PLATFORM_T20)
    uint32_t min_qp;
#endif
    uint32_t max_qp;
    unsigned int i;

    if (!config || !config->descriptor || !config->cabac_state ||
        !config->mb_width || !config->mb_height ||
        !config->width || !config->height || config->slice_type > 1u ||
        config->descriptor_words < (config->slice_type ? 1570u : 1222u) +
                                   2u * config->qp_table_words ||
        (config->qp_table_words && !config->qp_table) ||
        (config->slice_type &&
         (!config->reference_y || !config->reference_c))) {
        errno = EINVAL;
        return -1;
    }
    writer.cursor = config->descriptor;
    writer.end = config->descriptor + config->descriptor_words;
    aligned_width = (uint32_t)config->mb_width * 16u;
    aligned_height = (uint32_t)config->mb_height * 16u;
#if !defined(PLATFORM_T20)
    min_qp = config->qp > 12u ? config->qp - 12u : 0u;
#endif
    max_qp = config->qp < 39u ? config->qp + 13u : 51u;

#define EMIT(reg, value)                                                     \
    do {                                                                     \
        if (t30_emit(&writer, (reg), (value)) != 0)                         \
            return -1;                                                       \
    } while (0)

    EMIT(T30_TCSM_FLUSH, 0);
    EMIT(0x40004, ((uint32_t)config->first_mby << 24) |
                  ((uint32_t)config->last_mby << 8) |
                  ((uint32_t)config->mb_width - 1u));
    EMIT(0x4000c, 0x132c0000u);
    EMIT(0x40010, config->raw[0]);
    EMIT(0x40014, config->raw[1]);
    EMIT(0x40034, config->raw[2]);
    EMIT(0x40038, (config->stride[0] << 16) | config->stride[1]);
    EMIT(0x40018, T30_VRAM_TOPMV);
    EMIT(0x4001c, T30_VRAM_TOPPA);
    EMIT(0x40020, T30_VRAM_ME);
    EMIT(0x40024, T30_VRAM_MAU);
    EMIT(0x40028, T30_VRAM_DBLK);
    EMIT(0x4002c, T30_VRAM_SDE);
    EMIT(0x40030, T30_VRAM_RAW);

#if defined(PLATFORM_T20)
    EMIT(0x40040, config->max_qp_cap ? config->max_qp_cap : max_qp);
#else
    EMIT(0x40040, max_qp);
#endif
    EMIT(0x40044, 0);
    EMIT(0x40048, 0);
#if defined(PLATFORM_T20)
    /* T20's older JZ NVPU only has the first eight ROI slots.  Writes to the
     * later T30 EFE slots stop its VDMA walker before the terminal command. */
    for (i = 0; i < 8u; i++)
        EMIT(roi_position_registers[i], 0);
#else
    EMIT(0x40130, 0);
    EMIT(0x40134, 0);
    for (i = 0; i < 16u; i++)
        EMIT(roi_position_registers[i], 0);
#endif
#if defined(PLATFORM_T20)
    /* OEM H264E_T20_SliceInit 0x20f40: table length in words, VPU memory
     * address 0xc5800, enable; then the table words written there */
    EMIT(0x4006c, ((uint32_t)config->qp_table_words << 21) | 0x000c5800u |
                  (config->qp_table_words ? 0x80000000u : 0u));
    for (i = 0; i < config->qp_table_words; i++)
        EMIT(0xc5800u + 4u * i, config->qp_table[i]);
#else
    EMIT(0x4006c, 0x000c5800u);
#endif
    EMIT(0x40120, 0);
    EMIT(0x40108, 0);
#if defined(PLATFORM_T20)
    EMIT(0x4010c, 0x03400000u);
#else
    EMIT(0x4010c, 0x00400000u | ((uint32_t)config->dcs_oth << 24));
    EMIT(0x40074, (max_qp << 24) | (min_qp << 16) |
                  ((uint32_t)config->qp << 8));
    for (i = 0; i < 13u; i++)
        EMIT(qpg_zero_registers[i], 0);
#endif

    if (config->slice_type &&
        t30_emit_motion_estimation(&writer, config) != 0)
        return -1;

    /* VMAU mode decision and the 42 CABAC contexts it consumes directly. */
    EMIT(0x80040, 4);
    EMIT(0x80050, 0x80000b00u);
    EMIT(0x8000c, T30_VRAM_MAU);
    EMIT(0x8005c, T30_VRAM_DUMMY);
    EMIT(0x80058, 0x13200074u);
    EMIT(0x80054, (aligned_height << 16) | aligned_width);
    EMIT(0x80044, 0x01000001u);
    EMIT(0x80078, 0x0b150b15u);
    EMIT(0x80028, T30_VRAM_TOPPA);
    EMIT(0x80030, ((uint32_t)config->last_mby << 24) |
                  (((uint32_t)config->mb_width - 1u) << 16) |
                  (config->slice_type ? 2u : 1u));
#if defined(PLATFORM_T20)
    /* hwicodec_pf_h264e_t20_enc 0x24600: slice +132..+155 */
    EMIT(0x80034, config->mb_tune ? 0x896783e6u : 0x00008202u);
#else
    EMIT(0x80034, 0x0000c202u);
#endif
    EMIT(0x80038, 0xcccc0111u);
#if defined(PLATFORM_T20)
    EMIT(0x8003c, config->mb_tune ? 0x09000000u : 0u);
#else
    EMIT(0x8003c, 0);
#endif
    EMIT(0x800d8, 0);
    EMIT(0x800dc, 0);
#if !defined(PLATFORM_T20)
    EMIT(0x800fc, 0x00774533u);
    EMIT(0x80114, 0x00002a42u);
    EMIT(0x80118, 0x01800200u);
    EMIT(0x8011c, 0x00000400u);
#endif
    for (i = 0; i < 42u; i++)
        EMIT(0x8002c,
             config->cabac_state[t30_vmau_context_index(config->slice_type,
                                                        i)]);
    EMIT(0x8007c, 2);

    /* Deblocking. */
    EMIT(0x80040, 1);
    EMIT(0x70060, 4);
    EMIT(0x70000, T30_VRAM_DBLK);
    EMIT(0x70078, 0x13200078u);
    EMIT(0x70074, ((uint32_t)config->mb_height << 16) |
                  config->mb_width);
    EMIT(0x7007c, (uint32_t)config->first_mby << 16);
    EMIT(0x70064, 1);
    EMIT(0x70084, config->output_y);
    EMIT(0x70088, config->output_c);
    EMIT(0x7008c, T30_VRAM_DUMMY);
    EMIT(0x70080, ((uint32_t)config->mb_width << 23) |
                  ((uint32_t)config->mb_width << 8));
    EMIT(0x70068, ((uint32_t)config->slice_type << 3) | 1u);
    EMIT(0x70060, 8);
#if !defined(PLATFORM_T20)
    EMIT(0x70240, 0x68040100u);
    EMIT(0x70244, 0x132c7000u);
#endif

    /* Syntax/data encoder and all 460 H.264 CABAC contexts. */
    EMIT(0x90000, 0);
    EMIT(0x9000c, 0x11);
    EMIT(0x90008, ((uint32_t)config->mb_height << 24) |
                  ((uint32_t)config->mb_width << 16) |
                  ((uint32_t)config->first_mby << 8));
    EMIT(0x90010, 2);
    EMIT(0x90014, 3);
    EMIT(0x90018, ((uint32_t)config->qp << 8) |
                  (config->slice_type ? 0x12u : 0x11u));
    EMIT(0x9001c, T30_VRAM_SDE);
    EMIT(0x90020, 0x1320007cu);
    EMIT(0x90024, config->bitstream);
    for (i = 0; i < 460u; i++) {
        uint32_t state = config->cabac_state[i];
        uint32_t index = state <= 63u ? 63u - state : state - 64u;

        EMIT(0x92000u + i * 4u,
             t30_lps_range[index] | ((state >> 6) & 1u));
    }
    EMIT(0x90004, 2);

    for (i = 0; i < 8u; i++)
        EMIT(scheduler_zero_registers[i], 0);
    EMIT(0x00060, 0x0c0c0400u | ((uint32_t)config->slice_type << 2));
    EMIT(0x00064, 0x97850fceu | config->slice_type);
    if (t30_emit_final(&writer, 0x40000,
                       0xc0000000u | ((uint32_t)config->qp << 8) |
                       ((uint32_t)config->slice_type << 4) |
                       config->raw_format | 0x23u) != 0)
        return -1;

#undef EMIT

    if (pair_count)
        *pair_count = (size_t)(writer.cursor - config->descriptor) / 2u;
    return 0;
}

/*
 * T10 variant: Ingenic T10 JZ NVPU H.264 descriptor builder.
 *
 * The T10 and T20 share one stock libimp, which picks H264E_T10_SliceInit or
 * H264E_T20_SliceInit at run time from the SoC id.  The T10 variant programs
 * an older NVPU: a smaller VRAM layout, the classic MCE motion-estimation
 * block for every slice, flat quantisation tables in VMAU instead of the T20
 * VMAU CABAC contexts, and no T20-only EFE/VMAU/DBLK registers.  Writes to
 * the T20-only registers stop the T10 VDMA walker before its terminal entry,
 * so the job never raises ENDF.
 *
 * Order and values follow H264E_T10_SliceInit (stock T10/T20 libimp 3.12.0,
 * md5 da38e940020f876183a1ed19520f2526) and the command lists the stock
 * stack builds on a T10L (jxh42, 1280x720) - see
 * tests/t30/descriptor_test.c for the comparison against that capture.
 */

#define T10_VRAM_EFE_COEF 0x132c0000u
#define T10_VRAM_TOPMV    0x132c4000u
#define T10_VRAM_TOPPA    0x132c4400u
#define T10_VRAM_MAU      0x132c4800u
#define T10_VRAM_DBLK     0x132c4c00u
#define T10_VRAM_ME       0x132c5000u
#define T10_VRAM_SDE      0x132c5200u
#define T10_VRAM_RAW      0x132f0000u
#define T10_VRAM_DUMMY    0x132ffffcu

/* MCE configuration words that H264E_T10_SliceInit stores verbatim. */
static const uint32_t t10_mce_table[32] = {
    0x00000200u, 0, 0, 0, 0x80003004u, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0x8000b004u, 0, 0, 0, 0x80000000u, 0x8000e020u, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0
};

size_t T10_H264_ReferenceOffset(uint8_t mb_width, int chroma)
{
    /* Reconstructions carry a one-macroblock border: the luma plane is
     * (mb_width + 2) macroblocks wide and the picture starts one macroblock
     * row plus one macroblock in.  Only the MCE reference read (0x50304/
     * 0x50b04) adds it (H264E_T10_SliceInit: plane base + offset, stale
     * I-slice base 0 -> 0x5300/0x2980); the deblocker output (0x70084/
     * 0x70088) takes the plane base and applies the border itself. */
    return ((size_t)mb_width + 3u) * (chroma ? 128u : 256u);
}

size_t T10_H264_ReferencePlaneSize(uint8_t mb_width, uint8_t mb_height,
                                   int chroma)
{
    /* The padded plane, (mb_width + 2) x (mb_height + 2) macroblocks, plus
     * ReferenceOffset() of slack.  The canary overrun measured on a T10L
     * came from passing base + offset to the deblocker (which adds the
     * border itself); with plane bases it stays inside the padded plane,
     * the slack is kept as margin (stock planes are 0x400 larger too). */
    return ((size_t)mb_width + 2u) * ((size_t)mb_height + 2u) *
           (chroma ? 128u : 256u) + T10_H264_ReferenceOffset(mb_width, chroma);
}

int T10_H264_BuildDescriptor(const T30H264SliceConfig *config,
                             size_t *pair_count)
{
    T30DescriptorWriter writer;
    uint32_t padded_stride;
    uint32_t max_qp;
    uint32_t reference_y;
    uint32_t reference_c;
    unsigned int i;

    if (!config || !config->descriptor || !config->cabac_state ||
        !config->mb_width || !config->mb_height ||
        !config->width || !config->height || config->slice_type > 1u ||
        config->descriptor_words < 1270u ||
        !config->output_y || !config->output_c ||
        (config->slice_type &&
         (!config->reference_y || !config->reference_c))) {
        errno = EINVAL;
        return -1;
    }
    writer.cursor = config->descriptor;
    writer.end = config->descriptor + config->descriptor_words;
    padded_stride = (((uint32_t)config->mb_width + 2u) * 16u) & 0xfffu;
    max_qp = config->qp < 39u ? config->qp + 13u : 51u;
    /* reference_* and output_* are padded plane bases; the MCE reads the
     * picture origin inside the border.  The stock I slice still programs
     * the MCE reference (with a stale base); point an I slice at its own
     * reconstruction instead. */
    reference_y = (config->slice_type ? config->reference_y : config->output_y) +
                  (uint32_t)T10_H264_ReferenceOffset(config->mb_width, 0);
    reference_c = (config->slice_type ? config->reference_c : config->output_c) +
                  (uint32_t)T10_H264_ReferenceOffset(config->mb_width, 1);

#define EMIT(reg, value)                                                     \
    do {                                                                     \
        if (t30_emit(&writer, (reg), (value)) != 0)                      \
            return -1;                                                       \
    } while (0)

    EMIT(T30_TCSM_FLUSH, 0);
    EMIT(0x40004, ((uint32_t)config->first_mby << 24) |
                  ((uint32_t)config->last_mby << 8) |
                  ((uint32_t)config->mb_width - 1u));
    EMIT(0x4000c, T10_VRAM_EFE_COEF);
    EMIT(0x40010, config->raw[0]);
    EMIT(0x40014, config->raw[1]);
    EMIT(0x40034, config->raw[2]);
    EMIT(0x40038, (config->stride[0] << 16) | config->stride[1]);
    EMIT(0x40018, T10_VRAM_TOPMV);
    EMIT(0x4001c, T10_VRAM_TOPPA);
    EMIT(0x40020, T10_VRAM_ME);
    EMIT(0x40024, T10_VRAM_MAU);
    EMIT(0x40028, T10_VRAM_DBLK);
    EMIT(0x4002c, T10_VRAM_SDE);
    EMIT(0x40030, T10_VRAM_RAW);
    EMIT(0x40040, max_qp);
    EMIT(0x40044, 0);
    EMIT(0x40048, 0);
    for (i = 0; i < 8u; i++)
        EMIT(0x4004cu + i * 4u, 0);
    EMIT(0x40108, 0);
    EMIT(0x4010c, 0x00400000u);

    /* Classic MCE, programmed for I and P slices alike. */
    EMIT(0x50004, 7);
    EMIT(0x50804, 7);
    EMIT(0x50000, 0x0f7c0999u);
    EMIT(0x5005c, 0xa4);
    for (i = 0; i < 32u; i++)
        EMIT(0x50d00u + i * 4u, t10_mce_table[i]);
    EMIT(0x50030, 0x0cc04000u);
    EMIT(0x50830, 0x0001c000u);
    EMIT(0x50034, 0xa8100500u);
    EMIT(0x50038, 0xa1100500u);
    EMIT(0x5003c, 0x1414fb01u);
    EMIT(0x50044, 0x1fb);
    EMIT(0x50040, 0x1414fb01u);
    EMIT(0x50048, 0x1fb);
    EMIT(0x50304, reference_y);
    EMIT(0x50b04, reference_c);
    EMIT(0x50020, 0);
    EMIT(0x50820, 0);
    EMIT(0x50024, 0);
    EMIT(0x50824, 0);
    EMIT(0x50828, 0);
    EMIT(0x5002c, 0);
    EMIT(0x5082c, 0);
    EMIT(0x5004c, (padded_stride << 16) | 0x1000u);
    EMIT(0x5084c, padded_stride << 16);
    EMIT(0x50050, ((uint32_t)config->mb_height << 20) |
                  ((uint32_t)config->mb_width << 4));
    EMIT(0x50058, 0x13200070u);
    EMIT(0x5000c, 0x13240100u);
    EMIT(0x50054, T10_VRAM_ME);

    /* VMAU with flat (default) 4x4 quantisation tables. */
    EMIT(0x80040, 4);
    EMIT(0x80050, 0x80000b01u);
    EMIT(0x8000c, T10_VRAM_MAU);
    EMIT(0x8005c, T10_VRAM_DUMMY);
    EMIT(0x80058, 0x13200074u);
    EMIT(0x80054, (uint32_t)config->mb_width << 4);
    EMIT(0x80044, 0x01000001u);
    EMIT(0x80078, 0x0b150b15u);
    for (i = 0; i < 16u; i++)
        EMIT(0x88000u + i * 4u, 0x10101010u);
    for (i = 0; i < 32u; i++)
        EMIT(0x88040u + i * 4u, 0x00100010u);

    /* Deblocking. */
    EMIT(0x70060, 4);
    EMIT(0x70000, T10_VRAM_DBLK);
    EMIT(0x70078, 0x13200078u);
    EMIT(0x70074, ((uint32_t)config->mb_height << 16) |
                  config->mb_width);
    EMIT(0x7007c, (uint32_t)config->first_mby << 16);
    EMIT(0x70064, 0x11);
    EMIT(0x70084, config->output_y);
    EMIT(0x70088, config->output_c);
    EMIT(0x7008c, T10_VRAM_DUMMY);
    EMIT(0x70080, ((uint32_t)config->mb_width << 23) |
                  ((uint32_t)config->mb_width << 8));
    EMIT(0x70068, ((uint32_t)config->slice_type << 3) | 1u);
    EMIT(0x70060, 8);

    /* Syntax/data encoder and the 460 CABAC contexts. */
    EMIT(0x90000, 0);
    EMIT(0x9000c, 0x11);
    EMIT(0x90008, ((uint32_t)config->mb_height << 24) |
                  ((uint32_t)config->mb_width << 16) |
                  ((uint32_t)config->first_mby << 8));
    EMIT(0x90010, 2);
    EMIT(0x90014, 3);
    EMIT(0x90018, ((uint32_t)config->qp << 8) |
                  (config->slice_type ? 0x12u : 0x11u));
    EMIT(0x9001c, T10_VRAM_SDE);
    EMIT(0x90020, 0x1320007cu);
    EMIT(0x90024, config->bitstream);
    for (i = 0; i < 460u; i++) {
        uint32_t state = config->cabac_state[i];
        uint32_t index = state <= 63u ? 63u - state : state - 64u;

        EMIT(0x92000u + i * 4u,
             t30_lps_range[index] | ((state >> 6) & 1u));
    }
    EMIT(0x90004, 2);

    for (i = 0; i < 8u; i++)
        EMIT(0x00060u + i * 4u, 0);
    EMIT(0x00060, 0x0c0c0400u | ((uint32_t)config->slice_type << 2));
    EMIT(0x00064, 0x97850fceu | config->slice_type);
#undef EMIT
    if (t30_emit_final(&writer, 0x40000,
                 0xc0000000u | ((uint32_t)config->qp << 8) |
                 ((uint32_t)config->slice_type << 4) |
                 config->raw_format | 0x23u) != 0)
        return -1;

    if (pair_count)
        *pair_count = (size_t)(writer.cursor - config->descriptor) / 2u;
    return 0;
}
