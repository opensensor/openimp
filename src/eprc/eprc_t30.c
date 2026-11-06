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
/* The OEM multiplies in 32-bit registers (mul wraps); several budget
 * products overflow with large bit rates (smart-P S+1052 * A+252).  Signed
 * overflow must wrap here as on the camera: GCC builds this file with
 * -fwrapv (pragma below; build-t30.sh and tests/eprc pass the flag too). */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("wrapv")
#endif
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

/* ------------------------------------------------------- per picture */

/* .rodata (base 0x170000) */
static const int32_t e30_tab474[24] = {               /* -0x474 */
    2, 3, 2, 2, 1, 0, 0, 2, 4, 6, 4, 4, 2, 2, 2, 4, 32, 35, 38, 42, 44, 46, 48, 50 };
static const int32_t e30_tab494[8] = { 50, 70, 50, 70, 25, 25, 50, 50 }; /* -0x494 */
static const int32_t e30_tab434[3][10] = {            /* -0x434 */
    { 32, 35, 38, 42, 44, 46, 48, 50, 50, 50 },
    { 26, 29, 32, 35, 38, 42, 45, 47, 49, 49 },
    { 20, 23, 26, 29, 32, 35, 38, 42, 45, 47 } };
static const int32_t e30_tab3bc[9] = { -4, -3, -2, -1, 0, 1, 2, 3, 4 }; /* -0x3bc */

const uint32_t EPRC30_StatRegs[EPRC30_STAT_REGS] = {
    0x800e4, 0x800e8, 0x800ec, 0x80034, 0x80088, 0x8008c, 0x40094, 0x40098,
    0x4009c, 0x400a0, 0x400b8, 0x400bc, 0x400c0, 0x80120, 0x80124, 0x80128,
    0x8012c, 0x80130, 0x80134, 0x80138, 0x8013c, 0x80140, 0x80144, 0x80148,
    0x8014c };

/* MIPS32 trunc.w.s / trunc.w.d (FCSR invalid exception disabled): a NaN or
 * an out-of-range value gives 2^31 - 1.  The OEM runs into NaN when a
 * window holds a zero MAD; the host build of this file must convert as
 * the camera does. */
static int32_t e30_trunc_w(double v)
{
    if (!(v > -2147483649.0 && v < 2147483648.0))
        return 0x7fffffff;
    return (int32_t)v;
}

/* update_qp (0x1380e0): the QP change that scales 2^((QP - 4) / 6) by
 * real / ref, in whole QP steps (0 when out of range). */
static int32_t e30_update_qp(int32_t qp, int32_t ref, int32_t real)
{
    float ratio = (float)real / (ref < 1 ? 1.0f : (float)ref);
    double r = (double)ratio, num;
    int32_t s;

    if (ratio >= 1.0f) {
        if (qp < 0)
            return 0;
        num = pow(2.0, (double)((qp - 4) / 6));
        for (s = qp; s >= 0; s--)
            if (r < num / pow(2.0, (double)((s - 4) / 6)))
                return (int8_t)(s - qp);
        return 0;
    }
    if (qp >= 52)
        return 0;
    num = pow(2.0, (double)((qp - 4) / 6));
    for (s = 51; s >= qp; s--)
        if (r < num / pow(2.0, (double)((s - 4) / 6)))
            return (int8_t)(s - qp);
    return 0;
}

/* gop_init (0x129bc0): GOP budget and model at an IDR. */
static void e30_gop_init(uint8_t *A, uint8_t *S)
{
    int32_t gop, mode;

    if (S32(0) == 0) {                               /* first picture */
        mode = A32(156);
        if (mode == 1) {
            int32_t gs = A32(4), st = A32(0), a18 = A32(96), a19 = A32(100);
            int32_t g1 = A32(16) - 1, b = gs * A32(68);

            S32(2208) = A32(84);
            S32(1044) = b;
            S32(936) = st / gs;
            S32(872) = b * a18 / (g1 + a18);
            S32(876) = b * a19 / (g1 + a19);
            S32(72) = (st / gs) * g1;
        } else if (mode != 0 && (uint32_t)mode <= 3u) {
            int32_t gs = A32(4);

            S32(936) = A32(0) / gs;
            S32(1044) = gs * A32(68);
            S32(1056) = A32(76) * gs;
            S32(1052) = A32(72) * gs;
            if (A8(228) != 0) {
                A32(244) = A32(236) * gs;
                A32(248) = A32(240) * gs;
            }
            S32(2208) = gs * A32(68);
            S32(72) = A32(16) - 1;
        }
        S32(2220) = 0;
        return;
    }

    gop = A32(16);
    {
        int32_t n = S32(932), pos = S32(52);

        S32(68) = pos;
        S32(1060) = S32(1044);
        /* n (S+932) counts the GOPs of the static-time window: at most
         * A+0 / A+4 <= 60 (VIDEO_CFG clamps the static time to 60 s and
         * makes it a multiple of the GOP seconds), so the write stays in
         * S+1064..S+1304; n == 60 overwrites S+1064 as the OEM does
         * (tests/eprc/eprc_t30_static_vectors.txt). */
        S32(1304 - 4 * n) = S32(1048);
        S32(12) = S32(12) + S32(1048);
        if (pos < gop / 2) {
            S32(936) = 0;
        } else {
            S32(932) = n + 1;
            S32(936) = S32(936) - 1;
        }
    }
    if (gop == 1 || S32(68) == 1) {                  /* 0x13a444 */
        int32_t c = SS8(81);

        S32(968) = c + 3;
        S32(964) = c;
    } else if (A32(188) == 1) {
        int32_t pos = S32(52), d;

        if (pos < 2) {
            S32(52) = 2;
            pos = 2;
            d = 1;
        } else {
            d = pos - 1;
        }
        S32(968) = (pos + S32(960) - 2) / d;
        S32(964) = (pos + S32(956) - 1) / pos;
    } else if (A32(188) == 2) {
        if (SF(952) < 1.0f)
            SF(952) = 1.0f;
        if (SF(948) < 1.0f)
            SF(948) = 1.0f;
        S32(968) = e30_trunc_w((double)(SF(944) / SF(952)));
        S32(964) = e30_trunc_w((double)(SF(940) / SF(948)));
    }
    /* A+203 (history weighting) and A+205 (QP spread) are always 0 */
    S32(972) = -1;
    S32(976) = -1;
    gop = A32(16);
    mode = A32(156);
    if (gop != 1) {
        int32_t s34 = S32(208), g1 = gop - 1, t, t2, d9;
        float fv;

        t = (S32(1044) - s34) / g1;
        if (t < 1)
            t = 1;
        d9 = s34 * 10 / t;
        S32(868) = d9;
        t2 = (S32(1048) - s34) / g1;
        fv = (((float)d9 / 10.0f) * (float)SS8(82)) / (float)S32(968);
        if (t2 < 1)
            t2 = 1;
        S32(864) = s34 * 10 / t2;
        SF(880) = ((float)gop * fv) / ((float)g1 + fv);
    }
    if (mode == 1) {                                 /* CBR */
        int32_t v = S32(936), used;

        if (v == 0) {
            int32_t gs = A32(4);

            if (S32(60) < 1)
                S32(60) = 1;
            v = A32(0) / gs;
            S32(12) = 0;
            S32(932) = 0;
            S32(1064) = 0;
            S32(60) = 0;
            S32(2220) = 0;
            used = 0;
            S32(936) = v;
            S32(72) = A32(32) - v;
            S32(1044) = A32(68) * gs;
        } else {
            used = S32(1064);
        }
        S32(2208) = A32(84) - used;
        if (v < 1) {
            v = 1;
            S32(936) = 1;
        }
        S32(184) = (A32(84) - used) / (v * A32(16));
        S32(52) = 0;
    } else if (mode == 0) {
        S32(52) = 0;
    } else if ((uint32_t)mode <= 3u) {               /* VBR, SMART */
        int32_t b;

        if (S32(60) < 1)
            S32(60) = 1;
        S32(12) = 0;
        S32(932) = 0;
        S32(1064) = 0;
        S32(60) = 0;
        S32(72) = A32(16) - 1;
        S32(2220) = 0;
        S32(936) = A32(0) / A32(4);
        if (A8(148) == 0) {
            b = S32(1044);
        } else {
            int32_t c = (int8_t)A8(176), idx, k;
            const int32_t *row;

            if ((uint32_t)S32(884) > 3u) {
                c = c + 1;
                if (c > 2)
                    c = 2;
                if (c < 0)
                    c = 0;
            }
            row = e30_tab434[c];
            for (idx = 0; idx < 10 && S32(968) >= row[idx]; idx++)
                ;
            k = e30_tab3bc[AS8(168)] + idx;
            if (S32(968) < 37 || (uint32_t)S32(884) < 4u)
                idx = k < 0 ? 0 : k > 9 ? 9 : k;
            else
                idx = 9;
            b = (S32(1052) - S32(1056)) * idx / 10 + S32(1056);
            S32(1044) = b;
        }
        if (A8(148) != 0 && A8(228) != 0) {   /* 0x12a7a8: smart-P QP S+5 */
            int32_t pos = S32(52), q = S32(968);
            int8_t c4 = (int8_t)q;

            if (pos == A32(16) && S32(1048) < A32(244)) {
                int32_t used = S32(1048), hi = S32(1052), lo = S32(1056);
                int8_t mn = SS8(84), mx = SS8(84);
                int i;
                int flat;

                if (pos < 3) {
                    flat = 1;
                } else {
                    for (i = 0; i < pos - 2; i++) {
                        int8_t v = SS8(85 + i);
                        if (v > mx)
                            mx = v;
                        if (v < mn)
                            mn = v;
                    }
                    flat = (int8_t)(mx - mn) < 6;
                }
                S8(4) = (uint8_t)flat;
                if (q == 0)
                    c4 = (int8_t)(SS8(81) + 3);
                S8(5) = (uint8_t)c4;
                if (SS8(64) >= 21) {
                    if (hi < used) {
                        c4 = (int8_t)(c4 + 1);
                        S8(5) = (uint8_t)c4;
                    } else if (used < lo) {
                        c4 = (int8_t)(c4 - (AS8(169) < 3 ? 1 : 2));
                        S8(5) = (uint8_t)c4;
                    }
                }
                if (c4 < AS8(64)) {
                    c4 = AS8(64);
                    S8(5) = (uint8_t)c4;
                    if (SS8(65) < 21)
                        goto clamp5;
                } else if (AS8(63) < c4) {
                    c4 = AS8(63);
                    S8(5) = (uint8_t)c4;
                    if (SS8(65) < 21)
                        goto clamp5;
                } else if (SS8(65) < 21) {
                    goto done5;
                }
                if (used < S32(1056))
                    c4 = (int8_t)(c4 - 2);
                else if (S32(1052) * A32(256) / 100 < used)
                    c4 = (int8_t)(c4 + 1);
                else if (used < S32(1052) * A32(252) / 100 || used < S32(1052))
                    c4 = (int8_t)(c4 - 1);
                else
                    goto clamp5;
                S8(5) = (uint8_t)c4;
            } else {
                S8(4) = 0;
                c4 = q == 0 ? (int8_t)(SS8(81) + 2) : (int8_t)(c4 + 2);
                S8(5) = (uint8_t)c4;
            }
clamp5:
            if (c4 < AS8(64))
                S8(5) = A8(64);
            else if (AS8(63) < c4)
                S8(5) = A8(63);
done5:
            b = S32(1044);
        }
        S32(2208) = b;
        S32(2212) = b;
        S32(184) = b / A32(16);
        S32(52) = 0;
    } else {
        S32(52) = 0;
    }
    S32(1048) = 0;
    S32(956) = 0;
    S32(960) = 0;
    S32(944) = 0;
    S32(952) = 0;
    S32(940) = 0;
    S32(948) = 0;
}

/* h264_api_enc (0x127db4): the slice-parameter block of the picture. */
static void e30_api_enc(Eprc30 *rc)
{
    uint8_t *e = rc->e;
    uint8_t *p = rc->slice;
    uint32_t w = EU16(e, 0), h = EU16(e, 2);
    uint8_t mbw = (uint8_t)EU16(e, 4), mbh = (uint8_t)EU16(e, 6);
    uint8_t qp = EU8(e, 552);

    p[0] = (uint8_t)EU32(e, 544);
    p[1] = mbw;
    p[2] = mbh;
    p[3] = 0;
    EU32(p, 8) = w;
    EU32(p, 12) = h;
    p[4] = (uint8_t)(mbh - 1);
    p[0xd8] = qp;
    p[0x184] = qp;
    p[0x185] = (uint8_t)(qp + 13) > 51 ? 51 : (uint8_t)(qp + 13);
    p[0x186] = (uint8_t)(qp - 12) > 51 ? 51 : (uint8_t)(qp - 12);
    p[0x10] = p[0x11] = p[0x12] = p[0x13] = 0;
    p[0x15] = p[0x16] = p[0x17] = 1;
    EU32(p, 0x1c) = 500;
    EU32(p, 0x20) = 500;
    p[0x24] = 8;
    if (w < 0x791u && h < 0x451u) {
        p[0x14] = 3;
        EU32(p, 0x18) = 0x3f;
    } else {
        p[0x14] = 2;
        EU32(p, 0x18) = 0xf;
    }
    p[0x25] = 0;
    if (mbw >= 0x33 && p[2] > 0x26) {
        p[0x26] = 3;
        p[0x27] = 3;
        EU32(p, 0x28) = 8;
        EU32(p, 0x2c) = 8;
    } else {
        p[0x26] = 0;
        p[0x27] = 0;
        EU32(p, 0x28) = 0;
        EU32(p, 0x2c) = 0;
    }
    memset(p + 0x30, 0x10, 0x40);
    EU32(p, 0x70) = 0x15;
    EU32(p, 0x74) = 0xb;
    EU32(p, 0x78) = 0x15;
    EU32(p, 0x7c) = 0xb;
    p[0xcc] = 1;
    p[0xcd] = p[0xce] = p[0xcf] = 0;
    EU32(p, 0x80) = 0;
    EU32(p, 0x84) = 0;
    p[0x88] = p[0x89] = p[0x8a] = p[0x8b] = 0;
    p[0x91] = 0;
    p[0x92] = 1;
    p[0x93] = 0;
    p[0x94] = 0x33;
    p[0x95] = p[0x96] = p[0x97] = 1;
    p[0x98] = p[0x99] = p[0x9a] = p[0x9b] = p[0x9c] = p[0x9d] = 0;
    p[0xa0] = 0xcc;
    p[0xa1] = 0;
    EU32(p, 0xac) = 0;
    EU32(p, 0xb0) = 0;
    p[0xc4] = 0;
    EU32(p, 0xc8) = 0x30774533;
    p[0xdb] = 1;
    p[0xdc] = 1;
    p[0xd9] = 1;
    p[0xda] = 0;
    if (mbw < 0x33 || p[2] < 0x27 || EU32(e, 548) < 2u) {
        p[0x8c] = p[0x8d] = p[0x8e] = p[0x8f] = p[0x90] = 0;
        p[0xa2] = p[0xa3] = 0;
        p[0x9e] = p[0x9f] = 0;
    } else {
        p[0x8c] = 1;
        p[0x8d] = 9;
        p[0x8e] = 8;
        p[0x8f] = 7;
        p[0x90] = 6;
        p[0xa2] = 1;
        p[0xa3] = 3;
        p[0x9e] = 1;
        p[0x9f] = 9;
    }
    p[0xa4] = 1;
    p[0xa5] = 0;
    p[0xa6] = 0;
    p[0xa7] = 1;
    p[0xa8] = 1;
    p[0xb4] = 2;
    p[0xb5] = 4;
    p[0x1ec] = 0;
    p[0x1ed] = 1;
    p[0x1ee] = 4;
    p[0x1ef] = 8;
    p[0x1f0] = 6;
    EU16(p, 0xb6) = 0x2a;
    EU16(p, 0xb8) = 0x200;
    EU32(p, 0xbc) = 0x300;
    EU32(p, 0xc0) = 0x400;
}

/* h264_get_mb_qp (0x12b570), the part that the picture-level decisions
 * depend on: the mode word A+152 (0x230 for P, 0x30 for IDR pictures, 0 for
 * FIXQP) selects the per-macroblock QP map (bits 0..3, 0 here: cleared), the
 * SAS offsets (bits 4..7) and the basic-unit targets (bits 8..11), whose
 * setup forces a zero MAD (S+892) to 2.  The slice-block fields of the
 * macroblock rate control are not produced yet (the T30 command list does
 * not program them). */
static void e30_mb_qp(Eprc30 *rc)
{
    uint8_t *A = rc->blk;
    uint8_t *S = rc->blk + 0x120;
    uint32_t mw = AU32(152);

    rc->slice[499] = 1;
    if ((mw & 0xfu) == 0) {
        S8(1308) = 0;
        S32(1312) = 0;
        memset(S + 2020, 0, 112);
    }
    if (S32(0) == 0)
        return;
    if (((mw >> 8) & 0xfu) - 1u < 3u && S32(892) == 0)
        S32(892) = 2;
}

/* The RD-model picture QP of FRAME_START (0x134d18). */
static int32_t e30_rd_qp(uint8_t *S, int32_t tb)
{
    float a = (float)(double)SU32(892);
    float pm = a * SF(2768) + SF(2772);
    float x1 = SF(2504), x2 = SF(2508);
    float pmx1 = pm * x1, q;
    double l;

    if ((double)x2 < 1e-7) {
        q = (float)((double)pmx1 / (double)tb);
    } else {
        float disc = x2 * 4.0f * pm * (float)tb + pm * pmx1 * x1;

        if ((double)disc < 1e-7) {
            q = (float)((double)pmx1 / (double)tb);
        } else {
            double sq = sqrt((double)disc);

            if (sq - (double)pmx1 < 1e-7)
                q = (float)((double)pmx1 / (double)S32(188));
            else
                q = (float)((double)((x2 + x2) * pm) / (sq - (double)pmx1));
        }
    }
    l = log((double)q / 0.85);
    return e30_trunc_w((double)(float)e30_trunc_w((double)(float)(l * 6.0 / 0.6931471805599453 + 12.0) + 0.5));
}

/* JZ_VPU_RC_FRAME_START_T30 (0x1342d8), H.264. */
static void e30_frame_start(Eprc30 *rc)
{
    uint8_t *e = rc->e;
    uint8_t *A = rc->blk;
    uint8_t *S = rc->blk + 0x120;
    int8_t idr = EI8(e, 168);
    int32_t type, pos;
    int8_t q, lo, hi;

    S32(48) = EI32(e, 164);
    S8(36) = (uint8_t)idr;
    S8(17) = EU8(e, 496);
    if (idr == 0) {
        if ((uint32_t)A32(160) - 3u < 4u && S32(56) % A32(16) == 0)
            S32(40) = 6;
        else
            S32(40) = 1;
        if (EU32(e, 8) != 0)
            A32(152) = 0x230;
        S32(188) = 0;
        type = S32(40);
        if (type == 6) {
            if (A32(156) == 0) {
                S8(80) = A8(62);
                goto store_pos;
            }
            e30_gop_init(A, S);
            if (A32(180) == 1) {
                q = (int8_t)(S32(968) - AS8(90));
            } else if (A32(180) == 2) {
                int32_t v = A32(16) * A32(12) / A32(8);

                if (v > 2)
                    v = 2;
                q = (int8_t)(S32(968) - 1 - v);
                if (A8(228) != 0 && (uint32_t)A32(156) - 2u < 2u)
                    q = (int8_t)(SS8(5) - 1 - v);
            } else {
                q = SS8(80);
            }
            q = (int8_t)(q + AS8(88));
            S8(80) = (uint8_t)q;
            goto post;
        }
        if (A32(156) == 0) {
            S8(80) = A8(62);
            goto store_pos;
        }
        pos = S32(52);
        if (pos == 1) {                              /* first P of the GOP */
            uint32_t big = ((AU32(152) & 0xfffu) >> 8) >= 2u;

            S8(16) = (uint8_t)big;
            if (S32(0) == 1 || S32(968) == 0) {
                q = (int8_t)(SS8(81) + AS8(89));
            } else if (A8(228) != 0 && (uint32_t)A32(156) - 2u < 2u) {
                q = SS8(5);
            } else if (A8(228) == 0 && A32(184) == 2) {
                q = (int8_t)S32(968);
            } else {
                q = (int8_t)((int8_t)e30_tab474[big * 8 + S32(884)] + SS8(81));
                q = (int8_t)(q - AS8(88));
            }
            S8(80) = (uint8_t)q;
            S32(188) = S32(2212) / S32(72);
            goto post_p;
        }
        if (A8(228) != 0 && (uint32_t)A32(156) - 2u < 2u && A8(231) == 1) {
            q = SS8(5);
            S8(80) = (uint8_t)q;
            goto post_p;
        }
        {
            int32_t rem = S32(2216), n = S32(72), avg, i10, i15, t;

            S32(2216) = rem - S32(2224);
            if (n < 1) {
                S32(72) = 1;
                n = 1;
            }
            if (A8(207) == 0)
                avg = S32(2212) / n;
            else
                avg = ((S32(2212) / n) * 90 + S32(220) * 10) / 100;
            i10 = S32(2220);
            i15 = e30_tab494[S32(884)];
            if (avg < 0)
                avg = 0;
            t = S32(184) - ((i10 - (rem - S32(2224))) * i15) / 100;
            if (t < 0) {
                int32_t d = i10 / n;

                S32(2224) = d;
                S32(2216) = i10 - d;
                t = S32(184) - (d * i15) / 100;
            }
            if (t < 0)
                t = 0;
            t = (t + avg) >> 1;
            S32(188) = t;
            if (A8(208) != 0) {
                if (t < S32(204)) {
                    t = S32(204);
                    S32(188) = t;
                } else if (S32(200) < t) {
                    t = S32(200);
                    S32(188) = t;
                }
            }
            if (t < 0) {
                S32(188) = 0;
                q = (int8_t)(SS8(81) + AS8(95));
            } else if (t == 0) {
                q = (int8_t)(SS8(81) + AS8(95));
            } else {
                q = (int8_t)e30_rd_qp(S, t);
            }
            S8(80) = (uint8_t)q;
            goto post;
        }
    }

    /* IDR (0x134458) */
    S32(40) = 2;
    if (EU32(e, 8) != 0)
        A32(152) = 0x30;
    S32(188) = 0;
    if (A32(156) == 0) {
        S8(80) = A8(61);
        goto store_pos;
    }
    S32(56) = 0;
    e30_gop_init(A, S);
    if (S32(0) == 0) {
        q = AS8(60);
        if (q < 1)
            q = (int8_t)(e30_update_qp(26, 1000, A32(68)) +
                         e30_update_qp(26, 25, A32(8) / A32(12)) + 26 +
                         e30_update_qp(26, 0xe1000, A32(52)));
        S8(80) = (uint8_t)q;
        if (A8(228) != 0 && (uint32_t)A32(156) - 2u < 2u) {
            S8(5) = (uint8_t)q;
            goto post;
        }
        goto post;
    }
    if (S32(44) != 2) {
        int8_t c99;

        if (A32(180) == 1) {
            S8(80) = (uint8_t)(S32(968) - AS8(89));
        } else if (A32(180) == 2) {
            int32_t v = A32(16) * A32(12) / A32(8), v6 = v < 3 ? v : 2;

            q = (int8_t)(S32(968) - 1 - v6);
            S8(80) = (uint8_t)q;
            if (SS8(64) > 20) {
                int32_t d = (int32_t)SS8(82) - (int32_t)AS8(88);

                if ((uint32_t)(q - d + 2) < 5u)
                    S8(80) = (uint8_t)d;
            }
            if (A8(228) != 0 && (uint32_t)A32(156) - 2u < 2u)
                S8(80) = (uint8_t)(SS8(5) - 1 - v6);
        }
        c99 = AS8(153);
        if (c99 != 0) {
            int32_t t;

            if (A32(96) < S32(868))
                S8(80) = (uint8_t)(SS8(80) + 1);
            t = e30_trunc_w(floor((double)((float)S32(184) * SF(880))));
            S32(188) = t;
            if (t < S32(876))
                S32(188) = S32(876);
            else if (S32(872) < t)
                S32(188) = S32(872);
        }
        S8(80) = (uint8_t)(SS8(80) + AS8(88));
        goto post;
    }
    S8(80) = (uint8_t)(SS8(82) + AS8(88));
    goto post;

post:
    if (S32(40) == 2) {                              /* 0x1348d0 */
        q = SS8(80);
        if (q < AS8(65)) {
            q = AS8(65);
            S8(80) = (uint8_t)q;
        }
        goto minmax;
    }
post_p:
    q = SS8(80);
    {
        int32_t pt = S32(44);

        if (pt == 2) {
            lo = (int8_t)(SS8(81) - AS8(93));
            hi = (int8_t)(SS8(81) + AS8(93));
        } else if (pt == 6) {
            lo = (int8_t)(SS8(81) - AS8(94));
            hi = (int8_t)(SS8(81) + AS8(94));
        } else if (pt == 1) {
            lo = (int8_t)(SS8(81) - AS8(95));
            hi = (int8_t)(SS8(81) + AS8(95));
        } else {
            lo = 0;
            hi = 0;
        }
    }
    if (A8(228) == 0 || (uint32_t)A32(156) - 2u >= 2u || AS8(233) < 1 ||
        A32(244) <= S32(1048)) {
        if (q < lo) {
            q = lo;
            S8(80) = (uint8_t)q;
        } else if (hi < q) {
            q = hi;
            S8(80) = (uint8_t)q;
        }
    } else {
        int8_t c = (int8_t)(AS8(233) + SS8(82));

        if (c < q) {
            q = c;
            S8(80) = (uint8_t)q;
        }
    }
minmax:
    if (q < AS8(64))
        S8(80) = A8(64);
    else if (AS8(63) < q)
        S8(80) = A8(63);
store_pos:
    EI32(e, 540) = S32(52);
    EU8(e, 552) = S8(80);
    type = S32(40);
    EI32(e, 544) = type == 6 ? 1 : type;
    EI32(e, 548) = S32(884);
    EI32(e, 556) = S32(188);
    e30_api_enc(rc);
    e30_mb_qp(rc);
}

int EPRC30_FrameStart(Eprc30 *rc, int idr, EprcPicture *pic)
{
    EU32(rc->e, 164) = 0;
    EU8(rc->e, 168) = idr ? 1 : 0;
    e30_frame_start(rc);
    memset(pic, 0, sizeof(*pic));
    pic->type = EI32(rc->e, 544) == 2 ? 2 : 0;
    pic->qp = EU8(rc->e, 552);
    return 0;
}

/* -------------------------------------------------------- after coding */

static const int32_t e30_tab398[108] = {              /* -0x398: [t9 0..2][t1 0..5][level 0..5] */
    0, 0, 0, 0, 0, 0, -2, -2, -2, -2, -3, -3, -1, -1, -2, -2, -3, -3,
    3, 2, 2, 2, 1, 1, 3, 2, 2, 2, 1, 1, 4, 4, 3, 3, 2, 2,
    -1, -1, -1, -1, -1, -1, -2, -2, -2, -2, -3, -3, -1, -1, -2, -2, -3, -3,
    0, 0, 0, 0, 0, 0, 3, 2, 2, 2, 1, 1, 4, 4, 3, 3, 2, 2,
    -1, -1, -1, -1, -1, -1, -2, -2, -2, -2, -3, -3, -1, -1, -2, -2, -3, -3,
    0, 0, 0, 0, 0, 0, 3, 2, 2, 2, 1, 1, 4, 4, 3, 3, 2, 2 };
static const int32_t e30_tab1e8[10] = { 1000, 200, 100, 80, 60, 40, 30, 20, 10, 5 };
static const float e30_motion_lvls[4] = { 0.8f, 5.0f, 32.0f, 80.0f }; /* 0x1a0000 - 0x7668 */

static uint32_t e30_f2u(double v)
{
    return v < 2147483648.0 ? (uint32_t)e30_trunc_w(v)
                            : (uint32_t)e30_trunc_w(v - 2147483648.0) | 0x80000000u;
}

static float e30_u2f(uint32_t v)
{
    return (float)(double)v;
}

/* scene_judge_enc_frame (0x12853c): the picture class S+884. */
static void e30_scene_judge(uint8_t *A, uint8_t *S)
{
    float den, motion, cplx;
    int32_t lvl, cls;
    int8_t lim, c7;
    uint8_t gop = (uint8_t)A32(16);

    if (S32(40) == 2) {
        S32(884) = S32(888);
        return;
    }
    if (S32(1332) == 0) {
        S32(1332) = 1;
        den = 1.0f;
    } else {
        den = e30_u2f(SU32(1332));
    }
    motion = (float)S32(1336) / den;
    cplx = e30_u2f(SU32(1324)) / den;
    S8(920) = motion > 60.0f;
    if ((double)cplx > 0.98) {
        S32(884) = 6;
        S8(64) = 0;
        S8(65) = 0;
        return;
    }
    lvl = motion < e30_motion_lvls[0] ? 0 : motion < e30_motion_lvls[1] ? 1 :
          motion < e30_motion_lvls[2] ? 2 : motion < e30_motion_lvls[3] ? 3 : 4;
    lim = (int8_t)gop > 24 ? ((int8_t)gop > 100 ? 100 : (int8_t)gop) : 25;
    if (lvl >= 2) {
        if (lvl == 4 && (int8_t)gop <= 24)
            lim = 25;
        cls = lvl == 2 ? 4 : 5;
        if (SS8(64) == lim)
            cls = 3;
        S32(884) = cls;
        S8(64) = 0;
        S8(65) = (uint8_t)(S8(65) + 1);
    } else {
        int8_t b = SS8(65);

        S8(65) = 0;
        c7 = (int8_t)(S8(64) + 1);
        S32(884) = b == lim ? 2 : (lvl == 1 ? 0 : 1);
        S8(64) = (uint8_t)c7;
        if (c7 < 0)
            S8(64) = 0;
        else if (lim < c7)
            S8(64) = (uint8_t)lim;
    }
    c7 = SS8(65);
    if (c7 < 0)
        S8(65) = 0;
    else if (lim < c7)
        S8(65) = (uint8_t)lim;
}

/* rd_model_estimator (0x1270f0): the quadratic R-Q model X1 (S+2504) and
 * X2 (S+2508) over the unrejected window entries (JM RCModelEstimator). */
static void e30_rd_model(uint8_t *S)
{
    int32_t n = S8(2764), real = n, last = 0, i;
    int estx2 = 0;
    double a00 = 0, a01 = 0, a11 = 0, b0 = 0, b1 = 0, mv;

    SF(2504) = 0.0f;
    SF(2508) = 0.0f;
    if (n == 0)
        return;
    for (i = 0; i < n; i++) {
        if (S32(2512 + 4 * i) == 0)
            last = e30_trunc_w((double)SF(2680 + 4 * i));
        else
            real--;
    }
    for (i = 0; i < n; i++) {
        if (S32(2512 + 4 * i) != 0)
            continue;
        if (SF(2680 + 4 * i) != (float)last)
            estx2 = 1;
        SF(2504) = SF(2504) + (SF(2680 + 4 * i) * SF(2596 + 4 * i)) / (float)real;
    }
    if (!estx2)
        return;
    for (i = 0; i < n; i++) {
        float q = SF(2680 + 4 * i);

        if (S32(2512 + 4 * i) != 0)
            continue;
        a00 = a00 + 1.0;
        b1 = b1 + (double)SF(2596 + 4 * i);
        b0 = b0 + (double)(q * SF(2596 + 4 * i));
        a01 = a01 + 1.0 / (double)q;
        a11 = a11 + 1.0 / (double)(q * q);
    }
    mv = a00 * a11 - a01 * a01;
    if (fabs(mv) <= 1e-6) {
        SF(2504) = (float)(b0 / a00);
        return;
    }
    SF(2504) = (float)((a11 * b0 - a01 * b1) / mv);
    SF(2508) = (float)((a00 * b1 - a01 * b0) / mv);
}

/* mad_model_estimator (0x1277b0): MAD prediction c1 (S+2768), c2 (S+2772)
 * (JM MADModelEstimator). */
static void e30_mad_model(uint8_t *S)
{
    int32_t n = S8(3028), real = n, last = 0, i;
    int est = 0;
    float a00 = 0, a01 = 0, a11 = 0, b0 = 0, b1 = 0;
    double mv;

    SF(2768) = 0.0f;
    SF(2772) = 0.0f;
    if (n == 0)
        return;
    for (i = 0; i < n; i++) {
        if (S32(2776 + 4 * i) == 0)
            last = e30_trunc_w((double)SF(2860 + 4 * i));
        else
            real--;
    }
    for (i = 0; i < n; i++) {
        if (S32(2776 + 4 * i) != 0)
            continue;
        if (SF(2860 + 4 * i) != (float)last)
            est = 1;
        SF(2768) = SF(2768) + SF(2860 + 4 * i) / ((float)real * SF(2944 + 4 * i));
    }
    if (!est)
        return;
    for (i = 0; i < n; i++) {
        float x = SF(2944 + 4 * i), y = SF(2860 + 4 * i);

        if (S32(2776 + 4 * i) != 0)
            continue;
        a00 = a00 + 1.0f;
        a01 = a01 + x;
        b0 = b0 + y;
        a11 = a11 + x * x;
        b1 = b1 + x * y;
    }
    mv = (double)(a00 * a11 - a01 * a01);
    if (fabs(mv) <= 1e-6) {
        SF(2768) = b0 / a01;
        return;
    }
    SF(2772) = (float)((double)(a11 * b0 - a01 * b1) / mv);
    SF(2768) = (float)((double)(a00 * b1 - a01 * b0) / mv);
}

static uint8_t e30_window_count(uint8_t c)
{
    c = (uint8_t)(c + 1);
    return c == 0 ? 1 : c > 10 ? 10 : c;
}

/* The RD and MAD model updates of FRAME_END (0x1361f0), P pictures. */
static void e30_model_update(uint8_t *S)
{
    float err[10], sum = 0.0f, sd;
    int32_t n, i, m;

    for (i = 9; i > 0; i--) {
        SF(2596 + 4 * i) = SF(2592 + 4 * i);
        SF(2680 + 4 * i) = SF(2676 + 4 * i);
    }
    SF(2680) = (float)(pow(2.0, ((double)SS8(80) - 12.0) / 6.0) * 0.85);
    SF(2596) = (float)S32(196) / e30_u2f(SU32(892));
    S8(2764) = e30_window_count(S8(2764));
    memset(S + 2512, 0, 84);
    e30_rd_model(S);
    n = S8(2764);
    if (n != 0) {
        for (i = 0; i < n; i++) {
            float q = SF(2680 + 4 * i);

            err[i] = (SF(2508) / (q * q) + SF(2504) / q) - SF(2596 + 4 * i);
            sum = sum + err[i] * err[i];
        }
        sd = n < 3 ? 0.0f : (float)sqrt((double)(sum / (float)n));
        for (i = 0; i < n; i++)
            if (sd < fabsf(err[i]))
                S32(2512 + 4 * i) = 1;
    }
    S32(2512) = 0;
    e30_rd_model(S);

    for (i = 9; i > 0; i--) {
        SF(2860 + 4 * i) = SF(2856 + 4 * i);
        SF(2944 + 4 * i) = SF(2940 + 4 * i);
    }
    SF(2860) = e30_u2f(SU32(892));
    SF(2944) = SF(2864);
    S8(3028) = e30_window_count(S8(3028));
    memset(S + 2776, 0, 84);
    e30_mad_model(S);
    n = S8(3028);
    if (n != 0) {
        sum = 0.0f;
        m = n;          /* T30X stock build (uclibc 4.7.2); the 5.4.0 build stops at 9 */
        for (i = 0; i < m; i++) {
            err[i] = (SF(2768) * SF(2944 + 4 * i) + SF(2772)) - SF(2860 + 4 * i);
            sum = sum + err[i] * err[i];
        }
        sd = n < 3 ? 0.0f : (float)sqrt((double)(sum / (float)n));
        for (i = 0; i < m; i++)
            if (sd < fabsf(err[i]))
                S32(2776 + 4 * i) = 1;
    }
    S32(2776) = 0;
    e30_mad_model(S);
}

/* The smart-P QP S+5 of VBR/SMART (FRAME_END 0x135cc0). */
static void e30_smartp(uint8_t *A, uint8_t *S, int32_t bits, int32_t pos, int32_t s1048)
{
    int8_t c2, c7;
    int32_t rem, lvl, t9 = 0, t1, used;

    if (A32(72) / A32(8) < bits)
        c7 = (int8_t)(S8(7) + 1);
    else
        c7 = 0;
    S8(7) = (uint8_t)c7;
    c2 = SS8(5);
    rem = pos % A32(272);
    S8(9) = rem == 0;
    lvl = c2 < 30 ? 0 : c2 < 33 ? 1 : c2 < 36 ? 2 : c2 < 39 ? 3 : c2 < 42 ? 4 : 5;
    used = S32(72) * S32(220) + s1048;
    if (SS8(65) >= 21)
        t9 = pos < A32(16) / 2 ? 2 : 1;
    if (used < A32(248)) {
        t1 = 1;
    } else if (used < S32(1056)) {
        t1 = 2;
    } else if (used < S32(1052) || used < S32(1052) * A32(252) / 100) {
        S8(4) = 1;
        if (c7 == 6) {
            if (t9 == 0)
                c2 = (int8_t)(c2 + 1);
            else
                c2 = (int8_t)(e30_tab398[t9 * 36 + lvl] + c2);
        } else if (SS8(64) == 6) {
            if (t9 != 0)
                c2 = (int8_t)(e30_tab398[t9 * 36 + lvl] + c2);
            else
                c2 = (int8_t)(c2 - 1);
        } else if (rem == 0) {
            c2 = (int8_t)(e30_tab398[t9 * 36 + lvl] + c2);
        }
        goto clamp;
    } else if (used < S32(1052) * A32(256) / 100) {
        t1 = 3;
    } else {
        t1 = used < A32(244) ? 4 : 5;
    }
    S8(4) = 1;
    if (c7 == 6 || SS8(64) == 6 || rem == 0)
        c2 = (int8_t)(e30_tab398[t1 * 6 + t9 * 36 + lvl] + c2);
clamp:
    if (c2 < AS8(64))
        c2 = AS8(64);
    else if (AS8(63) < c2)
        c2 = AS8(63);
    S8(5) = (uint8_t)c2;
}

/* JZ_VPU_RC_FRAME_END_T30 (0x135014), H.264. */
static void e30_frame_end(Eprc30 *rc)
{
    uint8_t *e = rc->e;
    uint8_t *A = rc->blk;
    uint8_t *S = rc->blk + 0x120;
    int32_t bits = EI32(e, 172), type, mode, pos, gop;
    int8_t q;

    S8(80) = EU8(e, 552);
    S32(196) = bits;
    /* 0x135af8: the statistics */
    S32(892) = EI32(e, 176);
    S32(1324) = A32(56) - EI32(e, 208) - EI32(e, 204);
    S32(1332) = A32(56);
    S32(1336) = EI32(e, 196);
    memcpy(S + 1352, e + 212, 48);
    memcpy(S + 1848, e + 340, 32);
    memcpy(S + 1968, e + 460, 12);

    type = S32(40);
    pos = S32(52) + 1;
    S32(52) = pos;
    S32(56) = S32(56) + 1;
    S32(60) = S32(60) + 1;
    if (type == 1)
        S32(72) = S32(72) - 1;
    S32(0) = S32(0) + 1 == -1 ? 15 : S32(0) + 1;
    {
        int32_t old1048 = S32(1048);

        S32(1048) = bits + old1048;
        S32(1064) = S32(1064) + bits;
    }
    mode = A32(156);
    if (mode == 0)
        goto tail;
    q = SS8(80);
    if (A32(188) == 1) {
        S32(956) = S32(956) + q;
        if (type == 1)
            S32(960) = S32(960) + q;
    } else if (A32(188) == 2) {
        SF(940) = (float)((double)SF(940) * 0.95) + (float)q;
        SF(948) = (float)((double)SF(948) * 0.95) + 1.0f;
        if (type == 1) {
            SF(944) = (float)((double)SF(944) * 0.95) + (float)q;
            SF(952) = (float)((double)SF(952) * 0.95) + 1.0f;
        }
    }
    S8(81) = (uint8_t)q;
    S32(888) = S32(884);
    S32(44) = type;
    gop = A32(16);
    if (type == 2) {                                 /* 0x135f38 */
        int32_t n = S32(932);

        S8(82) = (uint8_t)q;
        S32(208) = bits;
        S32(860 - 4 * n) = bits;        /* n <= 60: S+620..S+860, see gop_init */
        S32(192) = 0;
        if (A8(228) != 0 && (uint32_t)mode - 2u < 2u)
            S8(7) = 0;
        if (mode == 1) {
            if (n == 0) {
                S32(2212) = A32(84) - S32(936) * bits;
                S32(2220) = (bits - S32(184)) * S32(936);
            } else {
                S32(2220) = (bits - S32(184)) * S32(936) + (S32(12) - n * S32(1044));
                S32(2212) = (A32(84) - S32(12)) - S32(936) * bits;
            }
            goto average;
        }
        if ((uint32_t)S32(884) >= 4u && S32(1044) < S32(1052)) {
            int32_t b = S32(1052);

            S32(1044) = b;
            S32(2208) = b;
            S32(2212) = b - S32(1048);
            S32(184) = b / gop;
        }
        S32(2220) = S32(2220) + (bits - S32(184));
        S32(2212) = S32(2212) - bits;
        goto average;
    }
    /* 0x135130 */
    S8(83) = (uint8_t)q;
    S32(212) = bits;
    S32(192) = S32(188);
    if (gop < 101) {
        int32_t i;

        for (i = gop - 1; i >= 1; i--) {
            S8(84 + i) = S8(83 + i);
            S32(224 + 4 * i) = S32(220 + 4 * i);
        }
        S8(84) = (uint8_t)q;
        S32(224) = bits;
    }
    if (A8(228) != 0 && (uint32_t)mode - 2u < 2u)
        e30_smartp(A, S, bits, pos, S32(1048));
    if (mode == 1) {                                 /* 0x136108 */
        if (type == 1) {
            S32(2220) = S32(2220) + (bits - S32(184));
            S32(2212) = S32(2212) - bits;
            if (S32(60) == 2) {
                int32_t n = S32(72);

                S32(2216) = S32(2220);
                if (n <= 0) {
                    S32(72) = 1;
                    n = 1;
                }
                S32(2224) = S32(2220) / n;
            }
        }
        goto average;
    }
    if ((uint32_t)S32(884) >= 4u && S32(1044) < S32(1052)) {   /* 0x135268 */
        int32_t b = S32(1052);

        S32(1044) = b;
        S32(2208) = b;
        S32(2212) = b - S32(1048);
        S32(184) = b / gop;
    }
    if (type == 6) {                                 /* 0x136154 */
        int32_t m = bits < S32(208) ? S32(208) : bits;

        S32(2220) = S32(2220) + (m - S32(184));
        S32(2212) = S32(2212) - m;
    } else if (type == 1) {                          /* 0x136188 */
        S32(2220) = S32(2220) + (bits - S32(184));
        S32(2212) = S32(2212) - bits;
        if (S32(52) == 2) {
            int32_t n = S32(72);

            S32(2216) = S32(2220);
            if (n <= 0) {
                S32(72) = 1;
                n = 1;
            }
            S32(2224) = S32(2220) / n;
        }
    }

average:                                             /* 0x1352cc */
    if (A8(208) == 1 || A8(207) == 1 || (A8(228) != 0 && A8(231) != 0)) {
        int32_t n = AS8(206), i;
        float sum = 0.0f, w = 0.0f;

        if (n <= 0 || S32(224) == 0) {
            S32(220) = 0;
            if (A8(208) == 1) {
                S32(200) = 0;
                S32(204) = 0;
            }
        } else {
            for (i = 0; i < n; i++) {
                int32_t b = S32(224 + 4 * i);

                if (b == 0)
                    break;
                if (i == 0) {
                    sum = (float)b + 0.0f;
                    w = 1.0f;
                } else {
                    sum = (float)b + (float)((double)sum * 0.95);
                    w = (float)((double)w * 0.95) + 1.0f;
                }
            }
            if (w >= 1.0f)
                sum = sum / w;
            SU32(220) = e30_f2u(floor((double)sum + 0.5));
            if (A8(208) == 1) {
                int32_t lvl = e30_tab1e8[AS8(170)], hi, lo;

                if (!(0.0f < w)) {
                    S32(204) = 0;
                    S32(200) = 0;
                    lo = 0;
                } else {
                    SU32(220) = sum < 2147483648.0f ? (uint32_t)e30_trunc_w((double)sum)
                                : (uint32_t)e30_trunc_w((double)(sum - 2147483648.0f)) | 0x80000000u;
                    hi = ((lvl + 100) * S32(220)) / 100;
                    lo = ((100 - lvl) * S32(220)) / 100;
                    S32(200) = hi;
                    S32(204) = lo;
                    if (hi < 0)
                        S32(200) = 0;
                }
                if (lo < 0)
                    S32(204) = 0;
                else if (S32(200) < lo)
                    S32(204) = S32(200);
            }
        }
    }
    if (type != 1)
        goto tail;
    if (A8(200) != 0 && A32(196) == 0)
        e30_scene_judge(A, S);
    if (S32(40) != 1)
        goto tail;
    if (S32(52) == 2 && A8(202) == 0)
        goto tail;
    if (A32(192) == 0)
        e30_model_update(S);
tail:
    EU8(e, 580) = S8(920);
}

/* JZ_VPU_RC_FRAME_REPEATE_JUDGE_T30 (0x13749c): 1 when the picture is
 * larger than the super-frame size and is to be coded again at a higher
 * QP (update_qp, at most while S+76 < A+112). */
static int e30_repeat_judge(Eprc30 *rc)
{
    uint8_t *e = rc->e;
    uint8_t *A = rc->blk;
    uint8_t *S = rc->blk + 0x120;
    uint32_t lim = S32(40) == 2 ? EU32(e, 100) : EU32(e, 104);
    uint32_t bits = EU32(e, 172);

    if (S32(52) == 1 && S32(932) == 0)
        return 0;
    if (lim < bits && S32(76) < A32(112)) {
        uint32_t t = lim * 95u / 100u;
        int32_t d = e30_update_qp(EU8(e, 552), (int32_t)bits, (int32_t)t);
        uint32_t v = (d != 0 ? (uint32_t)d & 0xffu : 1u) + EU8(e, 552);

        EU8(e, 552) = (v & 0xffu) > 51u ? 51 : (uint8_t)v;
        SU32(188) = t;
        S32(76) = S32(76) + 1;
        e30_api_enc(rc);
        e30_mb_qp(rc);
        return 1;
    }
    S32(76) = 0;
    return 0;
}

int EPRC30_FrameEnd(Eprc30 *rc, uint32_t bytes,
                    const uint32_t regs[EPRC30_STAT_REGS], uint32_t cmpx,
                    EprcPicture *pic, int may_repeat)
{
    uint8_t *e = rc->e;
    int i;

    /* i264e_ratecontrol_end / _is_reenc (0x55994 / 0x55b68) */
    EU32(e, 172) = bytes << 3;
    if (may_repeat && e30_repeat_judge(rc)) {
        pic->qp = EU8(e, 552);
        return 1;
    }
    EU32(e, 176) = cmpx;
    EU32(e, 196) = (regs[1] & 0x7ffffffu) + (regs[2] & 0x7ffffffu);
    EU32(e, 204) = (regs[0] >> 16) & 0x7fffu;
    EU32(e, 208) = regs[0] & 0x7fffu;
    for (i = 0; i < 8; i++)
        EU32(e, 212 + 4 * i) = regs[13 + i];
    for (i = 0; i < 4; i++)
        EU32(e, 244 + 4 * i) = regs[21 + i];
    for (i = 0; i < 4; i++) {
        EU32(e, 340 + 8 * i) = regs[6 + i] & 0xffffu;
        EU32(e, 344 + 8 * i) = regs[6 + i] >> 16;
    }
    EU32(e, 460) = regs[10] & 0x1ffffu;
    EU32(e, 464) = regs[11] & 0x1ffffu;
    EU32(e, 468) = regs[12] & 0x1ffffu;
    e30_frame_end(rc);
    return 0;
}
