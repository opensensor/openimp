/* Random channels for the t30_hevc_channel_params tests (hevc_cfg_diff
 * against the vendor, radix_hevc_test on the host). */
#ifndef HEVC_CHAN_CASE_H
#define HEVC_CHAN_CASE_H

#include <stdint.h>

#include "t30/radix/t30_radix_hevc.h"

static uint32_t chan_rnd_state = 0x208u;

static uint32_t chan_rnd(uint32_t n)
{
    chan_rnd_state = chan_rnd_state * 1103515245u + 12345u;
    return (chan_rnd_state >> 8) % n;
}

/* FNV-1a over the RBSP bytes, the pending bit count and byte (as
 * hevc_hash in hevc_case.h) */
static uint32_t chan_hash(uint32_t h, const uint8_t *buf, uint32_t len, uint32_t bits, uint8_t part)
{
    for (uint32_t i = 0; i < len; i++)
        h = (h ^ buf[i]) * 16777619u;
    h = (h ^ bits) * 16777619u;
    return (h ^ part) * 16777619u;
}

static void hevc_chan_case(int c, T30HevcChannel *ch)
{
    static const uint32_t sizes[][2] = {
        {1920, 1080}, {2048, 1536}, {1280, 720}, {640, 360}, {640, 352}, {2304, 1296},
        {2560, 1440}, {352, 288}, {176, 144}, {4096, 2304}, {3840, 2160}, {800, 600},
    };
    static const uint32_t fps[][2] = {
        {25, 1}, {15, 1}, {30, 1}, {30000, 1001}, {50, 2}, {0, 0}, {5, 1}, {60, 1}, {12, 1},
    };
    uint32_t k = chan_rnd(3);

    if (k == 0) {
        ch->width = sizes[chan_rnd(12)][0];
        ch->height = sizes[c % 12][1];
    } else if (k == 1) {
        uint32_t s = chan_rnd(12);

        ch->width = sizes[s][0];
        ch->height = sizes[s][1];
    } else {
        ch->width = 64 + chan_rnd(4200);
        ch->height = 8 + chan_rnd(2400);
    }
    k = chan_rnd(9);
    ch->fps_num = fps[k][0];
    ch->fps_den = fps[k][1];
    if (chan_rnd(4) == 0) {
        ch->fps_num = 1 + chan_rnd(120);
        ch->fps_den = 1 + chan_rnd(4);
    }
    ch->rc_mode = chan_rnd(4);
    ch->bitrate = chan_rnd(2) ? 64 + chan_rnd(8000) : 64 + chan_rnd(300000);
}

#endif
