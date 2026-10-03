/*
 * Ingenic Helix "eprc" picture rate control, reimplemented from the OEM T23
 * libimp 1.3.0 (JZ_VPU_RC_*_T21).  See eprc.h and docs/T23_EPRC.md.
 *
 * The state blocks keep the OEM layout; fields are addressed by their OEM
 * byte offset (the comments give the OEM code address).  Pointer slots of
 * the OEM layout are written for the emulator comparison only and never read
 * back: the module keeps its own pointers in Eprc.
 */
#include "eprc.h"
#include "eprc_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- tables */

/* .rodata of the OEM library */
static const int16_t eprc_fluct_lvls[5] = { 10, 50, 80, 100, 200 };  /* 0xee254 */
static const int8_t eprc_qual_lvls[9] = { 90, 80, 70, 60, 50, 40, 30, 20, 10 }; /* 0xee274 */

/* ------------------------------------------------------- default values */

/* eprc_default_set_T21 (0xc55b0).  The OEM caller always passes +12 == 0;
 * the other branch (0xc58e4) is not reproduced. */
void EPRC_DefaultSet(uint8_t *e)
{
    uint32_t w = EU16(e, 0), h = EU16(e, 2);
    uint32_t bits = (uint32_t)((int32_t)(3u * w * h) >> 1) << 3;
    uint32_t gop_mode = EU32(e, 16);
    uint32_t rc_mode;
    uint32_t mbw;

    EU16(e, 4) = (uint16_t)((int32_t)(w + 15u) >> 4);
    EU16(e, 6) = (uint16_t)((int32_t)(h + 15u) >> 4);
    EU32(e, 20) = 0;
    EU32(e, 28) = 48;
    EU32(e, 32) = 0;
    EU32(e, 24) = 1;
    EU32(e, 36) = 0;
    EU8(e, 40) = 48;
    EU32(e, 100) = 2;
    EU32(e, 104) = bits / 10u;
    EU32(e, 108) = bits / 20u;
    EU32(e, 112) = bits / 20u;
    EU8(e, 84) = 0;
    EU32(e, 88) = 1;
    EU32(e, 92) = 0xffffffffu;
    EU32(e, 96) = 2;
    {
        static const uint8_t b144[21] = { 2, 1, 2, 0, 1, 1, 1, 0, 0, 3, 0,
                                          0, 0, 0, 5, 1, 0, 5, 0, 10, 10 };
        memcpy(e + 144, b144, sizeof(b144));
    }
    EU32(e, 168) = 48;
    EU32(e, 172) = 48;
    if (gop_mode == 1) {                        /* 0xc5a80 */
        EU32(e, 120) = EU32(e, 48) * 3u;
        EU8(e, 124) = 3;
        EU8(e, 125) = 3;
    } else if (gop_mode == 0) {
        EU8(e, 116) = 3;
    } else if (gop_mode == 2) {                 /* 0xc5aec */
        EU32(e, 128) = 0;
        EU8(e, 132) = 3;
        EU8(e, 133) = 3;
    } else if (gop_mode == 3) {                 /* 0xc5a08 */
        EU32(e, 136) = 1;
        EU8(e, 140) = 3;
        EU8(e, 141) = 3;
    }
    rc_mode = EU32(e, 8);
    if (rc_mode == 1) {                         /* 0xc5a20: CBR */
        EU32(e, 48) = 25;
        EU32(e, 52) = 1;
        EU32(e, 60) = 1;
        EU32(e, 56) = 25;
        EU32(e, 64) = 1000;
        EU8(e, 68) = 1;
        EU8(e, 72) = 100;
        EU8(e, 73) = 1;
        EU8(e, 74) = 51;
        EU8(e, 75) = 10;
        EU8(e, 78) = 2;
        EU8(e, 81) = 5;
        EU8(e, 82) = 3;
        EU8(e, 80) = 3;
        EU8(e, 77) = 10;
        EU8(e, 83) = 0;
    } else if (rc_mode == 0) {                  /* 0xc5ac0: CQP */
        EU32(e, 56) = 1;
        EU32(e, 48) = 50;
        EU32(e, 52) = 25;
        EU8(e, 60) = 30;
        EU8(e, 61) = 33;
        EU8(e, 62) = 33;
    } else if (rc_mode < 4) {                   /* 0xc5704: VBR */
        EU32(e, 48) = 25;
        EU32(e, 52) = 1;
        EU32(e, 60) = 1;
        EU32(e, 56) = 25;
        EU32(e, 64) = 1000;
        EU8(e, 68) = 51;
        EU8(e, 69) = 10;
        EU8(e, 71) = 0;
        EU8(e, 72) = 80;
        EU8(e, 73) = 100;
        EU8(e, 74) = 1;
        EU8(e, 76) = 2;
        EU8(e, 77) = 5;
        EU8(e, 78) = 3;
        EU8(e, 75) = 3;
        EU8(e, 79) = 10;
        EU8(e, 80) = 3;
        EU8(e, 81) = 0;
        EU8(e, 82) = 0;
    }
    /* 0xc5774 */
    memset(e + 176, 0, 4);
    EU8(e, 180) = 3;
    EU8(e, 181) = 1;
    EU8(e, 182) = 1;
    EU32(e, 184) = 31;
    EU32(e, 188) = 128;
    EU32(e, 192) = 128;
    mbw = EU16(e, 4);
    EU8(e, 196) = 0;
    if (mbw < 51u || EU16(e, 6) < 39u) {
        EU32(e, 200) = 0;
        memset(e + 204, 0, 4);
    } else {
        EU32(e, 200) = 0;
        EU8(e, 204) = 3;
        EU8(e, 205) = 3;
        EU8(e, 206) = 8;
        EU8(e, 207) = 8;
    }
    {   /* 0xc57dc */
        static const uint8_t b208[6] = { 0, 3, 0, 1, 8, 10 };
        static const uint8_t b256[38] = { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                          1, 0, 1, 1, 1, 0, 0, 8, 4, 0, 6, 3,
                                          33, 0, 1, 25, 28, 1, 1, 1, 0, 0, 0,
                                          0, 0, 20 };
        memcpy(e + 208, b208, sizeof(b208));
        EU32(e, 216) = 21;
        EU32(e, 220) = 11;
        EU32(e, 224) = 21;
        EU32(e, 228) = 11;
        EU32(e, 232) = 21;
        EU32(e, 236) = 11;
        EU32(e, 240) = 21;
        EU32(e, 244) = 21;
        EU32(e, 248) = 11;
        EU32(e, 252) = 0x3fffe;
        memcpy(e + 256, b256, 8);              /* 256..263 */
        EU32(e, 264) = 17156;
        memcpy(e + 268, b256 + 12, 26);        /* 268..293 */
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

/* i264e_ratecontrol_init (0x40c7c), the eprc part. */
void EPRC_SetupE(uint8_t *e, const EprcParams *p)
{
    uint32_t mode = p->rc_mode;
    uint32_t f292, f296;
    uint32_t gop_mode;

    eprc_vbv_fields(p, &f292, &f296);
    EU32(e, 16) = mode == EPRC_MODE_SMART;
    EU16(e, 0) = (uint16_t)p->width;
    EU32(e, 12) = 0;
    EU16(e, 2) = (uint16_t)p->height;
    EPRC_DefaultSet(e);
    if (mode == 0)
        EU32(e, 28) = 0;
    EU16(e, 4) = (uint16_t)((int32_t)(p->width + 15u) >> 4);
    EU16(e, 6) = (uint16_t)((int32_t)(p->height + 15u) >> 4);
    EU32(e, 104) = f292;
    EU32(e, 108) = f296;
    EU32(e, 112) = f296;
    EU32(e, 20) = mode == EPRC_MODE_SMART ? 5u : 0u;
    EU32(e, 32) = 0;
    EU32(e, 100) = 2;
    EU8(e, 84) = 0;
    EU32(e, 88) = 1;
    EU32(e, 92) = 0xffffffffu;
    EU32(e, 96) = 2;
    EU32(e, 44) = p->field52;
    gop_mode = EU32(e, 16);
    if (gop_mode == 1) {                        /* 0x411f8 SmartP */
        EU8(e, 124) = 3;
        EU8(e, 125) = 3;
        EU32(e, 120) = p->gop * p->bg_interval_gops;
    } else if (gop_mode == 0) {
        EU8(e, 116) = 3;
    }
    if (mode == EPRC_MODE_CQP) {
        EU32(e, 8) = 0;
        EU32(e, 48) = p->gop;
        EU32(e, 52) = p->fps_num;
        EU32(e, 56) = p->fps_den;
        EU8(e, 60) = (uint8_t)(p->cqp - 3u);
        EU8(e, 61) = (uint8_t)p->cqp;
    } else if (mode == EPRC_MODE_CBR) {         /* 0x412dc */
        EU32(e, 8) = 1;
        EU32(e, 48) = p->gop;
        EU32(e, 52) = p->static_time;
        EU32(e, 56) = p->fps_num;
        EU32(e, 60) = p->fps_den;
        EU32(e, 64) = p->bitrate;
        EU8(e, 68) = 3;
        EU8(e, 72) = 100;
        EU8(e, 73) = 1;
        EU8(e, 74) = (uint8_t)p->max_qp;
        EU8(e, 75) = (uint8_t)p->min_qp;
        EU8(e, 76) = (uint8_t)p->max_qp;
        EU8(e, 77) = (uint8_t)p->min_qp;
        EU8(e, 78) = 3;
        EU8(e, 79) = 3;
        EU8(e, 80) = (uint8_t)((p->fps_num / p->fps_den) >> 1);
        EU8(e, 81) = (uint8_t)p->gop_qp_step;
        EU8(e, 82) = (uint8_t)p->frm_qp_step;
        EU8(e, 83) = (uint8_t)p->i_bias;
    } else {                                    /* 0x40ee0: VBR, SMART */
        EU32(e, 8) = mode == EPRC_MODE_VBR ? 2u : 3u;
        EU32(e, 48) = p->gop;
        EU32(e, 52) = p->static_time;
        EU32(e, 56) = p->fps_num;
        EU32(e, 60) = p->fps_den;
        EU32(e, 64) = p->max_bitrate;
        EU8(e, 68) = (uint8_t)p->max_qp;
        EU8(e, 69) = (uint8_t)p->min_qp;
        EU8(e, 70) = (uint8_t)p->min_qp;
        EU8(e, 71) = 1;
        EU8(e, 72) = (uint8_t)p->change_pos;
        EU8(e, 73) = 100;
        EU8(e, 74) = 1;
        EU8(e, 75) = (uint8_t)((p->fps_num / p->fps_den) >> 1);
        EU8(e, 76) = 3;
        EU8(e, 77) = (uint8_t)p->gop_qp_step;
        EU8(e, 78) = (uint8_t)p->frm_qp_step;
        EU8(e, 80) = (uint8_t)p->quality;
        EU8(e, 81) = 4;
        EU8(e, 82) = (uint8_t)p->i_bias;
    }
    EI8(e, 40) = (int8_t)(p->init_qp < 0 || mode == 0 ? -1 : p->init_qp);
}

/* ----------------------------------------------------- VIDEO_CFG */

/* Sizes of the arrays behind the 7152-byte state block (0xc5b08). */
static uint32_t eprc_block_size(const uint8_t *e, uint32_t sz[8])
{
    uint32_t mbw = EU16(e, 4), mbh = EU16(e, 6);
    int32_t gop = EI32(e, 48);
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
    return 7152u + sz[0] + sz[1] + sz[2] + sz[3] + sz[4] + sz[5] + sz[6] + sz[7];
}

void EPRC_Layout(Eprc *rc, uint8_t *block)
{
    uint32_t sz[8];
    uint8_t *q;

    rc->p_size = eprc_block_size(rc->e, sz);
    rc->p = block;
    q = block + 7152;
    rc->a1628 = q; q += sz[0];
    rc->a1632 = q; q += sz[1];
    rc->a1636 = q; q += sz[2];
    rc->a1640 = q; q += sz[3];
    rc->a304 = q;  q += sz[4];
    rc->a7140 = q; q += sz[5];
    rc->a7144 = q; q += sz[6];
    rc->a7148 = q;
}

/* JZ_VPU_RC_VIDEO_CFG_T21 (0xc5afc).  The OEM validates every field and
 * stops on out-of-range values; i264e only passes validated parameters, so
 * only the corrections that take effect for those are reproduced. */
int EPRC_VideoCfg(Eprc *rc)
{
    uint8_t *e = rc->e;
    uint8_t *p = rc->p;
    uint32_t sz[8];
    uint32_t mode, gop_mode;
    int32_t frames, stat, cls;
    uint32_t src, dst, target, gop;
    float scale;
    int32_t fl;

    eprc_block_size(e, sz);
    EPTR(e, 1628, rc->a1628);
    EPTR(e, 1632, rc->a1632);
    EPTR(e, 1636, rc->a1636);
    EPTR(e, 1640, rc->a1640);
    EPTR(e, 304, rc->a304);
    EPTR(e, 296, p);
    EPTR(e, 300, p + 352);
    memset(p, 0, 340);
    memset(p + 352, 0, 6800);
    memset(rc->a1628, 0, sz[0]);
    memset(rc->a1632, 0, sz[1]);
    memset(rc->a1636, 0, sz[2]);
    memset(rc->a1640, 0, sz[3]);
    memset(rc->a304, 0, sz[4]);
    EPTR(p, 7144, rc->a7144);
    EPTR(p, 7140, rc->a7140);
    memset(rc->a7140, 0, sz[5]);
    memset(rc->a7144, 0, sz[6]);
    EPTR(p, 7148, rc->a7148);

    /* 0xc6024: field checks with an effect for valid input */
    mode = EU32(e, 8);
    if (EU32(e, 100) != 0) {                         /* 0xc60c0 */
        uint32_t raw = (uint32_t)((int32_t)(EU16(e, 0) * EU16(e, 2) * 3u) >> 1) << 3;
        if (raw < EU32(e, 104))
            EU32(e, 104) = raw;
        if (raw < EU32(e, 108))
            EU32(e, 108) = raw;
    }
    if (EU32(e, 92) < 0xfa00u)
        EU32(e, 92) = 0xfa00u;
    if (EU8(e, 147) == 0)
        EU8(e, 147) = 1;
    if (EI32(e, 248) < 0)
        EU32(e, 248) = 0;
    if (mode == 1) {                                 /* 0xc73c0 CBR */
        if (EU32(e, 48) == 0)
            EU32(e, 48) = 1;
        if (EU32(e, 56) < EU32(e, 60))
            EU32(e, 60) = EU32(e, 56);
        if (EU8(e, 80) >= 4)
            EU8(e, 80) = 3;
        if (EU8(e, 77) < EU8(e, 75))
            EU8(e, 77) = EU8(e, 75);
        else if (EU8(e, 74) < EU8(e, 77))
            EU8(e, 77) = EU8(e, 74);
    } else if (mode >= 2) {                          /* 0xc6828 VBR */
        if (EU32(e, 56) < EU32(e, 60))
            EU32(e, 60) = EU32(e, 56);
        if (EU8(e, 75) >= 4)
            EU8(e, 75) = 3;
        if (EU8(e, 79) < EU8(e, 69))
            EU8(e, 79) = EU8(e, 69);
        else if (EU8(e, 68) < EU8(e, 79))
            EU8(e, 79) = EU8(e, 68);
        if (EU8(e, 80) == 0)
            EU8(e, 80) = 1;
    }

    /* 0xc6c94: copy */
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
    EU32(p, 104) = EU32(e, 84);
    EU32(p, 108) = EU32(e, 88);
    EU32(p, 112) = EU32(e, 92);
    EU32(p, 116) = EU32(e, 96);
    EU32(p, 120) = EU32(e, 100);
    EU32(p, 124) = EU32(e, 104);
    EU32(p, 128) = EU32(e, 108);
    EU32(p, 132) = EU32(e, 112);
    EI8(p, 56) = EI8(e, 40);
    EU32(p, 168) = EU8(e, 144);
    EU32(p, 172) = EU8(e, 145);
    EU32(p, 176) = EU8(e, 146);
    EU32(p, 180) = EU8(e, 147);
    {
        static const uint16_t map[][2] = {      /* P byte <- E byte */
            { 196, 148 }, { 207, 149 }, { 188, 150 }, { 189, 151 },
            { 190, 152 }, { 191, 153 }, { 192, 154 }, { 206, 155 },
            { 159, 156 }, { 195, 157 }, { 158, 158 }, { 160, 159 },
            { 194, 160 }, { 193, 161 }, { 197, 162 }, { 204, 163 },
            { 205, 164 },
        };
        size_t i;
        for (i = 0; i < sizeof(map) / sizeof(map[0]); i++)
            EU8(p, map[i][0]) = EU8(e, map[i][1]);
    }
    EU32(p, 208) = EU32(e, 168);
    EU32(p, 212) = EU32(e, 172);
    EPTR(p, 5744, rc->a1628);
    EPTR(p, 5748, rc->a1632);
    EPTR(p, 5752, rc->a1636);
    EPTR(p, 7136, rc->a1640);
    gop_mode = EU32(e, 16);
    if (gop_mode == 1) {                             /* 0xc7680 */
        EU32(p, 28) = EU32(e, 120);
        EI8(p, 77) = EI8(e, 124);
        EI8(p, 78) = EI8(e, 125);
    } else if (gop_mode == 0) {
        EI8(p, 77) = EI8(e, 116);
    }
    if (mode == 1) {                                 /* 0xc755c CBR */
        EU32(p, 16) = EU32(e, 48);
        EU32(p, 0) = EU32(e, 52);
        EU32(p, 12) = EU32(e, 60);
        EU32(p, 8) = EU32(e, 56);
        EU32(p, 64) = EU32(e, 64) << 10;
        EU8(p, 159) = EU8(e, 68);
        EU32(p, 84) = EU8(e, 72);
        EU32(p, 88) = EU8(e, 73);
        EU8(p, 59) = EU8(e, 74);
        EU8(p, 60) = EU8(e, 75);
        EI8(p, 77) = EI8(e, 78);
        EU8(p, 81) = EU8(e, 81);
        EU8(p, 83) = EU8(e, 82);
        EU32(p, 100) = EU8(e, 80);
        EU8(p, 61) = EU8(e, 77);
        EI8(p, 76) = EI8(e, 83);
    } else if (mode >= 2) {                          /* 0xc6e64 VBR */
        uint32_t maxbits = EU32(e, 64) << 10;
        EU32(p, 8) = EU32(e, 56);
        EU32(p, 68) = maxbits;
        EU32(p, 16) = EU32(e, 48);
        EU32(p, 0) = EU32(e, 52);
        EU32(p, 12) = EU32(e, 60);
        EU32(p, 64) = (EU8(e, 72) * maxbits) / 100u;
        EU8(p, 59) = EU8(e, 68);
        EU8(p, 60) = EU8(e, 69);
        EI8(p, 136) = EI8(e, 71);
        EU32(p, 84) = EU8(e, 73);
        EU32(p, 88) = EU8(e, 74);
        EI8(p, 77) = EI8(e, 76);
        EU8(p, 81) = EU8(e, 77);
        EU8(p, 83) = EU8(e, 78);
        EU32(p, 100) = EU8(e, 75);
        EU8(p, 61) = EU8(e, 79);
        EU8(p, 157) = EU8(e, 80);
        EI8(p, 156) = EI8(e, 81);
        EI8(p, 76) = EI8(e, 82);
    }
    memcpy(p + 216, e + 176, 120);                   /* 0xc6f28 */
    EU32(p, 52) = (uint32_t)EU16(e, 6) * EU16(e, 4);
    EU32(p, 48) = (uint32_t)EU16(e, 0) * EU16(e, 2);
    if (mode == 0)
        return 0;

    /* 0xc6fe8 */
    EI8(p, 508) = -1;
    {   /* 0xc769c: bit-rate class from the picture size */
        static const uint32_t thr[3][2] = {
            { 400000, 1000000 }, { 800000, 4000000 }, { 2000000, 2000000 },
        };
        uint32_t pixels = EU32(p, 48);
        target = EU32(p, 64);
        src = EU32(p, 8);
        dst = EU32(p, 12);
        if (pixels < 230400u) {
            cls = 0;
            EU32(p, 164) = 0;
        } else {
            int k = pixels >= 2073600u ? 2 : pixels >= 921600u ? 1 : 0;
            if ((src * thr[k][0]) / dst / 25u >= target) {
                EU32(p, 164) = 0;
                cls = 0;
            } else if ((src * thr[k][1]) / dst / 25u >= target) {
                EU32(p, 164) = 1;
                cls = 1;
            } else {
                EU32(p, 164) = 2;
                cls = 2;
            }
        }
    }
    (void)cls;
    if (EU32(p, 180) != 0) {                         /* 0xc7008 */
        EF32(p, 6324) = 3.2003f;
        EF32(p, 6328) = -1.367f;
    }
    EU8(p, 356) = 0;                                 /* +140 bits 8..11 are 0 */
    gop = EU32(p, 16);
    EU8(p, 192) = 0;
    frames = (int32_t)(dst * gop) / (int32_t)src;    /* 0xc7068 */
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
        } else if (stat % frames != 0) {
            stat = (stat + frames - 1) / frames * frames;
            EU32(p, 0) = (uint32_t)stat;
        }
    }
    /* 0xc71b0 */
    EI32(p, 5728) = (int32_t)(src * (uint32_t)stat) / (int32_t)dst;
    scale = (float)eprc_fluct_lvls[EI8(p, 159)] / 100.0f;
    {
        int32_t gop_bits = (int32_t)(target * (uint32_t)frames);
        float fgop = (float)gop_bits;
        fl = (int32_t)(scale * fgop);
        EI32(p, 200) = fl;
        if (mode == 1) {                             /* 0xc7cd8 CBR */
            int32_t gops = stat / frames;
            int32_t fluct = eprc_fluct_lvls[EI8(p, 159)];
            int32_t peak = (int32_t)((float)(fluct + 100) / 100.0f * (float)(int32_t)target);
            int32_t n, total;
            EI32(p, 700) = gop_bits;
            EU32(p, 5724) = 0;
            EI32(p, 68) = peak;
            EI32(p, 644) = gops;
            EI32(p, 5732) = peak * stat;
            EI32(p, 5740) = (int32_t)((uint32_t)stat * target);
            n = (int32_t)gop - 1;
            total = n * gops;
            EI32(p, 6300) = (int32_t)((uint32_t)stat * target);
            EI32(p, 6312) = fl;
            EI32(p, 412) = total;
            EI32(p, 488) = (int32_t)((float)EI32(p, 84) / (float)(n + EI32(p, 84)) * fgop);
            EI32(p, 492) = (int32_t)((float)EI32(p, 88) / (float)(n + EI32(p, 88)) * fgop);
            EI32(p, 432) = (int32_t)((uint32_t)stat * target) / (total + gops);
        } else {                                     /* 0xc7e10 VBR */
            int32_t maxbits = EI32(p, 68);
            int32_t q = (int32_t)((float)eprc_qual_lvls[EI8(p, 157)] / 100.0f * (float)maxbits);
            EI32(p, 700) = gop_bits;
            EU32(p, 5724) = 0;
            EI32(p, 72) = q;
            EI32(p, 644) = stat / frames;
            EI32(p, 5732) = maxbits * stat;
            EI32(p, 712) = q * frames;
            EI32(p, 5736) = stat * (int32_t)target;
            EI32(p, 708) = maxbits * frames;
            EI32(p, 6300) = gop_bits;
            EI32(p, 412) = (int32_t)gop - 1;
            EI32(p, 6312) = EI32(p, 200);
            EI32(p, 432) = gop_bits / (int32_t)gop;
        }
    }
    return 0;
}

int EPRC_Init(Eprc *rc, const EprcParams *params, uint8_t *slice)
{
    uint32_t sz[8];
    uint32_t size;
    uint8_t *block;

    memset(rc, 0, sizeof(*rc));
    EPRC_SetupE(rc->e, params);
    size = eprc_block_size(rc->e, sz);
    block = calloc(1, size);
    if (!block)
        return -1;
    EPRC_Layout(rc, block);
    EPTR(rc->e, 1620, block);
    EU32(rc->e, 1624) = size;
    rc->slice = slice;
    return EPRC_VideoCfg(rc);
}

void EPRC_Free(Eprc *rc)
{
    free(rc->p);
    rc->p = NULL;
}
