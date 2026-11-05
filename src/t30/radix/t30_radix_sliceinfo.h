/*
 * T30 Radix (H.265 encoder) per-picture parameter block.
 *
 * The vendor libimp 1.0.5 hands a 0x1204-byte block to H265E_T30_SliceInit
 * (0x3ad5c), which turns it into the Radix command list.  The field names and
 * offsets below are those printed by the vendor's own debug dumper
 * dump_t30_h265_sliceinfo (0x3e2d0; format strings at .rodata 0x1475d0 ..
 * 0x147f54, e.g. "frame_type = %d" at 0x147680), and the block size is the
 * memset/copy length in hwicodec_pf_h265e_t30_enc (0x3d858) and the malloc in
 * hwicodec_pf_h265e_t30_init (0x3ddec).  Several multi-byte fields are not
 * naturally aligned, so the block is kept as bytes and read/written through
 * the little-endian accessors at the end of this header.
 */
#ifndef T30_RADIX_SLICEINFO_H
#define T30_RADIX_SLICEINFO_H

#include <stdint.h>
#include <string.h>

#define T30_RADIX_SI_SIZE 0x1204u

/* addresses (u32) */
#define RSI_DES_VA            0x000  /* command list, CPU address */
#define RSI_DES_PA            0x004  /* command list, bus address */
#define RSI_RAW_Y_PA          0x008  /* NV12 source luma */
#define RSI_RAW_C_PA          0x00c  /* NV12 source chroma */
#define RSI_DST_Y_PA          0x010  /* reconstruction luma */
#define RSI_DST_C_PA          0x014  /* reconstruction chroma */
#define RSI_REF_Y_PA          0x018  /* reference luma (P only) */
#define RSI_REF_C_PA          0x01c  /* reference chroma (P only) */
#define RSI_BITSTREAM_PA      0x020  /* slice data output */
/* picture */
#define RSI_FRAME_TYPE        0x024  /* u8: 2 = I, otherwise P (1) */
#define RSI_RAW_FORMAT        0x025  /* u8: 0 NV12, 1 NV21 (enc: 4 -> 0, 5 -> 1) */
#define RSI_FRAME_WIDTH       0x026  /* u16 */
#define RSI_FRAME_HEIGHT      0x028  /* u16 */
#define RSI_FRAME_Y_STRIDE    0x02a  /* u16 */
#define RSI_FRAME_C_STRIDE    0x02c  /* u16 */
#define RSI_DST_Y_STRIDE      0x02e  /* u16 */
#define RSI_DST_C_STRIDE      0x030  /* u16 */
#define RSI_REF_Y_STRIDE      0x032  /* u16 */
#define RSI_REF_C_STRIDE      0x034  /* u16 */
#define RSI_FRAME_QP          0x036  /* u8 */
#define RSI_FRAME_CQP_OFFSET  0x037  /* s8 */
#define RSI_TU_SPLIT_EN       0x038
#define RSI_TIME_OUT_THRESHOLD 0x039
#define RSI_EFE_MODE          0x03a
#define RSI_ROI_INFO          0x03b  /* 16 x {en, md, qp(s8), lmbx, rmbx, umby, bmby} */
#define RSI_ROI_STRIDE        7
/* QP generator */
#define RSI_QPG_MAX_QP        0x0ab
#define RSI_QPG_MIN_QP        0x0ac
#define RSI_QP_MODE           0x0ad
#define RSI_QPG_CU32_QP_MODE  0x0ae
#define RSI_QP_TABLE_LEN      0x0b0  /* u16 */
#define RSI_QP_TAB_EN         0x0b2
#define RSI_QP_TABLE          0x0b4  /* u32 CPU pointer to qp_table_len words */
#define RSI_QP_TABLE_CU32_MODE 0x0b8
#define RSI_CRP_CU16_THRESHOLD 0x0ba /* u16[7] */
#define RSI_CRP_CU32_THRESHOLD 0x0c8 /* u16[7] */
#define RSI_CRP_CU16_OFFSET   0x0d6  /* s8[8] */
#define RSI_CRP_CU32_OFFSET   0x0de  /* s8[8] */
#define RSI_CRP_FILTER_LIMIT_THRESHOLD 0x0e6
#define RSI_CRP_FILTER_TOP_THRESHOLD   0x0e7
#define RSI_CRP_FILTER_LEFT_THRESHOLD  0x0e8
#define RSI_CRP_FILTER_RIGHT_THRESHOLD 0x0e9
#define RSI_SAS_CU16_THRESHOLD 0x0ea /* u16[6] */
#define RSI_SAS_CU32_THRESHOLD 0x0f6 /* u16[6] */
#define RSI_SAS_CU16_OFFSET   0x102  /* s8[7] */
#define RSI_SAS_CU32_OFFSET   0x109  /* s8[7] */
/* loop filters, entropy */
#define RSI_DBLK_EN           0x110
#define RSI_SAO_EN            0x111
#define RSI_SAO_Y_FLAG        0x112
#define RSI_SAO_C_FLAG        0x113
#define RSI_DBLK_GRAY_EN      0x114
#define RSI_BETA_OFFSET_DIV2  0x115  /* s8 */
#define RSI_TC_OFFSET_DIV2    0x116  /* s8 */
#define RSI_USE_DQP_FLAG      0x117
#define RSI_DQP_MAX_DEPTH     0x118
#define RSI_SIGN_HIDE_FLAG    0x119
#define RSI_MERGE_CAND_NUM    0x11a
#define RSI_CONTEXT_TYPE      0x11b
#define RSI_STATE             0x11c  /* u32 CPU pointer: CABAC context states */
#define RSI_TFM_BUF_EN        0x120
#define RSI_SSE_MASK          0x121
/* mode decision, motion search */
#define RSI_INTER_MODE        0x122  /* u8[4][4] */
#define RSI_HALF_PIXEL_SECH_EN 0x132
#define RSI_QUART_PIXEL_SECH_EN 0x133
#define RSI_REFINE_CU8_MODE   0x134
#define RSI_REFINE_CU16_MODE  0x135
#define RSI_REFINE_CU32_MODE  0x136
#define RSI_MCE_SCL_MODE      0x137
#define RSI_IPRED_SIZE        0x138
#define RSI_SOBEL_SIZE        0x139
#define RSI_SOBEL_EN          0x13a
#define RSI_MD_BC_CLOSE       0x13b  /* written by the builder: !sobel_en */
#define RSI_PPRED_SLV_DIF     0x13c
#define RSI_PPRED_MODE_NUM    0x13d
#define RSI_IPRED_SLV_EN      0x13e
#define RSI_PPRED_SLV_EN      0x13f
#define RSI_INTRA_MODE        0x140  /* u8[5] */
#define RSI_PPRED8_MODE_NUM   0x145
#define RSI_PPRED8_SLV_EN     0x146
#define RSI_INTRA8_MODE       0x147  /* u8[5] */
#define RSI_PPRED16_MODE_NUM  0x14c
#define RSI_PPRED16_SLV_EN    0x14d
#define RSI_INTRA16_MODE      0x14e  /* u8[5] */
#define RSI_PPRED32_MODE_NUM  0x153
#define RSI_PPRED32_SLV_EN    0x154
#define RSI_INTRA32_MODE      0x155  /* u8[5] */
#define RSI_MD_MVS_ALL        0x15a
#define RSI_MD_MVS_ABS        0x15b
#define RSI_MD_PRE_PMD        0x15c  /* u8[2] */
#define RSI_MD_PRE_CSD        0x15e  /* u8[2] */
#define RSI_MD_FORCE_SPLIT    0x160
#define RSI_MD_PMD_BIAS       0x161  /* u8[3][2] */
#define RSI_MD_CSD_BIAS       0x167  /* u8[4][2] */
#define RSI_MD_LAMBDA_BIAS    0x16f  /* u8[3][2] */
#define RSI_MD_FSKIP_EN       0x175
#define RSI_MD_FSKIP_CFG      0x176
#define RSI_MD_FSKIP_VAL      0x177  /* u8[8][16] */
/* rate control statistics */
#define RSI_RC_EN             0x1f7
#define RSI_BU_SIZE           0x1f8
#define RSI_BU_LEN            0x1f9
#define RSI_RC_METHOD         0x1fa
#define RSI_RC_THD            0x1fc  /* u16[4] */
#define RSI_RC_INFO           0x204  /* u32[1024] */

static inline uint8_t rsi_u8(const uint8_t *si, unsigned off)
{
    return si[off];
}

static inline uint16_t rsi_u16(const uint8_t *si, unsigned off)
{
    return (uint16_t)(si[off] | (si[off + 1] << 8));
}

static inline uint32_t rsi_u32(const uint8_t *si, unsigned off)
{
    return (uint32_t)si[off] | ((uint32_t)si[off + 1] << 8) |
           ((uint32_t)si[off + 2] << 16) | ((uint32_t)si[off + 3] << 24);
}

static inline void rsi_set_u8(uint8_t *si, unsigned off, uint8_t v)
{
    si[off] = v;
}

static inline void rsi_set_u16(uint8_t *si, unsigned off, uint16_t v)
{
    si[off] = (uint8_t)v;
    si[off + 1] = (uint8_t)(v >> 8);
}

static inline void rsi_set_u32(uint8_t *si, unsigned off, uint32_t v)
{
    si[off] = (uint8_t)v;
    si[off + 1] = (uint8_t)(v >> 8);
    si[off + 2] = (uint8_t)(v >> 16);
    si[off + 3] = (uint8_t)(v >> 24);
}

#endif
