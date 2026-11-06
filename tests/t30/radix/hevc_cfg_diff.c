/*
 * Differential test of t30_hevc_channel_params against the vendor libimp
 * 1.0.5 under qemu-mipsel:
 *   hevc_cfg_diff <vendor libimp.so> [cases]
 * For random channels (size, frame rate, RC mode, bit rate) the vendor
 * chain of IMP_Encoder_CreateChn / I265e::I265e (i265e_param_default, the
 * channel fields, configure, enforceLevel, determineLevel, initVPS/SPS/
 * PPS) runs on a zeroed I265e; its VPS, SPS and PPS go through
 * Entropy::codeVPS/codeSPS/codePPS as in I265e::getStreamHeaders, ours
 * through t30_hevc_write_*.  The RBSP bits must be identical.
 * Desk tool, not part of libimp (docs/T30_HEVC.md).
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "t30/radix/t30_radix_hevc.h"
#include "hevc_chan_case.h"

static void *H;

static void *sym(const char *name)
{
    void *p = dlsym(H, name);

    if (!p) {
        fprintf(stderr, "missing %s\n", name);
        exit(2);
    }
    return p;
}

static void w32(uint8_t *b, unsigned o, uint32_t v) { memcpy(b + o, &v, 4); }
static uint32_t r32(const uint8_t *b, unsigned o) { uint32_t v; memcpy(&v, b + o, 4); return v; }

typedef void (*fn1)(void *);
typedef int (*fn2)(void *, void *);
typedef void (*fn3)(void *, void *, void *);
typedef void (*sps_fn)(void *, const void *, const void *, const void *);
typedef void (*pps_fn)(void *, const void *, int, int);

static uint8_t bs_mem[256], ent_mem[8192];

static void vendor_begin(void)
{
    memset(bs_mem, 0, sizeof(bs_mem));
    memset(ent_mem, 0, sizeof(ent_mem));
    ((fn1)sym("_ZN8I265E_NS9BitstreamC1Ev"))(bs_mem);
    ((fn1)sym("_ZN8I265E_NS7EntropyC1Ev"))(ent_mem);
    memcpy(ent_mem, &(void *){ bs_mem }, sizeof(void *));
}

static int same_bits(const T30HevcBits *b)
{
    uint8_t *fifo;
    uint32_t len, bits;

    memcpy(&fifo, bs_mem + 4, 4);
    len = r32(bs_mem, 0xc);
    bits = r32(bs_mem, 0x10);
    if (len != b->len || bits != b->bits || b->overflow)
        return 0;
    if (len && memcmp(fifo, b->buf, len))
        return 0;
    return !bits || bs_mem[0x14] == (uint8_t)(b->cur << (8 - bits));
}

static uint32_t vendor_hash(uint32_t h)
{
    uint8_t *fifo;

    memcpy(&fifo, bs_mem + 4, 4);
    return chan_hash(h, fifo, r32(bs_mem, 0xc), r32(bs_mem, 0x10), bs_mem[0x14]);
}

static uint32_t gcd(uint32_t a, uint32_t b)
{
    while (b) {
        uint32_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

/* IMP_Encoder_CreateChn (H.265 branch) + the I265e constructor up to
 * initPPS; 0 when enforceLevel or the scaling list fail */
static int vendor_channel(uint8_t *obj, const T30HevcChannel *c)
{
    uint32_t num = c->fps_num, den = c->fps_den;

    memset(obj, 0, 0x2100);
    ((fn1)sym("i265e_param_default"))(obj);
    if (num && den) {
        uint32_t g = gcd(num, den);
        num /= g;
        den /= g;
    }
    if (!num || !den) {
        num = 25;
        den = 1;
    }
    w32(obj, 0x00, 3);
    obj[4] = 1;
    w32(obj, 0x08, 1);
    w32(obj, 0x0c, 1);
    w32(obj, 0x14, num);
    w32(obj, 0x18, den);
    w32(obj, 0x1c, 50);
    w32(obj, 0x24, 4);
    w32(obj, 0x28, c->width);
    w32(obj, 0x2c, c->height);
    w32(obj, 0x30, c->width);
    w32(obj, 0x34, c->height);
    w32(obj, 0x48, c->rc_mode);
    if (c->rc_mode == 0) {
        w32(obj, 0x4c, 35);
    } else {
        w32(obj, 0x9c, 45);
        w32(obj, 0xa0, 15);
        w32(obj, 0xa8, 2);
        w32(obj, 0x50, c->bitrate);
        w32(obj, 0x70, 3);
        w32(obj, 0x74, 15);
        if (c->rc_mode >= 2) {
            w32(obj, 0xb4, 80);
            w32(obj, 0xb8, 2);
        }
    }
    w32(obj, 0x148, 1);
    w32(obj, 0x150, c->width * c->height * 6 - 0x8400);
    ((fn1)sym("_ZN8I265E_NS16I265eScalingListC1Ev"))(obj + 0x308);
    ((fn1)sym("_ZN8I265E_NS13I265eRatectrlC1Ev"))(obj + 0x1c58);
    ((fn1)sym("_ZN8I265E_NS5I265e9configureEv"))(obj);
    if (!((fn2)sym("_ZN8I265E_NS12enforceLevelER13i265e_param_tRNS_8I265eVPSE"))(obj, obj + 0x84c))
        return 0;
    ((fn2)sym("_ZN8I265E_NS14determineLevelERK13i265e_param_tRNS_8I265eVPSE"))(obj, obj + 0x84c);
    obj[0x304] = 0;
    if (!((fn2)sym("_ZN8I265E_NS16I265eScalingList4initEv"))(obj + 0x308, NULL))
        return 0;
    obj[0x848] = 0;
    ((fn1)sym("_ZN8I265E_NS5I265e7initVPSEv"))(obj);
    ((fn1)sym("_ZN8I265E_NS5I265e7initSPSEv"))(obj);
    ((fn1)sym("_ZN8I265E_NS5I265e7initPPSEv"))(obj);
    if (obj[0x1b7])
        ((fn3)sym("_ZN8I265E_NS13I265eRatectrl7initHRDER13i265e_param_tRNS_8I265eSPSE"))(obj + 0x1c58, obj, obj + 0x8c0);
    return 1;
}

int main(int argc, char **argv)
{
    static uint8_t obj[0x2100] __attribute__((aligned(16)));
    int cases = argc > 2 ? atoi(argv[2]) : 2000, bad[3] = {0, 0, 0}, skipped = 0, c;
    uint32_t hash = 2166136261u;

    H = dlopen(argv[1], RTLD_NOW);
    if (!H) {
        fprintf(stderr, "%s\n", dlerror());
        return 2;
    }
    for (c = 0; c < cases; c++) {
        static uint8_t out[1024];
        T30HevcChannel ch;
        T30HevcVps V;
        T30HevcSps S;
        T30HevcPps P;
        T30HevcBits b;

        hevc_chan_case(c, &ch);
        if (!vendor_channel(obj, &ch)) {
            skipped++;
            continue;
        }
        t30_hevc_channel_params(&ch, &V, &S, &P);

        if (!obj[0x8bc]) {
            printf("vendor VPS extension off (/tmp/closeextvpflag exists?)\n");
            return 2;
        }
        vendor_begin();
        srandom(0x208u + (unsigned)c);
        ((fn2)sym("_ZN8I265E_NS7Entropy7codeVPSERKNS_8I265eVPSE"))(ent_mem, obj + 0x84c);
        srandom(0x208u + (unsigned)c);
        {
            uint32_t r1 = (uint32_t)random();

            V.extension_bits = t30_hevc_vps_extension_bits(r1, (uint32_t)random());
        }
        t30_hevc_bits_init(&b, out, sizeof(out));
        t30_hevc_write_vps(&b, &V);
        hash = vendor_hash(hash);
        if (!same_bits(&b) && bad[0]++ < 5)
            printf("vps: %ux%u %u/%u rc %u br %u: vendor level %u tier %u, ours %u %u\n", ch.width, ch.height,
                   ch.fps_num, ch.fps_den, ch.rc_mode, ch.bitrate, r32(obj, 0x84c + 0x24), obj[0x84c + 0x38],
                   V.ptl.level_idc, V.ptl.tier_flag);

        vendor_begin();
        ((sps_fn)sym("_ZN8I265E_NS7Entropy7codeSPSERKNS_8I265eSPSERKNS_16I265eScalingListERKNS_21I265eProfileTierLevelE"))(
            ent_mem, obj + 0x8c0, obj + 0x308, obj + 0x86c);
        t30_hevc_bits_init(&b, out, sizeof(out));
        t30_hevc_write_sps(&b, &S);
        hash = vendor_hash(hash);
        if (!same_bits(&b) && bad[1]++ < 5)
            printf("sps: %ux%u %u/%u rc %u br %u: vendor %ux%u, ours %ux%u\n", ch.width, ch.height, ch.fps_num,
                   ch.fps_den, ch.rc_mode, ch.bitrate, r32(obj, 0x8c4), r32(obj, 0x8c8), S.width, S.height);

        vendor_begin();
        ((pps_fn)sym("_ZN8I265E_NS7Entropy7codePPSERKNS_8I265ePPSEbi"))(ent_mem, obj + 0x99c,
                                                                         r32(obj, 0x1b0) < 2, (int)r32(obj, 0x9dc));
        t30_hevc_bits_init(&b, out, sizeof(out));
        t30_hevc_write_pps(&b, &P);
        hash = vendor_hash(hash);
        if (!same_bits(&b) && bad[2]++ < 5)
            printf("pps: %ux%u differs\n", ch.width, ch.height);
    }
    printf("channel params: %d cases (%d refused by the vendor): vps %d, sps %d, pps %d differ; vendor hash 0x%08x\n",
           cases, skipped, bad[0], bad[1], bad[2], hash);
    return bad[0] + bad[1] + bad[2] ? 1 : 0;
}
