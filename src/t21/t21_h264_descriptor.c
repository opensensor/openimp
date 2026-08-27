/*
 * Ingenic T21 Helix H.264 command-list builder.
 *
 * T21 shares the broad Helix block organization with T30, but its VDMA list
 * is not register-compatible.  The order below is recovered from the T21
 * encoder and checked against command lists produced on real T21N hardware.
 * Only the fixed-function SoC programming belongs here; sensor selection and
 * ISP tuning remain in the sensor tuning binary.
 */

#include "t21_h264_descriptor.h"

#include <errno.h>

#define T21_VDMA_VALID 0x80000000u
#define T21_VDMA_TERM  0x40000000u

#define T21_TCSM_FLUSH 0xc0000u

#define T21_VRAM_TOPPA 0x132c4000u
#define T21_VRAM_TOPMV 0x132c4800u
#define T21_VRAM_MAU   0x132c4c00u
#define T21_VRAM_DBLK  0x132c5000u
#define T21_VRAM_ME    0x132c5400u
#define T21_VRAM_SDE   0x132c5600u
#define T21_VRAM_RAW   0x132f0000u
#define T21_VRAM_DUMMY 0x132ffffcu

typedef struct {
    uint32_t *cursor;
    uint32_t *end;
} T21DescriptorWriter;

static const uint32_t t21_lps_range[64] = {
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

static const uint32_t t21_vmau_interpolation[40] = {
    0x8888801f, 0x40011008, 0x00044444, 0x88888007,
    0x40011001, 0x0000c444, 0x8888803b, 0x40011008,
    0x00044444, 0x88888037, 0x40011008, 0x00044444,
    0x8888802f, 0x40011008, 0x00044444, 0x88888800,
    0x40011002, 0x00014444, 0x88888800, 0x4001100f,
    0x0007c444, 0x88888000, 0x40011008, 0x00044444,
    0x41111100, 0x00000004, 0x41111100, 0x00000004,
    0x41111100, 0x00000004, 0x41111100, 0x00000004,
    0x41111100, 0x00000004, 0x41111100, 0x00000004,
    0x41111100, 0x00000004, 0x41111100, 0x00000004,
};

static const uint32_t t21_quant_matrix[96] = {
    0x000cc100, 0x0009d0b2, 0x000c30cc, 0x0009209d,
    0x0009d0b2, 0x0007c092, 0x0009209d, 0x0007507c,
    0x0007c092, 0x0006e075, 0x0007507c, 0x0006906e,
    0x0006e075, 0x00063069, 0x0006906e, 0x0005f063,
    0x0007c092, 0x0006e075, 0x0007507c, 0x0006906e,
    0x0006e075, 0x00063069, 0x0006906e, 0x0005f063,
    0x00063069, 0x0005905f, 0x0005f063, 0x00055059,
    0x0005905f, 0x00051055, 0x00055059, 0x0004e051,
    0x000b20d7, 0x000970a3, 0x000b20b2, 0x0008d097,
    0x000970a3, 0x0008408d, 0x0008d097, 0x00080084,
    0x0008408d, 0x00078080, 0x00080084, 0x00075078,
    0x00078080, 0x0006e075, 0x00075078, 0x0006b06e,
    0x0008408d, 0x00078080, 0x00080084, 0x00075078,
    0x00078080, 0x0006e075, 0x00075078, 0x0006b06e,
    0x0006e075, 0x0006606b, 0x0006b06e, 0x00061066,
    0x0006606b, 0x0005f061, 0x00061066, 0x0005b05f,
    0x00697510, 0x0071a554, 0x0085c697, 0x008e171a,
    0x0096385c, 0x009e58e1, 0x00a67963, 0x00ae99e5,
    0x0096385c, 0x009e58e1, 0x00a67963, 0x00ae99e5,
    0x00baba67, 0x00c2eae9, 0x00cb0bab, 0x00d32c2e,
    0x006d95d3, 0x0075b5d7, 0x007dd6d9, 0x0081f75b,
    0x008a07dd, 0x008e281f, 0x009638a0, 0x009a58e2,
    0x008a07dd, 0x008e281f, 0x009638a0, 0x009a58e2,
    0x00a26963, 0x00aa89a5, 0x00aeaa26, 0x00b6baa8,
};

static int t21_emit(T21DescriptorWriter *writer, uint32_t reg,
                    uint32_t value)
{
    if ((size_t)(writer->end - writer->cursor) < 2u) {
        errno = ENOSPC;
        return -1;
    }
    *writer->cursor++ = value;
    *writer->cursor++ = T21_VDMA_VALID | (reg & 0xffffcu);
    return 0;
}

static int t21_emit_final(T21DescriptorWriter *writer, uint32_t reg,
                          uint32_t value)
{
    if ((size_t)(writer->end - writer->cursor) < 2u) {
        errno = ENOSPC;
        return -1;
    }
    *writer->cursor++ = value;
    *writer->cursor++ = T21_VDMA_VALID | T21_VDMA_TERM |
                        (reg & 0xffffcu);
    return 0;
}

/* T21 uses the same Helix ME register bank as later SoCs, but not their
 * per-frame interpolation-table upload.  The OEM T21 encoder emits this
 * compact 19-write block for every P slice. */
static int t21_emit_motion_estimation(T21DescriptorWriter *writer,
                                      const T21H264SliceConfig *config)
{
#define MOTION_EMIT(reg, value)                                              \
    do {                                                                      \
        if (t21_emit(writer, (reg), (value)) != 0)                           \
            return -1;                                                        \
    } while (0)

    MOTION_EMIT(0x5010c, T21_VRAM_ME);
    MOTION_EMIT(0x50104, T21_VRAM_TOPMV);
    MOTION_EMIT(0x50108, 0x13200070u);
    MOTION_EMIT(0x50060, ((uint32_t)config->height - 1u) << 16 |
                         ((uint32_t)config->width - 1u));
    MOTION_EMIT(0x50064, (config->stride[1] << 16) |
                         config->stride[0]);
    MOTION_EMIT(0x50010, 0x800a8021u);
    MOTION_EMIT(0x50040, 0x873f5008u);
    MOTION_EMIT(0x50044, 0);
    MOTION_EMIT(0x50048, 0x03fc03fcu);
    MOTION_EMIT(0x5006c, config->reference_y);
    MOTION_EMIT(0x50070, config->reference_c);
    MOTION_EMIT(0x50110, config->raw[0]);
    MOTION_EMIT(0x50114, config->raw[1]);
    MOTION_EMIT(0x50074, 0);
    MOTION_EMIT(0x50078, 0);
    MOTION_EMIT(0x50068, 0);
    MOTION_EMIT(0x5004c, 0x88080303u);
    MOTION_EMIT(0x5007c, 0);
    MOTION_EMIT(0x50000, 0x547fdfb1u);

#undef MOTION_EMIT
    return 0;
}

static unsigned int t21_vmau_context_index(uint8_t slice_type,
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

static unsigned int t21_mau_context_index(unsigned int index)
{
    if (index < 4u)
        return index + 0x55u;
    if (index < 0x13u)
        return index + 0x65u;
    if (index < 0x22u)
        return index + 0x93u;
    if (index < 0x2cu)
        return index + 0xc1u;
    if (index < 0x30u)
        return index + 0x2du;
    if (index < 0x3eu)
        return index + 0x48u;
    if (index <= 0x4bu)
        return index + 0x77u;
    if (index <= 0x55u)
        return index + 0xa1u;
    if (index < 0x5au)
        return index + 7u;
    if (index <= 0x68u)
        return index + 0x2cu;
    if (index <= 0x77u)
        return index + 0x5au;
    if (index <= 0x81u)
        return index + 0x7fu;
    if (index <= 0x85u)
        return index - 0x21u;
    if (index < 0x89u)
        return index + 0x0fu;
    if (index <= 0x8bu)
        return index + 0x49u;
    if (index <= 0x94u)
        return index + 0x75u;
    if (index <= 0x98u)
        return index - 0x30u;
    if (index <= 0xa6u)
        return index - 1u;
    if (index < 0xb5u)
        return index + 0x2eu;
    if (index < 0xbfu)
        return index + 0x55u;
    return index + 0xd3u;
}

int T21_H264_BuildDescriptor(const T21H264SliceConfig *config,
                             size_t *pair_count)
{
    T21DescriptorWriter writer;
    uint32_t aligned_width;
    uint32_t aligned_height;
    uint32_t min_qp;
    uint32_t max_qp;
    uint32_t crop_flag;
    unsigned int i;

    if (!config || !config->descriptor || !config->cabac_state ||
        !config->mb_width || !config->mb_height || !config->width ||
        !config->height || config->slice_type > 1u ||
        config->descriptor_words < (config->slice_type ? 2060u : 2022u) ||
        (config->slice_type &&
         (!config->reference_y || !config->reference_c)) ||
        !config->raw[0] ||
        !config->raw[1] || !config->output_y || !config->output_c ||
        !config->bitstream || !config->scratch_base) {
        errno = EINVAL;
        return -1;
    }
    writer.cursor = config->descriptor;
    writer.end = config->descriptor + config->descriptor_words;
    aligned_width = (uint32_t)config->mb_width * 16u;
    aligned_height = (uint32_t)config->mb_height * 16u;
    min_qp = config->qp > 12u ? config->qp - 12u : 0u;
    max_qp = config->qp < 39u ? config->qp + 13u : 51u;
    crop_flag = (config->height & 15u) != 0u ? 0x80u : 0u;

#define EMIT(reg, value)                                                     \
    do {                                                                     \
        if (t21_emit(&writer, (reg), (value)) != 0)                         \
            return -1;                                                       \
    } while (0)

    EMIT(T21_TCSM_FLUSH, 0);
    EMIT(0x40004, ((uint32_t)config->first_mby << 24) |
                  ((uint32_t)config->last_mby << 8) |
                  ((uint32_t)config->mb_width - 1u));
    EMIT(0x4000c, 0x132c0000u);
    EMIT(0x40010, config->raw[0]);
    EMIT(0x40014, config->raw[1]);
    EMIT(0x40034, config->raw[2]);
    EMIT(0x40038, (config->stride[0] << 16) | config->stride[1]);
    EMIT(0x40018, T21_VRAM_TOPMV);
    EMIT(0x4001c, T21_VRAM_TOPPA);
    EMIT(0x40020, T21_VRAM_ME);
    EMIT(0x40024, T21_VRAM_MAU);
    EMIT(0x40028, T21_VRAM_DBLK);
    EMIT(0x4002c, T21_VRAM_SDE);
    EMIT(0x40030, T21_VRAM_RAW);
    EMIT(0x40040, max_qp);
    for (i = 0; i < 10u; i++)
        EMIT(0x40044u + i * 4u, 0);
    EMIT(0x4006c, 0x000c5800u);
    EMIT(0x40120, 0);
    EMIT(0x40108, 0);
    EMIT(0x4010c, 0x00400000u);
    EMIT(0x40074, (max_qp << 24) | (min_qp << 16) |
                  ((uint32_t)config->qp << 8));
    for (i = 0; i < 7u; i++)
        EMIT(0x40078u + i * 4u, 0);
    EMIT(0x400c0, 0x060407c1u);
    EMIT(0x400c4, 0x61615921u);
    EMIT(0x400c8, 0x12449240u);
    EMIT(0x400cc, 0x12492492u);
    EMIT(0x400d0, 0x0a0006dbu);
    EMIT(0x400d4, 0x0f0a0505u);
    EMIT(0x400d8, 0);
    EMIT(0x400dc, 0x00300180u);
    EMIT(0x400e0, 0x000c0060u);
    EMIT(0x400e4, 0x000c0060u);
    EMIT(0x400ac, 0x00001400u);
    EMIT(0x400b0, 0xaf8c7864u);
    EMIT(0x400b4, 0x8c7d6432u);
    EMIT(0x400b8, 0xe1af9678u);
    EMIT(0x400bc, 0x003e3c3au);
    EMIT(0x400e8, 0x0c25bd33u);
    EMIT(0x400ec, 0x10f65409u);
    EMIT(0x400f0, 0x37465405u);

    EMIT(0x10004, 0x20000000u | ((uint32_t)config->height << 14) |
                  config->width);
    EMIT(0x10008, 0);
    EMIT(0x1000c, 0);
    EMIT(0x10010, ((uint32_t)config->mb_width * 17u << 16) |
                  ((uint32_t)config->mb_width * 34u));
    EMIT(0x10014, config->reference_y);
    EMIT(0x10018, config->reference_c);
    EMIT(0x1001c, ((uint32_t)config->width << 16) |
                  ((uint32_t)config->width >> 1));
    EMIT(0x10020, 0);
    EMIT(0x10024, 0);
    EMIT(0x10028, 0);
    EMIT(0x1002c, 0);
    EMIT(0x10000, 0x20u);

    if (config->slice_type &&
        t21_emit_motion_estimation(&writer, config) != 0)
        return -1;

    EMIT(0x80040, 4);
    EMIT(0x80050, config->slice_type ? 0x80000b00u : 0x80001b00u);
    EMIT(0x8000c, T21_VRAM_MAU);
    EMIT(0x8005c, T21_VRAM_DUMMY);
    EMIT(0x80058, 0x13200074u);
    EMIT(0x80054, (aligned_height << 16) | aligned_width);
    EMIT(0x80044, 0x01000001u);
    EMIT(0x80078, 0x0b150b15u);
    EMIT(0x8019c, 0x0b5552d5u);
    EMIT(0x80028, T21_VRAM_TOPPA);
    EMIT(0x80030, ((uint32_t)config->last_mby << 24) |
                  (((uint32_t)config->mb_width - 1u) << 16) |
                  (config->slice_type ? 0x8002u : 0x8001u));
    EMIT(0x80034, 0x0000c200u);
    EMIT(0x80038, 0xcc000000u);
    EMIT(0x8003c, 0);
    EMIT(0x800d8, 0);
    EMIT(0x800dc, 0);
    EMIT(0x800fc, 0x55000004u | ((uint32_t)config->last_mby << 8));
    EMIT(0x80114, ((uint32_t)config->qp * 2u << 16) | 0x11u);
    EMIT(0x80118, 0);
    EMIT(0x8011c, 0);
    EMIT(0x80194, 0x23000000u);
    EMIT(0x8007c, 0xffff8002u);
    for (i = 0; i < 40u; i++)
        EMIT(0x80198, t21_vmau_interpolation[i]);
    for (i = 0; i < 42u; i++)
        EMIT(0x8002c,
             config->cabac_state[t21_vmau_context_index(config->slice_type,
                                                        i)]);
    for (i = 0; i < 225u; i++)
        EMIT(0x80174, config->cabac_state[t21_mau_context_index(i)]);
    EMIT(0x801c0, 0x70000006u);
    EMIT(0x801c4, 0x44444444u);
    EMIT(0x801c8, 0);
    EMIT(0x801cc, (uint32_t)config->last_mby << 16);
    EMIT(0x801d0, 0);
    EMIT(0x801d4, 0);
    for (i = 0; i < 96u; i++)
        EMIT(0x80800u + i * 4u, t21_quant_matrix[i]);

    EMIT(0x80040, 1);
    EMIT(0x7005c, 0x00000c0cu);
    EMIT(0x70060, 8);
    EMIT(0x90000, 0);
    EMIT(0x9000c, 0x11);
    EMIT(0x90008, ((uint32_t)config->mb_height << 24) |
                  ((uint32_t)config->mb_width << 16) |
                  ((uint32_t)config->first_mby << 8));
    EMIT(0x90010, 2);
    EMIT(0x90014, 3);
    EMIT(0x90018, ((uint32_t)config->qp << 8) |
                  (config->slice_type ? 0x32u : 0x31u));
    EMIT(0x9001c, T21_VRAM_SDE);
    EMIT(0x90020, 0x1320007cu);
    EMIT(0x90024, config->bitstream);
    for (i = 0; i < 460u; i++) {
        uint32_t state = config->cabac_state[i];
        uint32_t index = state <= 63u ? 63u - state : state - 64u;

        EMIT(0x92000u + i * 4u,
             t21_lps_range[index] | ((state >> 6) & 1u));
    }
    EMIT(0x90004, 2);

    EMIT(0x30000, ((uint32_t)config->last_mby << 8) |
                  ((uint32_t)config->mb_width - 1u));
    EMIT(0x30004, config->bitstream & ~0x7fu);
    EMIT(0x30018, config->scratch_base);
    EMIT(0x3004c, config->scratch_base + 0x30000u);
    EMIT(0x30050, config->scratch_base + 0xb0000u);
    EMIT(0x30058, config->scratch_base + 0x150000u);
    EMIT(0x30054, config->scratch_base + 0xd0000u);
    EMIT(0x30040, 0x400u);
    EMIT(0x30024, 1);
    for (i = 0; i < 8u; i++)
        EMIT(0x00060u + i * 4u, 0);
    EMIT(0x00060, 0x08080400u |
                  ((uint32_t)config->slice_type << 2));
    EMIT(0x00064, 0x97850f02u | config->slice_type);
    EMIT(0x60004, ((uint32_t)config->height << 14) | config->width);
    EMIT(0x60008, config->output_y);
    EMIT(0x6000c, config->output_c);
    EMIT(0x60010, ((uint32_t)config->width << 16) |
                  ((uint32_t)config->width >> 1));
    EMIT(0x60014, config->reference_y);
    EMIT(0x60018, config->reference_c);
    EMIT(0x6001c, 0);
    EMIT(0x60020, 0);
    EMIT(0x60000, 0x20u);
    EMIT(0xb0004, ((uint32_t)config->height - 1u) << 16 |
                  ((uint32_t)config->width - 1u));
    EMIT(0xb0008, config->raw[0]);
    EMIT(0xb000c, config->raw[1]);
    EMIT(0xb0014, config->reference_y);
    EMIT(0xb0018, config->reference_c);
    EMIT(0xb0030, config->raw[0]);
    EMIT(0xb0034, config->raw[1]);
    EMIT(0xb0010, (config->stride[0] << 16) | config->stride[1]);
    EMIT(0xb001c, 0x180u);
    EMIT(0xb0020, 0x00600060u);
    EMIT(0xb0000, 0x0002ffbdu);
    if (t21_emit_final(&writer, 0x40000,
                       0xc0000000u | ((uint32_t)config->qp << 8) |
                       ((uint32_t)config->slice_type << 4) |
                       crop_flag | config->raw_format | 0x23u) != 0)
        return -1;

#undef EMIT

    if (pair_count)
        *pair_count = (size_t)(writer.cursor - config->descriptor) / 2u;
    return 0;
}
