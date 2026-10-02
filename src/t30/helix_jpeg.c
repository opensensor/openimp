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
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <imp/imp_common.h>

#include "dma_alloc.h"
#include "imp_log_int.h"

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

    if (!out || !qt || capacity < HELIX_JPEG_HEADER_SIZE || !width ||
        !height || width > HELIX_JPEG_MAX_DIM || height > HELIX_JPEG_MAX_DIM)
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
    memcpy(p, sos, sizeof(sos));
    p += sizeof(sos);
    return (size_t)(p - out);
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
    int full_bitstream;        /* a limited buffer overflowed */
    uint32_t probe_limit;      /* JPGC_MAX_BS probe, bytes (0 = off) */
    IMPDMABufferInfo probe;
    IMPDMABufferInfo *active;  /* buffer of the last job */
    const char *reason;        /* why the last job failed */
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
#if defined(PLATFORM_T23)
    ret = DMA_AllocDescriptorTop(dma, (int)size, tag);
#else
    ret = DMA_AllocDescriptor(dma, (int)size, tag);
#endif
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
     * never read JPGC_MAX_BS and their libimp does not program it, so there
     * the buffer must hold the whole NV12 picture;
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
    helix_jpeg.probe_limit = helix_env_uint(
        "OPENIMP_HELIX_JPEG_PROBE_MAX_BS_KB", 0u, 1u, 4096u) << 10;
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
        close(helix_jpeg.fd);
        helix_jpeg.fd = -1;
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

void OpenIMP_HelixJpeg_Shutdown(void)
{
    if (helix_jpeg.fd >= 0) {
        if (helix_jpeg.channel.clist) {
            helix_jpeg.channel.workphase = HELIX_WORKPHASE_CLOSE;
            (void)ioctl(helix_jpeg.fd, HELIX_CHANNEL_RELEASE,
                        &helix_jpeg.channel);
        }
        close(helix_jpeg.fd);
    }
    helix_jpeg.fd = -1;
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

/* Bitstream size for a picture.  Without JPGC_MAX_BS (T20/T21/T30) the core
 * writes on regardless, so the buffer holds the whole NV12 picture as in
 * the stock library.  With it (T23) a quarter of NV12 (half or all of it
 * for average luma steps below 16 or 6), at least 256 KiB; a full buffer
 * repeats the job with the NV12 size. */
static uint32_t helix_bitstream_capacity(uint32_t nv12, const uint8_t qt[128],
                                         int full)
{
    uint32_t sum = 0;
    uint32_t capacity;
    unsigned int i;

    for (i = 0; i < 64u; i++)
        sum += qt[i];
    if (full || !helix_jpeg.max_bs || sum < 6u * 64u)
        capacity = nv12;
    else if (sum < 16u * 64u)
        capacity = nv12 / 2u;
    else
        capacity = nv12 / 4u;
    if (capacity < HELIX_BITSTREAM_MIN)
        capacity = HELIX_BITSTREAM_MIN;
    return (capacity + 0xfffu) & ~0xfffu;
}

/* The kept command list + bitstream buffer, grown when too small. */
static int helix_job_buffer(uint32_t capacity)
{
    if (helix_jpeg.job.phys_addr &&
        helix_jpeg.job.size >= HELIX_DESCRIPTOR_AREA + capacity)
        return 0;
    helix_dma_release(&helix_jpeg.job);
    return helix_dma_alloc(&helix_jpeg.job, HELIX_DESCRIPTOR_AREA + capacity,
                           "helix-jpeg-bs");
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
                 "status=0x%08x len=%u guard_bytes_written=%u "
                 "guard_end=%u errno=%d -> %s", helix_jpeg.probe_limit,
                 helix_jpeg.channel.status, helix_jpeg.channel.output_len,
                 written, last, errno,
                 written ? "LIMIT IGNORED (the core wrote past it)"
                         : "limit respected");
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
        /* the kept buffer may be larger (another channel): use all of it */
        capacity = buffer->size - HELIX_DESCRIPTOR_AREA;
        *capacity_used = capacity;
    }
    words = (uint32_t *)(uintptr_t)buffer->virt_addr;
    slice->bitstream = buffer->phys_addr + HELIX_DESCRIPTOR_AREA;
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
    else if (helix_jpeg.channel.output_len >= capacity)
        helix_jpeg.reason = "overflow";
    else
        return 0;
    *overflow = (helix_jpeg.channel.status & HELIX_STAT_BSFULL) ||
                helix_jpeg.channel.output_len >= capacity;
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
    uint32_t capacity;
    uint32_t length;
    uint8_t *output;
    size_t header;
    int overflow;
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

    capacity = helix_bitstream_capacity(nv12, qt, helix_jpeg.full_bitstream);
    *capacity_out = capacity;
    ret = helix_jpeg_job_locked(&slice, capacity, &overflow, capacity_out);
    if (ret == HELIX_RUN_FAILED && overflow && helix_jpeg.max_bs &&
        !helix_jpeg.probe_limit &&
        *capacity_out < ((nv12 + 0xfffu) & ~0xfffu)) {
        /* limited by JPGC_MAX_BS: repeat once with the NV12 size, and keep
         * that size from now on */
        helix_jpeg.full_bitstream = 1;
        capacity = helix_bitstream_capacity(nv12, qt, 1);
        *capacity_out = capacity;
        ret = helix_jpeg_job_locked(&slice, capacity, &overflow, capacity_out);
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
                                   frame->width, frame->height, qt);
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
    stream->timestamp = frame->timestamp;
    stream->frame_type = HW_FRAME_TYPE_I;
    helix_jpeg.pictures++;
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
    struct timespec start;
    const char *path = "-";
    uint32_t capacity = 0;
    int ret;

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
    ret = helix_jpeg_run_locked(frame, qt, stream, &path, &capacity);
    /* the command list + bitstream buffer is kept; copies and probe
     * buffers are per job */
    helix_dma_release(&helix_jpeg.source);
    helix_dma_release(&helix_jpeg.probe);
    if (helix_jpeg.stats) {
        size_t used = 0, total = 0, largest = 0;

        (void)DMA_RmemStats(&used, &total, &largest);
        IMP_LOG_INFO("Encoder", "Helix JPEG stats: job=%u %ux%u %s %s "
                     "status=0x%08x len=%u bytes=%u bs=%u q0=%u %uus "
                     "rmem used=%zu/%zu largest=%zu%s%s"
#if defined(PLATFORM_T23)
                     " act=%u"
#endif
                     , helix_jpeg.jobs, frame->width, frame->height, path,
                     ret == 0 ? "ok" : ret == HELIX_SKIPPED ? "SKIP" : "FAIL",
                     helix_jpeg.channel.status, helix_jpeg.channel.output_len,
                     ret == 0 ? stream->length : 0u, capacity, qt[0],
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

int OpenIMP_HelixJpeg_Reserve(uint32_t width, uint32_t height)
{
    uint8_t qt[128];
    uint32_t aligned_height = (height + 15u) & ~15u;
    size_t used = 0, total = 0, largest = 0;
    int ret;

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
    ret = helix_job_buffer(helix_bitstream_capacity(
        width * aligned_height * 3u / 2u, qt, helix_jpeg.full_bitstream));
    (void)DMA_RmemStats(&used, &total, &largest);
    IMP_LOG_INFO("Encoder", "Helix JPEG: %ux%u channel: bitstream buffer "
                 "%u bytes %s (rmem used %zu of %zu, largest free %zu)",
                 width, height, helix_jpeg.job.size,
                 ret == 0 ? "reserved" : "not reserved, retried at the "
                                         "first picture",
                 used, total, largest);
    pthread_mutex_unlock(&helix_jpeg.lock);
    return ret == 0 ? 0 : -1;
}
