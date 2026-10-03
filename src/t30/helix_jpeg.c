/*
 * Hardware JPEG on the Ingenic Helix VPU (T20, T21, T23, T30).
 *
 * The stock libimp encodes JPEG on the VPU's JPGC block (ijpege ->
 * hwicodec_pf_jpege_t20 -> T20_JPEGE_SliceInit): a VDMA command list loads
 * the Huffman and quantizer tables, points the JPGC at a bitstream buffer
 * and starts the EFE, which reads the NV12 picture and feeds 16x16 MCUs to
 * the JPGC through the VPU SRAM.  The CPU only writes the JFIF header and
 * the EOI.  This file builds the same command list and runs it through the
 * /dev/soc_vpu channel ABI that the native H.264 path uses; the kernel
 * serialises every RUN on the VPU, so JPEG and H.264 jobs interleave
 * picture by picture.  docs/HELIX_JPEG.md documents the interface.
 */

#include "t30/helix_jpeg.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <imp/imp_common.h>

#include "dma_alloc.h"
#include "imp_log_int.h"

#if defined(PLATFORM_T21) && !defined(PLATFORM_T20) && !defined(PLATFORM_T23)
/* T21: the bitstream goes to the buffer the H.264 channels share (the
 * stock "vpuBs", src/t30/helix_bitstream.h) instead of a JPEG buffer of
 * its own. */
#define HELIX_JPEG_SHARED_BS 1
#include "t30/helix_bitstream.h"
#endif
#if defined(PLATFORM_T23)
/* T23: the picture goes to the native H.264 encoder's shared bitstream
 * area when one exists (stock vpuBs + bsbufsem), else to a buffer of its
 * own (src/t30/t23_helix_bs.h). */
#include "t30/t23_helix_bs.h"
#endif

#ifndef OPENIMP_SW_JPEG
#define OPENIMP_SW_JPEG 1
#endif

/* ---- VDMA command list ---- */

#define VDMA_ACFG_VLD        (1u << 31)
#define VDMA_ACFG_TERM       (1u << 30)
#define VDMA_ACFG_IDX(reg)   ((reg) & 0xffffcu)

#define REG_TCSM_FLUSH       0xc0000u
#define REG_EFE_CTRL         0x40000u
#define REG_EFE_GEOM         0x40004u
#define REG_EFE_RAWY_SBA     0x40010u
#define REG_EFE_RAWC_SBA     0x40014u
#define REG_EFE_RAW_DBA      0x40030u
#define REG_EFE_RAWV_SBA     0x40034u
#define REG_EFE_RAW_STRD     0x40038u
#define REG_JPGC_TRIG        0xe0000u
#define REG_JPGC_GLBI        0xe0004u
#define REG_JPGC_STAT        0xe0008u
#define REG_JPGC_BSA         0xe000cu
#define REG_JPGC_P0A         0xe0010u
#define REG_JPGC_NMCU        0xe0028u
#define REG_JPGC_NRSM        0xe002cu
#define REG_JPGC_P0C         0xe0030u
#define REG_JPGC_P1C         0xe0034u
#define REG_JPGC_P2C         0xe0038u
#define REG_JPGC_MAX_BS      0xe0068u
#define REG_JPGC_QMEM        0xe1400u
#define REG_JPGC_HUFE        0xe1800u

/* JPGC_GLBI: 1 opens the core clock before the tables are loaded; the run
 * value selects encode, three components, 4:2:0 and EFE (raster) input. */
#define JPGC_GLBI_OPEN       0x00001u
#define JPGC_GLBI_ENCODE_EFE 0xa0121u
/* JPGC_TRIG: CORE_OPEN | BS_TRIG | PP_TRIG */
#define JPGC_TRIG_START      0x7u
/* Component tables: Y uses quantizer/Huffman set 0, Cb and Cr set 1. */
#define JPGC_P0C_LUMA        0x30u
#define JPGC_PNC_CHROMA      0x07u
#define JPGC_MAX_BS_ENABLE   (1u << 31)
/* EFE_CTRL: EFE_ID_JPEG | EFE_EN | EFE_RUN; the plane format is or-ed in
 * (NV12 is 0x08, already part of 0x400b, NV21 0x0c). */
#define EFE_CTRL_JPEG        0x400bu
#define EFE_CTRL_T23_UNALIGNED (1u << 7)

#define HELIX_SRAM_T21       0x132f0000u
#define HELIX_SRAM_T23       0x131f0000u

#define HELIX_JPEG_HUFFMAN_WORDS 384u
#define HELIX_JPEG_QMEM_WORDS    256u

/* ---- Annex K tables ---- */

/* natural (row-major) index -> zigzag position */
static const uint8_t helix_zigzag[64] = {
    0, 1, 5, 6, 14, 15, 27, 28, 2, 4, 7, 13, 16, 26, 29, 42,
    3, 8, 12, 17, 25, 30, 41, 43, 9, 11, 18, 24, 31, 40, 44, 53,
    10, 19, 23, 32, 39, 45, 52, 54, 20, 22, 33, 38, 46, 51, 55, 60,
    21, 34, 37, 47, 50, 56, 59, 61, 35, 36, 48, 49, 57, 58, 62, 63
};

static const uint8_t helix_luma_quant[64] = {
    16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55,
    14, 13, 16, 24, 40, 57, 69, 56, 14, 17, 22, 29, 51, 87, 80, 62,
    18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92,
    49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99
};

static const uint8_t helix_chroma_quant[64] = {
    17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99,
    24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99
};

static const uint8_t helix_dc_luma_counts[16] = {
    0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0
};
static const uint8_t helix_dc_chroma_counts[16] = {
    0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0
};
static const uint8_t helix_dc_values[12] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
};
static const uint8_t helix_ac_luma_counts[16] = {
    0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d
};
static const uint8_t helix_ac_luma_values[162] = {
    0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,
    0x13,0x51,0x61,0x07,0x22,0x71,0x14,0x32,0x81,0x91,0xa1,0x08,
    0x23,0x42,0xb1,0xc1,0x15,0x52,0xd1,0xf0,0x24,0x33,0x62,0x72,
    0x82,0x09,0x0a,0x16,0x17,0x18,0x19,0x1a,0x25,0x26,0x27,0x28,
    0x29,0x2a,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,
    0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,
    0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,
    0x76,0x77,0x78,0x79,0x7a,0x83,0x84,0x85,0x86,0x87,0x88,0x89,
    0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,
    0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,
    0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,
    0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,
    0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf1,0xf2,0xf3,0xf4,
    0xf5,0xf6,0xf7,0xf8,0xf9,0xfa
};
static const uint8_t helix_ac_chroma_counts[16] = {
    0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77
};
static const uint8_t helix_ac_chroma_values[162] = {
    0x00,0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,
    0x51,0x07,0x61,0x71,0x13,0x22,0x32,0x81,0x08,0x14,0x42,0x91,
    0xa1,0xb1,0xc1,0x09,0x23,0x33,0x52,0xf0,0x15,0x62,0x72,0xd1,
    0x0a,0x16,0x24,0x34,0xe1,0x25,0xf1,0x17,0x18,0x19,0x1a,0x26,
    0x27,0x28,0x29,0x2a,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,
    0x45,0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,
    0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,
    0x75,0x76,0x77,0x78,0x79,0x7a,0x82,0x83,0x84,0x85,0x86,0x87,
    0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,
    0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,
    0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,
    0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,
    0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf2,0xf3,0xf4,
    0xf5,0xf6,0xf7,0xf8,0xf9,0xfa
};

/* ---- table formats ---- */

/* JPGC_QMEM holds a reciprocal per quantizer step q: bits 13..11 a shift
 * k, bits 10..0 round(2^(11+k) / q).  Steps 0 and 1 are stored as is.  The
 * shift chosen for each step follows the stock lookup table (qlook in
 * libimp's ijpege reconfig.c; its selection rule is not a simple error
 * bound), two steps per byte, low nibble first. */
static const uint8_t helix_qmem_shift[128] = {
    0x00, 0x00, 0x10, 0x10, 0x10, 0x20, 0x11, 0x12, 0x10, 0x22, 0x11, 0x11, 0x42, 0x32, 0x33, 0x42,
    0x41, 0x22, 0x33, 0x23, 0x42, 0x32, 0x22, 0x42, 0x43, 0x52, 0x33, 0x24, 0x44, 0x54, 0x43, 0x22,
    0x32, 0x43, 0x43, 0x53, 0x44, 0x44, 0x54, 0x63, 0x53, 0x43, 0x53, 0x54, 0x33, 0x33, 0x53, 0x45,
    0x64, 0x45, 0x53, 0x34, 0x34, 0x64, 0x55, 0x63, 0x35, 0x45, 0x35, 0x65, 0x64, 0x55, 0x63, 0x33,
    0x43, 0x64, 0x74, 0x65, 0x64, 0x65, 0x54, 0x64, 0x45, 0x45, 0x45, 0x45, 0x65, 0x56, 0x64, 0x45,
    0x54, 0x46, 0x54, 0x65, 0x74, 0x76, 0x55, 0x65, 0x74, 0x44, 0x44, 0x44, 0x74, 0x64, 0x75, 0x55,
    0x65, 0x44, 0x64, 0x65, 0x44, 0x56, 0x75, 0x64, 0x65, 0x64, 0x75, 0x74, 0x46, 0x76, 0x64, 0x45,
    0x76, 0x74, 0x76, 0x75, 0x76, 0x54, 0x64, 0x76, 0x45, 0x75, 0x66, 0x76, 0x74, 0x54, 0x54, 0x54,
};

uint32_t HelixJpeg_QmemEntry(uint8_t quantizer)
{
    uint32_t shift;

    if (quantizer <= 1u)
        return quantizer;
    shift = (helix_qmem_shift[quantizer >> 1] >> ((quantizer & 1u) * 4u)) &
            0x7u;
    return (shift << 11) |
           (((1u << (12u + shift)) / quantizer + 1u) >> 1);
}

/* Code assignment of ITU-T T.81 Annex C for a counts/values table. */
static void helix_huffman_codes(const uint8_t counts[16],
                                const uint8_t *values, size_t value_count,
                                uint16_t code_of[256], uint8_t length_of[256])
{
    unsigned int length, i;
    size_t k = 0;
    uint16_t code = 0;

    for (length = 1; length <= 16u; length++) {
        for (i = 0; i < counts[length - 1u] && k < value_count; i++, k++) {
            code_of[values[k]] = code++;
            length_of[values[k]] = (uint8_t)length;
        }
        code <<= 1;
    }
}

/* JPGC_HUFE entry: (length - 1) << 8 | the low eight code bits.  Codes
 * longer than eight bits of the Annex K tables start with ones, which the
 * core supplies. */
static uint32_t helix_hufe_entry(uint16_t code, uint8_t length)
{
    return ((uint32_t)(length - 1u) << 8) | (code & 0xffu);
}

/* 176 words per AC table: run * 10 + size - 1 for runs 0..15 and sizes
 * 1..10, EOB, ZRL, six unused words and RST0..7 (encoded as 16-bit codes
 * 0xffd0..0xffd7); then 16 words per DC table (categories 0..11). */
static void helix_huffman_ac(uint32_t *out, const uint8_t counts[16],
                             const uint8_t values[162])
{
    uint16_t code_of[256];
    uint8_t length_of[256];
    unsigned int run, size, i;

    memset(length_of, 0, sizeof(length_of));
    helix_huffman_codes(counts, values, 162u, code_of, length_of);
    for (run = 0; run < 16u; run++)
        for (size = 1; size <= 10u; size++) {
            unsigned int symbol = (run << 4) | size;

            out[run * 10u + size - 1u] =
                helix_hufe_entry(code_of[symbol], length_of[symbol]);
        }
    out[160] = helix_hufe_entry(code_of[0x00], length_of[0x00]);
    out[161] = helix_hufe_entry(code_of[0xf0], length_of[0xf0]);
    for (i = 162; i < 168u; i++)
        out[i] = 0xfffu;
    for (i = 0; i < 8u; i++)
        out[168u + i] = 0xfd0u + i;
}

static void helix_huffman_dc(uint32_t *out, const uint8_t counts[16])
{
    uint16_t code_of[256];
    uint8_t length_of[256];
    unsigned int i;

    memset(length_of, 0, sizeof(length_of));
    helix_huffman_codes(counts, helix_dc_values, sizeof(helix_dc_values),
                        code_of, length_of);
    for (i = 0; i < 16u; i++)
        out[i] = i < 12u ? helix_hufe_entry(code_of[i], length_of[i])
                         : 0xfffu;
}

void HelixJpeg_HuffmanTable(uint32_t table[HELIX_JPEG_HUFFMAN_WORDS])
{
    helix_huffman_ac(table, helix_ac_luma_counts, helix_ac_luma_values);
    helix_huffman_ac(table + 176, helix_ac_chroma_counts,
                     helix_ac_chroma_values);
    helix_huffman_dc(table + 352, helix_dc_luma_counts);
    helix_huffman_dc(table + 368, helix_dc_chroma_counts);
}

void HelixJpeg_QualityTables(uint32_t quality, uint8_t qt[128])
{
    unsigned int i;
    int scale;

    if (quality < 1u || quality > 100u)
        quality = 75u;
    scale = quality < 50u ? 5000 / (int)quality : 200 - 2 * (int)quality;
    for (i = 0; i < 64u; i++) {
        int luma = (helix_luma_quant[i] * scale + 50) / 100;
        int chroma = (helix_chroma_quant[i] * scale + 50) / 100;

        qt[helix_zigzag[i]] = (uint8_t)(luma < 1 ? 1 : luma > 255 ? 255 : luma);
        qt[64u + helix_zigzag[i]] =
            (uint8_t)(chroma < 1 ? 1 : chroma > 255 ? 255 : chroma);
    }
}

/* ---- command list ---- */

typedef struct {
    uint32_t *words;
    size_t capacity;
    size_t pairs;
} HelixList;

static void helix_put(HelixList *list, uint32_t reg, uint32_t value,
                      uint32_t flags)
{
    if ((list->pairs + 1u) * 2u <= list->capacity) {
        list->words[list->pairs * 2u] = value;
        list->words[list->pairs * 2u + 1u] =
            VDMA_ACFG_VLD | flags | VDMA_ACFG_IDX(reg);
    }
    list->pairs++;
}

static uint32_t helix_huffman[HELIX_JPEG_HUFFMAN_WORDS];

static void helix_huffman_init(void)
{
    HelixJpeg_HuffmanTable(helix_huffman);
}

int HelixJpeg_BuildDescriptor(const HelixJpegSlice *slice, uint32_t *words,
                              size_t capacity_words)
{
    static pthread_once_t huffman_once = PTHREAD_ONCE_INIT;
    HelixList list = { words, capacity_words, 0 };
    uint32_t sram;
    uint32_t ctrl;
    unsigned int i;

    if (!slice || !words || !slice->qt || !slice->mb_width ||
        !slice->mb_height || slice->mb_width > 0x10000u ||
        slice->mb_height > 0x10000u || slice->stride > 0xffffu ||
        (slice->raw_format != HELIX_JPEG_PLANE_NV12 &&
         slice->raw_format != HELIX_JPEG_PLANE_NV21) ||
        (slice->variant != HELIX_JPEG_T21 &&
         slice->variant != HELIX_JPEG_T23) ||
        slice->bitstream_limit >= JPGC_MAX_BS_ENABLE)
        return -1;
    (void)pthread_once(&huffman_once, helix_huffman_init);
    sram = slice->variant == HELIX_JPEG_T23 ? HELIX_SRAM_T23
                                            : HELIX_SRAM_T21;

    helix_put(&list, REG_TCSM_FLUSH, 0, 0);
    helix_put(&list, REG_JPGC_GLBI, JPGC_GLBI_OPEN, 0);
    for (i = 0; i < HELIX_JPEG_HUFFMAN_WORDS; i++)
        helix_put(&list, REG_JPGC_HUFE + i * 4u, helix_huffman[i], 0);
    for (i = 0; i < HELIX_JPEG_QMEM_WORDS; i++)
        helix_put(&list, REG_JPGC_QMEM + i * 4u,
                  i < 128u ? HelixJpeg_QmemEntry(slice->qt[i]) : 0u, 0);
    helix_put(&list, REG_JPGC_STAT, 0, 0);
    helix_put(&list, REG_JPGC_BSA, slice->bitstream, 0);
    helix_put(&list, REG_JPGC_P0A, sram, 0);
    helix_put(&list, REG_JPGC_NMCU,
              slice->mb_width * slice->mb_height - 1u, 0);
    helix_put(&list, REG_JPGC_NRSM, 0, 0);
    helix_put(&list, REG_JPGC_P0C, JPGC_P0C_LUMA, 0);
    helix_put(&list, REG_JPGC_P1C, JPGC_PNC_CHROMA, 0);
    helix_put(&list, REG_JPGC_P2C, JPGC_PNC_CHROMA, 0);
    if (slice->variant == HELIX_JPEG_T23 || slice->bitstream_limit)
        helix_put(&list, REG_JPGC_MAX_BS,
                  slice->bitstream_limit
                      ? JPGC_MAX_BS_ENABLE | slice->bitstream_limit : 0u,
                  0);
    helix_put(&list, REG_JPGC_GLBI, JPGC_GLBI_ENCODE_EFE, 0);
    helix_put(&list, REG_JPGC_TRIG, JPGC_TRIG_START, 0);
    helix_put(&list, REG_EFE_GEOM,
              ((slice->mb_height - 1u) << 16) |
                  ((slice->mb_width - 1u) & 0xffffu), 0);
    helix_put(&list, REG_EFE_RAWY_SBA, slice->raw_y, 0);
    helix_put(&list, REG_EFE_RAWC_SBA, slice->raw_c, 0);
    helix_put(&list, REG_EFE_RAWV_SBA, 0, 0);
    helix_put(&list, REG_EFE_RAW_STRD, (slice->stride << 16) | slice->stride,
              0);
    helix_put(&list, REG_EFE_RAW_DBA, sram, 0);
    ctrl = EFE_CTRL_JPEG | slice->raw_format;
    if (slice->variant == HELIX_JPEG_T23 && (slice->width & 15u))
        ctrl |= EFE_CTRL_T23_UNALIGNED;
    helix_put(&list, REG_EFE_CTRL, ctrl, VDMA_ACFG_TERM);
    if (list.pairs * 2u > capacity_words)
        return -1;
    return (int)list.pairs;
}

/* ---- JFIF header ---- */

static uint8_t *helix_put_dht(uint8_t *p, uint8_t id, const uint8_t counts[16],
                              const uint8_t *values, size_t value_count)
{
    size_t length = 2u + 1u + 16u + value_count;

    *p++ = 0xff;
    *p++ = 0xc4;
    *p++ = (uint8_t)(length >> 8);
    *p++ = (uint8_t)length;
    *p++ = id;
    memcpy(p, counts, 16u);
    p += 16;
    memcpy(p, values, value_count);
    return p + value_count;
}

size_t HelixJpeg_WriteHeader(uint8_t *out, size_t capacity, uint32_t width,
                             uint32_t height, const uint8_t qt[128])
{
    return HelixJpeg_WriteHeaderEx(out, capacity, width, height, qt, 0u);
}

size_t HelixJpeg_WriteHeaderEx(uint8_t *out, size_t capacity, uint32_t width,
                               uint32_t height, const uint8_t qt[128],
                               uint32_t restart_interval)
{
    static const uint8_t app0[] = {
        0xff, 0xd8, 0xff, 0xe0, 0x00, 0x10, 'J', 'F', 'I', 'F', 0x00, 0x01,
        0x01, 0x01, 0x00, 0x48, 0x00, 0x48, 0x00, 0x00
    };
    static const uint8_t sos[] = {
        0xff, 0xda, 0x00, 0x0c, 0x03, 0x01, 0x00, 0x02, 0x11, 0x03, 0x11,
        0x00, 0x3f, 0x00
    };
    uint8_t *p = out;
    unsigned int table;

    if (!out || !qt || !width || !height || width > HELIX_JPEG_MAX_DIM ||
        height > HELIX_JPEG_MAX_DIM || restart_interval > 0xffffu ||
        capacity < HELIX_JPEG_HEADER_SIZE +
                       (restart_interval ? HELIX_JPEG_DRI_SIZE : 0u))
        return 0;
    memcpy(p, app0, sizeof(app0));
    p += sizeof(app0);
    for (table = 0; table < 2u; table++) {
        *p++ = 0xff;
        *p++ = 0xdb;
        *p++ = 0x00;
        *p++ = 0x43;
        *p++ = (uint8_t)table;
        memcpy(p, qt + table * 64u, 64u);
        p += 64;
    }
    {
        const uint8_t sof0[] = {
            0xff, 0xc0, 0x00, 0x11, 0x08,
            (uint8_t)(height >> 8), (uint8_t)height,
            (uint8_t)(width >> 8), (uint8_t)width,
            0x03, 0x01, 0x22, 0x00, 0x02, 0x11, 0x01, 0x03, 0x11, 0x01
        };

        memcpy(p, sof0, sizeof(sof0));
        p += sizeof(sof0);
    }
    p = helix_put_dht(p, 0x00, helix_dc_luma_counts, helix_dc_values,
                      sizeof(helix_dc_values));
    p = helix_put_dht(p, 0x10, helix_ac_luma_counts, helix_ac_luma_values,
                      sizeof(helix_ac_luma_values));
    p = helix_put_dht(p, 0x01, helix_dc_chroma_counts, helix_dc_values,
                      sizeof(helix_dc_values));
    p = helix_put_dht(p, 0x11, helix_ac_chroma_counts,
                      helix_ac_chroma_values, sizeof(helix_ac_chroma_values));
    if (restart_interval) {
        *p++ = 0xff;
        *p++ = 0xdd;
        *p++ = 0x00;
        *p++ = 0x04;
        *p++ = (uint8_t)(restart_interval >> 8);
        *p++ = (uint8_t)restart_interval;
    }
    memcpy(p, sos, sizeof(sos));
    p += sizeof(sos);
    return (size_t)(p - out);
}

/* ---- worst-case bitstream size ---- */

/* Most bits one block can take with a DC and an AC table: the longest DC
 * code plus its magnitude bits, and for the 63 AC positions the most bits
 * per position any AC symbol costs (code plus magnitude bits over the run
 * of zeros and the coefficient it covers; ZRL over 16 positions), plus EOB.
 * The core codes nothing outside these tables (categories 0..11, runs
 * 0..15, sizes 1..10). */
static uint32_t helix_block_worst_bits(const uint8_t dc_counts[16],
                                       const uint8_t ac_counts[16],
                                       const uint8_t ac_values[162])
{
    uint16_t code_of[256];
    uint8_t length_of[256];
    uint32_t dc = 0, cost = 0, positions = 1;
    unsigned int i;

    memset(length_of, 0, sizeof(length_of));
    helix_huffman_codes(dc_counts, helix_dc_values, sizeof(helix_dc_values),
                        code_of, length_of);
    for (i = 0; i < sizeof(helix_dc_values); i++)
        if (length_of[i] && length_of[i] + i > dc)
            dc = length_of[i] + i;
    memset(length_of, 0, sizeof(length_of));
    helix_huffman_codes(ac_counts, ac_values, 162u, code_of, length_of);
    for (i = 1; i < 256u; i++) {
        uint32_t c, n;

        if (!length_of[i])
            continue;
        if (i == 0xf0u) {
            c = length_of[i];
            n = 16u;
        } else if ((i & 15u) && (i & 15u) <= 10u) {
            c = length_of[i] + (i & 15u);
            n = (i >> 4) + 1u;
        } else {
            continue;
        }
        if (c * positions > cost * n) {
            cost = c;
            positions = n;
        }
    }
    return dc + (63u * cost + positions - 1u) / positions + length_of[0];
}

uint32_t HelixJpeg_McuWorstBytes(void)
{
    uint32_t bits = 4u * helix_block_worst_bits(helix_dc_luma_counts,
                                                helix_ac_luma_counts,
                                                helix_ac_luma_values) +
                    2u * helix_block_worst_bits(helix_dc_chroma_counts,
                                                helix_ac_chroma_counts,
                                                helix_ac_chroma_values);

    /* a 0x00 stuffed after every byte */
    return 2u * ((bits + 7u) / 8u);
}

/* ---- runtime: one /dev/soc_vpu channel for the process ---- */

#if defined(PLATFORM_T23)
/* _IOWR('c', n, struct channel_node) with the 88-byte T23 node */
#define HELIX_CHANNEL_REQUEST 0xc0586300u
#define HELIX_CHANNEL_RELEASE 0xc0586301u
#define HELIX_CHANNEL_RUN     0xc0586302u
#else
#define HELIX_CHANNEL_REQUEST 0xc0386300u
#define HELIX_CHANNEL_RELEASE 0xc0386301u
#define HELIX_CHANNEL_RUN     0xc0386302u
#endif
/* RUN waits this long for a free VPU and again for the job.  The helix
 * interrupt handler completes a job only on ENDFLAG or BSFULL: an error
 * interrupt leaves RUN asleep for the whole timeout, with the VPU (H.264)
 * and the lent capture frame blocked.  The stock 20 s is far too long; a
 * 1080p picture takes tens of milliseconds.  OPENIMP_HELIX_JPEG_TIMEOUT_MS
 * (T23 also OPENIMP_T23_HELIX_TIMEOUT_MS) overrides it. */
#define HELIX_JPEG_TIMEOUT_MS 2000u
#if defined(PLATFORM_T20)
/* T20's JZ NVPU: the stock encoder passes RANDOM_ID, as for H.264. */
#define HELIX_JPEG_CORE       0xffffffffu
#else
#define HELIX_JPEG_CORE       0x02000001u /* VPU_HELIX_ID | 1 */
#endif
#define HELIX_CODEC_JPEG_ENC  0x10000u    /* HWJPEGENC */
#define HELIX_WORKPHASE_CLOSE 2u

/* SCH_STAT (soc_vpu helix.h): a JPEG job is complete with ENDFLAG and
 * JPGEND; ENDFLAG alone means the kernel took the length from an H.264
 * register. */
#define HELIX_STAT_DONE       0x11u
#define HELIX_STAT_BSFULL     (1u << 20)
#define HELIX_STAT_ERRORS     ((1u << 2) | (1u << 7) | (1u << 10) | \
                               HELIX_STAT_BSFULL)
/* Consecutive hardware failures after which the hardware path is given up
 * (only with the software encoder to fall back to). */
#define HELIX_JPEG_MAX_FAILURES 3u

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
    uint32_t frame_type;
    uint32_t overflow_cnt;
    uint32_t ivdc_mem_line;
    uint32_t data_threshold;
    uint32_t max_bs_act;
    uint32_t reserved;
    uint64_t time;
#endif
} HelixJpegChannel;

#if defined(PLATFORM_T23)
_Static_assert(sizeof(HelixJpegChannel) == 88, "T23 soc_vpu channel ABI");
#else
_Static_assert(sizeof(HelixJpegChannel) == 56, "soc_vpu channel ABI");
#endif

/* rmem: like the stock encoder, which allocates its bitstream buffer (the
 * NV12 picture size) and command list once per JPEG channel at channel
 * creation (hwicodec_pf_jpege_init_bpool), one buffer with the command
 * list first and the bitstream behind it (the kernel's RUN writes back and
 * invalidates the first MiB from the command list) is allocated when a
 * JPEG channel is created (OpenIMP_HelixJpeg_Reserve), shared by all JPEG
 * channels and kept.  If that failed, the first picture tries again.  A
 * source copy for frames outside rmem is per job.  Allocations keep a
 * reserve free: 1/16 of the arena (min 512 KiB) unless
 * OPENIMP_HELIX_JPEG_RMEM_RESERVE_KB says otherwise. */
#define HELIX_RMEM_RESERVE_MIN  (512u << 10)
#define HELIX_DESCRIPTOR_AREA   0x2000u
/* OPENIMP_HELIX_JPEG_PROBE_MAX_BS_KB=n: device probe for JPGC_MAX_BS */
#define HELIX_PROBE_GUARD       (1024u << 10)
#define HELIX_PROBE_PATTERN     0xa5u
#define HELIX_BITSTREAM_MIN     (256u << 10)

static struct {
    pthread_mutex_t lock;
    int state;                 /* 0 idle, 1 ready, -1 off */
    int fd;
    HelixJpegChannel channel;
    IMPDMABufferInfo job;      /* command list + bitstream, per job */
    IMPDMABufferInfo source;   /* copy of a frame outside rmem, per job */
    uint32_t failures;
    uint32_t pictures;
    uint32_t jobs;
    uint32_t rmem_reserve;
    int stats;                 /* OPENIMP_HELIX_JPEG_STATS=1 */
    int max_bs;                /* JPGC_MAX_BS limits the core */
    uint32_t bs_limit;         /* bitstream buffer cap with max_bs */
    int limit_hit;             /* the last picture reached JPGC_MAX_BS */
    const char *dump_dir;      /* OPENIMP_HELIX_JPEG_DUMP */
    uint32_t dumps;
    uint32_t probe_limit;      /* JPGC_MAX_BS probe, bytes (0 = off) */
    IMPDMABufferInfo probe;
    IMPDMABufferInfo *active;  /* buffer of the last job */
    const char *reason;        /* why the last job failed */
    uint32_t bs_offset;        /* bitstream start behind the command list */
    uint32_t stripe_rows;      /* OPENIMP_HELIX_JPEG_STRIPE_ROWS (0 = auto) */
    uint32_t stripes;          /* jobs of the last picture */
    uint32_t channels;         /* JPEG channels (Reserve .. Release) */
    int borrowed;              /* job is the T23 shared H.264 area */
} helix_jpeg = { .lock = PTHREAD_MUTEX_INITIALIZER, .fd = -1 };

static void helix_dma_release(IMPDMABufferInfo *dma)
{
    if (dma->phys_addr)
        DMA_FreePhys(dma->phys_addr);
    memset(dma, 0, sizeof(*dma));
}

/* helix_jpeg_run_locked results besides 0 and -1 */
#define HELIX_RUN_FAILED (-2)  /* the VPU failed: counts towards giving up */
#define HELIX_SKIPPED    (-3)  /* not enough rmem to spare: soft skip */

/* Allocates a per-job buffer when rmem keeps its reserve afterwards.  The
 * contents are not cleared: the range is invalidated instead, so no dirty
 * line of an earlier user is evicted over what the VPU writes. */
static int helix_dma_alloc(IMPDMABufferInfo *dma, uint32_t size,
                           const char *tag)
{
    size_t used, total, largest;
    int ret;

    memset(dma, 0, sizeof(*dma));
    if (!size || size > INT32_MAX / 2) {
        helix_jpeg.reason = "size";
        return -1;
    }
    if (DMA_RmemStats(&used, &total, &largest) == 0 &&
        largest < (size_t)size + helix_jpeg.rmem_reserve) {
        helix_jpeg.reason = "rmem budget";
        if (!helix_jpeg.stats) {
            static unsigned int warned;

            if (warned++ % 100u == 0u)
                IMP_LOG_WARN("Encoder", "Helix JPEG: skipping a picture: "
                             "%u bytes (%s) would leave less than %u KiB "
                             "of rmem (largest free block %zu, used %zu of "
                             "%zu) [%u]", size, tag,
                             helix_jpeg.rmem_reserve >> 10, largest, used,
                             total, warned);
        }
        return HELIX_SKIPPED;
    }
    /* With the encoder buffers at the top of rmem, clear of the
     * FrameSource pools that come and go at the bottom. */
    ret = DMA_AllocDescriptorTop(dma, (int)size, tag);
    if (ret != 0 || !dma->phys_addr || !dma->virt_addr) {
        memset(dma, 0, sizeof(*dma));
        helix_jpeg.reason = "rmem allocation";
        return HELIX_SKIPPED;
    }
    if (DMA_RmemFlushCache((void *)(uintptr_t)dma->virt_addr, size, 2) != 0) {
        helix_dma_release(dma);
        helix_jpeg.reason = "cache";
        return -1;
    }
    return 0;
}

static int helix_env_flag(const char *name)
{
    const char *value = getenv(name);

    return value && value[0] == '1' && value[1] == '\0';
}

static uint32_t helix_env_uint(const char *name, uint32_t fallback,
                               uint32_t minimum, uint32_t maximum)
{
    const char *value = getenv(name);
    unsigned long number;

    if (!value || !value[0])
        return fallback;
    number = strtoul(value, NULL, 0);
    return number >= minimum && number <= maximum ? (uint32_t)number
                                                  : fallback;
}

static int helix_jpeg_open_locked(void)
{
    uint32_t mdelay;

#if OPENIMP_SW_JPEG
    if (getenv("OPENIMP_HELIX_HW_JPEG") &&
        !strcmp(getenv("OPENIMP_HELIX_HW_JPEG"), "0")) {
        IMP_LOG_INFO("Encoder", "Helix JPEG: off (OPENIMP_HELIX_HW_JPEG=0), "
                     "using the software encoder");
        return -1;
    }
#endif
    mdelay = HELIX_JPEG_TIMEOUT_MS;
#if defined(PLATFORM_T23)
    mdelay = helix_env_uint("OPENIMP_T23_HELIX_TIMEOUT_MS", mdelay, 100u,
                            20000u);
#endif
    mdelay = helix_env_uint("OPENIMP_HELIX_JPEG_TIMEOUT_MS", mdelay, 100u,
                            20000u);
    helix_jpeg.stats = helix_env_flag("OPENIMP_HELIX_JPEG_STATS");
    /* T23 limits the bitstream like its libimp.  The T20/T21/T30 kernels
     * never read JPGC_MAX_BS and their libimp does not program it, so
     * there the picture is bounded by striping instead;
     * OPENIMP_HELIX_JPEG_MAX_BS=1 tries the register there. */
    helix_jpeg.max_bs = HELIX_JPEG_VARIANT == HELIX_JPEG_T23 ||
                        helix_env_flag("OPENIMP_HELIX_JPEG_MAX_BS");
    {
        size_t total = 0;
        uint32_t reserve = HELIX_RMEM_RESERVE_MIN;

        if (DMA_RmemStats(NULL, &total, NULL) == 0 &&
            total / 16u > reserve)
            reserve = (uint32_t)(total / 16u);
        helix_jpeg.rmem_reserve = helix_env_uint(
            "OPENIMP_HELIX_JPEG_RMEM_RESERVE_KB", reserve >> 10, 0u,
            65536u) << 10;
    }
    /* the stock T23 library limits JPEG to its encoder pool (2.4 MB or
     * 600 KB); 1 MiB holds a quality-75 1080p picture several times over */
    helix_jpeg.bs_limit = helix_env_uint("OPENIMP_HELIX_JPEG_BS_KB",
                                         1024u,
                                         256u, 65536u) << 10;
    helix_jpeg.dump_dir = getenv("OPENIMP_HELIX_JPEG_DUMP");
    helix_jpeg.probe_limit = helix_env_uint(
        "OPENIMP_HELIX_JPEG_PROBE_MAX_BS_KB", 0u, 1u, 4096u) << 10;
    /* fewer macroblock rows per job than the buffer allows (testing the
     * striped path) */
    helix_jpeg.stripe_rows = helix_env_uint(
        "OPENIMP_HELIX_JPEG_STRIPE_ROWS", 0u, 1u, 4096u);
    /* kept open after a shutdown, see OpenIMP_HelixJpeg_Shutdown */
    if (helix_jpeg.fd < 0)
        helix_jpeg.fd = open("/dev/soc_vpu", O_RDWR | O_CLOEXEC);
    if (helix_jpeg.fd < 0) {
        IMP_LOG_ERR("Encoder", "Helix JPEG: cannot open /dev/soc_vpu: %s",
                    strerror(errno));
        return -1;
    }
    memset(&helix_jpeg.channel, 0, sizeof(helix_jpeg.channel));
    helix_jpeg.channel.mdelay = mdelay;
    helix_jpeg.channel.codecdir = HELIX_CODEC_JPEG_ENC;
    helix_jpeg.channel.thread_id = -1;
    if (ioctl(helix_jpeg.fd, HELIX_CHANNEL_REQUEST, &helix_jpeg.channel) != 0) {
        IMP_LOG_ERR("Encoder", "Helix JPEG: channel request failed: %s",
                    strerror(errno));
        memset(&helix_jpeg.channel, 0, sizeof(helix_jpeg.channel));
        return -1;
    }
    IMP_LOG_INFO("Encoder", "Helix JPEG: hardware encoder ready channel=%u "
                 "timeout=%ums max_bs=%d rmem_reserve=%uK stats=%d",
                 helix_jpeg.channel.channel_id, mdelay, helix_jpeg.max_bs,
                 helix_jpeg.rmem_reserve >> 10, helix_jpeg.stats);
    return 0;
}

int OpenIMP_HelixJpeg_Available(void)
{
    int state;

    pthread_mutex_lock(&helix_jpeg.lock);
    if (helix_jpeg.state == 0)
        helix_jpeg.state = helix_jpeg_open_locked() == 0 ? 1 : -1;
    state = helix_jpeg.state;
    pthread_mutex_unlock(&helix_jpeg.lock);
    return state > 0;
}

void OpenIMP_HelixJpeg_Release(void)
{
    pthread_mutex_lock(&helix_jpeg.lock);
    if (helix_jpeg.channels)
        helix_jpeg.channels--;
    if (!helix_jpeg.channels) {
        /* as the stock DestroyChn frees the channel's buffer pool; the T21
         * shared buffer stays (System_Exit), the VPU channel stays open */
#if !defined(HELIX_JPEG_SHARED_BS)
        helix_dma_release(&helix_jpeg.job);
#endif
        helix_dma_release(&helix_jpeg.source);
        helix_dma_release(&helix_jpeg.probe);
    }
    pthread_mutex_unlock(&helix_jpeg.lock);
}

void OpenIMP_HelixJpeg_Exit(void)
{
    pthread_mutex_lock(&helix_jpeg.lock);
    OpenIMP_HelixJpeg_Shutdown();
    helix_jpeg.channels = 0;
    pthread_mutex_unlock(&helix_jpeg.lock);
}

/* Releases the channel but keeps /dev/soc_vpu open until the process
 * exits.  close() runs the kernel's release-on-close
 * (soc_vpu.c soc_channel_vpu_release), which picks the VPUs to release by
 * the closing thread's id without a lock: it can hand back and power down
 * the VPU while the native H.264 encoder of this process has a job on it,
 * and that job's own release then puts the VPU on the free list a second
 * time (a self-linked list and a hard hang in soc_vpu_request).  The open
 * descriptor carries no state; the next open reuses it. */
void OpenIMP_HelixJpeg_Shutdown(void)
{
    if (helix_jpeg.fd >= 0 && helix_jpeg.channel.clist) {
        /* a channel release with a VPU in vlist and CLOSE would power the
         * VPU down; this channel holds none between jobs */
        helix_jpeg.channel.vlist = 0;
        helix_jpeg.channel.workphase = HELIX_WORKPHASE_CLOSE;
        (void)ioctl(helix_jpeg.fd, HELIX_CHANNEL_RELEASE,
                    &helix_jpeg.channel);
    }
    memset(&helix_jpeg.channel, 0, sizeof(helix_jpeg.channel));
    helix_dma_release(&helix_jpeg.job);
    helix_dma_release(&helix_jpeg.source);
    helix_dma_release(&helix_jpeg.probe);
    helix_jpeg.state = 0;
    helix_jpeg.failures = 0;
}

/* Copy a frame the VPU cannot address into a source buffer in the
 * framesource layout, replicating the last row into the macroblock
 * padding. */
static int helix_copy_source(const HelixJpegFrame *frame, uint32_t stride,
                             uint32_t aligned_height, uint32_t chroma_offset)
{
    const uint8_t *luma = (const uint8_t *)(uintptr_t)frame->virt_addr;
    const uint8_t *chroma = luma + chroma_offset;
    uint32_t chroma_rows = (frame->height + 1u) / 2u;
    uint32_t luma_size = stride * aligned_height;
    uint8_t *out;
    uint32_t row;
    int ret;

    ret = helix_dma_alloc(&helix_jpeg.source, luma_size + luma_size / 2u,
                          "helix-jpeg-src");
    if (ret != 0)
        return ret;
    out = (uint8_t *)(uintptr_t)helix_jpeg.source.virt_addr;
    /* a capture frame (lent by a video channel, whose OSD pass leaves the
     * cache to CPU readers) was written by DMA: read memory, not stale
     * cached lines */
    if (frame->phys_addr)
        (void)DMA_RmemFlushCache((void *)(uintptr_t)frame->virt_addr,
                                 chroma_offset + stride * chroma_rows, 2);
    memcpy(out, luma, (size_t)stride * frame->height);
    for (row = frame->height; row < aligned_height; row++)
        memcpy(out + (size_t)stride * row,
               luma + (size_t)stride * (frame->height - 1u), stride);
    memcpy(out + luma_size, chroma, (size_t)stride * chroma_rows);
    for (row = chroma_rows; row < aligned_height / 2u; row++)
        memcpy(out + luma_size + (size_t)stride * row,
               chroma + (size_t)stride * (chroma_rows - 1u), stride);
    if (DMA_RmemFlushCache(out, luma_size + luma_size / 2u, 1) != 0) {
        helix_jpeg.reason = "cache";
        return -1;
    }
    return 0;
}

/* The VPU codes whole macroblock rows: a 1080-line picture is read as 1088
 * lines, and the framesource leaves luma rows 1080..1087 and chroma rows
 * 540..543 unwritten.  Those rows share 8x8 chroma blocks with visible
 * rows, so replicate the last visible rows into them (as the H.264 path
 * does) instead of coding stale memory.  The frame was written back just
 * before (T23) or only written by the ISP and IPU, so invalidating the two
 * source rows loses nothing and reads what the ISP wrote.  The padding rows are outside the picture (the H.264
 * encoder writes the same values into a shared frame); rows beyond the
 * frame's buffer size are not written. */
static void helix_pad_rows(const HelixJpegFrame *frame, uint32_t stride,
                           uint32_t aligned_height, uint32_t chroma_offset)
{
    uint8_t *luma = (uint8_t *)(uintptr_t)frame->virt_addr;
    uint8_t *chroma = luma + chroma_offset;
    uint32_t chroma_rows = (frame->height + 1u) / 2u;
    uint32_t chroma_end = chroma_rows;
    uint32_t row;

    (void)DMA_RmemFlushCache(luma + (size_t)stride * (frame->height - 1u),
                             stride, 2);
    for (row = frame->height; row < aligned_height; row++)
        memcpy(luma + (size_t)stride * row,
               luma + (size_t)stride * (frame->height - 1u), stride);
    (void)DMA_RmemFlushCache(luma + (size_t)stride * frame->height,
                             stride * (aligned_height - frame->height), 1);
    while (chroma_end < aligned_height / 2u &&
           (uint64_t)chroma_offset + (uint64_t)stride * (chroma_end + 1u) <=
               frame->size)
        chroma_end++;
    if (chroma_end == chroma_rows)
        return;
    (void)DMA_RmemFlushCache(chroma + (size_t)stride * (chroma_rows - 1u),
                             stride, 2);
    for (row = chroma_rows; row < chroma_end; row++)
        memcpy(chroma + (size_t)stride * row,
               chroma + (size_t)stride * (chroma_rows - 1u), stride);
    (void)DMA_RmemFlushCache(chroma + (size_t)stride * chroma_rows,
                             stride * (chroma_end - chroma_rows), 1);
}

/* Bitstream size for a picture: at most OPENIMP_HELIX_JPEG_BS_KB (default
 * 1 MiB, like the stock JPGC maximum bitstream; the stock T23 library uses
 * its 2.4 MB / 600 KB encoder pool).  With JPGC_MAX_BS (T23) a picture that
 * reaches the limit is repeated in the same buffer with coarser quantizers.
 * Without it (T20/T21/T30: the core writes on regardless) the picture goes
 * in stripes whose worst case fits the buffer (helix_jpeg_stripes_locked),
 * so a buffer smaller than the NV12 picture never overflows. */
static uint32_t helix_bitstream_capacity(uint32_t nv12, const uint8_t qt[128])
{
    uint32_t capacity = nv12;

    (void)qt;
    if (helix_jpeg.bs_limit && capacity > helix_jpeg.bs_limit)
        capacity = helix_jpeg.bs_limit;
    if (capacity < HELIX_BITSTREAM_MIN)
        capacity = HELIX_BITSTREAM_MIN;
    return (capacity + 0xfffu) & ~0xfffu;
}

/* The kept command list + bitstream buffer, grown when too small. */
static int helix_job_buffer(uint32_t capacity)
{
    IMPDMABufferInfo next;
    int ret;

#if defined(HELIX_JPEG_SHARED_BS)
    /* taken for this job, large enough, by OpenIMP_HelixJpeg_EncodeEx */
    if (helix_jpeg.job.phys_addr &&
        helix_jpeg.job.size >= HELIX_DESCRIPTOR_AREA + capacity)
        return 0;
    helix_jpeg.reason = "shared bitstream buffer";
    return -1;
#endif
    if (helix_jpeg.borrowed) {
        /* never grown or freed here: it belongs to the H.264 encoder */
        if (helix_jpeg.job.size >= HELIX_DESCRIPTOR_AREA + capacity)
            return 0;
        helix_jpeg.reason = "shared bitstream area";
        return -1;
    }

    if (helix_jpeg.job.phys_addr &&
        helix_jpeg.job.size >= HELIX_DESCRIPTOR_AREA + capacity)
        return 0;
    /* grow: the old buffer stays until the new one exists */
    ret = helix_dma_alloc(&next, HELIX_DESCRIPTOR_AREA + capacity,
                          "helix-jpeg-bs");
    if (ret == 0) {
        helix_dma_release(&helix_jpeg.job);
        helix_jpeg.job = next;
        return 0;
    }
    /* with JPGC_MAX_BS a smaller buffer is still safe: the core stops at
     * its end and a full picture is repeated with coarser steps */
    if (helix_jpeg.job.phys_addr && helix_jpeg.max_bs &&
        helix_jpeg.job.size > HELIX_DESCRIPTOR_AREA + HELIX_BITSTREAM_MIN) {
        helix_jpeg.reason = NULL;
        return 0;
    }
    return ret;
}

/* Probe buffer: command list, probe_limit bytes of bitstream, then a
 * 1 MiB guard filled with a pattern that shows whether the core stopped at
 * JPGC_MAX_BS. */
static int helix_probe_buffer(void)
{
    uint32_t size = HELIX_DESCRIPTOR_AREA + helix_jpeg.probe_limit +
                    HELIX_PROBE_GUARD;
    uint8_t *guard;
    int ret;

    helix_dma_release(&helix_jpeg.probe);
    ret = helix_dma_alloc(&helix_jpeg.probe, size, "helix-jpeg-probe");
    if (ret != 0)
        return ret;
    guard = (uint8_t *)(uintptr_t)helix_jpeg.probe.virt_addr +
            HELIX_DESCRIPTOR_AREA + helix_jpeg.probe_limit;
    memset(guard, HELIX_PROBE_PATTERN, HELIX_PROBE_GUARD);
    return DMA_RmemFlushCache(guard, HELIX_PROBE_GUARD, 1) == 0 ? 0 : -1;
}

static void helix_probe_report(void)
{
    const uint8_t *guard = (const uint8_t *)(uintptr_t)
        helix_jpeg.probe.virt_addr + HELIX_DESCRIPTOR_AREA +
        helix_jpeg.probe_limit;
    uint32_t written = 0, last = 0, i;

    (void)DMA_RmemFlushCache((void *)guard, HELIX_PROBE_GUARD, 2);
    for (i = 0; i < HELIX_PROBE_GUARD; i++)
        if (guard[i] != HELIX_PROBE_PATTERN) {
            written++;
            last = i + 1u;
        }
    IMP_LOG_INFO("Encoder", "Helix JPEG probe: JPGC_MAX_BS=%u bytes "
                 "status=0x%08x len=%u act=0x%08x guard_bytes_written=%u "
                 "guard_end=%u errno=%d -> %s", helix_jpeg.probe_limit,
                 helix_jpeg.channel.status, helix_jpeg.channel.output_len,
#if defined(PLATFORM_T23)
                 helix_jpeg.channel.max_bs_act,
#else
                 0u,
#endif
                 written, last, errno,
                 written ? "LIMIT IGNORED (the core wrote past it)"
                         : "limit respected");
}

/* A job that ran into JPGC_MAX_BS completes normally (ENDFLAG|JPGEND) with
 * a truncated bitstream.  The T23 kernel returns JPGC_ACT_BS
 * (max_bs_act), whose bit 29 the stock T23 library tests for this
 * (do_channel_process_jpege, IMP_Encoder_InputJpege).  Other kernels do
 * not return it: there a length within 4 KiB of the limit counts as
 * reaching it. */
static int helix_limit_reached(uint32_t capacity)
{
    if (!helix_jpeg.max_bs && !helix_jpeg.probe_limit)
        return 0;
#if defined(PLATFORM_T23)
    if (helix_jpeg.channel.max_bs_act & (1u << 29))
        return 1;
#endif
    return helix_jpeg.channel.output_len + 4096u >= capacity;
}

/* One RUN with a bitstream of capacity bytes.  Returns 0, -1, or
 * HELIX_RUN_FAILED / HELIX_SKIPPED; *overflow is set when the job ran out
 * of bitstream. */
static int helix_jpeg_job_locked(HelixJpegSlice *slice, uint32_t capacity,
                                 int *overflow, uint32_t *capacity_used)
{
    IMPDMABufferInfo *buffer;
    uint32_t *words;
    int pairs;
    int ret;

    *overflow = 0;
    *capacity_used = capacity;
    if (helix_jpeg.probe_limit) {
        ret = helix_probe_buffer();
        if (ret != 0)
            return ret;
        buffer = &helix_jpeg.probe;
        capacity = helix_jpeg.probe_limit;
        *capacity_used = capacity;
    } else {
        ret = helix_job_buffer(capacity);
        if (ret != 0)
            return ret;
        buffer = &helix_jpeg.job;
        /* the kept buffer may be larger (another channel): use all of it,
         * from bs_offset on (stripes, HELIX_JPEG_SHARED_BS) */
        if (helix_jpeg.bs_offset >= buffer->size - HELIX_DESCRIPTOR_AREA) {
            helix_jpeg.reason = "bitstream offset";
            return -1;
        }
        capacity = buffer->size - HELIX_DESCRIPTOR_AREA -
                   helix_jpeg.bs_offset;
        *capacity_used = capacity;
    }
    words = (uint32_t *)(uintptr_t)buffer->virt_addr;
    slice->bitstream = buffer->phys_addr + HELIX_DESCRIPTOR_AREA +
                       (buffer == &helix_jpeg.job ? helix_jpeg.bs_offset
                                                  : 0u);
    slice->bitstream_limit = helix_jpeg.max_bs || helix_jpeg.probe_limit
        ? capacity : 0u;
    pairs = HelixJpeg_BuildDescriptor(slice, words,
                                      HELIX_DESCRIPTOR_AREA /
                                          sizeof(uint32_t));
    if (pairs < 0 || DMA_RmemFlushCache(words, (uint32_t)pairs * 8u, 1) != 0) {
        helix_jpeg.reason = "descriptor";
        return -1;
    }
    helix_jpeg.channel.vpu_id = (int32_t)HELIX_JPEG_CORE;
    helix_jpeg.channel.codecdir = HELIX_CODEC_JPEG_ENC;
    helix_jpeg.channel.dma_addr = buffer->phys_addr;
    helix_jpeg.channel.thread_id = -1;
    helix_jpeg.channel.status = 0;
    helix_jpeg.channel.output_len = 0;
#if defined(PLATFORM_T23)
    helix_jpeg.channel.frame_type = 0;
    helix_jpeg.channel.max_bs_act = 0;
    helix_jpeg.channel.time = 0;
#endif
    helix_jpeg.active = buffer;
    errno = 0;
    ret = ioctl(helix_jpeg.fd, HELIX_CHANNEL_RUN, &helix_jpeg.channel);
    if (helix_jpeg.probe_limit)
        helix_probe_report();
    if (ret != 0)
        helix_jpeg.reason = errno == EBUSY ? "VPU busy" : "run/timeout";
    else if (helix_jpeg.channel.status & HELIX_STAT_BSFULL)
        helix_jpeg.reason = "bitstream full";
    else if (helix_jpeg.channel.status & HELIX_STAT_ERRORS)
        helix_jpeg.reason = "error status";
    else if ((helix_jpeg.channel.status & HELIX_STAT_DONE) !=
             HELIX_STAT_DONE)
        helix_jpeg.reason = "not a JPEG completion";
    else if (!helix_jpeg.channel.output_len)
        helix_jpeg.reason = "no length";
    else if (helix_limit_reached(capacity))
        helix_jpeg.reason = "bitstream limit reached";
    else if (helix_jpeg.channel.output_len >= capacity)
        helix_jpeg.reason = "overflow";
    else
        return 0;
    *overflow = (helix_jpeg.channel.status & HELIX_STAT_BSFULL) ||
                helix_limit_reached(capacity) ||
                helix_jpeg.channel.output_len >= capacity;
    if (*overflow)
        helix_jpeg.limit_hit = 1;
    IMP_LOG_ERR("Encoder", "Helix JPEG: %ux%u %s errno=%d status=0x%08x "
                "len=%u/%u", slice->width,
                slice->mb_height * 16u, helix_jpeg.reason, errno,
                helix_jpeg.channel.status, helix_jpeg.channel.output_len,
                capacity);
    if (*overflow && !helix_jpeg.max_bs)
        IMP_LOG_ERR("Encoder", "Helix JPEG: the core may have written past "
                    "the %u-byte bitstream buffer", capacity);
    return HELIX_RUN_FAILED;
}

/* OPENIMP_HELIX_JPEG_DUMP=dir: the first four pictures as the VPU read
 * them (luma rows, then chroma rows, at the offsets programmed) and the
 * JPEG made of them, for offline comparison. */
static void helix_dump(const HelixJpegFrame *frame, const uint8_t *luma,
                       uint32_t chroma_offset, uint32_t stride,
                       uint32_t aligned_height, const uint8_t *jpeg,
                       uint32_t length)
{
    char path[256];
    FILE *file;
    unsigned int index = helix_jpeg.dumps++;

    snprintf(path, sizeof(path), "%s/helix-%u-%ux%u-s%u-c%u.nv12",
             helix_jpeg.dump_dir, index, frame->width, frame->height, stride,
             chroma_offset);
    file = fopen(path, "wb");
    if (file) {
        (void)DMA_RmemFlushCache((void *)luma, stride * aligned_height, 2);
        (void)DMA_RmemFlushCache((void *)(luma + chroma_offset),
                                 stride * aligned_height / 2u, 2);
        (void)fwrite(luma, 1, (size_t)stride * aligned_height, file);
        (void)fwrite(luma + chroma_offset, 1,
                     (size_t)stride * aligned_height / 2u, file);
        fclose(file);
    }
    snprintf(path, sizeof(path), "%s/helix-%u-%ux%u.jpg", helix_jpeg.dump_dir,
             index, frame->width, frame->height);
    file = fopen(path, "wb");
    if (file) {
        (void)fwrite(jpeg, 1, length, file);
        fclose(file);
    }
    IMP_LOG_INFO("Encoder", "Helix JPEG: dumped picture %u (size %u, pixfmt "
                 "0x%x, chroma at +%u) to %s", index, frame->size,
                 frame->pixfmt, chroma_offset, helix_jpeg.dump_dir);
}

/*
 * T20/T21/T30 have no bitstream limit (JPGC_MAX_BS is ignored: the probe on
 * the T21 PC420 saw the core write 34 KB into a 16 KiB limit).  The stock
 * T21 library points the JPEG bitstream into the 2 MB "vpuBs" shared with
 * H.264, the T20/T30 ones into an NV12-sized buffer, both without any
 * guard; a picture cannot be bounded below several times its NV12 size.
 * OpenIMP encodes a picture whose worst case (every MCU taking
 * HelixJpeg_McuWorstBytes()) does not fit in the buffer in horizontal
 * stripes of whole macroblock rows, each job sized so that its worst case
 * fits in the rest of the buffer: no job can write past the buffer,
 * whatever the picture.  Each job starts with fresh DC predictors; the
 * file joins the stripes with RST0..7 markers and a DRI of the stripe's
 * MCU count.  (T23 programs JPGC_MAX_BS instead.)
 *
 * The core writes its bitstream in 128-byte bursts and drops the last,
 * partial one: on the PC420 the final 1..127 bytes of a job were missing
 * after RUN (also 5 ms later), and the next job's first bytes later landed
 * at that old address.  A single job per picture, as in the stock library,
 * loses that much at the end (decoders conceal it before EOI); a stripe
 * must not, and a small picture loses a visible part.  So every job also
 * encodes one or two macroblock rows more (at least 4 bytes per MCU, so at
 * least 128 bytes behind the stripe's own data) - the next stripe's, or
 * for the last stripe whatever lies behind the picture in rmem (read only,
 * never shown; without such memory the last stripe ends with its job, as
 * in the stock library) - and the stripe is cut after its own MCUs: the
 * entropy-coded data is parsed on the CPU up to that MCU (Huffman codes
 * only) and the last byte padded with 1 bits.  The next job starts behind
 * the lost burst.
 */
#define HELIX_STRIPE_SLACK  256u  /* the core writes whole 128-byte bursts:
                                   * up to 127 bytes past its length */
#define HELIX_STRIPE_PIECES 64u   /* stripes held before a copy-out */
#define HELIX_BURST         128u

typedef struct {
    uint32_t offset;           /* from the bitstream start */
    uint32_t length;           /* the job's output */
    uint32_t index;            /* stripe number in the picture */
    uint32_t mcus;             /* the stripe's own MCUs (0: whole job) */
} HelixStripe;

static uint32_t helix_mcu_worst(void)
{
    static uint32_t bytes;

    if (!bytes)
        bytes = HelixJpeg_McuWorstBytes();
    return bytes;
}

/* Whether the VPU may read [phys, phys + size): inside the reserved arena. */
static int helix_in_rmem(uint32_t phys, uint32_t size)
{
    uint32_t base;
    size_t total;

    if (DMA_Get_RMEM_Base(&base) != 0 ||
        DMA_RmemStats(NULL, &total, NULL) != 0)
        return 0;
    return phys >= base && (uint64_t)(phys - base) + size <= total;
}

/* Bitstream bytes one job of rows macroblock rows may need. */
static uint32_t helix_stripe_bound(uint32_t mb_width, uint32_t rows)
{
    return mb_width * rows * helix_mcu_worst() + HELIX_STRIPE_SLACK;
}

/* Rows a stripe's job encodes after the stripe: at least 128 bytes. */
static uint32_t helix_stripe_extra(uint32_t mb_width)
{
    return mb_width >= 32u ? 1u : 2u;
}

/* Buffer bytes a JPEG channel needs at least: the command list and one
 * stripe of one row with its extra rows. */
static uint32_t helix_stripe_minimum(uint32_t width)
{
    uint32_t mb_width = width / 16u;

    return HELIX_DESCRIPTOR_AREA +
           helix_stripe_bound(mb_width, 1u + helix_stripe_extra(mb_width));
}

/* ---- entropy-coded data: where an MCU ends ---- */

typedef struct {
    uint16_t maxcode[18];      /* largest code of each length, -1 none */
    uint16_t valptr[17];
    uint16_t mincode[17];
    int16_t has[17];
    const uint8_t *values;
    uint8_t fast[256];         /* (length << 4) | index for codes <= 8 bits */
    uint8_t fast_symbol[256];
} HelixHuffDecode;

static void helix_huff_decode_init(HelixHuffDecode *t,
                                   const uint8_t counts[16],
                                   const uint8_t *values)
{
    unsigned int length, i, k = 0;
    uint16_t code = 0;

    memset(t, 0, sizeof(*t));
    t->values = values;
    for (length = 1; length <= 16u; length++) {
        t->valptr[length] = (uint16_t)k;
        t->mincode[length] = code;
        t->has[length] = counts[length - 1u] != 0;
        for (i = 0; i < counts[length - 1u]; i++, k++, code++) {
            if (length <= 8u) {
                unsigned int first = (unsigned int)code << (8u - length);
                unsigned int n = 1u << (8u - length), j;

                for (j = 0; j < n; j++) {
                    t->fast[first + j] = (uint8_t)length;
                    t->fast_symbol[first + j] = values[k];
                }
            }
        }
        t->maxcode[length] = (uint16_t)(code - 1u);
        code <<= 1;
    }
}

typedef struct {
    const uint8_t *data;
    uint32_t size;
    uint32_t position;         /* next byte to load (stuffed stream) */
    uint32_t bits;             /* MSB-aligned */
    int count;                 /* valid bits in bits */
    uint32_t used;             /* unstuffed bits consumed */
    int error;                 /* a code that is none */
} HelixBitReader;

static void helix_bits_fill(HelixBitReader *r)
{
    while (r->count <= 24) {
        uint32_t byte = 0;

        if (r->position < r->size) {
            byte = r->data[r->position++];
            /* a 0xff without its stuffed 0x00 can only be read ahead
             * past the data (dropped burst): taken as data, never used */
            if (byte == 0xffu && r->position < r->size &&
                r->data[r->position] == 0u)
                r->position++;
        }
        r->bits |= byte << (24 - r->count);
        r->count += 8;
    }
}

static uint32_t helix_bits_get(HelixBitReader *r, int n)
{
    uint32_t value;

    if (!n)
        return 0;
    if (r->count < n)
        helix_bits_fill(r);
    value = r->bits >> (32 - n);
    r->bits <<= n;
    r->count -= n;
    r->used += (uint32_t)n;
    return value;
}

static int helix_huff_symbol(HelixBitReader *r, const HelixHuffDecode *t)
{
    unsigned int length;
    uint32_t code;

    if (r->count < 16)
        helix_bits_fill(r);
    length = t->fast[r->bits >> 24];
    if (length) {
        int symbol = t->fast_symbol[r->bits >> 24];

        r->bits <<= length;
        r->count -= (int)length;
        r->used += length;
        return symbol;
    }
    for (length = 9; length <= 16u; length++) {
        code = r->bits >> (32u - length);
        if (t->has[length] && code >= t->mincode[length] &&
            code <= t->maxcode[length]) {
            r->bits <<= length;
            r->count -= (int)length;
            r->used += length;
            return t->values[t->valptr[length] + code - t->mincode[length]];
        }
    }
    r->error = 1;
    return -1;
}

static struct {
    pthread_once_t once;
    HelixHuffDecode dc[2], ac[2];
} helix_parse = { .once = PTHREAD_ONCE_INIT };

static void helix_parse_init(void)
{
    helix_huff_decode_init(&helix_parse.dc[0], helix_dc_luma_counts,
                           helix_dc_values);
    helix_huff_decode_init(&helix_parse.dc[1], helix_dc_chroma_counts,
                           helix_dc_values);
    helix_huff_decode_init(&helix_parse.ac[0], helix_ac_luma_counts,
                           helix_ac_luma_values);
    helix_huff_decode_init(&helix_parse.ac[1], helix_ac_chroma_counts,
                           helix_ac_chroma_values);
}

/* Unstuffed bits of the first mcus 4:2:0 MCUs of data, or -1 when the data
 * ends or breaks before. */
static int64_t helix_mcus_bits(const uint8_t *data, uint32_t size,
                               uint32_t mcus)
{
    HelixBitReader r;
    uint32_t m;
    unsigned int b;

    (void)pthread_once(&helix_parse.once, helix_parse_init);
    memset(&r, 0, sizeof(r));
    r.data = data;
    r.size = size;
    for (m = 0; m < mcus; m++)
        for (b = 0; b < 6u; b++) {
            unsigned int table = b < 4u ? 0u : 1u;
            int symbol = helix_huff_symbol(&r, &helix_parse.dc[table]);
            unsigned int k = 1;

            if (symbol < 0 || symbol > 11)
                return -1;
            (void)helix_bits_get(&r, symbol);
            while (k < 64u) {
                symbol = helix_huff_symbol(&r, &helix_parse.ac[table]);
                if (symbol < 0)
                    return -1;
                if (symbol == 0)
                    break;
                if (symbol == 0xf0) {
                    k += 16u;
                    continue;
                }
                k += ((unsigned int)symbol >> 4) + 1u;
                (void)helix_bits_get(&r, symbol & 15);
            }
            if (k > 64u)
                return -1;
            /* bits past the data are zeros: invalid once used */
            if (r.used > (uint64_t)size * 8u)
                return -1;
        }
    return (int64_t)r.used;
}

/* Stuffed length of the first n unstuffed bytes of data. */
static uint32_t helix_stuffed_length(const uint8_t *data, uint32_t size,
                                     uint32_t n)
{
    uint32_t position = 0;

    while (n && position < size) {
        if (data[position++] == 0xffu && position < size &&
            data[position] == 0u)
            position++;
        n--;
    }
    return position;
}

/* Copies the held stripes into the file, RSTn in front of every stripe
 * but the first, EOI after the last one when final.  A stripe with mcus
 * is cut after its own MCUs, the last byte padded with 1 bits. */
static int helix_stripes_out(uint8_t **file, size_t *length,
                             const HelixStripe *stripes, unsigned int count,
                             int final)
{
    const uint8_t *base = (const uint8_t *)(uintptr_t)
        (helix_jpeg.job.virt_addr + HELIX_DESCRIPTOR_AREA);
    size_t need = *length;
    unsigned int i;
    uint8_t *p;

    for (i = 0; i < count; i++) {
        /* the job's own data is read from here on */
        if (DMA_RmemFlushCache((void *)(base + stripes[i].offset),
                               stripes[i].length, 2) != 0) {
            helix_jpeg.reason = "cache";
            return -1;
        }
        need += stripes[i].length + 2u + (stripes[i].index ? 2u : 0u);
    }
    if (final)
        need += 2u;
    p = realloc(*file, need);
    if (!p) {
        helix_jpeg.reason = "malloc";
        return -1;
    }
    *file = p;
    p += *length;
    for (i = 0; i < count; i++) {
        const uint8_t *data = base + stripes[i].offset;
        uint32_t copy = stripes[i].length;
        int pad = -1;

        if (stripes[i].index) {
            *p++ = 0xff;
            *p++ = (uint8_t)(0xd0u + ((stripes[i].index - 1u) & 7u));
        }
        if (stripes[i].mcus) {
            int64_t bits = helix_mcus_bits(data, stripes[i].length,
                                           stripes[i].mcus);
            uint32_t whole;

            if (bits < 0) {
                helix_jpeg.reason = "stripe data";
                IMP_LOG_ERR("Encoder", "Helix JPEG: stripe %u: no end of "
                            "MCU %u in its %u bytes", stripes[i].index,
                            stripes[i].mcus, stripes[i].length);
                return HELIX_RUN_FAILED;
            }
            whole = (uint32_t)(bits / 8);
            copy = helix_stuffed_length(data, stripes[i].length, whole);
            if (bits % 8) {
                uint32_t used = (uint32_t)(bits % 8);

                if (copy >= stripes[i].length) {
                    helix_jpeg.reason = "stripe data";
                    return HELIX_RUN_FAILED;
                }
                pad = data[copy] | (0xffu >> used);
            }
        }
        memcpy(p, data, copy);
        p += copy;
        if (pad >= 0) {
            *p++ = (uint8_t)pad;
            if (pad == 0xff)
                *p++ = 0x00;
        }
    }
    if (final) {
        *p++ = 0xff;
        *p++ = 0xd9;
    }
    *length = (size_t)(p - *file);
    return 0;
}

/* The picture in stripes (or one job when its worst case fits); on success
 * *file is the heap JFIF file. */
static int helix_jpeg_stripes_locked(HelixJpegSlice *slice,
                                     const HelixJpegFrame *frame,
                                     const uint8_t qt[128], uint8_t **file,
                                     size_t *file_length,
                                     uint32_t *capacity_out)
{
    HelixStripe stripes[HELIX_STRIPE_PIECES];
    const HelixJpegSlice whole = *slice;
    uint32_t capacity;
    uint32_t mb_width = whole.mb_width;
    uint32_t extra = helix_stripe_extra(mb_width);
    uint32_t rows, row, offset = 0, index = 0;
    unsigned int held = 0;
    uint8_t *out;
    size_t length;
    int overflow, ret = -1;

#if !defined(HELIX_JPEG_SHARED_BS)
    /* T20/T30: the channel's buffer, the NV12 picture as in the stock
     * library; a smaller one kept from before still works */
    ret = helix_job_buffer(helix_bitstream_capacity(
        whole.stride * whole.mb_height * 16u * 3u / 2u, qt));
    if (ret != 0 && (!helix_jpeg.job.phys_addr ||
                     helix_jpeg.job.size <
                         helix_stripe_minimum(whole.mb_width * 16u)))
        return ret;
    helix_jpeg.reason = NULL;
#endif
    capacity = helix_jpeg.job.size - HELIX_DESCRIPTOR_AREA;
    *capacity_out = capacity;
    if (helix_stripe_bound(mb_width, whole.mb_height + extra) <= capacity &&
        !helix_jpeg.stripe_rows) {
        rows = whole.mb_height;          /* one job, as the stock library */
    } else {
        rows = capacity > HELIX_STRIPE_SLACK
            ? (capacity - HELIX_STRIPE_SLACK) /
                  (mb_width * helix_mcu_worst())
            : 0u;
        rows = rows > extra ? rows - extra : 0u;
        if (rows > whole.mb_height)
            rows = whole.mb_height;
        if (rows > 0xffffu / mb_width)
            rows = 0xffffu / mb_width;
        if (helix_jpeg.stripe_rows && rows > helix_jpeg.stripe_rows)
            rows = helix_jpeg.stripe_rows;
    }
    if (!rows) {
        helix_jpeg.reason = "bitstream buffer below one macroblock row";
        return -1;
    }
    out = malloc(HELIX_JPEG_HEADER_SIZE + HELIX_JPEG_DRI_SIZE);
    if (!out) {
        helix_jpeg.reason = "malloc";
        return -1;
    }
    length = HelixJpeg_WriteHeaderEx(out, HELIX_JPEG_HEADER_SIZE +
                                              HELIX_JPEG_DRI_SIZE,
                                     frame->width, frame->height, qt,
                                     rows < whole.mb_height
                                         ? rows * mb_width : 0u);
    if (!length) {
        helix_jpeg.reason = "header";
        goto fail;
    }
    for (row = 0; row < whole.mb_height; row += rows, index++) {
        uint32_t n = whole.mb_height - row < rows ? whole.mb_height - row
                                                  : rows;
        /* the next rows push the stripe's end out of the core; behind
         * the last stripe, rows past the picture if that is rmem */
        uint32_t more = whole.mb_height - row - n < extra
            ? whole.mb_height - row - n : extra;

        if (!more &&
            helix_in_rmem(whole.raw_y + row * 16u * whole.stride,
                          (n + extra) * 16u * whole.stride) &&
            helix_in_rmem(whole.raw_c + row * 8u * whole.stride,
                          (n + extra) * 8u * whole.stride))
            more = extra;
        uint32_t bound = helix_stripe_bound(mb_width, n + more);
        uint32_t used;

        if (held == HELIX_STRIPE_PIECES ||
            (uint64_t)offset + bound > capacity) {
            ret = helix_stripes_out(&out, &length, stripes, held, 0);
            if (ret != 0)
                goto fail;
            ret = -1;
            held = 0;
            offset = 0;
        }
        slice->mb_height = n + more;
        slice->raw_y = whole.raw_y + row * 16u * whole.stride;
        slice->raw_c = whole.raw_c + row * 8u * whole.stride;
        helix_jpeg.bs_offset = offset;
        ret = helix_jpeg_job_locked(slice, capacity - offset, &overflow,
                                    &used);
        if (ret != 0)
            goto fail;
        if (helix_jpeg.channel.output_len > bound) {
            /* cannot happen with the tables loaded; never trust the data */
            helix_jpeg.reason = "stripe above its worst case";
            IMP_LOG_ERR("Encoder", "Helix JPEG: stripe %u (%u rows) wrote "
                        "%u bytes, more than its bound %u", index, n + more,
                        helix_jpeg.channel.output_len, bound);
            ret = HELIX_RUN_FAILED;
            goto fail;
        }
        stripes[held].offset = offset;
        stripes[held].length = helix_jpeg.channel.output_len;
        stripes[held].index = index;
        stripes[held].mcus = more ? n * mb_width : 0u;
        held++;
        /* past the burst the core dropped (the next job's first bytes go
         * there) */
        offset = (offset + helix_jpeg.channel.output_len + HELIX_BURST +
                  HELIX_BURST - 1u) & ~(HELIX_BURST - 1u);
    }
    ret = helix_stripes_out(&out, &length, stripes, held, 1);
    if (ret != 0)
        goto fail;
    helix_jpeg.stripes = index;
    helix_jpeg.bs_offset = 0;
    *slice = whole;
    *file = out;
    *file_length = length;
    return 0;
fail:
    helix_jpeg.bs_offset = 0;
    *slice = whole;
    free(out);
    return ret ? ret : -1;
}

static int helix_jpeg_run_locked(const HelixJpegFrame *frame,
                                 const uint8_t qt[128],
                                 HWStreamBuffer *stream, const char **path,
                                 uint32_t *capacity_out)
{
    HelixJpegSlice slice;
    uint32_t stride = frame->width;
    uint32_t aligned_height = (frame->height + 15u) & ~15u;
    uint32_t chroma_offset = frame->chroma_offset
        ? frame->chroma_offset : stride * aligned_height;
    uint32_t nv12 = stride * aligned_height * 3u / 2u;
    uint64_t needed = (uint64_t)chroma_offset +
                      (uint64_t)stride * (aligned_height / 2u);
    uint64_t visible = (uint64_t)chroma_offset +
                       (uint64_t)stride * ((frame->height + 1u) / 2u);
    const uint8_t *header_qt = qt;
    uint8_t coarse[128];
    uint32_t capacity;
    uint32_t length;
    uint8_t *output;
    size_t header;
    int overflow;
    unsigned int attempt;
    int ret;

    memset(&slice, 0, sizeof(slice));
    slice.variant = HELIX_JPEG_VARIANT;
    if (chroma_offset < stride * frame->height ||
        (uint64_t)stride * frame->height * 3u / 2u > frame->size) {
        helix_jpeg.reason = "frame too small";
        return -1;
    }
    /* A capture frame in the framesource layout is read in place even when
     * its reported size (the kernel's sizeimage, width * height * 3 / 2)
     * ends before the padded chroma rows: the VPU then reads up to
     * 15 * width / 2 bytes of chroma padding past it, exactly like the H.264
     * encoder reading the same buffer.  Other layouts must hold every row
     * the VPU reads, or are copied. */
    if (frame->phys_addr &&
        (needed <= frame->size || !frame->chroma_offset)) {
        *path = "direct";
#if defined(PLATFORM_T23)
        /* publish what the CPU wrote (T23 draws OSD lines and mosaics on
         * the CPU; IMP_Encoder_InputJpege sources), as the T23 H.264 path
         * does.  T20/T21/T30 capture frames are only written by the ISP and
         * the IPU, like for their H.264 path. */
        if (DMA_RmemFlushCache((void *)(uintptr_t)frame->virt_addr,
                               (uint32_t)(needed <= frame->size
                                              ? needed : frame->size),
                               1) != 0) {
            helix_jpeg.reason = "cache";
            return -1;
        }
#endif
        if (!frame->chroma_offset && frame->height < aligned_height)
            helix_pad_rows(frame, stride, aligned_height, chroma_offset);
        slice.raw_y = frame->phys_addr;
        slice.raw_c = frame->phys_addr + chroma_offset;
    } else {
        *path = "copy";
        if (visible > frame->size) {
            helix_jpeg.reason = "frame too small";
            return -1;
        }
        ret = helix_copy_source(frame, stride, aligned_height, chroma_offset);
        if (ret != 0)
            return ret;
        slice.raw_y = helix_jpeg.source.phys_addr;
        slice.raw_c = slice.raw_y + stride * aligned_height;
    }
    slice.stride = stride;
    slice.width = frame->width;
    slice.mb_width = frame->width / 16u;
    slice.mb_height = aligned_height / 16u;
    slice.raw_format =
        (frame->pixfmt == PIX_FMT_NV21 || frame->pixfmt == 0x3132564eu)
            ? HELIX_JPEG_PLANE_NV21 : HELIX_JPEG_PLANE_NV12;
    slice.qt = qt;

    if (!helix_jpeg.max_bs && !helix_jpeg.probe_limit) {
        size_t file_length = 0;

        ret = helix_jpeg_stripes_locked(&slice, frame, qt, &output,
                                        &file_length, capacity_out);
        if (ret != 0)
            return ret;
        memset(stream, 0, sizeof(*stream));
        stream->virt_addr = (uint32_t)(uintptr_t)output;
        stream->length = (uint32_t)file_length;
        goto done;
    }
    helix_jpeg.stripes = 1u;
    capacity = helix_bitstream_capacity(nv12, qt);
    *capacity_out = capacity;
    ret = helix_jpeg_job_locked(&slice, capacity, &overflow, capacity_out);
    /* Reached the JPGC_MAX_BS limit: repeat the picture in the same buffer
     * (no allocation) with the steps doubled, then quadrupled, rather than
     * drop it.  The stock T23 library drops it and lowers the channel
     * quality by 5 for the following pictures. */
    for (attempt = 1; attempt <= 2 && ret == HELIX_RUN_FAILED && overflow &&
                      helix_jpeg.max_bs && !helix_jpeg.probe_limit;
         attempt++) {
        unsigned int i;

        for (i = 0; i < 128u; i++) {
            uint32_t step = (uint32_t)qt[i] << attempt;

            coarse[i] = (uint8_t)(step > 255u ? 255u : step);
        }
        slice.qt = coarse;
        header_qt = coarse;
        IMP_LOG_WARN("Encoder", "Helix JPEG: %ux%u reached the %u-byte "
                     "bitstream limit, repeating it with %ux quantizer steps",
                     frame->width, frame->height, *capacity_out,
                     1u << attempt);
        ret = helix_jpeg_job_locked(&slice, capacity, &overflow,
                                    capacity_out);
    }
    if (ret != 0)
        return ret;
    length = helix_jpeg.channel.output_len;
    if (DMA_RmemFlushCache((void *)(uintptr_t)(helix_jpeg.active->virt_addr +
                                               HELIX_DESCRIPTOR_AREA),
                           length, 2) != 0) {
        helix_jpeg.reason = "cache";
        return -1;
    }
    output = malloc(HELIX_JPEG_HEADER_SIZE + (size_t)length + 2u);
    if (!output) {
        helix_jpeg.reason = "malloc";
        return -1;
    }
    header = HelixJpeg_WriteHeader(output, HELIX_JPEG_HEADER_SIZE,
                                   frame->width, frame->height, header_qt);
    if (!header) {
        free(output);
        helix_jpeg.reason = "header";
        return -1;
    }
    memcpy(output + header,
           (const void *)(uintptr_t)(helix_jpeg.active->virt_addr +
                                     HELIX_DESCRIPTOR_AREA),
           length);
    output[header + length] = 0xff;
    output[header + length + 1u] = 0xd9;
    memset(stream, 0, sizeof(*stream));
    stream->virt_addr = (uint32_t)(uintptr_t)output;
    stream->length = (uint32_t)(header + length + 2u);
done:
    stream->timestamp = frame->timestamp;
    stream->frame_type = HW_FRAME_TYPE_I;
    helix_jpeg.pictures++;
    if (helix_jpeg.dump_dir && helix_jpeg.dumps < 4u)
        helix_dump(frame, slice.raw_y == frame->phys_addr && frame->phys_addr
                       ? (const uint8_t *)(uintptr_t)frame->virt_addr
                       : (const uint8_t *)(uintptr_t)
                             helix_jpeg.source.virt_addr,
                   slice.raw_c - slice.raw_y, stride, aligned_height,
                   output, stream->length);
    if (!helix_jpeg.stats &&
        (helix_jpeg.pictures <= 3u || helix_jpeg.pictures % 500u == 0u))
        IMP_LOG_INFO("Encoder", "Helix JPEG: %ux%u -> %u bytes status=0x%08x"
                     " [#%u]", frame->width, frame->height, stream->length,
                     helix_jpeg.channel.status, helix_jpeg.pictures);
    return 0;
}

static uint32_t helix_elapsed_us(const struct timespec *start)
{
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint32_t)((now.tv_sec - start->tv_sec) * 1000000l +
                      (now.tv_nsec - start->tv_nsec) / 1000l);
}

int OpenIMP_HelixJpeg_Encode(const HelixJpegFrame *frame,
                             const uint8_t qt[128], HWStreamBuffer *stream)
{
    return OpenIMP_HelixJpeg_EncodeEx(frame, qt, stream, NULL);
}

int OpenIMP_HelixJpeg_EncodeEx(const HelixJpegFrame *frame,
                               const uint8_t qt[128], HWStreamBuffer *stream,
                               uint32_t *flags)
{
    struct timespec start;
    const char *path = "-";
    uint32_t capacity = 0;
    uint32_t may_skip = flags ? *flags & HELIX_JPEG_MAY_SKIP : 0u;
    int ret;

    if (flags)
        *flags = 0;
    if (!frame || !qt || !stream || !frame->virt_addr ||
        frame->width < HELIX_JPEG_MIN_WIDTH || (frame->width & 15u) ||
        frame->width > HELIX_JPEG_MAX_DIM || frame->height < 16u ||
        (frame->height & 1u) || frame->height > HELIX_JPEG_MAX_DIM)
        return -1;
    if (!OpenIMP_HelixJpeg_Available())
        return -1;
    pthread_mutex_lock(&helix_jpeg.lock);
    if (helix_jpeg.state <= 0) {
        pthread_mutex_unlock(&helix_jpeg.lock);
        return -1;
    }
    clock_gettime(CLOCK_MONOTONIC, &start);
    helix_jpeg.reason = NULL;
    helix_jpeg.jobs++;
    helix_jpeg.channel.status = 0;
    helix_jpeg.channel.output_len = 0;
    helix_jpeg.limit_hit = 0;
    helix_jpeg.stripes = 0;
#if defined(HELIX_JPEG_SHARED_BS)
    {
        /* Hold the shared bitstream buffer from the command list to the
         * copy into the stream, as the stock library holds its bitstream
         * semaphore.  Without a limit the picture goes in stripes: the
         * buffer needs one macroblock row (it is the pool size, 2 MB). */
        uint32_t aligned_height = (frame->height + 15u) & ~15u;
        uint32_t need = !helix_jpeg.max_bs && !helix_jpeg.probe_limit
            ? helix_stripe_minimum(frame->width)
            : HELIX_DESCRIPTOR_AREA + helix_bitstream_capacity(
                  frame->width * aligned_height * 3u / 2u, qt);

        int locked = may_skip
            ? OpenIMP_HelixBitstream_LockTimeout(need, &helix_jpeg.job,
                                                 HELIX_JPEG_BUSY_WAIT_MS)
            : OpenIMP_HelixBitstream_Lock(need, &helix_jpeg.job);

        if (locked != 0) {
            helix_jpeg.reason = locked == -EBUSY ? "VPU busy"
                                                 : "shared bitstream buffer";
            ret = HELIX_SKIPPED;
        } else {
            ret = helix_jpeg_run_locked(frame, qt, stream, &path, &capacity);
            memset(&helix_jpeg.job, 0, sizeof(helix_jpeg.job));
            helix_jpeg.active = NULL;
            OpenIMP_HelixBitstream_Unlock();
        }
    }
#elif defined(PLATFORM_T23)
    {
        uint32_t aligned_height = (frame->height + 15u) & ~15u;
        uint32_t need = HELIX_DESCRIPTOR_AREA + helix_bitstream_capacity(
            frame->width * aligned_height * 3u / 2u, qt);
        IMPDMABufferInfo shared;

        (void)may_skip; /* T23 never skips: the shared area lock waits */

        if (!helix_jpeg.probe_limit &&
            OpenIMP_T23_HelixBs_Lock(need, &shared) == 0) {
            if (helix_jpeg.job.phys_addr) {
                /* the area took over: the buffer of its own goes */
                IMP_LOG_INFO("Encoder", "Helix JPEG: using the shared "
                             "%u-byte H.264 bitstream area, freeing the "
                             "%u-byte JPEG buffer", shared.size,
                             helix_jpeg.job.size);
                helix_dma_release(&helix_jpeg.job);
            }
            helix_jpeg.job = shared;
            helix_jpeg.borrowed = 1;
            ret = helix_jpeg_run_locked(frame, qt, stream, &path, &capacity);
            helix_jpeg.borrowed = 0;
            memset(&helix_jpeg.job, 0, sizeof(helix_jpeg.job));
            helix_jpeg.active = NULL;
            OpenIMP_T23_HelixBs_Unlock();
        } else {
            ret = helix_jpeg_run_locked(frame, qt, stream, &path, &capacity);
        }
    }
#else
    (void)may_skip;
    ret = helix_jpeg_run_locked(frame, qt, stream, &path, &capacity);
#endif
    if (flags)
        *flags = (helix_jpeg.limit_hit ? HELIX_JPEG_LIMIT_HIT : 0u) |
                 (ret == HELIX_SKIPPED ? HELIX_JPEG_SKIPPED : 0u);
    /* the command list + bitstream buffer is kept; copies and probe
     * buffers are per job */
    helix_dma_release(&helix_jpeg.source);
    helix_dma_release(&helix_jpeg.probe);
    if (helix_jpeg.stats) {
        size_t used = 0, total = 0, largest = 0;

        (void)DMA_RmemStats(&used, &total, &largest);
        IMP_LOG_INFO("Encoder", "Helix JPEG stats: job=%u %ux%u %s %s "
                     "status=0x%08x len=%u bytes=%u bs=%u stripes=%u q0=%u "
                     "%uus "
                     "rmem used=%zu/%zu largest=%zu%s%s"
#if defined(PLATFORM_T23)
                     " act=%u"
#endif
                     , helix_jpeg.jobs, frame->width, frame->height, path,
                     ret == 0 ? "ok" : ret == HELIX_SKIPPED ? "SKIP" : "FAIL",
                     helix_jpeg.channel.status, helix_jpeg.channel.output_len,
                     ret == 0 ? stream->length : 0u, capacity,
                     helix_jpeg.stripes, qt[0],
                     helix_elapsed_us(&start), used, total, largest,
                     helix_jpeg.reason ? " reason=" : "",
                     helix_jpeg.reason ? helix_jpeg.reason : ""
#if defined(PLATFORM_T23)
                     , helix_jpeg.channel.max_bs_act
#endif
                     );
    }
    if (ret == 0) {
        helix_jpeg.failures = 0;
    } else if (ret == HELIX_RUN_FAILED &&
               ++helix_jpeg.failures >= HELIX_JPEG_MAX_FAILURES &&
               OPENIMP_SW_JPEG) {
        IMP_LOG_ERR("Encoder", "Helix JPEG: %u consecutive failures, "
                    "switching to the software encoder", helix_jpeg.failures);
        OpenIMP_HelixJpeg_Shutdown();
        helix_jpeg.state = -1;
    }
    pthread_mutex_unlock(&helix_jpeg.lock);
    return ret == 0 ? 0 : -1;
}

#if defined(PLATFORM_T23)
/* The native H.264 encoder created the shared area: the JPEG buffer of
 * its own is freed now rather than at the next picture, when the area
 * holds a picture of the bitstream limit. */
void OpenIMP_HelixJpeg_AreaReady(void)
{
    pthread_mutex_lock(&helix_jpeg.lock);
    if (helix_jpeg.job.phys_addr && !helix_jpeg.borrowed &&
        helix_jpeg.bs_limit &&
        OpenIMP_T23_HelixBs_Size() >= HELIX_DESCRIPTOR_AREA +
            ((helix_jpeg.bs_limit + 0xfffu) & ~0xfffu)) {
        IMP_LOG_INFO("Encoder", "Helix JPEG: pictures go to the shared "
                     "%u-byte H.264 bitstream area, freeing the %u-byte "
                     "JPEG buffer", OpenIMP_T23_HelixBs_Size(),
                     helix_jpeg.job.size);
        helix_dma_release(&helix_jpeg.job);
    }
    pthread_mutex_unlock(&helix_jpeg.lock);
}
#endif

int OpenIMP_HelixJpeg_Reserve(uint32_t width, uint32_t height)
{
    uint8_t qt[128];
    uint32_t aligned_height = (height + 15u) & ~15u;
    size_t used = 0, total = 0, largest = 0;
    int ret;

    /* one per JPEG channel, whatever happens below: the buffers are freed
     * when the last channel is destroyed (OpenIMP_HelixJpeg_Release) */
    pthread_mutex_lock(&helix_jpeg.lock);
    helix_jpeg.channels++;
    pthread_mutex_unlock(&helix_jpeg.lock);
    if (width < HELIX_JPEG_MIN_WIDTH || (width & 15u) ||
        width > HELIX_JPEG_MAX_DIM || height < 16u ||
        height > HELIX_JPEG_MAX_DIM || !OpenIMP_HelixJpeg_Available())
        return -1;
    /* =0: allocate at the first picture instead */
    if (getenv("OPENIMP_HELIX_JPEG_RESERVE_AT_CREATE") &&
        !strcmp(getenv("OPENIMP_HELIX_JPEG_RESERVE_AT_CREATE"), "0"))
        return -1;
    HelixJpeg_QualityTables(75u, qt);
    pthread_mutex_lock(&helix_jpeg.lock);
    if (helix_jpeg.state <= 0) {
        pthread_mutex_unlock(&helix_jpeg.lock);
        return -1;
    }
#if defined(HELIX_JPEG_SHARED_BS)
    {
        uint32_t before = OpenIMP_HelixBitstream_Size();
        uint32_t after;

        ret = OpenIMP_HelixBitstream_Reserve(
            !helix_jpeg.max_bs && !helix_jpeg.probe_limit
                ? helix_stripe_minimum(width)
                : HELIX_DESCRIPTOR_AREA + helix_bitstream_capacity(
                      width * aligned_height * 3u / 2u, qt));
        after = OpenIMP_HelixBitstream_Size();
        (void)DMA_RmemStats(&used, &total, &largest);
        IMP_LOG_INFO("Encoder", "Helix JPEG: %ux%u channel: bitstream in the "
                     "shared %u-byte buffer, %s (rmem used %zu of %zu, "
                     "largest free %zu)", width, height, after,
                     ret != 0 ? "not grown, retried at the first picture"
                     : after == before ? "already large enough" : "grown",
                     used, total, largest);
    }
#else
#if defined(PLATFORM_T23)
    if (OpenIMP_T23_HelixBs_Size() >= HELIX_DESCRIPTOR_AREA +
            helix_bitstream_capacity(width * aligned_height * 3u / 2u, qt)) {
        (void)DMA_RmemStats(&used, &total, &largest);
        IMP_LOG_INFO("Encoder", "Helix JPEG: %ux%u channel: bitstream in the "
                     "shared %u-byte H.264 area (rmem used %zu of %zu, "
                     "largest free %zu)", width, height,
                     OpenIMP_T23_HelixBs_Size(), used, total, largest);
        pthread_mutex_unlock(&helix_jpeg.lock);
        return 0;
    }
#endif
    {
        uint32_t before = helix_jpeg.job.size;

        ret = helix_job_buffer(helix_bitstream_capacity(
            width * aligned_height * 3u / 2u, qt));
        (void)DMA_RmemStats(&used, &total, &largest);
        IMP_LOG_INFO("Encoder", "Helix JPEG: %ux%u channel: shared bitstream "
                     "buffer %u bytes, %s (rmem used %zu of %zu, largest "
                     "free %zu)", width, height, helix_jpeg.job.size,
                     ret != 0 ? "not allocated, retried at the first picture"
                     : helix_jpeg.job.size == before ? "already large enough"
                     : before ? "grown" : "allocated",
                     used, total, largest);
    }
#endif
    pthread_mutex_unlock(&helix_jpeg.lock);
    return ret == 0 ? 0 : -1;
}
