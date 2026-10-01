#ifndef OPENIMP_T30_H264_LEVEL_H
#define OPENIMP_T30_H264_LEVEL_H

#include <stdint.h>

/*
 * Smallest H.264 level (Table A-1) that holds the native Helix stream:
 * frame size, macroblock rate, High-profile bitrate (cpbBrVclFactor 1250)
 * and the decoded picture buffer for the advertised reference frames.
 * Level 3.1 stays the floor so small substreams keep their old SPS.
 */
static inline uint32_t t30_h264_level(uint32_t width, uint32_t height,
                                      uint32_t fps_num, uint32_t fps_den,
                                      uint32_t bitrate,
                                      uint32_t reference_frames)
{
    static const struct {
        uint32_t level_idc;
        uint32_t max_mbps;
        uint32_t max_fs;
        uint32_t max_dpb_mbs;
        uint32_t max_br_kbps;
    } levels[] = {
        { 31u, 108000u, 3600u, 18000u, 14000u },
        { 32u, 216000u, 5120u, 20480u, 20000u },
        { 40u, 245760u, 8192u, 32768u, 20000u },
        { 41u, 245760u, 8192u, 32768u, 50000u },
        { 42u, 522240u, 8704u, 34816u, 50000u },
        { 50u, 589824u, 22080u, 110400u, 135000u },
        { 51u, 983040u, 36864u, 184320u, 240000u },
    };
    uint64_t frame_mbs = (((uint64_t)width + 15u) / 16u) *
                         (((uint64_t)height + 15u) / 16u);
    uint64_t mb_rate;
    unsigned int i;

    if (!fps_num || !fps_den) {
        fps_num = 25u;
        fps_den = 1u;
    }
    mb_rate = (frame_mbs * fps_num + fps_den - 1u) / fps_den;
    if (!reference_frames)
        reference_frames = 1u;
    for (i = 0; i < sizeof(levels) / sizeof(levels[0]); i++) {
        if (frame_mbs <= levels[i].max_fs &&
            mb_rate <= levels[i].max_mbps &&
            frame_mbs * reference_frames <= levels[i].max_dpb_mbs &&
            (uint64_t)bitrate <= (uint64_t)levels[i].max_br_kbps * 1250u)
            return levels[i].level_idc;
    }
    return 51u;
}

#endif
