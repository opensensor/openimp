/*
 * Macroblock-level rate control of the Helix picture rate controller
 * ("eprc", T21/T23): the OEM h264_get_mb_qp (T23 1.3.0 0xc1cbc, T21 1.0.33
 * 0x93908), which h264_api_enc runs for every picture, and the encoding of
 * its slice fields into Helix registers by H264E_T21_SliceInit.  See
 * docs/T23_EPRC.md "Macroblock rate control".
 *
 * The controller's mode words (A+208 for IDR, A+212 for P pictures, from
 * E+168/E+172) select three parts: bits 0..3 a per-macroblock QP map, bits
 * 4..7 the SAS ("smooth area") QP offsets, bits 8..11 a basic-unit mode.
 * The OEM fills them from its rate-control file defaults (0x30: no QP map,
 * SAS mode 3, no basic units); no IMP call changes them (IMP_Encoder_SetMbRC
 * only stores i264e param[280], which nothing reads).  Only that
 * configuration is ported; any other mode word leaves state and slice
 * untouched.
 *
 * SAS mode 3: the Helix counts the macroblocks of the previous picture in
 * seven activity classes (statistics registers 0x40094..0x400a0, eprc
 * S+4488..S+4503 - T21 S+4448..); the shares of classes 0-2 (x), 3-4 (y)
 * and 5-6 (z) pick one row of seven QP offsets (h264_sasm_ofst) that the
 * hardware adds per activity class.  The OEM classifies (y, z) through a
 * nine-entry table it indexes with up to 15 (an out-of-bounds stack read
 * whenever y or z is above the band's limits); the bytes behind the table
 * are what the emulator and a cleared stack hold: zero.
 *
 * State is in the OEM layout; on T21 every S offset from 280 on is 40
 * lower (d = -40).
 */
#include "eprc.h"
#include "eprc_internal.h"

/* h264_sasm_ofst: [band 0..3][q 0..1][class 0..3][7] */
static const int8_t sasm_ofst[32][7] = {
    {-5, -3, -2, 0, 0, 1, 2}, {-5, -3, -2, 0, 0, 1, 2},
    {-5, -3, -1, 0, 1, 1, 2}, {-4, -3, -1, 0, 0, 1, 2},
    {-5, -3, -2, 0, 0, 1, 2}, {-5, -2, -1, 0, 0, 1, 2},
    {-5, -2, -1, 0, 1, 2, 2}, {-5, -2, -1, 0, 0, 1, 2},
    {-6, -4, -2, 0, 1, 2, 3}, {-6, -4, -2, 0, 1, 2, 3},
    {-6, -4, -2, 0, 1, 2, 3}, {-6, -4, -2, 0, 1, 2, 3},
    {-6, -5, -2, 0, 1, 2, 3}, {-6, -4, -1, 0, 1, 2, 3},
    {-6, -4, -1, 0, 1, 2, 3}, {-6, -4, -1, 0, 1, 2, 3},
    {-7, -5, -3, 0, 0, 3, 4}, {-7, -5, -3, 0, 1, 2, 3},
    {-7, -5, -3, 0, 2, 3, 4}, {-7, -5, -3, 0, 2, 3, 3},
    {-7, -4, -3, 0, 1, 3, 3}, {-7, -4, -2, 0, 1, 3, 3},
    {-7, -4, -2, 0, 1, 3, 4}, {-7, -4, -2, 0, 1, 3, 4},
    {-8, -5, -3, 0, 2, 3, 4}, {-8, -5, -3, 0, 2, 3, 4},
    {-8, -5, -3, 0, 2, 3, 4}, {-8, -5, -3, 0, 2, 3, 4},
    {-8, -5, -2, 0, 2, 4, 5}, {-8, -5, -2, 0, 2, 4, 5},
    {-8, -5, -2, 1, 2, 4, 5}, {-8, -5, -2, 1, 2, 3, 5},
};

/* sas_mb_thd_init (activity class limits, 0x4007c..0x40088),
 * sas_flt_thd_init (0x40078), rc_bu_alg0_qpo, rc_bu_alg1_qpo, rc_mb_cs_qpo,
 * rc_mb_top_bs_qpo / rc_mb_rinfo_qpo (the same two bytes) */
static const uint8_t sas_mb_thd[14] = {
    0x96, 0x00, 0x2c, 0x01, 0x90, 0x01, 0xe8, 0x03, 0xd0, 0x07, 0xa0, 0x0f,
    0x00, 0x00
};
static const uint8_t sas_flt_thd[5] = {2, 3, 4, 4, 4};
static const uint8_t bu_alg0_qpo[6] = {1, 2, 3, 4, 5, 6};
static const uint8_t bu_alg1_qpo[6] = {1, 1, 1, 1, 1, 1};
static const uint8_t mb_cs_qpo[7] = {0xfd, 0xfe, 0xff, 0, 1, 2, 3};
static const uint8_t mb_top_bs_qpo[2] = {0xff, 1};

/* The class of a band flag set (0xee040, 9 bytes) and what follows it on
 * a cleared stack. */
static const uint8_t flag_class[16] = {0, 0, 1, 0, 2, 0, 0, 0, 3};

/* Picture bands of x (OEM floats 12, 22, 52, 82, 92) and the (y, z)
 * limits of each band (40/80, 35/70, 20/40, 7/16). */
static const float band_lo[4] = {12.0f, 22.0f, 52.0f, 82.0f};
static const float band_hi[4] = {22.0f, 52.0f, 82.0f, 92.0f};
static const float flat_lo[4] = {40.0f, 35.0f, 20.0f, 7.0f};
static const float flat_hi[4] = {80.0f, 70.0f, 40.0f, 16.0f};

static float eprc_share(uint32_t count, int32_t mbs)
{
    float v = (float)(int32_t)count * 100.0f;

    return v / (float)mbs;
}

/* h264_get_mb_qp 0xc2a74..0xc2db8: the SAS offsets of the picture. */
static void eprc_sas_offsets(const uint8_t *A, uint8_t *S, int d)
{
    uint32_t r0 = EU32(S, 4488 + d), r1 = EU32(S, 4492 + d);
    uint32_t r2 = EU32(S, 4496 + d);
    uint32_t r3 = EU16(S, 4500 + d);
    int32_t mbs = EI32(A, 52);
    float x = eprc_share((r0 & 0xffffu) + (r0 >> 16) + (r1 & 0xffffu), mbs);
    float y = eprc_share((r1 >> 16) + (r2 & 0xffffu), mbs);
    float z = eprc_share(r3 + (r2 >> 16), mbs);
    int32_t qp = EI8(S, 68);
    unsigned int band, bands = 0, flags = 0, q, row, i;
    int off;

    /* no offsets below 12 % or from 92 % flat macroblocks, nor below QP 20
     * (written so that a NaN share takes the OEM branches) */
    off = x < 12.0f ? 1 : (92.0f <= x ? 1 : qp < 20);
    for (band = 0; band < 4; band++)
        if (band_lo[band] <= x && x < band_hi[band])
            break;
    if (band < 4) {
        bands = 1u << band;
        /* the OEM sets "neither y nor z high" (8) always, and the bits
         * below for y or z above the band's limits */
        flags = 8u;
        if (!((y <= flat_lo[band] && z <= flat_hi[band]) ||
              (y <= flat_hi[band] && z <= flat_lo[band])))
            flags |= 1u;
    }
    q = (uint8_t)(qp - 20) < (band == 3 ? 13u : 16u);
    row = (flag_class[flags] & 3u) | (q << 2) |
          ((flag_class[bands] & 3u) << 3);
    for (i = 0; i < 7; i++)
        EI8(S, 5336 + d + (int)i) = off ? 0 : sasm_ofst[row][i];
}

void EPRC_MbQp(uint8_t *A, uint8_t *S, uint8_t *SL, int t21)
{
    int d = t21 ? -40 : 0;
    uint32_t mode = EI32(S, 28) == 2 ? EU32(A, 208) : EU32(A, 212);
    uint32_t mbw, mbh16, mbw16, mbh8;
    int i;

    if ((mode & 0xfu) != 0u || ((mode >> 4) & 0xfu) != 3u ||
        ((mode >> 8) & 0xfu) != 0u)
        return;
    /* QP map mode 0 (0xc2728): no map */
    EU8(S, 368 + d) = 0;
    EU32(S, 372 + d) = 0;
    memset(S + 5184 + d, 0, 56);
    /* SAS mode 3 */
    eprc_sas_offsets(A, S, d);
    if (!SL)
        return;
    /* 0xc211c */
    EU8(SL, 812) = 0;
    EU32(SL, 816) = EU32(S, 372 + d);
    EPTR(SL, 820, S + 376 + d);
    memcpy(SL + 752, S + 5184 + d, 56);
    memset(SL + 842, 0, 8);
    if (EU32(S, 0) == 0)            /* the first picture: nothing more */
        return;
    EU8(SL, 825) = 0;
    EU8(SL, 824) = 1;
    EU8(SL, 826) = 1;
    memcpy(SL + 842, S + 5336 + d, 7);
    memcpy(SL + 828, sas_mb_thd, sizeof(sas_mb_thd));
    memcpy(SL + 850, sas_flt_thd, sizeof(sas_flt_thd));
    /* basic-unit mode 0 (0xc3284) */
    mbw = EU32(A, 40);
    mbh16 = EU16(A, 44);
    mbw16 = mbw & 0xffffu;
    mbh8 = EU8(A, 44);
    EU8(SL, 855) = 0;
    EU8(SL, 856) = 0;
    EU8(SL, 857) = 0;
    EU8(SL, 858) = 0;
    EU8(SL, 859) = (uint8_t)mbh8;
    EU16(SL, 860) = (uint16_t)mbw16;
    EU8(SL, 862) = 1;
    EU8(SL, 863) = 0;
    EU32(SL, 864) = 0;
    EU32(SL, 868) = 0;
    EU32(SL, 872) = EU32(S, 88);
    EU32(SL, 876) = 0;
    memset(SL + 880, 0, 12);
    memset(SL + 916, 0, 4);
    EU16(SL, 920) = (uint16_t)(mbh16 * (uint32_t)(((int32_t)mbw + 1) / 2));
    EU16(SL, 922) = (uint16_t)(mbh16 * mbw16 - mbw16 * (mbh8 - 1u));
    EU8(SL, 924) = 0;
    memcpy(SL + 892, bu_alg0_qpo, 6);
    memcpy(SL + 898, bu_alg1_qpo, 6);
    memcpy(SL + 904, mb_cs_qpo, 7);
    for (i = 0; i < 2; i++) {
        EU8(SL, 911 + i) = mb_top_bs_qpo[i];
        EU8(SL, 913 + i) = mb_top_bs_qpo[i];
    }
}

/* H264E_T21_SliceInit (T23 0x25b9c, T21 0x1ce20), same on both: the
 * macroblock rate-control slice fields as register values. */
void EPRC_MbRcRegs(const uint8_t *SL, EprcMbRc *out)
{
    int i;

    memset(out, 0, sizeof(*out));
    if (!SL)
        return;
    out->qp_flags = (EU8(SL, 824) & 1u) | (EU8(SL, 825) & 1u) << 1 |
                    (EU8(SL, 826) & 3u) << 2 | (EU8(SL, 862) & 3u) << 4 |
                    (EU8(SL, 857) & 1u) << 7 | (EU8(SL, 863) & 3u) << 14 |
                    (EU8(SL, 812) & 3u) << 22 | (EU8(SL, 855) & 1u) << 23 |
                    (uint32_t)(EU8(SL, 858) & 3u) << 30;
    for (i = 0; i < 5; i++)
        out->reg[0] |= (uint32_t)(EU8(SL, 850 + i) & 7u) << (4 * i);
    for (i = 0; i < 3; i++)
        out->reg[1 + i] = (uint32_t)EU16(SL, 828 + 4 * i) |
                          (uint32_t)EU16(SL, 830 + 4 * i) << 16;
    out->reg[4] = EU16(SL, 840);
    for (i = 0; i < 4; i++) {
        out->reg[5] |= (uint32_t)(EU8(SL, 842 + i) & 0x3fu) << (8 * i);
        out->reg[6] |= (uint32_t)(EU8(SL, 846 + i) & 0x3fu) << (8 * i);
    }
}

/* h264_api_enc (T23 0xc43a0..0xc48d0, T21 0x95f90..0x962ec): the slice
 * fields of the picture control registers 0x400c0/0x400c4 (by picture type
 * from the configuration A+311..A+327; T23: small pictures and the region
 * input).  Not part of the macroblock rate control. */
void EPRC_PictureCtrl(const uint8_t *A, const uint8_t *S, const uint8_t *E,
                      uint8_t *SL, int t21)
{
    int32_t type = EI32(S, 28), n = EI32(S, 44);
    int32_t width = EI32(A, 32);
    int small;

    if (!SL)
        return;
    EU8(SL, 976) = 0;
    EU8(SL, 984) = EU8(A, 317);
    EU8(SL, 985) = EU8(A, 322);
    EU8(SL, 986) = EU8(A, 313) && type == 0 && n != 0 && n == n / 5 * 5 &&
                   EU32(S, 148) >= 4u;
    EU8(SL, 987) = EU8(A, 314);
    EU8(SL, 988) = EU8(A, 315);
    EU8(SL, 989) = EU8(A, 316);
    EU8(SL, 990) = 1;
    EU8(SL, 991) = 0;
    EU8(SL, 992) = 0;
    EU8(SL, 993) = EU8(A, type == 2 ? 318 : 319);
    EU8(SL, 994) = EU8(A, 320);
    EU8(SL, 995) = EU8(A, 321);
    EU8(SL, 996) = EU8(A, type == 2 ? 323 : 324);
    /* T23: below 256 pixels width (or with E+1572 < 0, not a multiple of
     * 128) the fixed set; T21 refuses such sizes */
    small = !t21 && ((EI32(E, 1572) < 0 && (width & 0x7f) != 0) ||
                     width < 256);
    EU8(SL, 997) = small ? 0 : EU8(A, 325);
    EU8(SL, 998) = 10;
    EU8(SL, 999) = 33;
    EU8(SL, 1000) = 33;
    EU8(SL, 1001) = !small;
    EU8(SL, 1002) = !small;
    EU8(SL, 1048) = small ? 1 : EU8(A, 311);
    EU8(SL, 1049) = small ? 0 : EU8(A, 312);
    EU8(SL, 1050) = (uint8_t)t21;
    EU8(SL, 1051) = (uint8_t)t21;
    EU8(SL, 1052) = EU8(A, 326) && type != 2;
    /* T23 after h264_get_mb_qp: the region input (E+1800/E+1804) or a
     * region count (S+264) */
    if (!t21 && (EI32(E, 1800) == 1 || EI32(E, 1804) == 1 || EI32(S, 264) != 0))
        EU8(SL, 976) = 1;
}

/* H264E_T21_SliceInit (T23 and T21 alike): 0x400c0 and 0x400c4. */
void EPRC_PictureCtrlRegs(const uint8_t *SL, uint32_t out[2])
{
    out[0] = out[1] = 0;
    if (!SL)
        return;
    out[0] = (EU8(SL, 985) & 1u) | (EU8(SL, 986) & 1u) << 1 |
             (EU8(SL, 987) & 1u) << 2 | (EU8(SL, 988) & 7u) << 3 |
             (EU8(SL, 1048) & 1u) << 6 | (EU8(SL, 1049) & 1u) << 7 |
             (EU8(SL, 1050) & 1u) << 8 | (EU8(SL, 1051) & 1u) << 9 |
             (EU8(SL, 990) & 1u) << 10 | (EU8(SL, 991) & 1u) << 11 |
             (EU8(SL, 992) & 3u) << 12 | (EU8(SL, 976) & 1u) << 14 |
             (EU8(SL, 1052) & 1u) << 15 | (uint32_t)EU8(SL, 989) << 16 |
             (uint32_t)EU8(SL, 993) << 24;
    out[1] = (EU8(SL, 994) & 63u) | (EU8(SL, 995) & 1u) << 6 |
             (EU8(SL, 996) & 63u) << 8 | (EU8(SL, 997) & 1u) << 14 |
             (EU8(SL, 999) & 63u) << 16 | (EU8(SL, 1001) & 1u) << 22 |
             (uint32_t)(EU8(SL, 1000) & 63u) << 24 |
             (uint32_t)(EU8(SL, 1002) & 1u) << 30;
}
