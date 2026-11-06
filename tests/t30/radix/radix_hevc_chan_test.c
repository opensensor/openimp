/*
 * Host regression of t30_hevc_channel_params: the VPS, SPS and PPS bits for
 * 500 random channels (hevc_chan_case.h) hash to the value that
 * hevc_cfg_diff got from the vendor libimp 1.0.5 (IMP_Encoder_CreateChn /
 * I265e setup, Entropy::codeVPS/SPS/PPS) under qemu-mipsel for the same
 * channels.  The vendor's VPS extension bits come from random() after
 * srandom(0x208 + case), as in the desk tool.
 */
#include <stdio.h>
#include <stdlib.h>

#include "hevc_chan_case.h"

#define VENDOR_HASH 0xfb857310u

static uint32_t mine(uint32_t h, const T30HevcBits *b)
{
    return chan_hash(h, b->buf, (uint32_t)b->len, b->bits,
                     b->bits ? (uint8_t)(b->cur << (8 - b->bits)) : 0);
}

int main(void)
{
    static uint8_t buf[1024];
    uint32_t hash = 2166136261u;
    T30HevcChannel ch;
    T30HevcVps v;
    T30HevcSps s;
    T30HevcPps p;
    T30HevcBits b;

    for (int c = 0; c < 500; c++) {
        uint32_t r1;

        hevc_chan_case(c, &ch);
        t30_hevc_channel_params(&ch, &v, &s, &p);
        srandom(0x208u + (unsigned)c);
        r1 = (uint32_t)random();
        v.extension_bits = t30_hevc_vps_extension_bits(r1, (uint32_t)random());
        t30_hevc_bits_init(&b, buf, sizeof(buf));
        t30_hevc_write_vps(&b, &v);
        hash = mine(hash, &b);
        t30_hevc_bits_init(&b, buf, sizeof(buf));
        t30_hevc_write_sps(&b, &s);
        hash = mine(hash, &b);
        t30_hevc_bits_init(&b, buf, sizeof(buf));
        t30_hevc_write_pps(&b, &p);
        hash = mine(hash, &b);
    }
    printf("radix_hevc_chan_test: %s (hash 0x%08x, vendor 0x%08x)\n",
           hash == VENDOR_HASH ? "PASS" : "FAIL", hash, VENDOR_HASH);
    return hash != VENDOR_HASH;
}
