#ifndef OPENIMP_T40_VENDOR_CMD_H
#define OPENIMP_T40_VENDOR_CMD_H

/*
 * T40 command-list values recovered from the vendor libimp 1.3.1 (Eufy
 * T40XP, SC830AI, H.264 High; command lists captured at the AVPU push at
 * 3840x2160, 1920x1080, 1280x720 and 640x360, CBR/VBR/FixQP, 15/20 fps,
 * GOP 20..80).  Pure functions so tests/t40/vendor_cmd_test.c can check
 * them against the captured words.
 */
#include <stdint.h>

/* cmd[0x12] bits 27:24: vendor UpdateCommand stores SliceParam+0xae =
 * 512 / (64-pixel columns), at most 128; SliceParamToCmdRegsEnc1 packs
 * (value - 1) into four bits (3840 -> 7, 1920 -> 0, 1280 -> 8, 640 -> 2). */
static inline uint32_t t40_vendor_cmd12_hi(uint32_t width)
{
    uint32_t columns = (width + 63u) >> 6;
    uint32_t value = columns ? 512u / columns : 128u;

    if (value > 128u)
        value = 128u;
    return (value - 1u) & 0xfu;
}

/* Hardware RC targets in bits per HWRC group:
 *   cmd[0x15] = target bit/s * 1.9 * groups / LCUs for IDR, 5/7 of that
 *               for P (independent of frame rate and GOP);
 *   cmd[0x16] = maximum bit/s per frame * groups / LCUs (CBR: the target,
 *               VBR: uMaxBitRate). */
static inline void t40_vendor_hwrc_targets(uint64_t bitrate,
                                           uint64_t max_bitrate,
                                           uint32_t fps_num, uint32_t fps_den,
                                           uint32_t lcu_count, uint32_t groups,
                                           int is_idr, uint32_t *word15,
                                           uint32_t *word16)
{
    uint64_t lcus = lcu_count ? lcu_count : 1u;
    uint64_t num = fps_num ? fps_num : 25u;
    uint64_t den = fps_den ? fps_den : 1u;

    if (max_bitrate < bitrate)
        max_bitrate = bitrate;
    *word16 = (uint32_t)(max_bitrate * den * groups / (lcus * num));
    if (is_idr)
        *word15 = (uint32_t)(bitrate * groups * 19u / (lcus * 10u));
    else
        *word15 = (uint32_t)(bitrate * groups * 95u / (lcus * 70u));
}

/* iInitialQP -1: vendor AL_Common_Encoder_ComputeRCParam (0x302c0) takes
 * the first row of its table (0xd5bf4: AVC, +0x120: HEVC) whose threshold
 * reaches bit rate / frame rate * 1000 / pixels; QP = 40 - row.  Past the
 * last row the QP is the configured minimum. */
static inline int t40_vendor_initial_qp(uint32_t bitrate, uint32_t fps,
                                        uint64_t pixels, int hevc,
                                        int min_qp, int max_qp)
{
    static const uint16_t thresholds[2][29] = {
        { 33, 37, 41, 45, 51, 56, 62, 70, 76, 83, 94, 104, 114, 129, 140,
          156, 176, 194, 215, 241, 262, 289, 321, 349, 382, 423, 458, 504,
          551 },
        { 3, 8, 11, 14, 18, 23, 28, 32, 40, 48, 56, 64, 104, 114, 129, 140,
          156, 176, 194, 215, 241, 262, 289, 321, 349, 382, 423, 458, 504 },
    };
    uint64_t per_kpixel;
    int qp = min_qp;
    unsigned int i;

    if (!pixels || !fps)
        return 26;
    per_kpixel = (uint64_t)(bitrate / fps) * 1000u / pixels;
    for (i = 0; i < 29u; ++i) {
        if (thresholds[hevc ? 1 : 0][i] >= per_kpixel) {
            qp = 40 - (int)i;
            break;
        }
    }
    if (qp < min_qp)
        qp = min_qp;
    if (qp > max_qp)
        qp = max_qp;
    return qp;
}

/* cmd[0x17] bits 31:24, the hardware RC's lowest QP: the configured
 * minimum, but at most 32 below the maximum (vendor: 15..48 -> 16,
 * 20..44 -> 20). */
static inline uint32_t t40_vendor_hwrc_min_qp(uint32_t min_qp, uint32_t max_qp)
{
    if (max_qp > 32u && min_qp < max_qp - 32u)
        return max_qp - 32u;
    return min_qp;
}

/* imp_encoder.h: "GOPLength = uGopLength * uMaxSameSenceCnt"; the vendor
 * codes an IDR every uGopLength * uMaxSameSenceCnt pictures. */
static inline uint32_t t40_vendor_gop_length(uint32_t gop, uint32_t same_scene)
{
    if (same_scene > 1u)
        gop *= same_scene;
    return gop > 0xffffu ? 0xffffu : gop;
}

#endif
