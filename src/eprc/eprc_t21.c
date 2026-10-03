/*
 * Ingenic Helix "eprc" picture rate control, T21 revision: reimplemented
 * from the OEM T21 libimp 1.0.33 (JZ_VPU_RC_*_T21 at 0x96574..0x9d630).
 * See eprc.h and docs/T23_EPRC.md, "Other SoCs".
 *
 * The code follows eprc.c (the T23 1.3.0 controller) where the two OEM
 * revisions agree; the T21 differences are its state layout (controller
 * block E one word shorter from E+44, state block A 336 bytes, S = A + 336
 * without the per-picture input fields S+240..S+279 of the T23), its
 * defaults, the integer arithmetic of VIDEO_CFG, the first IDR QP from
 * update_qp (0x906b8) and the re-encode limits of FRAME_REPEATE_JUDGE.
 * Fields are addressed by their T21 OEM byte offset, comments give T21 OEM
 * code addresses.  Pointer slots of the OEM layout are written for the
 * emulator comparison only and never read back.
 */
#include "eprc.h"
#include "eprc_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- tables */

/* .rodata of the OEM library */
static const int16_t eprc_fluct_lvls[5] = { 10, 50, 80, 100, 200 };  /* 0xb7f54 */
static const int8_t eprc_qual_lvls[9] = { 90, 80, 70, 60, 50, 40, 30, 20, 10 }; /* 0xb7f74 */

/* ------------------------------------------------------- default values */

/* eprc_default_set_T21 (0x96574).  The OEM caller always passes +12 == 0;
 * the other branch (0xc58e4) is not reproduced. */
static void EPRC21_DefaultSet(uint8_t *e)
{
    uint32_t w = EU16(e, 0), h = EU16(e, 2);
    uint32_t bits = (uint32_t)((int32_t)(3u * w * h) >> 1) << 3;
    uint32_t gop_mode = EU32(e, 16);
    uint32_t rc_mode;

    EU16(e, 4) = (uint16_t)((int32_t)(w + 15u) >> 4);
    EU16(e, 6) = (uint16_t)((int32_t)(h + 15u) >> 4);
    EU32(e, 20) = 0;
    EU32(e, 28) = 48;
    EU32(e, 32) = 0;
    EU32(e, 24) = 1;
    EU32(e, 36) = 0;
    EU8(e, 40) = 48;
    EU32(e, 96) = 2;
    EU32(e, 100) = bits / 10u;
    EU32(e, 104) = bits / 20u;
    EU32(e, 108) = bits / 20u;
    EU8(e, 80) = 0;
    EU32(e, 84) = 1;
    EU32(e, 88) = 0xffffffffu;
    EU32(e, 92) = 2;
    {
        static const uint8_t b144[21] = { 2, 1, 2, 0, 1, 1, 1, 0, 0, 3, 0,
                                          0, 0, 0, 5, 1, 0, 5, 0, 10, 10 };
        memcpy(e + 140, b144, sizeof(b144));
    }
    EU32(e, 164) = 48;
    EU32(e, 168) = 48;
    if (gop_mode == 1) {                        /* 0x96a44 */
        EU32(e, 116) = EU32(e, 44) * 3u;
        EU8(e, 120) = 3;
        EU8(e, 121) = 3;
    } else if (gop_mode == 0) {
        EU8(e, 112) = 3;
    } else if (gop_mode == 2) {                 /* 0x96ab0 */
        EU32(e, 124) = 0;
        EU8(e, 128) = 3;
        EU8(e, 129) = 3;
    } else if (gop_mode == 3) {                 /* 0x969cc */
        EU32(e, 132) = 1;
        EU8(e, 136) = 3;
        EU8(e, 137) = 3;
    }
    rc_mode = EU32(e, 8);
    if (rc_mode == 1) {                         /* 0x969e4: CBR */
        EU32(e, 44) = 25;
        EU32(e, 48) = 1;
        EU32(e, 56) = 1;
        EU32(e, 52) = 25;
        EU32(e, 60) = 1000;
        EU8(e, 64) = 1;
        EU8(e, 68) = 100;
        EU8(e, 69) = 1;
        EU8(e, 70) = 51;
        EU8(e, 71) = 10;
        EU8(e, 74) = 2;
        EU8(e, 77) = 5;
        EU8(e, 78) = 3;
        EU8(e, 76) = 3;
        EU8(e, 73) = 10;
        EU8(e, 79) = 0;
    } else if (rc_mode == 0) {                  /* 0x96a84: CQP */
        EU32(e, 52) = 1;
        EU32(e, 44) = 50;
        EU32(e, 48) = 25;
        EU8(e, 56) = 30;
        EU8(e, 57) = 33;
        EU8(e, 58) = 33;
    } else if (rc_mode < 4) {                   /* 0x966c8: VBR */
        EU32(e, 44) = 25;
        EU32(e, 48) = 1;
        EU32(e, 56) = 1;
        EU32(e, 52) = 25;
        EU32(e, 60) = 1000;
        EU8(e, 64) = 51;
        EU8(e, 65) = 10;
        EU8(e, 67) = 0;
        EU8(e, 68) = 80;
        EU8(e, 69) = 100;
        EU8(e, 70) = 1;
        EU8(e, 72) = 2;
        EU8(e, 73) = 5;
        EU8(e, 74) = 3;
        EU8(e, 71) = 3;
        EU8(e, 75) = 10;
        EU8(e, 76) = 3;
        EU8(e, 77) = 0;
        EU8(e, 78) = 0;
    }
    /* 0x96738 */
    memset(e + 172, 0, 4);
    EU8(e, 176) = 3;
    EU8(e, 177) = 1;
    EU8(e, 178) = 1;
    EU32(e, 180) = 63;
    EU32(e, 184) = 255;
    EU32(e, 188) = 255;
    EU8(e, 192) = 0;
    if (EU16(e, 4) < 51u || EU16(e, 6) < 39u) {
        EU32(e, 196) = 0;
        memset(e + 200, 0, 4);
    } else {
        EU8(e, 192) = 1;
        EU32(e, 196) = 0;
        EU8(e, 200) = 3;
        EU8(e, 201) = 3;
        EU8(e, 202) = 8;
        EU8(e, 203) = 8;
    }
    {   /* 0x967a4 */
        static const uint8_t b204[6] = { 1, 3, 1, 1, 8, 10 };
        static const uint8_t b264[25] = { 1, 0, 1, 1, 1, 0, 0, 8, 4, 1, 6, 3, 33,
                                          0, 1, 25, 28, 1, 1, 1, 3, 1, 0, 0, 20 };
        memcpy(e + 204, b204, sizeof(b204));
        EU32(e, 212) = 21;
        EU32(e, 216) = 11;
        EU32(e, 220) = 21;
        EU32(e, 224) = 11;
        EU32(e, 228) = 21;
        EU32(e, 232) = 11;
        EU32(e, 236) = 21;
        EU32(e, 240) = 21;
        EU32(e, 244) = 11;
        EU32(e, 248) = 0x3fffe;
        EU8(e, 252) = 1;
        memset(e + 253, 0, 6);
        EU32(e, 260) = 17156;
        memcpy(e + 264, b264, sizeof(b264));    /* 264..288 */
    }
}

/* ------------------------------------------------- i264e glue (init) */

/* i264e_validate_parameters (0x33ab0): param[292] and param[296]. */
static void eprc_vbv_fields(const EprcParams *p, uint32_t *f292, uint32_t *f296)
{
    int32_t pixels = (int32_t)(((p->width + 15u) & ~15u) * ((p->height + 15u) & ~15u));
    int32_t lo = (int32_t)(((int64_t)(pixels * 3) * 0x51eb851f) >> 36) -
                 ((pixels * 3) >> 31);
    int32_t hi = (pixels * 12) / (p->width < 801u ? 3 : 6);
    int32_t v = 19660800;

    v = v < lo ? lo : v > hi ? hi : v;
    *f292 = (uint32_t)v;
    hi = (int32_t)((float)v / 1.4f);
    v = 14043429;
    v = v < lo ? lo : v > hi ? hi : v;
    *f296 = (uint32_t)v;
}

/* i264e_ratecontrol_init (0x370a4), the eprc part. */
static void EPRC21_SetupE(uint8_t *e, const EprcParams *p)
{
    uint32_t mode = p->rc_mode;
    uint32_t f292, f296;
    uint32_t gop_mode;

    eprc_vbv_fields(p, &f292, &f296);
    EU32(e, 16) = mode == EPRC_MODE_SMART;
    EU16(e, 0) = (uint16_t)p->width;
    EU32(e, 12) = 0;
    EU16(e, 2) = (uint16_t)p->height;
    EPRC21_DefaultSet(e);
    if (mode == 0)
        EU32(e, 28) = 0;
    EU16(e, 4) = (uint16_t)((int32_t)(p->width + 15u) >> 4);
    EU16(e, 6) = (uint16_t)((int32_t)(p->height + 15u) >> 4);
    EU32(e, 100) = f292;
    EU32(e, 104) = f296;
    EU32(e, 108) = f296;
    EU32(e, 20) = mode == EPRC_MODE_SMART ? 5u : 0u;
    EU32(e, 32) = 0;
    EU32(e, 96) = 2;
    EU8(e, 80) = 0;
    EU32(e, 84) = 1;
    EU32(e, 88) = 0xffffffffu;
    EU32(e, 92) = 2;
    gop_mode = EU32(e, 16);
    if (gop_mode == 1) {                        /* 0x37614 SmartP */
        EU8(e, 120) = 3;
        EU8(e, 121) = 3;
        EU32(e, 116) = p->gop * p->bg_interval_gops;
    } else if (gop_mode == 0) {
        EU8(e, 112) = 3;
    }
    if (mode == EPRC_MODE_CQP) {
        EU32(e, 8) = 0;
        EU32(e, 44) = p->gop;
        EU32(e, 48) = p->fps_num;
        EU32(e, 52) = p->fps_den;
        EU8(e, 56) = (uint8_t)(p->cqp - 3u);
        EU8(e, 57) = (uint8_t)p->cqp;
    } else if (mode == EPRC_MODE_CBR) {         /* 0x376f8 */
        EU32(e, 8) = 1;
        EU32(e, 44) = p->gop;
        EU32(e, 48) = p->static_time;
        EU32(e, 52) = p->fps_num;
        EU32(e, 56) = p->fps_den;
        EU32(e, 60) = p->bitrate;
        EU8(e, 64) = 3;
        EU8(e, 68) = 100;
        EU8(e, 69) = 1;
        EU8(e, 70) = (uint8_t)p->max_qp;
        EU8(e, 71) = (uint8_t)p->min_qp;
        EU8(e, 72) = (uint8_t)p->max_qp;
        EU8(e, 73) = (uint8_t)p->min_qp;
        EU8(e, 74) = 3;
        EU8(e, 75) = 3;
        EU8(e, 76) = (uint8_t)((p->fps_num / p->fps_den) >> 1);
        EU8(e, 77) = (uint8_t)p->gop_qp_step;
        EU8(e, 78) = (uint8_t)p->frm_qp_step;
        EU8(e, 79) = (uint8_t)p->i_bias;
    } else {                                    /* 0x372c0: VBR, SMART */
        EU32(e, 8) = mode == EPRC_MODE_VBR ? 2u : 3u;
        EU32(e, 44) = p->gop;
        EU32(e, 48) = p->static_time;
        EU32(e, 52) = p->fps_num;
        EU32(e, 56) = p->fps_den;
        EU32(e, 60) = p->max_bitrate;
        EU8(e, 64) = (uint8_t)p->max_qp;
        EU8(e, 65) = (uint8_t)p->min_qp;
        EU8(e, 66) = (uint8_t)p->min_qp;
        EU8(e, 67) = 1;
        EU8(e, 68) = (uint8_t)p->change_pos;
        EU8(e, 69) = 100;
        EU8(e, 70) = 1;
        EU8(e, 71) = (uint8_t)((p->fps_num / p->fps_den) >> 1);
        EU8(e, 72) = 3;
        EU8(e, 73) = (uint8_t)p->gop_qp_step;
        EU8(e, 74) = (uint8_t)p->frm_qp_step;
        EU8(e, 76) = (uint8_t)p->quality;
        EU8(e, 77) = 4;
        EU8(e, 78) = (uint8_t)p->i_bias;
    }
    EI8(e, 40) = (int8_t)(p->init_qp < 0 || mode == 0 ? -1 : p->init_qp);
}

/* ----------------------------------------------------- VIDEO_CFG */

/* Sizes of the arrays behind the 7104-byte state block (0x96acc). */
static uint32_t eprc_block_size(const uint8_t *e, uint32_t sz[8])
{
    uint32_t mbw = EU16(e, 4), mbh = EU16(e, 6);
    int32_t gop = EI32(e, 44);
    uint32_t mbs = mbw * mbh;
    uint32_t mb8 = (uint32_t)(((int32_t)(mbw + 15u) >> 3) * ((int32_t)(mbh + 15u) >> 3));
    uint32_t mb16 = (mbs + 15u) & ~15u;

    sz[0] = (uint32_t)gop & ~1u;                     /* E+1628 */
    sz[1] = (uint32_t)(4 * gop - 3) & ~1u;           /* E+1632 */
    sz[2] = (uint32_t)(gop + 1) & ~1u;               /* E+1636 */
    sz[3] = (mb16 + 15u) & ~15u;                     /* E+1640 */
    sz[4] = mb8 * 384u;                              /* E+304 */
    sz[5] = mb16;                                    /* P+7140 */
    sz[6] = mb16 * 2u;                               /* P+7144 */
    sz[7] = 3648u;                                   /* P+7148 */
    return 7104u + sz[0] + sz[1] + sz[2] + sz[3] + sz[4] + sz[5] + sz[6] + sz[7];
}

static void EPRC21_Layout(Eprc *rc, uint8_t *block)
{
    uint32_t sz[8];
    uint8_t *q;

    rc->p_size = eprc_block_size(rc->e, sz);
    rc->p = block;
    q = block + 7104;
    rc->a1628 = q; q += sz[0];
    rc->a1632 = q; q += sz[1];
    rc->a1636 = q; q += sz[2];
    rc->a1640 = q; q += sz[3];
    rc->a304 = q;  q += sz[4];
    rc->a7140 = q; q += sz[5];
    rc->a7144 = q; q += sz[6];
    rc->a7148 = q;
}

/* JZ_VPU_RC_VIDEO_CFG_T21 (0x96ac0).  The OEM validates every field and
 * stops on out-of-range values; i264e only passes validated parameters, so
 * only the corrections that take effect for those are reproduced. */
static int EPRC21_VideoCfg(Eprc *rc)
{
    uint8_t *e = rc->e;
    uint8_t *p = rc->p;
    uint32_t sz[8];
    uint32_t mode, gop_mode;
    int32_t frames, stat, cls;
    uint32_t src, dst, target, gop;
    int32_t fl;

    eprc_block_size(e, sz);
    EPTR(e, 1624, rc->a1628);
    EPTR(e, 1628, rc->a1632);
    EPTR(e, 1632, rc->a1636);
    EPTR(e, 1636, rc->a1640);
    EPTR(e, 300, rc->a304);
    EPTR(e, 292, p);
    EPTR(e, 296, p + 336);
    memset(p, 0, 336);
    memset(p + 336, 0, 6760);
    memset(rc->a1628, 0, sz[0]);
    memset(rc->a1632, 0, sz[1]);
    memset(rc->a1636, 0, sz[2]);
    memset(rc->a1640, 0, sz[3]);
    memset(rc->a304, 0, sz[4]);
    EPTR(p, 7088, rc->a7144);
    EPTR(p, 7084, rc->a7140);
    memset(rc->a7140, 0, sz[5]);
    memset(rc->a7144, 0, sz[6]);
    EPTR(p, 7092, rc->a7148);

    /* 0x96ff8: field checks with an effect for valid input */
    mode = EU32(e, 8);
    if (EU32(e, 96) != 0) {                         /* 0x97094 */
        uint32_t raw = (uint32_t)((int32_t)(EU16(e, 0) * EU16(e, 2) * 3u) >> 1) << 3;
        if (raw < EU32(e, 100))
            EU32(e, 100) = raw;
        if (raw < EU32(e, 104))
            EU32(e, 104) = raw;
    }
    if (EU32(e, 88) < 0xfa00u)
        EU32(e, 88) = 0xfa00u;
    if (EU8(e, 143) == 0)
        EU8(e, 143) = 1;
    if (EI32(e, 244) < 0)
        EU32(e, 244) = 0;
    if (mode == 1) {                                 /* 0x9838c CBR */
        if (EU32(e, 44) == 0)
            EU32(e, 44) = 1;
        if (EU32(e, 52) < EU32(e, 56))
            EU32(e, 56) = EU32(e, 52);
        if (EU8(e, 76) >= 4)
            EU8(e, 76) = 3;
        if (EU8(e, 73) < EU8(e, 71))
            EU8(e, 73) = EU8(e, 71);
        else if (EU8(e, 70) < EU8(e, 73))
            EU8(e, 73) = EU8(e, 70);
    } else if (mode >= 2) {                          /* 0x977fc VBR */
        if (EU32(e, 52) < EU32(e, 56))
            EU32(e, 56) = EU32(e, 52);
        if (EU8(e, 71) >= 4)
            EU8(e, 71) = 3;
        if (EU8(e, 75) < EU8(e, 65))
            EU8(e, 75) = EU8(e, 65);
        else if (EU8(e, 64) < EU8(e, 75))
            EU8(e, 75) = EU8(e, 64);
        if (EU8(e, 76) == 0)
            EU8(e, 76) = 1;
    }

    if (EU32(e, 16) == 1) {                          /* 0x98894 */
        if (EU32(e, 116) < EU32(e, 44))
            EU32(e, 116) = EU32(e, 44);
        else if (EU32(e, 116) > 0x10000u)
            EU32(e, 116) = 0x10000u;
    }

    /* 0x97c68: copy */
    EU32(p, 148) = EU32(e, 20);
    EU32(p, 140) = EU32(e, 28);
    EU32(p, 152) = EU32(e, 24);
    EU32(p, 184) = 0;
    EU8(p, 198) = (uint8_t)EU32(e, 32);
    EU32(p, 32) = EU16(e, 0);
    EU32(p, 36) = EU16(e, 2);
    EU32(p, 40) = EU16(e, 4);
    EU32(p, 44) = EU16(e, 6);
    EU32(p, 144) = mode;
    EU32(p, 104) = EU32(e, 80);
    EU32(p, 108) = EU32(e, 84);
    EU32(p, 112) = EU32(e, 88);
    EU32(p, 116) = EU32(e, 92);
    EU32(p, 120) = EU32(e, 96);
    EU32(p, 124) = EU32(e, 100);
    EU32(p, 128) = EU32(e, 104);
    EU32(p, 132) = EU32(e, 108);
    EI8(p, 56) = EI8(e, 40);
    EU32(p, 168) = EU8(e, 140);
    EU32(p, 172) = EU8(e, 141);
    EU32(p, 176) = EU8(e, 142);
    EU32(p, 180) = EU8(e, 143);
    {
        static const uint16_t map[][2] = {      /* P byte <- E byte */
            { 196, 144 }, { 207, 145 }, { 188, 146 }, { 189, 147 },
            { 190, 148 }, { 191, 149 }, { 192, 150 }, { 206, 151 },
            { 159, 152 }, { 195, 153 }, { 158, 154 }, { 160, 155 },
            { 194, 156 }, { 193, 157 }, { 197, 158 }, { 204, 159 },
            { 205, 160 },
        };
        size_t i;
        for (i = 0; i < sizeof(map) / sizeof(map[0]); i++)
            EU8(p, map[i][0]) = EU8(e, map[i][1]);
    }
    EU32(p, 208) = EU32(e, 164);
    EU32(p, 212) = EU32(e, 168);
    EPTR(p, 5688, rc->a1628);
    EPTR(p, 5692, rc->a1632);
    EPTR(p, 5696, rc->a1636);
    EPTR(p, 7080, rc->a1640);
    gop_mode = EU32(e, 16);
    if (gop_mode == 1) {                             /* 0x9864c */
        EU32(p, 28) = EU32(e, 116);
        EI8(p, 77) = EI8(e, 120);
        EI8(p, 78) = EI8(e, 121);
    } else if (gop_mode == 0) {
        EI8(p, 77) = EI8(e, 112);
    } else if (gop_mode == 2) {
        EU32(p, 24) = EU32(e, 124);
        EI8(p, 79) = EI8(e, 128);
        EI8(p, 77) = EI8(e, 129);
    } else if (gop_mode == 3) {
        EU32(p, 20) = EU32(e, 132);
        EI8(p, 80) = EI8(e, 136);
        EI8(p, 77) = EI8(e, 137);
    }
    if (mode == 1) {                                 /* 0x98528 CBR */
        EU32(p, 16) = EU32(e, 44);
        EU32(p, 0) = EU32(e, 48);
        EU32(p, 12) = EU32(e, 56);
        EU32(p, 8) = EU32(e, 52);
        EU32(p, 64) = EU32(e, 60) << 10;
        EU8(p, 159) = EU8(e, 64);
        EU32(p, 84) = EU8(e, 68);
        EU32(p, 88) = EU8(e, 69);
        EU8(p, 59) = EU8(e, 70);
        EU8(p, 60) = EU8(e, 71);
        EI8(p, 77) = EI8(e, 74);
        EU8(p, 81) = EU8(e, 77);
        EU8(p, 83) = EU8(e, 78);
        EU32(p, 100) = EU8(e, 76);
        EU8(p, 61) = EU8(e, 73);
        EI8(p, 76) = EI8(e, 79);
    } else if (mode == 0) {                          /* 0x984fc */
        EU32(p, 16) = EU32(e, 44);
        EU32(p, 8) = EU32(e, 48);
        EU32(p, 12) = EU32(e, 52);
        EU8(p, 57) = EU8(e, 56);
        EU8(p, 58) = EU8(e, 57);
    } else {                                         /* 0x97e2c VBR */
        uint32_t maxbits = EU32(e, 60) << 10;
        EU32(p, 8) = EU32(e, 52);
        EU32(p, 68) = maxbits;
        EU32(p, 16) = EU32(e, 44);
        EU32(p, 0) = EU32(e, 48);
        EU32(p, 12) = EU32(e, 56);
        EU32(p, 64) = (EU8(e, 68) * maxbits) / 100u;
        EU8(p, 59) = EU8(e, 64);
        EU8(p, 60) = EU8(e, 65);
        EI8(p, 136) = EI8(e, 67);
        EU32(p, 84) = EU8(e, 69);
        EU32(p, 88) = EU8(e, 70);
        EI8(p, 77) = EI8(e, 72);
        EU8(p, 81) = EU8(e, 73);
        EU8(p, 83) = EU8(e, 74);
        EU32(p, 100) = EU8(e, 71);
        EU8(p, 61) = EU8(e, 75);
        EU8(p, 157) = EU8(e, 76);
        EI8(p, 156) = EI8(e, 77);
        EI8(p, 76) = EI8(e, 78);
    }
    memcpy(p + 216, e + 172, 120);                   /* 0x97efc */
    EU32(p, 52) = (uint32_t)EU16(e, 6) * EU16(e, 4);
    EU32(p, 48) = (uint32_t)EU16(e, 0) * EU16(e, 2);
    if (mode == 0)
        return 0;

    /* 0x97fbc */
    EI8(p, 492) = -1;
    if (EI8(p, 196) != 0) {                          /* 0x98668: bit-rate class */
        static const uint32_t thr[3][2] = {
            { 400000, 1000000 }, { 800000, 4000000 }, { 2000000, 2000000 },
        };
        uint32_t pixels = EU32(p, 48);
        if (pixels < 230400u) {
            EU32(p, 164) = 0;
        } else {
            int k = pixels >= 2073600u ? 2 : pixels >= 921600u ? 1 : 0;
            uint32_t src0 = EU32(p, 8), dst0 = EU32(p, 12), tgt = EU32(p, 64);
            if ((src0 * thr[k][0]) / dst0 / 25u >= tgt)
                EU32(p, 164) = 0;
            else if ((src0 * thr[k][1]) / dst0 / 25u >= tgt)
                EU32(p, 164) = 1;
            else
                EU32(p, 164) = 2;
        }
    }
    cls = EI32(p, 164);
    target = EU32(p, 64);
    src = EU32(p, 8);
    dst = EU32(p, 12);
    if (EU32(p, 180) != 0) {                         /* 0x97fe8 */
        EF32(p, 6268) = 3.2003f;
        EF32(p, 6272) = -1.367f;
    }
    {
        uint32_t f = EU32(p, 140);
        EU8(p, 340) = ((f >> 8) & 15u) != 0 && ((f >> 4) & 15u) != 0;
    }
    gop = EU32(p, 16);
    {   /* 0x98024 */
        int32_t t1 = EI8(p, 192) != 0 && (int32_t)gop <= 100;
        frames = (int32_t)(dst * gop) / (int32_t)src;
        EU8(p, 192) = (uint8_t)t1;
    }
    if (frames <= 0)
        frames = 1;
    EU32(p, 4) = (uint32_t)frames;
    if (EI8(p, 159) == 0) {
        EU32(p, 0) = (uint32_t)frames;
        stat = frames;
    } else {
        stat = EI32(p, 0);
        if (stat == 0) {
            EU32(p, 0) = (uint32_t)frames;
            stat = frames;
        } else if (stat % frames != 0) {             /* 0x98a1c */
            stat = (stat + frames - 1) / frames * frames;
        }
    }
    /* 0x98080: field limits */
    if (EI8(p, 191) < 2)
        EI8(p, 191) = 2;
    else if (EI8(p, 191) >= 8)
        EI8(p, 191) = 7;
    if (EI8(p, 193) < 2)
        EI8(p, 193) = 2;
    else if (EI8(p, 193) >= 8)
        EI8(p, 193) = 7;
    if (EI8(p, 194) < 0)
        EI8(p, 194) = 0;
    else if (EI8(p, 194) >= 2)
        EI8(p, 194) = 1;
    if (EI8(p, 158) < 0)
        EI8(p, 158) = 0;
    else if (EI8(p, 158) >= 10)
        EI8(p, 158) = 9;
    if (EI8(p, 160) < 0)
        EI8(p, 160) = 0;
    else if (EI8(p, 160) >= 5)
        EI8(p, 160) = 4;
    if ((uint32_t)cls >= 3u)
        EU32(p, 164) = 2;
    if (EU8(p, 204) < 10)
        EU8(p, 204) = 10;
    else if (EU8(p, 204) >= 101)
        EU8(p, 204) = 100;
    if (EU8(p, 205) < 10)
        EU8(p, 205) = 10;
    else if (EU8(p, 205) >= 26)
        EU8(p, 205) = 25;
    if (EI32(p, 184) != 0) {                         /* 0x9832c */
        EU32(p, 5668) = 0;
        EI32(p, 588) = stat / frames;
        EI32(p, 644) = frames * (int32_t)target;
        if (EI32(p, 588) <= 0)
            EI32(p, 588) = 1;
        return -1;      /* +184 is 0 for i264e */
    }
    EU32(p, 0) = (uint32_t)stat;
    {
        int32_t fluct = eprc_fluct_lvls[EI8(p, 159)];
        int32_t gop_bits = (int32_t)(target * (uint32_t)frames);
        int32_t gops;

        EI32(p, 5672) = (int32_t)(src * (uint32_t)stat) / (int32_t)dst;
        fl = (int32_t)((uint32_t)gop_bits * (uint32_t)fluct) / 100;
        EI32(p, 200) = fl;
        EI32(p, 644) = gop_bits;
        EU32(p, 5668) = 0;
        gops = stat / frames;
        EI32(p, 588) = gops;
        if (mode == 1) {                             /* 0x98ca4 CBR */
            int32_t peak = (int32_t)((uint32_t)(fluct + 100) * target) / 100;
            int32_t n = (int32_t)gop - 1, total;
            int32_t st = (int32_t)((uint32_t)stat * target);
            EI32(p, 68) = peak;
            EI32(p, 5676) = (int32_t)((uint32_t)peak * (uint32_t)stat);
            EI32(p, 5684) = st;
            if (gops <= 0)
                EI32(p, 588) = 1;   /* the OEM goes on with the old value */
            total = (int32_t)((uint32_t)n * (uint32_t)gops);
            EI32(p, 6244) = st;
            EI32(p, 6256) = fl;
            EI32(p, 396) = total;
            EI32(p, 472) = (int32_t)((uint32_t)EI32(p, 84) * (uint32_t)gop_bits) /
                           (n + EI32(p, 84));
            EI32(p, 476) = (int32_t)((uint32_t)EI32(p, 88) * (uint32_t)gop_bits) /
                           (n + EI32(p, 88));
            EI32(p, 416) = st / (total + gops);
        } else {                                     /* 0x98db4 VBR */
            int32_t maxbits = EI32(p, 68);
            int32_t q = (int32_t)((uint32_t)eprc_qual_lvls[EI8(p, 157)] *
                                  (uint32_t)maxbits) / 100;
            EI32(p, 72) = q;
            EI32(p, 5676) = (int32_t)((uint32_t)maxbits * (uint32_t)stat);
            EI32(p, 656) = (int32_t)((uint32_t)q * (uint32_t)frames);
            EI32(p, 5680) = (int32_t)((uint32_t)stat * target);
            EI32(p, 652) = (int32_t)((uint32_t)maxbits * (uint32_t)frames);
            if (gops <= 0)
                EI32(p, 588) = 1;
            EI32(p, 6244) = gop_bits;
            EI32(p, 396) = (int32_t)gop - 1;
            EI32(p, 6256) = fl;
            EI32(p, 416) = gop_bits / (int32_t)gop;
        }
    }
    return 0;
}

int EPRC21_Init(Eprc *rc, const EprcParams *params, uint8_t *slice)
{
    uint32_t sz[8];
    uint32_t size;
    uint8_t *block;

    memset(rc, 0, sizeof(*rc));
    rc->qp_down_max = params->qp_down_max;
    rc->prev_type = -1;
    rc->e = rc->e_store;
    EPRC21_SetupE(rc->e, params);
    size = eprc_block_size(rc->e, sz);
    block = calloc(1, size);
    if (!block)
        return -1;
    EPRC21_Layout(rc, block);
    EPTR(rc->e, 1616, block);
    EU32(rc->e, 1620) = size;
    rc->slice = slice;
    return EPRC21_VideoCfg(rc);
}

void EPRC21_Free(Eprc *rc)
{
    free(rc->p);
    rc->p = NULL;
}


/* ------------------------------------------------------- per picture */

#define A8(o)   EU8(A, o)
#define AS8(o)  EI8(A, o)
#define A32(o)  EI32(A, o)
#define S8(o)   EU8(S, o)
#define SS8(o)  EI8(S, o)
#define S16(o)  EI16(S, o)
#define S32(o)  EI32(S, o)
#define SF(o)   EF32(S, o)

static const int8_t eprc_ip_qp_delta[18] = {          /* 0xb7e6c */
    -6, -5, -4, -3, -2, -2, -2, -1, -1, 0, 0, 0, 2, 3, 4, 5, 6, 6 };
static const int8_t eprc_ip_qp_dlt_idx[18] = {        /* 0xb7e80 */
    -8, -7, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 100 };
static const int32_t eprc_fst_p_qp_dlt[16] = {        /* 0xb7e94 */
    2, 3, 2, 2, 1, 0, 0, 2, 4, 6, 4, 4, 2, 2, 2, 4 };
static const int8_t eprc_clip_lvls[6] = { 6, 5, 4, 4, 3, 3 };   /* 0xb7f60 */
static const int8_t eprc_qp_fluct_nums[5] = { 0, 1, 2, 3, 4 };  /* 0xb7f4c */

/* update_qp (0x906b8): QP change that scales 2^((QP - 4) / 6) by the
 * ratio real / ref (searched in whole QP steps, 0 when out of range). */
static int32_t eprc_update_qp(int32_t qp, int32_t ref, int32_t real)
{
    float ratio;
    double r, num;
    int32_t s;

    if (ref < 2) {
        if (real <= 0)
            ratio = (float)real / 1.0f;
        else
            ratio = (float)real / (float)real;
    } else {
        ratio = (float)real / (float)ref;
    }
    r = (double)ratio;
    if (ratio < 1.0f) {                              /* 0x90960 */
        if (qp >= 52)
            return 0;
        num = pow(2.0, (double)((qp - 4) / 6));
        for (s = 51; s >= qp; s--)
            if (r < num / pow(2.0, (double)((s - 4) / 6)))
                return (int8_t)(s - qp);
        return 0;
    }
    if (qp < 0)                                      /* 0x9072c */
        return 0;
    num = pow(2.0, (double)((qp - 4) / 6));
    for (s = qp; s >= 0; s--)
        if (r < num / pow(2.0, (double)((s - 4) / 6)))
            return (int8_t)(s - qp);
    return 0;
}

/* estimate_qp (0x91634), R-lambda branch: lambda = alpha * bpp^beta.
 * The other (rate-distortion) branch needs +180 == 0, which VIDEO_CFG
 * never leaves. */
static int32_t eprc_estimate_qp(uint8_t *A, uint8_t *S)
{
    float bpp, lambda;
    int32_t qp;

    bpp = (float)S32(88) / (float)A32(48);
    lambda = (float)((double)SF(5932) * pow((double)bpp, (double)SF(5936)));
    SF(5944) = lambda;
    qp = (int32_t)(log((double)lambda) * 4.2005 + 13.7122 + 0.5);
    S8(69) = (uint8_t)qp;
    return qp;
}

/* QP change relative to the previous picture QP from its distance to the
 * GOP model QP (0xc9288 / 0xc9bf0). */
static int32_t eprc_ip_delta(int32_t d)
{
    int32_t k = 0;

    if (-8 < d) {
        k = 1;
        while (eprc_ip_qp_dlt_idx[k] < d) {
            if (++k == 18)
                return 0;
        }
    }
    return (uint8_t)eprc_ip_qp_delta[k];
}

/* 0x9a42c / 0x9a214: limit against the previous QP from the size ratio. */
static void eprc_ratio_rule(uint8_t *S, int32_t *lo, int32_t *hi)
{
    int32_t v1 = S16(192), s3 = S8(190);
    int32_t clip = eprc_clip_lvls[SS8(185)];
    int32_t prev = SS8(71);
    int32_t mode = S32(152);
    int32_t a2 = prev + clip;
    int32_t s1 = (int8_t)(s3 - v1);

    if (!(*lo < prev))
        prev = *lo;
    if (!(*hi < a2))
        a2 = *hi;
    *lo = (int8_t)prev;
    if ((uint32_t)mode < 4u) {
        if (s1 < 21)
            SS8(68) = (int8_t)(a2 / 2 + S8(74));
        else
            SS8(68) = (int8_t)a2;
    } else if (mode == 4) {
        if (s1 < 51)
            SS8(68) = (int8_t)(a2 / 2 + S8(74));
        else
            SS8(68) = (int8_t)a2;
    }
    *hi = (int8_t)a2;
}

/* 0x9a4f4 / 0x9a598 */
static void eprc_ref_rule(uint8_t *S, int32_t *lo)
{
    int32_t m = SS8(71) - 2;

    if (!(*lo < m))
        m = *lo;
    if (SS8(68) < S32(76))
        SS8(68) = (int8_t)S32(76);
    *lo = (int8_t)m;
}

/* 0x9a094: QP window from the picture types around the current one in the
 * GOP pattern (+5400) and the coded-size history. */
static void eprc_qp_window(Eprc *rc, int32_t *lo, int32_t *hi)
{
    uint8_t *A = rc->p;
    uint8_t *S = rc->p + 336;
    int32_t gop = A32(16), cur = S32(44);
    int32_t past3 = -1, past2 = -1, fut3 = -1, fut2 = -1;
    int32_t s4, s5, s7, t7, t6, t9, ra, t4, t5, i;

    if (gop <= 0) {
        t7 = 4;
        s7 = -1;
        s4 = -1;
        s5 = -1;
    } else {
        const uint8_t *tp = rc->a1636;     /* S+5400 */
        for (i = 0; i < gop; i++) {
            if (i > cur) {
                if (tp[i] == 3)
                    fut3 = i;
                else if (tp[i] == 2)
                    fut2 = i;
            } else {
                if (tp[i] == 3)
                    past3 = i;
                else if (tp[i] == 2)
                    past2 = i;
            }
        }
        s5 = fut3;
        if (past2 < past3) {
            t7 = past3 + 5;
            s7 = -1;
            s4 = 0;
            s5 = -1;
        } else if (past3 < past2) {
            t7 = past2 + 5;
            s7 = -1;
            s4 = 1;
            s5 = -1;
        } else if (fut2 < fut3) {
            t7 = 4;
            s7 = 0;
            s4 = -1;
        } else {
            t7 = 4;
            s4 = -1;
            if (fut3 < fut2) {
                s7 = 1;
                s5 = fut2;
            } else {
                s7 = -1;
                s5 = -1;
            }
        }
    }
    /* 0x9a37c */
    t4 = S32(148);
    t5 = A8(207);
    {
        int32_t t3 = cur;
        if ((uint32_t)t4 < 4u) {
            if (t5 == 1) {
                if ((uint32_t)t4 < 2u) {
                    t9 = 1;
                    ra = 0;
                } else {
                    ra = t4 == 3;
                    t9 = t4 == 2;
                }
                goto c986c;
            }
            if ((uint32_t)t4 >= 2u) {
                t6 = s5 - gop + 5;
                if (s4 == 0) {
                    t9 = 0;
                    goto c9880;
                }
                ra = 0;
                t9 = 0;
                goto c9788;
            }
            t6 = s5 - gop + 5;
            if (s4 != 0) {
                ra = 0;
                if (s4 == 1) {
                    t9 = 1;
                    goto c990c;
                }
                t9 = 1;
                goto c9794;
            }
            t9 = 1;
            goto c9880;
        }
        ra = 1;
        if (t5 == 1) {
            t9 = t4 == 2;
            goto c986c;
        }
        t6 = s5 - gop + 5;
        if (s4 == 0) {
            t9 = 0;
            goto c9508;
        }
        ra = 1;
        t9 = 0;
        goto c9788;

c986c:
        t6 = s5 - gop + 5;
        if (s4 != 0)
            goto c9788;
        if (ra != 0)
            goto c9508;
        goto c9880;
c9508:
        if (t7 < t3) {
            if (s7 == 0) {
                if (t6 < t3)
                    return;
                goto c97ac;
            }
            goto c988c;
        }
        if (S16(192) < S8(190) || (t5 == 1 && t4 == 3))
            eprc_ratio_rule(S, lo, hi);
        return;
c9788:
        if (s4 == 1) {
            if (t9 == 0)
                goto c9794;
c990c:
            if (t7 < t3)
                goto c9794;
            eprc_ref_rule(S, lo);
            return;
        }
c9794:
        if (s7 != 0)
            goto c988c;
        if (ra == 0)
            return;
        if (t6 < t3)
            return;
c97ac:
        if (S16(192) < S8(190) || (t5 == 1 && t4 == 3))
            eprc_ratio_rule(S, lo, hi);
        return;
c9880:
        if (s7 == 0)
            return;
c988c:
        if (s7 != 1 || t9 == 0 || t6 < t3)
            return;
        eprc_ref_rule(S, lo);
    }
}

static const int32_t eprc_cplx_lvls[3][10] = {        /* 0xb7ed4 */
    { 32, 35, 38, 42, 44, 46, 48, 50, 50, 50 },
    { 26, 29, 32, 35, 38, 42, 45, 47, 49, 49 },
    { 20, 23, 26, 29, 32, 35, 38, 42, 45, 47 },
};

/* gop_init.part.23 (0x92da8): start of a GOP (I picture, SMART GOP-start
 * P picture): GOP model QPs and the bit budget of the coming GOP. */
static void EPRC21_GopInit(Eprc *rc)
{
    uint8_t *A = rc->p;
    uint8_t *S = rc->p + 336;
    int32_t gop = A32(16);
    int32_t pos = S32(44);
    int32_t n288 = S32(248);
    int32_t s2 = 0, t4 = 0;
    int32_t i;

    S32(56) = pos;
    S32(324) = S32(308);
    S32(5664 + 4 * n288) = S32(312);
    S32(5904) = S32(5904) + S32(312);
    if (pos < (gop >> 1)) {
        S32(252) = 0;
    } else {
        S32(248) = n288 + 1;
        S32(252) = S32(252) - 1;
    }
    if (gop < 2)
        goto report;
    if (S32(32) != 0) {                              /* 0x93434 */
        S32(296) = -1;
        S32(284) = SS8(71) + 2;
        S32(280) = SS8(71) + 1;
        goto c1800;
    }
    if (A32(176) == 2) {                             /* 0x9372c */
        if ((double)SF(268) < 0.1)
            SF(268) = 0.1f;
        if ((double)SF(264) < 0.1)
            SF(264) = 0.1f;
        t4 = (int32_t)(SF(260) / SF(268));
        s2 = (int32_t)(SF(256) / SF(264));
        S32(284) = t4;
        S32(292) = t4;
        S32(280) = s2;
        S32(288) = s2;
    } else {
        s2 = S32(280);
        t4 = S32(284);
    }
    /* 0x92e6c */
    for (i = 6; i > 0; i--) {
        S32(5604 + 4 * i) = S32(5600 + 4 * i);
        S32(5632 + 4 * i) = S32(5628 + 4 * i);
    }
    S32(5604) = s2;
    S32(5632) = t4;
    S32(296) = -1;                                   /* +190, +192 are 0 */
c1800:
    if (S32(44) == gop) {                            /* 0x93200 */
        int32_t t3 = S32(104);
        int32_t n = S32(44) - 1;
        int32_t a0 = (S32(312) - t3) / n;
        int32_t t5 = (S32(308) - t3) / n;
        int32_t div, s;
        float f19, f21, f4;

        if (a0 <= 0)
            a0 = 1;
        if (t5 <= 0)
            t5 = 1;
        if (S32(284) <= 0) {
            S32(284) = 1;                            /* 0x937ec */
            f19 = 1.0f;
        } else {
            f19 = (float)S32(284);
        }
        if (SS8(72) <= 0) {
            S8(72) = 1;
            f21 = 1.0f;
        } else {
            f21 = (float)SS8(72);
        }
        div = t5 == 1 ? a0 : t5;
        s = (t3 * 10) / div;
        S32(132) = s;
        S32(128) = (t3 * 10) / a0;
        f4 = (float)s / 10.0f * f21 / f19;
        SF(144) = (float)gop * f4 / ((float)(gop - 1) + f4);
    } else {
        SF(144) = 1e-4f;
    }
report:
    if (A32(144) == 1) {                             /* 0x9346c CBR */
        int32_t frames, t3;
        if (S32(252) == 0) {
            frames = A32(0) / A32(4);
            S32(5904) = 0;
            S32(5660) = 0;
            S32(5332) = 0;
            S32(248) = 0;
            S32(5920) = A32(200);
            S32(252) = frames;
            S32(60) = S32(5336) - frames;
            t3 = 0;
        } else {
            frames = S32(252);
            t3 = S32(5660);
        }
        S32(5908) = S32(5348) - t3;
        S32(80) = (S32(5348) - t3) / (frames * gop);
    } else if ((uint32_t)(A32(144) - 2) < 2u) {      /* 0x934f0 VBR */
        int32_t t0;
        S32(5904) = 0;
        S32(248) = 0;
        S32(5332) = 0;
        S32(60) = gop - 1;
        S32(5660) = 0;
        S32(252) = A32(0) / A32(4);
        if (AS8(136) != 0) {                         /* 0x935dc */
            int32_t cls = A32(164), lvl, q, k, a2;
            int32_t lo = (uint32_t)S32(148) < 4u;
            if (!lo) {
                cls = (int8_t)(cls + 1);
                if (cls >= 3)
                    cls = 2;
                if ((int8_t)cls < 0)
                    cls = 0;
            }
            cls = (int8_t)cls;
            q = S32(284);
            for (lvl = 0; lvl < 10 && !(q < eprc_cplx_lvls[cls][lvl]); lvl++)
                ;
            k = lvl + AS8(156);
            if (q < 37) {
                a2 = (int8_t)k;
            } else if (!lo) {
                a2 = 9;
                goto c1aa0;
            } else if (q < 42) {
                a2 = (int8_t)(k + 1);
            } else if (q < 46) {
                a2 = (int8_t)(k + 2);
            } else {
                a2 = (int8_t)(k + 3);
            }
            if (a2 < 0)
                a2 = 0;
            else if (a2 >= 10)
                a2 = 9;
c1aa0:
            S32(308) = (S32(316) - S32(320)) * a2 / 10 + S32(320);
        }
        t0 = S32(308);
        {
            int32_t fl = (int32_t)((float)eprc_fluct_lvls[AS8(159)] / 100.0f * (float)t0);
            A32(200) = fl;
            S32(5908) = t0;
            S32(80) = t0 / gop;
            S32(5912) = t0;
            S32(5920) = fl;
        }
    }
    S32(44) = 0;                                     /* 0x933ac */
    S32(312) = 0;
    S32(272) = 0;
    S32(276) = 0;
    S32(260) = 0;
    S32(268) = 0;
    S32(256) = 0;
    S32(264) = 0;
}

/* Hardware fields h264_api_enc (0x9553c) derives from the picture QP: the
 * slice block fields 448/808 (QP), 809/810 (QP window of the macroblock
 * rate control, 0x40040/0x40074) and 1058/1060/1062 (0xb001c/0xb0020).
 * The lambda table is the fixed one (the adaptive variant needs E+269). */
static void eprc_picture_fields(Eprc *rc, EprcPicture *pic)
{
    uint8_t *S = rc->p + 336;
    uint8_t *SL = rc->slice;
    int32_t qp = SS8(68);
    uint32_t hi = (uint8_t)(qp + 13);
    uint32_t lo = (uint8_t)(qp - 12);           /* T21: wraps below QP 12 */
    uint32_t over = (uint8_t)qp > 33u ? (uint8_t)qp - 33u : 0u;

    if (hi == 0)
        hi = 1;
    else if (hi >= 52)
        hi = 51;
    if (lo >= 52)
        lo = 51;
    pic->type = S32(28);
    pic->qp = (uint8_t)qp;
    pic->qp_max = (uint8_t)hi;
    pic->qp_min = (uint8_t)lo;
    pic->lambda[0] = (uint16_t)(384u + 48u * over);
    pic->lambda[1] = (uint16_t)(96u + 12u * over);
    pic->lambda[2] = pic->lambda[1];
    if (SL) {
        EU8(SL, 0) = (uint8_t)S32(28);
        EU8(SL, 448) = (uint8_t)qp;
        EU8(SL, 808) = (uint8_t)qp;
        EU8(SL, 809) = (uint8_t)hi;
        EU8(SL, 810) = (uint8_t)lo;
        EU16(SL, 1058) = pic->lambda[0];
        EU16(SL, 1060) = pic->lambda[1];
        EU16(SL, 1062) = pic->lambda[2];
    }
}

/* JZ_VPU_RC_FRAME_START_T21 (0x990e0) without the region/QP-map inputs. */
static int eprc_frame_start(Eprc *rc, EprcPicture *pic)
{
    uint8_t *E = rc->e;
    uint8_t *A = rc->p;
    uint8_t *S = rc->p + 336;
    uint8_t *SL = rc->slice;
    int32_t lo, hi;

    S8(24) = (uint8_t)EU32(E, 308);
    S32(36) = EI32(E, 304);
    S8(5) = EU8(E, 636);
    EPTR(S, 5928, E + 640);
    memset(rc->a1640, 0, (size_t)EU16(E, 4) * EU16(E, 6));
    if (SS8(5) != 0 || EI8(E, 637) != 0)
        return -1;              /* region / macroblock QP map: not supported */
    S8(6) = 0;

    S8(41) = 0;                                      /* 0x991b0 */
    S8(40) = 0;
    S8(42) = 0;
    if (SS8(24) != 0) {
        S32(28) = 2;
        S8(41) = 1;
    } else if (A32(148) == 0) {                      /* 0x99c30 */
        S8(40) = 1;
        S32(28) = 0;
    } else {
        if ((uint32_t)A32(148) >= 7u)
            return -1;
        if (S32(48) % A32(16) != 0) {                /* 0x99908 */
            S8(40) = 1;
            S32(28) = 0;
        } else {
            S32(28) = 6;
            S8(42) = 1;
        }
    }
    S32(88) = 0;
    if (A32(144) == 0)
        return -1;              /* FIXQP is not run through this module */

    /* 0x99c48 */
    if (S32(0) != 0 && A8(207) != 0 && (uint32_t)A32(184) < 2u)
        A8(207) = 0;            /* scene_judge_ncu (0x8edc0) without regions */

    switch (S32(28)) {
    case 2:
        break;
    case 6:
        goto pic_gop;
    case 0:
        goto pic_p;
    default:
        return -1;
    }

    /* 0x9a038: IDR */
    S32(48) = 0;
    if (A32(16) != 1 && S32(0) != 0)
        EPRC21_GopInit(rc);
    if (S32(0) == 0) {                               /* 0x9a054 */
        int32_t q = AS8(56);
        if (q < 0)                                   /* 0x9a780 */
            q = 26 + eprc_update_qp(26, 1000, A32(64)) +
                eprc_update_qp(26, 25, A32(8) / A32(12)) +
                eprc_update_qp(26, 921600, A32(48));
        SS8(68) = (int8_t)q;
        goto clamp;
    }
    {                                                /* 0x9a810 */
        int32_t t2, t7 = S32(32);
        if (t7 == 2) {                               /* 0x9a74c */
            SS8(68) = SS8(72);
            S32(88) = S32(104);
            if (SS8(41) != 0)
                goto clamp_first;
            lo = (int8_t)(S8(71) - A8(81));
            hi = (int8_t)(S8(71) + A8(81));
            goto window;
        }
        if (A32(168) == 1) {
            SS8(68) = (int8_t)(S32(284) - A8(77));
        } else if (A32(168) == 2) {
            int32_t v1 = (A32(16) * A32(12)) / A32(8);
            if (!(v1 < 3))
                v1 = 2;
            SS8(68) = (int8_t)(S32(284) - 1 - v1);
        }
        if (A32(84) < S32(132))                      /* 0x9a834 */
            S8(68) = (uint8_t)(S8(68) + 1);
        {
            int32_t d = S32(284) - SS8(73);
            if ((uint32_t)d < 4u) {
                t2 = SS8(68);
            } else {
                t2 = (int8_t)(eprc_ip_delta(d) + SS8(73));
                SS8(68) = (int8_t)t2;
            }
        }
        S8(68) = (uint8_t)(t2 + A8(76));             /* 0x9a93c */
        {
            float f = (float)S32(80) * SF(144);
            int32_t bits = (int32_t)floor((double)f);
            S32(88) = bits;
            if (bits < S32(140))
                S32(88) = S32(140);
            else if (S32(136) < bits)
                S32(88) = S32(136);
        }
        goto clamp;
    }

pic_gop:                                             /* 0x99f64 */
    {
        int32_t t7, s6, d, q;
        if (A32(16) != 1 && S32(0) != 0)
            EPRC21_GopInit(rc);
        t7 = A32(168);
        s6 = S32(284);
        if (t7 == 1) {
            SS8(68) = (int8_t)(s6 - A8(78));
        } else if (t7 == 2) {
            int32_t v1 = (A32(16) * A32(12)) / A32(8);
            if (!(v1 < 3))
                v1 = 2;
            SS8(68) = (int8_t)(s6 - 1 - v1);
        }
        d = s6 - SS8(73);                            /* 0x99f98 */
        if ((uint32_t)d < 4u) {
            q = SS8(68);
        } else {
            q = (int8_t)(eprc_ip_delta(d) + SS8(73));
            SS8(68) = (int8_t)q;
        }
        S8(68) = (uint8_t)(q + A8(76));
        goto clamp;
    }

pic_p:                                               /* 0x99cb0 */
    if (S32(44) == 1) {                              /* 0x9ab4c */
        if (S32(0) == 1) {
            S8(68) = (uint8_t)(S8(71) + A8(77));
        } else if (A32(172) == 2) {
            S8(68) = (uint8_t)S32(284);
        } else if (SS8(52) < 26) {
            S8(68) = (uint8_t)(eprc_fst_p_qp_dlt[S32(148)] + S8(71));
            S8(68) = (uint8_t)(S8(68) - A8(76));
        } else {
            SS8(68) = SS8(71);
            if (SS8(71) < SS8(72))
                SS8(68) = SS8(72);
        }
        if (S32(60) <= 0) {                          /* 0x9ab88 */
            S32(88) = 0;
            goto clamp;
        }
        S32(88) = S32(5912) / S32(60);
        if (S32(88) <= 0)
            goto clamp;
        if ((uint32_t)S32(148) >= 2u)
            goto clamp_nofirst;
        {
            int32_t prev = SS8(68);
            int32_t q = (int8_t)eprc_estimate_qp(A, S);
            SS8(68) = (int8_t)q;
            if (prev + 3 < q)
                SS8(68) = (int8_t)(prev + 3);
            else if (q < prev - 3)
                SS8(68) = (int8_t)(prev - 3);
        }
        goto clamp;
    }
    {
        int32_t t5 = S32(5912), s4, s2, t9, t7, s5, s1, q;

        if (t5 <= 0 && A32(152) != 1)
            return -1;          /* +152 is always 1 */
        t9 = S32(5916) - S32(5924);                  /* 0x99ccc */
        S32(5916) = t9;
        s4 = S32(60);
        if (s4 <= 0) {
            S32(60) = 1;
            s4 = 1;
        }
        s2 = t5 / s4;
        if (AS8(194) != 0)
            s2 = (s2 * 6 * 15 + S32(124) * 10) / 100;
        if (s2 < 0)
            s2 = 0;
        t7 = s2;
        if (AS8(197) != 0)
            return -1;          /* gamma table by +148: not reachable */
        S8(186) = 50;
        {
            int32_t ra = 50;
            int32_t a3 = S32(5920);
            int32_t t2 = S32(80) - (a3 - t9) * ra / 100;
            if (t2 < 0) {
                int32_t r = (a3 - A32(200)) / s4;
                S32(5924) = r;
                S32(5916) = a3 - r;
                t2 = S32(80) - r * ra / 100;
                if (t2 < 0)
                    t2 = 0;
            }
            s1 = (t2 + t7) >> 1;
            S32(88) = s1;
        }
        if (AS8(195) != 0) {                         /* 0x99df4 */
            if (s1 < S32(100)) {
                S32(88) = S32(100);
                s1 = S32(100);
            } else if (S32(96) < s1) {
                S32(88) = S32(96);
                s1 = S32(96);
            }
        }
        if (s1 < 0)
            S32(88) = 0;
        s5 = S32(5660);                              /* 0x99e30 */
        if (s5 < (A32(144) == 1 ? S32(5340) : S32(5344)) && S32(88) > 0) {
            q = (int8_t)eprc_estimate_qp(A, S);
            s5 = S32(5660);
        } else {
            q = (int8_t)(S8(71) + A8(83));
        }
        SS8(68) = (int8_t)q;
        {                                            /* 0x99e60 */
            int32_t t5b = S32(108), q100, s189, s190, s4b;
            if (t5b <= 0) {
                S32(108) = 1;
                t5b = 1;
            }
            s1 = S32(88);
            q100 = s1 * 100;
            s4b = (int16_t)(q100 / t5b);
            S16(192) = (int16_t)s4b;
            if (s1 == 0)
                goto clamp;
            s189 = S8(189);
            s190 = S8(190);
            {
                int32_t ra;
                if (s189 < s4b) {
                    if (SS8(68) < SS8(74)) {
                        if (s4b < s190) {
                            SS8(68) = (int8_t)(SS8(74) + 1);
                            goto c9200;
                        }
                        ra = s190 < s4b;
                    } else {
                        SS8(68) = (int8_t)(SS8(74) - 1);
                        goto c9200;
                    }
                } else {
                    ra = s190 < s4b;
                    if (s4b < s190) {
                        if (!(SS8(74) < SS8(68))) {
                            SS8(68) = (int8_t)(SS8(74) + 1);
                            goto c9200;
                        }
                    }
                }
                /* 0x9aa98 */
                if (ra && s4b < s189 && A8(204) < SS8(52)) {
                    int32_t t5c = S32(112), r;
                    if (t5c <= 0) {
                        S32(112) = 1;
                        t5c = 1;
                    }
                    r = (int16_t)(q100 / t5c);
                    S16(194) = (int16_t)r;
                    if (s190 < r) {
                        if (r < s189)
                            S8(68) = S8(74);
                        goto c9200;
                    }
                    goto c9200;
                }
            }
c9200:
            {
                int32_t s4c = A8(206);
                if (s4c != 0 && SS8(52) == A8(204)) {
                    if (S8(196) == (uint8_t)eprc_qp_fluct_nums[s4c]) {
                        S8(196) = 1;
                    } else {
                        S8(68) = S8(71);
                        S8(196) = (uint8_t)(S8(196) + 1);
                    }
                    goto clamp;
                }
            }
        }
    }
    goto clamp_nofirst;

clamp:                                               /* 0x99f14 */
    if (SS8(41) != 0) {
clamp_first:                                         /* 0x9a028 */
        if (SS8(68) < AS8(61))
            SS8(68) = AS8(61);
        goto clamp_qp;
    }
clamp_nofirst:                                       /* 0x9a068 */
    if (S32(32) == 2) {
        lo = (int8_t)(S8(71) - A8(81));
        hi = (int8_t)(S8(71) + A8(81));
    } else if (S32(32) == 6) {
        lo = (int8_t)(S8(71) - A8(82));
        hi = (int8_t)(S8(71) + A8(82));
    } else {
        lo = (int8_t)(S8(71) - A8(83));
        hi = (int8_t)(S8(71) + A8(83));
    }
window:
    if (AS8(188) != 0)
        eprc_qp_window(rc, &lo, &hi);
    if (SS8(68) < lo)                                /* 0x9a298 */
        SS8(68) = (int8_t)lo;
    else if (hi < SS8(68))
        SS8(68) = (int8_t)hi;
clamp_qp:                                            /* 0x9a040 */
    if (SS8(68) < AS8(60))
        SS8(68) = AS8(60);
    else if (AS8(59) < SS8(68))
        SS8(68) = AS8(59);
    /* OpenIMP extra (qp_down_max, 0 = OEM): same limit as in eprc.c.
     * P after P falls at most qp_down_max below the last coded QP; applied
     * before the picture fields so slice QP, window and lambda follow. */
    rc->cur_type = S32(28);
    if (rc->qp_down_max && S32(28) == 0 && rc->prev_type == 0 &&
        rc->prev_qp > 0 && SS8(68) < rc->prev_qp - (int32_t)rc->qp_down_max) {
        int32_t q = rc->prev_qp - (int32_t)rc->qp_down_max;

        SS8(68) = (int8_t)(q > AS8(59) ? AS8(59) : q);
    }

    /* 0x99240 */
    EPTR(S, 6740, (uintptr_t)EU32(E, 1612));
    EPTR(S, 6732, SL);
    if (SL)
        EU32(SL, 980) = EU32(E, 1644);
    eprc_picture_fields(rc, pic);
    EI32(E, 1584) = S32(44);
    EU8(E, 1596) = S8(68);
    EI32(E, 1588) = S32(28) == 6 ? 0 : S32(28);
    EI32(E, 1592) = S32(148);
    return 0;
}

int EPRC21_FrameStart(Eprc *rc, const EprcFrameIn *in, EprcPicture *pic)
{
    uint8_t *E = rc->e;

    /* i264e_ratecontrol_start (0x38504); no region / QP-map input */
    EPTR(E, 1608, rc->slice);
    EU8(E, 636) = 0;
    EU8(E, 637) = 0;
    EU32(E, 1612) = in->r11832;
    EU32(E, 1568) = 0;
    EU32(E, 1572) = 0;
    EU32(E, 1576) = 0;
    EU32(E, 1580) = 0;
    EU32(E, 664) = 0;
    EU32(E, 16) = in->gop_ctrl;
    EU32(E, 304) = in->r2820;
    EU32(E, 308) = in->frames_since_idr == 0;
    return eprc_frame_start(rc, pic);
}

static const float eprc_mv_thr[3][4] = {               /* 0xce068 */
    { 0.8f, 5.0f, 32.0f, 80.0f },
    { 1.2f, 7.0f, 48.0f, 100.0f },
    { 2.0f, 10.0f, 64.0f, 120.0f },
};

/* scene_judge_enc_frame (0x8f390): picture class (+148) from the motion
 * statistics of the coded picture, kept per GOP position in +5400. */
static void eprc_scene_judge(Eprc *rc)
{
    uint8_t *A = rc->p;
    uint8_t *S = rc->p + 336;
    uint8_t *types = rc->a1636;
    float mbs, motion;
    int32_t ratio, lvl, cls, pos, found, a2, a3, t7, t8;
    uint32_t bit;

    if (S32(28) == 2) {                              /* 0x8f830 */
        S32(148) = S32(152);
        types[0] = (uint8_t)S32(152);
        return;
    }
    if (S32(4440) == 0) {
        S32(4440) = 1;
        mbs = 1.0f;
    } else {
        mbs = (float)(uint32_t)S32(4440);
    }
    if (A32(52) <= 0)
        A32(52) = 1;
    {
        int32_t t4 = S32(4444);
        motion = (float)((t4 < 0 ? t4 + 3 : t4) >> 2) / mbs;
    }
    pos = S32(44);
    ratio = (int8_t)((uint32_t)(S32(4432) * 100) / (uint32_t)A32(52));
    S8(188) = (uint8_t)ratio;
    if (ratio >= 21 && S32(6200) == 10 && SS8(52) == A8(204)) {
        cls = 6;                                     /* 0x8f8f8 */
        S32(148) = cls;
        goto counters;
    }
    {
        const float *thr = eprc_mv_thr[A32(164)];
        lvl = motion < thr[0] ? 0 : motion < thr[1] ? 1 : motion < thr[2] ? 2 :
              motion < thr[3] ? 3 : 4;
    }
    /* 0x8f494: a background (type 3) picture among the last five */
    found = 0;
    {
        int32_t j, m;
        for (m = 0, j = pos - 1; j >= 0 && m < 5; m++, j--)
            if (types[j] == 3) {
                found = 1;
                break;
            }
    }
    t8 = S32(152);
    if (t8 == 2) {                                   /* 0x8f784 */
        a3 = 0;
    } else {
        a3 = SS8(53) == A8(205) ? 1 : SS8(54) == 5;
        if (t8 == 3) {
            t7 = 0;
            a2 = 0;
            goto c_bde94;
        }
    }
    if (SS8(52) == A8(204)) {
        t7 = 1;
        a2 = 1;
    } else if (found) {
        t7 = 0;
        a2 = 0;
    } else {
        t7 = 5 * S32(80) < S32(108);
        a2 = t7;
    }
c_bde94:
    /* 0x8f654 */
    if (SS8(71) < 37 && !(ratio < 6) && ((uint32_t)t8 < 3u || t8 == 4)) {
        t7 = 1;                                      /* 0x8f840 */
        if (lvl == 1)
            cls = a3 ? 2 : 0;
        else if (lvl == 0)
            cls = a3 ? 2 : 1;
        else if (lvl == 2)
            cls = a2 ? 3 : 4;
        else if (lvl == 3)
            cls = 3;
        else
            cls = 1;
    } else {
        if (lvl == 1)
            cls = a3 ? 2 : 0;
        else if (lvl == 0)
            cls = a3 ? 2 : 1;
        else if (lvl == 2)
            cls = a2 ? 3 : 4;
        else if (lvl == 3)
            cls = t7 ? 3 : 5;
        else
            cls = 1;
    }
    S32(148) = cls;
counters:
    bit = 1u << cls;
    if (bit & 0x34u) {                               /* 2, 4, 5 */
        S8(52) = 0;
        S8(54) = 0;
        S8(53) = (uint8_t)(S8(53) + 1);
    } else if (bit & 0x0bu) {                        /* 0, 1, 3 */
        S8(53) = 0;
        S8(54) = 0;
        SS8(52) = (int8_t)(S8(52) + 1);
        if (SS8(52) < 0)
            S8(52) = 0;
        else if (A8(204) < SS8(52))
            S8(52) = A8(204);
    } else if (bit & 0x40u) {                        /* 6 */
        S8(52) = 0;
        S8(53) = 0;
        S8(54) = (uint8_t)(S8(54) + 1);
    } else {
        if (SS8(52) < 0)
            S8(52) = 0;
        else if (A8(204) < SS8(52))
            S8(52) = A8(204);
    }
    if (SS8(53) < 0)
        S8(53) = 0;
    else if (A8(205) < SS8(53)) {
        if (pos <= 0) {
            S8(53) = A8(205);
            return;
        }
        S8(53) = A8(205);
    }
    if (pos > 0)
        types[pos - 1] = (uint8_t)cls;
}

static const int8_t eprc_ratio_lvls[8] = { 50, 40, 30, 25, 20, 18, 12, 8 };  /* 0xb7f68 */

static int32_t eprc_wrap(int32_t v)
{
    return v == 0x0fffffff ? 0x10000 : v + 1;
}

/* JZ_VPU_RC_FRAME_END_T21 (0x9acf4), R-lambda model (+180 == 1). */
static void eprc_frame_end(Eprc *rc)
{
    uint8_t *E = rc->e;
    uint8_t *A = rc->p;
    uint8_t *S = rc->p + 336;
    int32_t bits, qp, isp, t6;

    SS8(68) = EI8(E, 1596);
    qp = SS8(68);
    bits = EI32(E, 312);
    S32(92) = bits;
    if (EU32(E, 12) == 0) {
        S32(160) = (int32_t)((EU32(E, 316) + 128u) >> 8);
        S32(176) = EI32(E, 324);
        S32(180) = EI32(E, 328);
        S32(4440) = A32(52);
        S32(4444) = EI32(E, 336);
        S32(4432) = EI32(E, 340);
        S32(172) = EI32(E, 332);
        memcpy(S + 4476, E + 352, 32);
        memcpy(S + 4508, E + 384, 16);
        memcpy(S + 4448, E + 400, 16);
    }
    /* 0x9aeb8 */
    S32(0) = eprc_wrap(S32(0));
    S32(44) = eprc_wrap(S32(44));
    S32(48) = eprc_wrap(S32(48));
    S32(5332) = eprc_wrap(S32(5332));
    isp = SS8(40);
    if (isp)
        S32(60) = S32(60) - 1;
    if (S32(0) == -1)
        S32(0) = 15;
    S32(312) = S32(312) + bits;
    S32(5660) = S32(5660) + bits;
    if (A32(144) == 0)
        goto done;
    if (A32(176) == 2) {                             /* 0x9c754 */
        SF(256) = (float)((double)SF(256) * 0.95) + (float)qp;
        SF(264) = (float)((double)SF(264) * 0.95) + 1.0f;
        if (isp) {
            SF(260) = (float)((double)SF(260) * 0.95) + (float)qp;
            SF(268) = (float)((double)SF(268) * 0.95) + 1.0f;
        }
    }
    if (isp || A32(176) != 2) {                      /* 0x9af48 */
        static const int32_t lim[8] = { 10000, 20000, 30000, 40000, 60000,
                                        80000, 100000, 120000 };
        int32_t k;
        for (k = 0; k < 8 && !(bits < lim[k]); k++)
            ;
        if (k < 8) {
            S8(184) = (uint8_t)k;
            S8(189) = (uint8_t)(eprc_ratio_lvls[k] + 100);
            S8(190) = (uint8_t)(100 - eprc_ratio_lvls[k]);
        } else {
            S8(184) = 10;
            S8(189) = 100;
            S8(190) = 100;
        }
        S8(185) = qp < 25 ? 0 : qp < 28 ? 1 : qp < 33 ? 2 : qp < 37 ? 3 :
                  qp < 42 ? 4 : 5;
    }
    /* 0x9afec */
    S8(71) = (uint8_t)qp;
    S32(152) = S32(148);
    S32(32) = S32(28);
    S32(112) = S32(88);
    if (S32(88) <= 0)
        S32(112) = 1;
    if (isp && SS8(41) == 0) {                       /* P picture */
        int32_t gop = A32(16), i;
        S8(74) = (uint8_t)qp;
        S32(108) = bits;
        for (i = gop - 2; i > 0; i--) {
            rc->a1628[i] = rc->a1628[i - 1];
            eprc_st32(rc->a1632 + 4 * i, eprc_ld32(rc->a1632 + 4 * (i - 1)));
        }
        rc->a1628[0] = S8(68);
        eprc_st32(rc->a1632, S32(92));
    } else if (SS8(41) != 0) {                       /* 0x9bf30: IDR */
        if (!isp)
            S8(73) = (uint8_t)qp;
        S8(72) = (uint8_t)qp;
        S32(104) = bits;
        S32(5600 - 4 * S32(248)) = bits;
    } else {                                         /* 0x9c2fc: GOP-start P */
        S8(73) = (uint8_t)qp;
        S8(74) = (uint8_t)qp;
        S32(108) = bits;
    }
    S32(84) = -1;
    t6 = S32(92);
    if (A32(144) == 1) {                             /* 0x9b1d4 CBR */
        if (SS8(41) != 0) {
            int32_t t7 = (A32(200) - S32(80) + t6) * S32(252);
            int32_t a3 = S32(5348) - t6 * S32(252);
            S32(5912) = a3;
            S32(5920) = t7;
            if (S32(248) != 0) {                     /* 0x9cdac */
                S32(5912) = a3 - S32(5904);
                S32(5920) = S32(5904) - S32(248) * S32(308) + t7;
            }
        } else {                                     /* 0x9c7e0 */
            int32_t t5 = t6 + S32(5920) - S32(80);
            S32(5920) = t5;
            S32(5912) = S32(5912) - t6;
            if (isp && S32(44) == 2) {               /* 0x9cbc4 */
                if (S32(60) <= 0)
                    S32(60) = 1;    /* the OEM stores its mode register (1) */
                S32(5916) = t5;
                S32(5924) = (t5 - A32(200)) / S32(60);
            }
        }
    } else {                                         /* 0x9bf64 */
        int32_t v0;
        if (SS8(42) != 0 && t6 < S32(104)) {
            S32(84) = S32(104) - t6;
            t6 = S32(104);
        }
        v0 = S32(5920) - S32(80) + t6;
        S32(5920) = v0;
        S32(5912) = S32(5912) - t6;
        if (isp && S32(44) == 2) {
            int32_t n = S32(60);
            if (n <= 0) {
                n = 1;
                S32(60) = 1;
            }
            S32(5916) = v0;
            S32(5924) = (v0 - A32(200)) / n;
        }
    }
    /* 0x9b214 */
    if (AS8(188) != 0 && A8(207) != 1 && S32(28) == 0 && A32(184) == 0)
        eprc_scene_judge(rc);
    if (S32(32) == 0 && !(SS8(52) < 26) && !(A32(16) < 25)) {   /* 0x9b26c */
        float sb = 0.0f, sq = 0.0f;
        int i;
        for (i = 0; i < 10; i++) {
            sb = (float)eprc_ld32(rc->a1632 + 4 * i) + sb;
            sq = (float)rc->a1628[i] + sq;
        }
        S32(116) = (int32_t)(sb / 10.0f);
        S32(76) = (int32_t)(sq / 10.0f);
    }
    if (AS8(195) == 1 || AS8(194) == 1)
        return;                 /* 0x9c320: options not set by i264e */
    /* 0x9b3e4 */
    if (SS8(40) == 0)
        goto c_cb1c4;
    if (S32(44) == 2 && AS8(189) == 0)
        goto done;
    {
        int32_t ratio = (int16_t)((S32(92) * 100) / S32(112));
        int32_t mode = S32(148);
        if (A32(180) == 0)
            return;             /* rate-distortion model: +180 is always 1 */
        if (mode == 6 || (mode == 3 && !(ratio < 201))) {   /* 0x9ce94 */
            S32(6200) = 4;
            memset(S + 6036, 0, 36);
            memset(S + 6120, 0, 36);
        }
        if (A32(180) != 1)
            return;
    }
    {                                                /* 0x9cf24 */
        double alpha, beta, bpp_d, lam_c_d;
        float lam, bpp, lam_c, sa, sb;
        if (S32(0) == 2) {                           /* 0x9d25c */
            lam = (float)pow(2.718282, ((double)S8(71) - 13.7122) / 4.2005);
            t6 = S32(92);
            SF(5944) = lam;
            SF(5932) = 3.2003f;
            SF(5936) = -1.367f;
            beta = -1.367;
            alpha = 3.2003;
        } else {
            alpha = (double)SF(5932);
            beta = (double)SF(5936);
            lam = SF(5944);
            t6 = S32(92);
        }
        bpp = (float)t6 / (float)A32(48);
        bpp_d = (double)bpp;
        if (!(0.0001 < bpp_d)) {                     /* 0x9d1fc */
            SF(5932) = (float)(alpha * 0.96);
            SF(5936) = (float)(beta * 0.98);
        } else {
            double ln_l, ln_c;
            if (bpp_d < 0.03) {
                sb = 0.0025f;
                sa = 0.005f;
            } else if (bpp_d < 0.08) {
                sb = 0.0125f;
                sa = 0.025f;
            } else if (bpp_d < 0.2) {
                sb = 0.025f;
                sa = 0.05f;
            } else if (bpp < 0.5f) {
                sb = 0.05f;
                sa = 0.1f;
            } else {
                sb = 0.1f;
                sa = 0.2f;
            }
            lam_c_d = pow(bpp_d, beta);
            ln_l = log((double)lam);
            lam_c = (float)(lam_c_d * alpha);
            ln_c = log((double)lam_c);
            SF(5932) = (float)((ln_l - ln_c) * (double)sa * alpha + alpha);
            ln_l = log((double)lam);
            ln_c = log((double)lam_c);
            SF(5936) = (float)((ln_l - ln_c) * (double)sb * log(bpp_d) + beta);
        }
        /* 0x9d0b8 */
        if ((double)SF(5932) < 0.05)
            SF(5932) = 0.05f;
        else if (200.0f < SF(5932))
            SF(5932) = 200.0f;
        if (SF(5936) < -3.0f)
            SF(5936) = -3.0f;
        else if (-0.1 < (double)SF(5936))
            SF(5936) = -0.1f;
    }
c_cb1c4:
done:
    return;
}

/* JZ_VPU_RC_FRAME_REPEATE_JUDGE_T21 (0x9d390): 1 when the picture is too
 * large and is to be coded again with the QP raised (update_qp). */
static int eprc_repeat_judge(Eprc *rc, int may_repeat)
{
    uint8_t *E = rc->e;
    uint8_t *A = rc->p;
    uint8_t *S = rc->p + 336;
    uint8_t *SL = rc->slice;
    uint32_t limit, bits, target;
    int32_t delta, q;

    if (SL) {
        EU32(E, 1644) = EU32(SL, 724);
        EU32(E, 1648) = EU32(SL, 740);
        EU32(E, 1652) = EU32(SL, 716);
    }
    if (EU32(E, 8) == 0 || EI32(E, 96) != 2)
        return 0;
    if (!may_repeat) {
        /* as a picture within its size limit */
        S32(64) = 0;
        return 0;
    }
    limit = S32(28) == 2 ? EU32(E, 100) : EU32(E, 104);
    if (S32(0) == 0) {                               /* 0x9d42c */
        int32_t br = A32(64);
        if (br < 0xfa001) {
            limit = 0x15e000u;
        } else {
            double d = (double)br * 1.4;
            limit = d >= 2147483648.0 ? (uint32_t)(int32_t)(d - 2147483648.0) | 0x80000000u
                                      : (uint32_t)(int32_t)d;
        }
    }
    bits = EU32(E, 312);
    if (!(limit < bits) || !(S32(64) < A32(100))) {
        S32(64) = 0;
        return 0;
    }
    /* 0x9d4cc */
    target = (uint32_t)(((uint64_t)(limit * 95u) * 0x51eb851fu) >> 37);
    delta = eprc_update_qp(SS8(68), (int32_t)bits, (int32_t)target);
    if (delta <= 0)
        delta = 1;
    q = (int8_t)(uint8_t)(delta + S8(68));
    if (q < 0)
        q = 0;
    else if (q >= 52)
        q = 51;
    SS8(68) = (int8_t)q;
    S32(88) = (int32_t)target;
    EU8(E, 1596) = (uint8_t)q;
    S32(64) = S32(64) + 1;
    return 1;
}

int EPRC21_FrameEnd(Eprc *rc, uint32_t bytes, const uint32_t regs[EPRC_STAT_REGS],
                  EprcPicture *pic)
{
    return EPRC21_FrameEndEx(rc, bytes, regs, pic, 1);
}

int EPRC21_FrameEndEx(Eprc *rc, uint32_t bytes,
                    const uint32_t regs[EPRC_STAT_REGS], EprcPicture *pic,
                    int may_repeat)
{
    uint8_t *E = rc->e;
    uint32_t bits = bytes * 8u;

    /* i264e_ratecontrol_end (0x38748) and i264e_ratecontrol_is_reenc
     * (0x38890) */
    EU32(E, 312) = bits;
    if (eprc_repeat_judge(rc, may_repeat) > 0) {
        if (pic)
            eprc_picture_fields(rc, pic);
        return 1;
    }
    EU8(E, 1640) |= 2;
    EU32(E, 316) = regs[15];
    EU32(E, 336) = (regs[20] & 0x7ffffffu) + (regs[21] & 0x7ffffffu);
    memcpy(E + 352, regs, 32);
    EU32(E, 384) = (regs[8] & 0xffffu) + (regs[9] & 0xffff0000u);
    EU32(E, 388) = (regs[10] & 0xffffu) + (regs[11] & 0xffff0000u);
    EU32(E, 392) = 0;
    EU32(E, 396) = 0;
    EU32(E, 324) = regs[12];
    EU32(E, 328) = regs[13];
    EU32(E, 332) = regs[14];
    EU32(E, 340) = (((regs[23] >> 16) & 0x7fffu) >> 4) +
                   ((regs[24] & 0x7fffu) >> 2) + (regs[23] & 0x7fffu);
    memcpy(E + 400, regs + 16, 16);
    rc->prev_type = rc->cur_type;
    rc->prev_qp = EU8(E, 1596);
    eprc_frame_end(rc);
    return 0;
}
