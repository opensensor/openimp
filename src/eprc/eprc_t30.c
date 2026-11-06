/*
 * Ingenic Helix "eprc" picture rate control, T30 revision: reimplemented
 * from the OEM T30 libimp 1.0.5 (eprc_default_set_T30, JZ_VPU_RC_*_T30 and
 * their helpers at 0x11aa14..0x1376b0 of the uclibc 5.4.0 build; the 4.7.2
 * build the T30X ships, md5 abd61e9a, is the same code).  See eprc.h and
 * docs/T23_EPRC.md, "T30".
 *
 * The T30 controller is an older relative of the T21/T23 eprc with its own
 * state layout: the controller block E (i264e parameters + 0x9044c), a
 * 0x120-byte block A, the state S = A + 0x120 (0xbe0 bytes) and the arrays
 * behind them (A + 0xd00).  Fields are addressed by their OEM byte offset;
 * comments give OEM code addresses (uclibc 5.4.0 build).  Pointer slots of
 * the OEM layout are not written: the module keeps its pointers in Eprc30.
 * Only the H.264 path (E+12 == 0) is reproduced.
 */
#include "eprc.h"
#include "eprc_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* .rodata of the OEM library (base 0x170000) */
static const int16_t e30_fluct[7] = { 10, 50, 80, 100, 200, 0, 90 }; /* -1220, read [E+64 + 1] */
static const int32_t e30_quality[10] = { 90, 80, 70, 60, 50, 40, 30, 20, 10, 50 }; /* -1208 */

#define A8(o)   EU8(A, o)
#define AS8(o)  EI8(A, o)
#define A32(o)  EI32(A, o)
#define AU32(o) EU32(A, o)
#define S8(o)   EU8(S, o)
#define SS8(o)  EI8(S, o)
#define S32(o)  EI32(S, o)
#define SU32(o) EU32(S, o)
#define SF(o)   EF32(S, o)

/* ------------------------------------------------------- default values */

/* eprc_default_set_T30 (0x131ed4), the E+12 == 0 (H.264) branch. */
static void e30_default_set(uint8_t *e)
{
    uint32_t bits = (uint32_t)((int32_t)(EU16(e, 0) * EU16(e, 2) * 3u) >> 1) << 3;
    uint32_t gop_mode = EU32(e, 16);
    uint32_t mode;

    EU16(e, 0) = 1280;
    EU16(e, 2) = 720;
    EU16(e, 4) = 120;
    EU16(e, 6) = 80;
    EU32(e, 8) = 1;
    EU32(e, 20) = 0;
    EU32(e, 28) = 0x230;
    EU32(e, 32) = 0;
    EU32(e, 24) = 1;
    EU32(e, 36) = 0;
    EU8(e, 40) = 26;
    EU8(e, 148) = 0;
    EU8(e, 149) = 0;
    EU8(e, 150) = 1;
    EU32(e, 96) = 2;
    EU32(e, 100) = bits / 10u;
    EU32(e, 104) = bits / 20u;
    EU32(e, 108) = bits / 20u;
    EU8(e, 80) = 0;
    EU32(e, 84) = 1;
    EU32(e, 88) = 0xffffffffu;
    EU32(e, 92) = 2;
    EU32(e, 144) = 0x1000;
    EF32(e, 152) = 1.2f;
    if (gop_mode == 1) {
        EU8(e, 120) = 3;
        EU32(e, 116) = EU32(e, 44) * 3u;
        EU8(e, 121) = 3;
    } else if (gop_mode == 0) {
        EU8(e, 112) = 3;
    } else if (gop_mode == 2) {
        EU32(e, 124) = 0;
        EU8(e, 128) = 3;
        EU8(e, 129) = 3;
    } else if (gop_mode == 3) {
        EU32(e, 132) = 1;
        EU8(e, 136) = 3;
        EU8(e, 137) = 3;
    }
    mode = EU32(e, 8);                          /* 1: the CBR defaults */
    (void)mode;
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
}

static void e30_vbv_fields(const EprcParams *p, uint32_t *f280, uint32_t *f284)
{
    int32_t pixels = (int32_t)(((p->width + 15u) & ~15u) * ((p->height + 15u) & ~15u));
    int32_t lo = (int32_t)(((int64_t)(pixels * 3) * 0x51eb851f) >> 36) -
                 ((pixels * 3) >> 31);
    int32_t hi = (pixels * 12) / (p->width < 801u ? 3 : 6);
    int32_t v = 19660800;

    /* i264e_validate_parameters (0x488ec) stops before these fields for a
     * picture size it refuses; i264e then keeps the defaults */
    if (p->width < 256u || (p->width & 15u) || (p->height & 1u) || p->height < 16u) {
        *f280 = 19660800u;
        *f284 = 14043429u;
        return;
    }
    v = v < lo ? lo : v > hi ? hi : v;
    *f280 = (uint32_t)v;
    hi = (int32_t)((float)v / 1.4f);
    v = 14043429;
    v = v < lo ? lo : v > hi ? hi : v;
    *f284 = (uint32_t)v;
}

/* i264e_ratecontrol_init (0x52a24), the T30 part (i264e parameter [0] = 3):
 * the i264e parameters in the T20 layout into E. */
static void e30_setup_e(uint8_t *e, const EprcParams *p)
{
    uint32_t mode = p->rc_mode;
    uint32_t f280, f284;

    e30_vbv_fields(p, &f280, &f284);
    EU32(e, 12) = 0;
    EU32(e, 16) = mode == EPRC_MODE_SMART;
    e30_default_set(e);
    EU16(e, 0) = (uint16_t)p->width;
    EU16(e, 2) = (uint16_t)p->height;
    EU16(e, 4) = (uint16_t)((int32_t)(p->width + 15u) >> 4);
    EU16(e, 6) = (uint16_t)((int32_t)(p->height + 15u) >> 4);
    if (mode == EPRC_MODE_CQP)
        EU32(e, 28) = 0;
    EU32(e, 20) = mode == EPRC_MODE_SMART ? 5u : 0u;
    EU32(e, 100) = f280;
    EU32(e, 104) = f284;
    EU32(e, 108) = f284;
    EU32(e, 84) = 1;
    EU32(e, 32) = 0;
    EU32(e, 96) = 2;
    EU8(e, 80) = 0;
    EU32(e, 88) = 0xffffffffu;
    EU32(e, 92) = 2;
    if (p->init_qp > 0)
        EI8(e, 40) = (int8_t)p->init_qp;
    if (EU32(e, 16) == 1) {
        EU8(e, 120) = 3;
        EU32(e, 116) = p->gop * 3u;
        EU8(e, 121) = 3;
    } else {
        EU8(e, 112) = 3;
    }
    if (mode == EPRC_MODE_CQP) {
        EU32(e, 8) = 0;
        EU32(e, 44) = p->gop;
        EU32(e, 48) = p->fps_num;
        EU32(e, 52) = p->fps_den;
        EU8(e, 56) = (uint8_t)(p->cqp - 3u);
        EU8(e, 57) = (uint8_t)p->cqp;
    } else if (mode == EPRC_MODE_CBR) {         /* 0x54f18 */
        EU32(e, 8) = 1;
        EU32(e, 44) = p->gop;
        EU32(e, 48) = p->static_time;
        EU32(e, 52) = p->fps_num;
        EU32(e, 56) = p->fps_den;
        EU32(e, 60) = p->bitrate;
        EU8(e, 64) = 3;
        EU8(e, 68) = 100;
        EU8(e, 69) = 1;
        EU8(e, 76) = (uint8_t)((p->fps_num / p->fps_den) >> 1);
        EU8(e, 70) = (uint8_t)p->max_qp;
        EU8(e, 72) = (uint8_t)p->max_qp;
        EU8(e, 74) = 3;
        EU8(e, 71) = (uint8_t)p->min_qp;
        EU8(e, 73) = (uint8_t)p->min_qp;
        EU8(e, 75) = 3;
        EU8(e, 77) = (uint8_t)p->gop_qp_step;
        EU8(e, 78) = (uint8_t)p->frm_qp_step;
        EU8(e, 79) = (uint8_t)p->i_bias;
    } else {                                    /* 0x53dfc: VBR, SMART */
        EU32(e, 8) = mode == EPRC_MODE_VBR ? 2u : 3u;
        EU32(e, 44) = p->gop;
        EU32(e, 48) = p->static_time;
        EU32(e, 52) = p->fps_num;
        EU32(e, 56) = p->fps_den;
        EU32(e, 60) = p->max_bitrate;
        EU8(e, 64) = (uint8_t)p->max_qp;
        EU8(e, 67) = 1;
        EU8(e, 65) = (uint8_t)p->min_qp;
        EU8(e, 66) = (uint8_t)p->min_qp;
        EU8(e, 69) = 100;
        EU8(e, 70) = 0;
        EU8(e, 68) = (uint8_t)p->change_pos;
        EU8(e, 71) = (uint8_t)((p->fps_num / p->fps_den) >> 1);
        EU8(e, 72) = (uint8_t)p->i_bias;
        EU8(e, 73) = (uint8_t)p->gop_qp_step;
        EU8(e, 74) = (uint8_t)p->frm_qp_step;
        EU8(e, 78) = (uint8_t)p->i_bias;
        EU8(e, 76) = (uint8_t)p->quality;
        EU8(e, 77) = (uint8_t)p->quality;
    }
}

/* ----------------------------------------------------- VIDEO_CFG */

#define CLAMP_U32(o, lo, hi) do { if (EU32(e, o) < (lo)) EU32(e, o) = (lo); \
    else if (EU32(e, o) > (hi)) EU32(e, o) = (hi); } while (0)
#define CLAMP_U8(o, lo, hi) do { if (EU8(e, o) < (lo)) EU8(e, o) = (lo); \
    else if (EU8(e, o) > (hi)) EU8(e, o) = (hi); } while (0)
#define CLAMP_I8(o, lo, hi) do { if (EI8(e, o) < (lo)) EI8(e, o) = (lo); \
    else if (EI8(e, o) > (hi)) EI8(e, o) = (hi); } while (0)

/* The validation of JZ_VPU_RC_VIDEO_CFG_T30 for H.264 (0x132ae8..). */
static void e30_validate(uint8_t *e)
{
    uint32_t mode;

    if (EU32(e, 8) > 3u)
        EU32(e, 8) = 3;
    if (EU32(e, 20) > 6u)
        EU32(e, 20) = 6;
    if (EU32(e, 16) > 3u)
        EU32(e, 16) = 3;
    if (EU32(e, 32) > 2u)
        EU32(e, 32) = 2;
    CLAMP_U32(24, 1u, 2u);
    CLAMP_I8(40, -1, 51);
    if (EU32(e, 96) != 0) {
        uint32_t f = (uint32_t)((int32_t)(EU16(e, 0) * EU16(e, 2) * 3u) >> 1) << 3;
        if (f < EU32(e, 100))
            EU32(e, 100) = f;
        if (f < EU32(e, 104))
            EU32(e, 104) = f;
    }
    if (EU32(e, 84) > 1u)
        EU32(e, 84) = 1;
    if (EU32(e, 88) < 64000u)
        EU32(e, 88) = 64000;
    if (EU32(e, 92) > 0xffffu)
        EU32(e, 92) = 0xffff;
    mode = EU32(e, 8);
    CLAMP_U32(44, 1u, 0xffffu);
    if (mode == 0) {                            /* 0x132e90 */
        CLAMP_U32(48, 1u, 240u);
        if (EU32(e, 52) == 0)
            EU32(e, 52) = 1;
        else if (EU32(e, 48) < EU32(e, 52))
            EU32(e, 52) = EU32(e, 48);
        if (EU8(e, 56) > 51)
            EU8(e, 56) = 51;
        if (EU8(e, 57) > 51)
            EU8(e, 57) = 51;
        return;
    }
    CLAMP_U32(52, 1u, 240u);
    if (EU32(e, 56) == 0)
        EU32(e, 56) = 1;
    else if (EU32(e, 52) < EU32(e, 56))
        EU32(e, 56) = EU32(e, 52);
    CLAMP_U32(48, 1u, 60u);
    CLAMP_U32(60, 2u, 120400u);
    if (mode == 1) {                            /* 0x133448 */
        CLAMP_U8(64, 1, 5);
        CLAMP_U8(69, 1, 100);
        CLAMP_U8(68, 1, 100);
        if (EU8(e, 70) > 51)
            EU8(e, 70) = 51;
        if (EU8(e, 71) > 51)
            EU8(e, 71) = 51;
        CLAMP_I8(74, -10, 30);
        if (EU8(e, 77) > 30)
            EU8(e, 77) = 30;
        if (EU8(e, 76) > 3)
            EU8(e, 76) = 3;
        if (EU8(e, 73) < EU8(e, 71))
            EU8(e, 73) = EU8(e, 71);
        else if (EU8(e, 70) < EU8(e, 73))
            EU8(e, 73) = EU8(e, 70);
        CLAMP_I8(79, -10, 10);
        return;
    }
    /* 0x132c18: VBR, SMART */
    if (EU8(e, 64) > 51)
        EU8(e, 64) = 51;
    if (EU8(e, 65) > 51)
        EU8(e, 65) = 51;
    CLAMP_U8(70, 1, 100);
    CLAMP_U8(69, 1, 100);
    CLAMP_U8(68, 50, 100);
    CLAMP_I8(72, -10, 30);
    if (EU8(e, 73) > 30)
        EU8(e, 73) = 30;
    if (EU8(e, 71) > 3)
        EU8(e, 71) = 3;
    if (EU8(e, 75) < EU8(e, 65))
        EU8(e, 75) = EU8(e, 65);
    else if (EU8(e, 64) < EU8(e, 75))
        EU8(e, 75) = EU8(e, 64);
    CLAMP_U8(76, 1, 8);
    CLAMP_I8(77, -9, 9);
    CLAMP_I8(78, -10, 10);
}

/* JZ_VPU_RC_VIDEO_CFG_T30 (0x132374), H.264. */
static int e30_video_cfg(Eprc30 *rc)
{
    uint8_t *e = rc->e;
    uint8_t *A, *S;
    uint32_t w64 = (uint32_t)((int32_t)(EU16(e, 4) * 16u + 63u) >> 6);
    uint32_t h64 = (uint32_t)((int32_t)(EU16(e, 6) * 16u + 63u) >> 6);
    uint32_t n64 = w64 * h64;
    uint32_t ysz = (EU16(e, 6) * 4u + 15u) & ~15u;
    uint32_t mode, gop_mode;

    EU32(e, 608) = w64;
    EU32(e, 612) = h64;
    EU32(e, 616) = n64;
    if (!rc->blk) {
        rc->blk_size = n64 * 16u + 0xd00u + ysz;
        rc->blk = calloc(1, rc->blk_size);
        if (!rc->blk)
            return -1;
    }
    A = rc->blk;
    S = A + 0x120;
    rc->x = A + 0xd00;                          /* E+572, 588..600 */
    rc->y = A + 0xd00 + n64 * 16u;              /* E+576 */
    memset(A, 0, 0x114);
    memset(S, 0, 0xbe0);
    EU32(e, 144) = n64 << 6;
    e30_validate(e);

    /* 0x132f0c */
    A8(210) = (uint8_t)EU32(e, 32);
    AU32(196) = EU32(e, 12);
    AU32(160) = EU32(e, 20);
    AU32(152) = EU32(e, 28);
    AU32(164) = EU32(e, 24);
    AU32(36) = EU16(e, 0);
    AU32(40) = EU16(e, 2);
    AU32(44) = EU16(e, 4);
    AU32(48) = EU16(e, 6);
    AU32(156) = EU32(e, 8);
    AU32(116) = EU32(e, 80);
    AU32(120) = EU32(e, 84);
    AU32(124) = EU32(e, 88);
    AU32(128) = EU32(e, 92);
    AU32(136) = EU32(e, 100);
    AU32(140) = EU32(e, 104);
    AU32(132) = EU32(e, 96);
    AU32(144) = EU32(e, 108);
    A8(60) = EU8(e, 40);
    A8(229) = EU8(e, 148);
    A8(230) = EU8(e, 149);
    A8(228) = EU8(e, 150);
    AU32(212) = EU32(e, 144);
    AU32(216) = w64;
    AU32(220) = h64;
    AU32(224) = n64;
    gop_mode = EU32(e, 16);
    if (gop_mode == 1) {                        /* 0x1337ac */
        AU32(28) = EU32(e, 116);
        A8(89) = EU8(e, 120);
        A8(90) = EU8(e, 121);
    } else if (gop_mode == 0) {
        A8(89) = EU8(e, 112);
    } else if (gop_mode == 2) {
        AU32(24) = EU32(e, 124);
        A8(91) = EU8(e, 128);
        A8(89) = EU8(e, 129);
    } else if (gop_mode == 3) {
        AU32(20) = EU32(e, 132);
        A8(92) = EU8(e, 136);
        A8(89) = EU8(e, 137);
    }
    mode = EU32(e, 8);
    if (mode == 1) {                            /* 0x133640 */
        int32_t gop = EI32(e, 44), den = EI32(e, 56), num = EI32(e, 52);
        int32_t st = EI32(e, 48), br = EI32(e, 60) * 1024, gs, t;

        A32(16) = gop;
        A32(0) = st;
        A32(12) = den;
        A32(8) = num;
        A32(68) = br;
        A8(171) = EU8(e, 64);
        AU32(96) = EU8(e, 68);
        AU32(100) = EU8(e, 69);
        A8(63) = EU8(e, 70);
        A8(64) = EU8(e, 71);
        A8(89) = EU8(e, 74);
        A8(93) = EU8(e, 77);
        A8(95) = EU8(e, 78);
        AU32(112) = EU8(e, 76);
        A8(65) = EU8(e, 73);
        A8(88) = EU8(e, 79);
        gs = gop * den / num;
        A32(4) = gs;
        if (gs < 1) {
            A32(4) = 1;
            if (st < 1) {
                st = 1;
                A32(0) = st;
            }
        } else if (st < gs || st % gs != 0) {
            st = gs;
            A32(0) = st;
        }
        A32(32) = num * st / den;
        t = (e30_fluct[EI8(e, 64) + 1] + 100) * br / 100;
        A32(72) = t;
        A32(56) = EU16(e, 4) * EU16(e, 6);
        A32(52) = EU16(e, 0) * EU16(e, 2);
        A32(84) = br * st;
        A32(80) = t * st;
    } else if (mode == 0) {                     /* 0x133410 */
        A32(16) = EI32(e, 44);
        A32(8) = EI32(e, 48);
        A32(12) = EI32(e, 52);
        A8(61) = EU8(e, 56);
        A8(62) = EU8(e, 57);
    } else {                                    /* 0x133044: VBR, SMART */
        int32_t gop = EI32(e, 44), den = EI32(e, 56), num = EI32(e, 52);
        int32_t st = EI32(e, 48), br = EI32(e, 60) * 1024, gs, q;

        A32(16) = gop;
        A32(0) = st;
        A32(12) = den;
        A32(8) = num;
        A32(72) = br;
        A32(68) = (int32_t)((uint32_t)EU8(e, 68) * (uint32_t)br / 100u);
        A8(63) = EU8(e, 64);
        A8(64) = EU8(e, 65);
        A8(148) = EU8(e, 67);
        AU32(96) = EU8(e, 69);
        AU32(100) = EU8(e, 70);
        A8(89) = EU8(e, 72);
        A8(93) = EU8(e, 73);
        A8(95) = EU8(e, 74);
        AU32(112) = EU8(e, 71);
        A8(65) = EU8(e, 75);
        A8(169) = EU8(e, 76);
        A8(168) = EU8(e, 77);
        A8(88) = EU8(e, 78);
        gs = gop * den / num;
        A32(4) = gs;
        if (gs < 1) {
            A32(4) = 1;
            gs = 1;
            if (st < 1) {
                st = gs;
                A32(0) = st;
            }
        } else if (st < gs || st % gs != 0) {
            st = gs;
            A32(0) = st;
        }
        q = e30_quality[EI8(e, 76)];
        A32(32) = num * st / den;
        A32(76) = br * q / 100;
        A32(56) = EU16(e, 4) * EU16(e, 6);
        A32(52) = EU16(e, 0) * EU16(e, 2);
        A32(80) = br * st;
    }

    /* 0x133184 */
    A32(180) = 2;
    A32(184) = 2;
    A32(188) = 1;
    A32(192) = A32(196) != 0 ? 2 : 0;
    A8(209) = 1;
    A8(200) = 1;
    A8(201) = 0;
    A8(202) = 0;
    A8(203) = 0;
    A8(204) = 3;
    A8(205) = 0;
    A8(211) = 0;
    if (A32(164) == 1) {
        A8(208) = 0;
        A8(170) = 5;
        A8(172) = 1;
        A8(207) = 0;
        A8(206) = 5;
    } else if (A32(164) == 2) {
        A8(208) = 1;
        A8(170) = 5;
        A8(172) = 3;
        A8(207) = 1;
        A8(206) = 5;
    }
    if (A8(228) != 0 && mode - 2u < 2u) {       /* VBR, SMART, H.264 */
        A8(231) = 1;
        A8(232) = 1;
        A8(233) = 20;
        A32(260) = 4;
        A32(264) = 5;
        A32(256) = 150;
        A32(252) = 120;
        A32(236) = A32(72) * 180 / 100;
        A8(268) = 4;
        A32(240) = A32(76) * 100 / 200;
        A32(272) = A32(16) / 5 < 1 ? 1 : A32(16) / 5;
    }
    {
        static const uint32_t lim[6] = { 400000, 1000000, 800000, 4000000,
                                         2000000, 2000000 };
        uint32_t px = AU32(52);
        int k = -1;

        if (px >= 0x1fa400u)
            k = 2;
        else if (px >= 0x38400u)
            k = px >= 0xe1000u ? 1 : 0;
        if (k < 0) {
            AU32(176) = 0;
        } else if ((AU32(8) * lim[2 * k]) / AU32(12) / 25u < AU32(68)) {
            AU32(176) = (AU32(8) * lim[2 * k + 1]) / AU32(12) / 25u < AU32(68) ? 2 : 1;
        } else {
            AU32(176) = 0;
        }
    }
    (void)S;
    return 0;
}

int EPRC30_Init(Eprc30 *rc, const EprcParams *params, uint8_t *slice)
{
    memset(rc, 0, sizeof(*rc));
    if (!params->gop || !params->fps_num || !params->fps_den)
        return -1;
    e30_setup_e(rc->e, params);
    rc->slice = slice;
    return e30_video_cfg(rc);
}

void EPRC30_Free(Eprc30 *rc)
{
    free(rc->blk);
    rc->blk = NULL;
}
