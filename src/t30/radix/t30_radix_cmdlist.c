/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T30 Radix (H.265) command list builder, see t30_radix_cmdlist.h.
 *
 * Written from the behaviour of the vendor libimp 1.0.5 H265E_T30_SliceInit
 * (0x3ad5c .. 0x3d5cc); the register map is the GPL-2.0 kernel header
 * drivers/video/soc_vpu/radix/radix.h (module n at n << 15: CFGC 0, ODMA
 * 0x10000, EFE 0x20000, MCE 0x30000, MD 0x40000, TFM 0x48000, DBLK 0x50000,
 * SAO 0x58000, BC 0x60000, SDE 0x68000, IPRED 0x70000, STC 0x78000) and the
 * interpolation formats are that header's radix_IntpFMT (t30_radix_intp.h).
 * Each pair below names the register; the order of the pairs is the vendor
 * order (the hardware applies them in sequence).  tests/t30/radix_diff
 * checks the output word for word against the vendor function.
 */
#include "t30/radix/t30_radix_cmdlist.h"

#include "t30/radix/t30_radix_intp.h"

#define VLD  0x80000000u
#define TERM 0x40000000u

#define B(o)  ((uint32_t)rsi_u8(si, (o)))
#define SB(o) ((int32_t)(int8_t)rsi_u8(si, (o)))
#define H(o)  ((uint32_t)rsi_u16(si, (o)))
#define W(o)  rsi_u32(si, (o))

/* CABAC context states sent to the BC (0x61000) block, in vendor order */
static const uint8_t bc_state_index[111] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0c, 0x0d,
    0x0e, 0x13, 0x14, 0x15, 0x16, 0x17, 0x1a, 0x1b, 0x96, 0x25, 0x22, 0x23,
    0x24, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x10, 0x26, 0x27,
    0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
    0x60, 0x61, 0x62,
    0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x71,
    0x72, 0x73, 0x74,
    0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35,
    0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x41,
    0x42, 0x43, 0x44,
    0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f, 0x80, 0x81, 0x82, 0x83,
    0x84, 0x85, 0x86, 0x87,
    0x90, 0x91, 0x92, 0x93,
};

/* QP generator mode tables (vendor .rodata 0x144f10, 0x144f14, 0x144f18),
 * indexed by (qp_mode >> 4) - 1 */
static const uint8_t qpg_t10[3] = { 0, 0, 1 };
static const uint8_t qpg_t14[3] = { 1, 0, 0 };
static const uint8_t qpg_t18[3] = { 0, 1, 1 };

#define EMIT(val, reg)                         \
    do {                                       \
        *p++ = (uint32_t)(val);                \
        *p++ = VLD | ((uint32_t)(reg) & 0xffffcu); \
    } while (0)

static uint32_t pack4(const int8_t *c)
{
    return ((uint32_t)(uint8_t)c[0]) | ((uint32_t)(uint8_t)c[1] << 8) |
           ((uint32_t)(uint8_t)c[2] << 16) | ((uint32_t)(uint8_t)c[3] << 24);
}

/* one ROI's control byte: enable, mode, QP */
static uint32_t roi_ctrl(const uint8_t *si, int n)
{
    unsigned o = RSI_ROI_INFO + RSI_ROI_STRIDE * (unsigned)n;

    return (B(o) & 1) | ((B(o + 1) & 1) << 1) | ((B(o + 2) & 0x3f) << 2);
}

static uint32_t six4(const uint8_t *si, unsigned o, int n)
{
    uint32_t v = 0;

    for (int i = 0; i < n; i++)
        v |= (B(o + i) & 0x3f) << (8 * i);
    return v;
}

size_t t30_radix_build_cmdlist(uint8_t *si, uint32_t *list,
                               const uint8_t *state,
                               const uint32_t *qp_table)
{
    uint32_t *p = list;
    const uint32_t w = H(RSI_FRAME_WIDTH), h = H(RSI_FRAME_HEIGHT);
    const uint32_t wh = W(RSI_FRAME_WIDTH);    /* width | height << 16 */
    const uint32_t ft = B(RSI_FRAME_TYPE);
    const int intra = ft == 2;
    const uint32_t ctb_w = (w + 63) >> 6, ctb_h = (h + 63) >> 6;
    const uint32_t size_m1 = (w - 1) | ((h - 1) << 16);
    uint32_t ip = B(RSI_IPRED_SIZE);
    uint32_t mode_or, cu_lo, cu_hi, bc_open;
    uint32_t pu_i0, pu_i1, pu_i2, pu_p0;    /* uVar41/31/30/27 of the vendor */
    uint32_t v, a[4];

    if (!state)
        state = (const uint8_t *)(uintptr_t)W(RSI_STATE);
    if (!qp_table)
        qp_table = (const uint32_t *)(uintptr_t)W(RSI_QP_TABLE);

    /* inter_mode[4][4] (0x122): partition modes per CU size */
    if (intra) {
        pu_i0 = ip & 1;
        pu_i1 = ip & 1;
        pu_i2 = (ip & 3) >> 1;
        pu_p0 = 0;
        cu_lo = 0;
        cu_hi = ip & 7;
        mode_or = ip & 7;
        bc_open = 1;
    } else {
        uint32_t m0 = B(0x12e) | B(0x131);
        uint32_t m1 = B(0x12a) | B(0x12b) | B(0x12c) | B(0x12d);
        uint32_t m2 = B(0x126) | B(0x127) | B(0x128) | B(0x129);
        uint32_t m3 = B(0x125) << 3;
        uint32_t i1 = (ip & 3) >> 1, i2 = (ip & 7) >> 2;

        pu_i0 = ip & 1;
        pu_p0 = m0;
        pu_i1 = m0 | pu_i0;
        pu_i2 = m1 | i1;
        cu_lo = ((m0 | m3 | (m1 << 1) | (m2 << 2)) & 0xf) << 4;
        bc_open = m0 == 0;
        mode_or = (pu_i2 << 1) | m3 | pu_i1 | ((m2 | i2) << 2);
        cu_hi = (i1 << 1) | (i2 << 2) | pu_i0;
    }

    /* BC: picture and the CABAC context states */
    EMIT((B(RSI_USE_DQP_FLAG) & 1) * 0x100000 + 0x200000 + (ft & 3) * 0x40000 +
         (((h >> 3) - 1) & 0x1ff) * 0x200 + (((w >> 3) - 1) & 0x1ff),
         0x60000);
    EMIT((B(RSI_SIGN_HIDE_FLAG) & 1) * 0x400 + (B(RSI_MERGE_CAND_NUM) & 7) * 0x80 +
         (B(RSI_FRAME_QP) & 0x3f) * 2 + 1, 0x60004);
    for (unsigned i = 0; i < sizeof(bc_state_index); i++)
        EMIT(state[bc_state_index[i]], 0x61000 + 4 * i);

    /* SDE: the entropy coder */
    EMIT(((B(RSI_DQP_MAX_DEPTH) & 3) << 18) | ((B(RSI_SAO_C_FLAG) & 1) << 17) |
         ((B(RSI_SAO_Y_FLAG) & 1) << 16) | ((B(RSI_SAO_EN) & 1) << 15) |
         ((B(RSI_SIGN_HIDE_FLAG) & 1) << 14) | ((B(RSI_MERGE_CAND_NUM) & 7) << 11) |
         ((B(RSI_FRAME_QP) & 0x3f) << 5) | ((B(RSI_USE_DQP_FLAG) & 1) << 4) |
         ((ft & 3) << 2), 0x68000);
    EMIT((((w >> 4) - 1) << 24) + (((h >> 4) - 1) << 16), 0x68004);
    EMIT(W(RSI_BITSTREAM_PA), 0x68008);
    EMIT(0, 0x6800c);
    EMIT(1, 0x68010);
    for (unsigned i = 0; i < 24; i++)
        EMIT(state[i], 0x69000 + 4 * i);
    for (unsigned i = 0; i < 127; i++)
        EMIT(state[0x1a + i], 0x69060 + 4 * i);

    /* IPRED */
    EMIT(state[0x0d], 0x70004);
    EMIT(1, 0x70008);
    EMIT(size_m1, 0x7000c);
    EMIT((B(RSI_PPRED_SLV_EN) << 31) | (B(RSI_IPRED_SLV_EN) << 30) |
         B(RSI_INTRA_MODE) | (B(RSI_INTRA_MODE + 4) << 24) |
         (B(RSI_INTRA_MODE + 3) << 18) | (B(RSI_INTRA_MODE + 2) << 12) |
         (B(RSI_INTRA_MODE + 1) << 6), 0x70014);
    EMIT((B(RSI_PPRED32_MODE_NUM) << 6) | B(RSI_PPRED8_MODE_NUM) | 0x9200 |
         (B(RSI_PPRED16_MODE_NUM) << 3), 0x70018);
    EMIT((B(RSI_PPRED8_SLV_EN) << 31) | (B(RSI_INTRA8_MODE + 4) << 24) |
         B(RSI_INTRA8_MODE) | (B(RSI_INTRA8_MODE + 3) << 18) |
         (B(RSI_INTRA8_MODE + 2) << 12) | (B(RSI_INTRA8_MODE + 1) << 6), 0x7001c);
    EMIT((B(RSI_PPRED16_SLV_EN) << 31) | (B(RSI_INTRA16_MODE + 4) << 24) |
         B(RSI_INTRA16_MODE) | (B(RSI_INTRA16_MODE + 3) << 18) |
         (B(RSI_INTRA16_MODE + 2) << 12) | (B(RSI_INTRA16_MODE + 1) << 6), 0x70020);
    EMIT((B(RSI_PPRED32_SLV_EN) << 31) | (B(RSI_INTRA32_MODE + 4) << 24) |
         B(RSI_INTRA32_MODE) | (B(RSI_INTRA32_MODE + 3) << 18) |
         (B(RSI_INTRA32_MODE + 2) << 12) | (B(RSI_INTRA32_MODE + 1) << 6), 0x70024);
    v = B(RSI_SOBEL_EN);
    rsi_set_u8(si, RSI_MD_BC_CLOSE, v == 0);
    EMIT((B(RSI_PPRED_SLV_DIF) << 28) | (v << 4) | 0x7e01821 |
         ((ip & 7) << 18) | ((B(RSI_SOBEL_SIZE) & 7) << 15) |
         ((B(RSI_PPRED_MODE_NUM) & 7) << 8) | (((v == 0) | bc_open) << 27),
         0x70000);

    /* MCE (first part) */
    EMIT(((B(RSI_TFM_BUF_EN) & 0x1f) << 21) | (B(RSI_SSE_MASK) << 31) |
         0xc000182 | (intra ? 0x8000 : 0), 0x38000);
    EMIT(wh, 0x3807c);
    EMIT(1, 0x38004);

    /* MD: mode decision */
    {
        uint32_t r = ((h & 0xf) ? 0x800000 : 0) + ((w & 0xf) ? 0x400000 : 0) +
                     (ft & 3) + cu_lo + cu_hi * 0x100 + 0x1000 +
                     (mode_or & 0xf) * 0x10000 + (B(RSI_TU_SPLIT_EN) & 1) * 0x100000;
        uint32_t hi;

        if (w > 0x180) {
            r += 0x8000000;
            hi = 0;
        } else {
            r += 0x9000000;
            hi = 0x20000000;
        }
        EMIT((B(RSI_SAO_EN) ? 0x40000000u : 0) + hi + r, 0x40004);
    }
    EMIT((((ctb_w - 1) * 0x100) & 0xffff) + (ctb_h - 1) * 0x1000000, 0x40008);
    {
        uint32_t pf = !intra;

        EMIT(((uint32_t)(pf && B(RSI_MD_BC_CLOSE) == 0) & pu_i0 & pu_p0) +
             (B(RSI_MD_PRE_PMD) & 3) * 2 + (B(RSI_MD_PRE_PMD + 1) & 3) * 8 +
             (pf & pu_i1) * 0x100 + (B(RSI_MD_PRE_CSD) & 3) * 0x200 +
             ((B(0x165) & 1) * 4 + (B(0x163) & 1) * 2 + (B(0x161) & 1)) * 0x1000 +
             (B(0x162) & 0xf) * 0x10000 + (B(0x164) & 0xf) * 0x100000 +
             (B(0x166) & 0xf) * 0x1000000 + (pf & pu_i2) * 0x10000000 +
             (B(RSI_MD_PRE_CSD + 1) & 3) * 0x20000000, 0x4000c);
    }
    EMIT(((B(0x16d) & 1) * 8 + (B(0x16b) & 1) * 4 + (B(0x169) & 1) * 2 +
          (B(0x167) & 1)) * 0x1000 + (B(RSI_FRAME_QP) & 0x3f) + 0x40 +
         (B(RSI_MD_FORCE_SPLIT) & 1) * 0x80 + (B(RSI_DQP_MAX_DEPTH) == 2) * 0x100 +
         (B(0x168) & 0xf) * 0x10000 + (B(0x16a) & 0xf) * 0x100000 +
         (B(0x16c) & 0xf) * 0x1000000 + B(0x16e) * 0x10000000, 0x40010);
    EMIT((B(0x173) & 1) * 4 + (B(0x171) & 1) * 2 + (B(0x16f) & 1) +
         (B(0x170) & 0xf) * 0x10 + (B(0x172) & 0xf) * 0x100 +
         (B(0x174) & 0xf) * 0x1000 + 0xf0000 +
         (B(RSI_MD_MVS_ALL) & 0xf) * 0x100000 +
         (B(RSI_MD_MVS_ABS) & 0xf) * 0x1000000, 0x40014);
    EMIT(((uint32_t)!intra & B(RSI_MD_FSKIP_EN)) + (uint32_t)(w >= 0x801) * 2 +
         (B(RSI_MD_FSKIP_CFG) & 3) * 4, 0x40018);
    /* md_fskip_val[8][16]: two rows per word, the odd row in bits 31:16 */
    a[0] = a[1] = a[2] = a[3] = 0;
    for (unsigned row = 0; row < 8; row++)
        for (unsigned j = 0; j < 16; j++)
            a[row >> 1] += B(RSI_MD_FSKIP_VAL + 16 * row + j)
                           << ((row & 1) ? j + 16 : j);
    EMIT(a[0], 0x400c0);
    EMIT(a[1], 0x400c4);
    EMIT(a[2], 0x400c8);
    EMIT(a[3], 0x400cc);
    EMIT(((uint32_t)((int32_t)B(RSI_BU_SIZE) - 1) * 0x10 & 0xffff) +
         (B(RSI_RC_EN) & 1) + 0x40000 + (B(RSI_RC_METHOD) & 3) * 0x100000, 0x40100);
    EMIT((H(RSI_RC_THD + 2) << 16) | H(RSI_RC_THD), 0x40104);
    EMIT((H(RSI_RC_THD + 6) << 16) | H(RSI_RC_THD + 4), 0x40108);
    for (unsigned i = 0; i < B(RSI_BU_LEN); i++)
        EMIT(W(RSI_RC_INFO + 4 * i), 0x40800 + 4 * i);
    EMIT(1, 0x40000);

    /* TFM */
    EMIT(B(RSI_FRAME_CQP_OFFSET) * 0x100000 + (B(RSI_TU_SPLIT_EN) ? 0x10000 : 0) +
         (ft & 0xf) * 0x100 + 0x6040, 0x48004);
    EMIT(B(RSI_DBLK_EN) + 0x70, 0x48008);
    EMIT(1, 0x48000);

    /* DBLK */
    EMIT(0x10110, 0x50000);
    EMIT(((w & 0xf) ? 0x20000 : 0) | ((h & 0xf) ? 0x10000 : 0) |
         (B(RSI_DBLK_EN) << 1) | B(RSI_DBLK_EN) | 0x2e00 |
         (B(RSI_SAO_EN) << 2) | (B(RSI_DBLK_GRAY_EN) << 6), 0x50004);
    EMIT(((((h + 0xf) >> 4) - 1) * 0x100) | (((w + 0xf) >> 4) - 1), 0x50008);
    EMIT(B(RSI_TC_OFFSET_DIV2) | ((uint32_t)SB(RSI_FRAME_CQP_OFFSET) << 24) |
         (B(RSI_BETA_OFFSET_DIV2) << 8) | (B(RSI_FRAME_CQP_OFFSET) << 16), 0x50010);
    EMIT(size_m1, 0x50018);
    EMIT(0x11100, 0x50000);

    /* ODMA: reconstruction */
    EMIT((h << 14) | w | 0x20000000, 0x10004);
    EMIT(4, 0x10000);
    EMIT(0, 0x10008);
    EMIT(0, 0x1000c);
    EMIT(0, 0x10010);
    EMIT(W(RSI_DST_Y_PA), 0x10014);
    EMIT(W(RSI_DST_C_PA), 0x10018);
    EMIT((H(RSI_DST_Y_STRIDE) << 16) | H(RSI_DST_C_STRIDE), 0x1001c);
    EMIT(0x24, 0x10000);

    /* MCE: motion search and compensation (P pictures) */
    if (!intra) {
        uint32_t c0 = ((B(RSI_MCE_SCL_MODE) & 3) << 24) |
                      (B(RSI_REFINE_CU32_MODE) << 31) | 0x4000000 |
                      ((B(RSI_QUART_PIXEL_SECH_EN) & 3) << 14) |
                      ((B(RSI_HALF_PIXEL_SECH_EN) & 3) << 12) |
                      ((B(RSI_REFINE_CU8_MODE) & 1) << 29) |
                      ((B(RSI_REFINE_CU16_MODE) & 1) << 30) |
                      ((w < 0x791 && h < 0x451) ? 0x170000u : 0xfa0000u);
        uint32_t c1 = B(0x131) != 0;

        if (B(0x12e)) c0 |= 1;
        if (B(0x12b)) c0 |= 4;
        if (B(0x12c)) c0 |= 2;
        if (B(0x12a)) c0 |= 8;
        if (B(0x127)) c0 |= 0x20;
        if (B(0x12d)) c1 |= 8;
        if (B(0x128)) c0 |= 0x10;
        if (B(0x129)) c1 |= 0x40;
        if (B(0x126)) c0 |= 0x40;
        if (B(0x125)) {
            c0 |= 0x200;
            c1 |= 0x200;
        }
        EMIT((((w - 1) & 0xffff) | ((h - 1) << 16)), 0x30060);
        EMIT(W(RSI_REF_Y_STRIDE), 0x30064);
        EMIT(0x801e8031u, 0x30010);
        EMIT(c0, 0x30040);
        EMIT(c1, 0x30044);
        EMIT(0x2000200, 0x30048);
        EMIT(W(RSI_REF_Y_PA), 0x30800);
        EMIT(W(RSI_REF_C_PA), 0x30804);
        /* luma: the HEVC 8-tap quarter-sample formats */
        for (unsigned k = 0; k < 16; k++) {
            const int8_t *e = T30_RADIX_INTP_HEVC_QPEL[k];

            for (unsigned d = 0; d < 2; d++)
                EMIT((((uint32_t)(uint8_t)e[25 + d]) << 16) |
                     ((uint32_t)e[5 + d] << 31) | ((uint32_t)e[33 + d] & 1) |
                     (((uint32_t)e[1 + d] & 1) << 27) |
                     (((uint32_t)e[7 + d] & 1) << 24) |
                     (((uint32_t)e[27 + d] & 0xf) << 8) |
                     (((uint32_t)e[29 + d] & 1) << 2) |
                     (((uint32_t)e[31 + d] & 1) << 1), 0x30900 + 8 * k + 4 * d);
            EMIT(pack4(e + 9), 0x30a00 + 16 * k);
            EMIT(pack4(e + 13), 0x30a04 + 16 * k);
            EMIT(pack4(e + 17), 0x30a08 + 16 * k);
            EMIT(pack4(e + 21), 0x30a0c + 16 * k);
        }
        /* chroma: the HEVC 4-tap eighth-sample formats */
        for (unsigned k = 0; k < 8; k++) {
            const int8_t *e = T30_RADIX_INTP_HEVC_EPEL[k];

            for (unsigned d = 0; d < 2; d++)
                EMIT((((uint32_t)(uint8_t)e[25 + d]) << 16) |
                     ((uint32_t)e[5 + d] << 31) |
                     (((uint32_t)e[1 + d] & 1) << 27) |
                     (((uint32_t)e[7 + d] & 1) << 24) |
                     (((uint32_t)e[27 + d] & 0xf) << 8), 0x30980 + 8 * k + 4 * d);
            EMIT(pack4(e + 9), 0x30b00 + 16 * k);
            EMIT(pack4(e + 17), 0x30b08 + 16 * k);
        }
        EMIT(0, 0x30068);
        EMIT(0x11, 0x30000);
    }

    /* STC, SAO */
    EMIT((h & 0x1fff) * 0x2000 + (w & 0x1fff) + 0x80000000u, 0x78000);
    EMIT((ctb_h & 0xff) * 0x100 + 0x550000 + (ctb_w & 0xff), 0x78004);
    EMIT(state[0x97], 0x79000);
    EMIT(state[0x98] + 0x80000000u, 0x79004);
    EMIT(((B(RSI_SAO_EN) & 1) << 1) | 0x714, 0x58020);
    EMIT((w << 16) | h, 0x58028);
    EMIT(0, 0x5802c);
    EMIT(1, 0x58034);

    /* EFE: the front end (source, ROI, QP generator, CRP, SAS) */
    {
        uint32_t ref_md, sob, efe;

        if (intra) {
            ref_md = 8;
            v = 0;
        } else {
            v = B(0x125);
            ref_md = (uint32_t)(ip != 0) << 3;
        }
        sob = B(RSI_SOBEL_EN) ? 0x20u : (uint32_t)(B(RSI_PPRED_SLV_EN) != 0) << 5;
        efe = w > 0x180 ? B(RSI_EFE_MODE) << 8 : 0x300u;
        EMIT((B(RSI_RAW_FORMAT) << 1) | (B(RSI_SAO_EN) << 10) | 0x800 |
             ((uint32_t)!intra << 4) | v | ref_md | sob | efe, 0x20000);
    }
    for (int g = 0; g < 4; g++)
        EMIT(roi_ctrl(si, 4 * g) | (roi_ctrl(si, 4 * g + 1) << 8) |
             (roi_ctrl(si, 4 * g + 2) << 16) | (roi_ctrl(si, 4 * g + 3) << 24),
             g < 2 ? 0x20044 + 4 * g : 0x20140 + 4 * (g - 2));
    for (int n = 0; n < 16; n++)
        EMIT(W(RSI_ROI_INFO + RSI_ROI_STRIDE * n + 3),
             n < 8 ? 0x2004c + 4 * n : 0x20148 + 4 * (n - 8));
    EMIT(W(RSI_RAW_Y_PA), 0x2000c);
    EMIT(W(RSI_RAW_C_PA), 0x20010);
    EMIT(W(RSI_FRAME_Y_STRIDE), 0x20014);
    EMIT(wh, 0x20004);
    EMIT(((ctb_w - 1) << 16) | ((ctb_h - 1) << 24), 0x20008);
    {
        uint32_t m = ((B(RSI_QP_MODE) >> 4) - 1) & 0xff, qm = 0;

        if (m < 3)
            qm = (qpg_t14[m] << 2) | (qpg_t18[m] << 1) | (qpg_t14[m] << 3) |
                 (qpg_t10[m] << 4);
        EMIT((B(RSI_QPG_CU32_QP_MODE) << 6) | (B(RSI_QP_TABLE_CU32_MODE) << 7) |
             B(RSI_QP_TAB_EN) | (B(RSI_FRAME_QP) << 8) | (B(RSI_QPG_MIN_QP) << 16) |
             (B(RSI_QPG_MAX_QP) << 24) | qm, 0x20040);
    }
    for (int i = 0; i < 7; i++)
        EMIT((H(RSI_CRP_CU32_THRESHOLD + 2 * i) << 16) |
             H(RSI_CRP_CU16_THRESHOLD + 2 * i), 0x20074 + 4 * i);
    EMIT(six4(si, RSI_CRP_CU16_OFFSET, 4), 0x20090);
    EMIT(six4(si, RSI_CRP_CU16_OFFSET + 4, 4), 0x20094);
    EMIT(six4(si, RSI_CRP_CU32_OFFSET, 4), 0x20098);
    EMIT(six4(si, RSI_CRP_CU32_OFFSET + 4, 4), 0x2009c);
    EMIT((B(RSI_CRP_FILTER_RIGHT_THRESHOLD) << 4) |
         (B(RSI_CRP_FILTER_TOP_THRESHOLD) << 8) |
         B(RSI_CRP_FILTER_LEFT_THRESHOLD) |
         (B(RSI_CRP_FILTER_LIMIT_THRESHOLD) << 12), 0x20070);
    v = H(RSI_QP_TABLE_LEN);
    EMIT(v, 0x200e0);
    for (uint32_t i = 0; i < v; i++) {
        EMIT(i | 0x80000000u, 0x20168);
        EMIT(qp_table[i], 0x2016c);
    }
    EMIT((H(RSI_SAS_CU16_THRESHOLD + 2) << 9) | (H(RSI_SAS_CU16_THRESHOLD + 4) << 19) |
         H(RSI_SAS_CU16_THRESHOLD), 0x20100);
    EMIT(W(RSI_SAS_CU16_THRESHOLD + 6), 0x20104);
    EMIT(H(RSI_SAS_CU16_THRESHOLD + 10), 0x20108);
    EMIT(W(RSI_SAS_CU32_THRESHOLD), 0x2010c);
    EMIT(W(RSI_SAS_CU32_THRESHOLD + 4), 0x20110);
    EMIT(W(RSI_SAS_CU32_THRESHOLD + 8), 0x20114);
    EMIT(six4(si, RSI_SAS_CU16_OFFSET, 4), 0x20118);
    EMIT(six4(si, RSI_SAS_CU16_OFFSET + 4, 3), 0x2011c);
    EMIT(six4(si, RSI_SAS_CU32_OFFSET, 4), 0x20120);
    EMIT(six4(si, RSI_SAS_CU32_OFFSET + 4, 3), 0x20124);
    EMIT(1, 0x20018);                       /* EFE run */
    *p++ = 0;
    *p++ = VLD | TERM | 0x8008;             /* end of the list */
    return (size_t)(p - list);
}
