/* Host test of the T23 Helix H.264 command list (src/t21 builder built
 * with PLATFORM_T23).
 *
 * The register order of the OEM T23 1.3.0 slice builder was recorded by
 * running it under emulation (tools/helix_oem_descriptor_compare.py); only
 * the FNV-1a digest of that order and the count of writes are kept here.
 * The test checks that OpenIMP's list has exactly that order, that every
 * Helix-internal address points into the T23 Helix window (0x131xxxxx, not
 * the direct-connect block at 0x132xxxxx where T21/T30 have their Helix),
 * that frame addresses pass through unchanged, and that invalid inputs are
 * refused before anything is written. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "t21/t21_h264_descriptor.h"

#define WORDS 8192u
#define VALID 0x80000000u
#define TERM  0x40000000u

/* OEM T23 1.3.0 H264E_T21_SliceInit: number of writes and FNV-1a over
 * (offset | TERM) as little-endian words */
#define OEM_T23_I_WRITES 1015u
#define OEM_T23_I_DIGEST 0xe734d389u
#define OEM_T23_P_WRITES 1034u
#define OEM_T23_P_DIGEST 0x042e72a7u

static uint32_t descriptor[WORDS];
static uint8_t cabac_state[1024];

static uint32_t fnv1a(const uint32_t *list, size_t pairs)
{
    uint32_t hash = 0x811c9dc5u;
    size_t i;
    unsigned int b;

    for (i = 0; i < pairs; i++) {
        uint32_t command = list[2 * i + 1];
        uint32_t key = (command & 0xffffcu) | (command & TERM);

        for (b = 0; b < 4u; b++) {
            hash ^= (key >> (8u * b)) & 0xffu;
            hash *= 0x01000193u;
        }
    }
    return hash;
}

static void fill(T21H264SliceConfig *c, int p, unsigned int w,
                 unsigned int h)
{
    memset(c, 0, sizeof(*c));
    c->slice_type = (uint8_t)p;
    c->mb_width = (uint8_t)((w + 15u) / 16u);
    c->mb_height = (uint8_t)((h + 15u) / 16u);
    c->last_mby = (uint8_t)(c->mb_height - 1u);
    c->qp = 30;
    c->raw_format = 8;
    c->width = (uint16_t)w;
    c->height = (uint16_t)h;
    c->cabac_state = cabac_state;
    c->raw[0] = 0x03000000u;
    c->raw[1] = 0x03000000u + c->mb_width * 16u * c->mb_height * 16u;
    c->stride[0] = w;
    c->stride[1] = w;
    c->reference_y = 0x03400000u;
    c->reference_c = 0x03500000u;
    c->output_y = 0x03600000u;
    c->output_c = 0x03700000u;
    c->bitstream = 0x03800100u;
    c->scratch_base = 0x03900000u;
    c->descriptor = descriptor;
    c->descriptor_words = WORDS;
}

static uint32_t value_of(size_t pairs, uint32_t reg, unsigned int nth)
{
    size_t i;

    for (i = 0; i < pairs; i++) {
        if ((descriptor[2 * i + 1] & 0xffffcu) == reg && nth-- == 0u)
            return descriptor[2 * i];
    }
    fprintf(stderr, "register 0x%05x not written\n", reg);
    abort();
}

static int writes(size_t pairs, uint32_t reg)
{
    size_t i;
    int n = 0;

    for (i = 0; i < pairs; i++)
        n += (descriptor[2 * i + 1] & 0xffffcu) == reg;
    return n;
}

static void check_list(int p, unsigned int w, unsigned int h)
{
    /* registers that carry Helix-internal SRAM or scheduler addresses */
    static const uint32_t helix_address_registers[] = {
        0x4000c, 0x40018, 0x4001c, 0x40020, 0x40024, 0x40028, 0x4002c,
        0x40030, 0x8000c, 0x80028, 0x80058, 0x8005c, 0x9001c, 0x90020,
    };
    T21H264SliceConfig c;
    size_t pairs = 0;
    size_t i;

    fill(&c, p, w, h);
    memset(descriptor, 0xa5, sizeof(descriptor));
    assert(T21_H264_BuildDescriptor(&c, &pairs) == 0);
    assert(pairs == (p ? OEM_T23_P_WRITES : OEM_T23_I_WRITES));
    assert(fnv1a(descriptor, pairs) ==
           (p ? OEM_T23_P_DIGEST : OEM_T23_I_DIGEST));
    for (i = 0; i < pairs; i++) {
        uint32_t command = descriptor[2 * i + 1];

        assert(command & VALID);
        assert(!(command & TERM) == (i + 1u != pairs));
    }
    assert((descriptor[2 * (pairs - 1u) + 1u] & 0xffffcu) == 0x40000u);
    assert(writes(pairs, 0xc0000) == 0);   /* no TCSM flush on T23 */

    for (i = 0; i < sizeof(helix_address_registers) /
                    sizeof(helix_address_registers[0]); i++) {
        uint32_t v = value_of(pairs, helix_address_registers[i], 0);

        assert(v >= 0x13100000u && v < 0x13200000u);
    }
    if (p) {
        assert(value_of(pairs, 0x5010c, 0) == 0x131c5400u);
        assert(value_of(pairs, 0x50104, 0) == 0x131c4800u);
        assert(value_of(pairs, 0x50108, 0) == 0x13100070u);
        assert(value_of(pairs, 0x5006c, 0) == c.reference_y);
        assert(value_of(pairs, 0x50070, 0) == c.reference_c);
    } else {
        assert(writes(pairs, 0x5010c) == 0);
    }
    /* nothing may point into the direct-connect block or other 0x13xxxxxx
     * peripherals */
    for (i = 0; i < pairs; i++) {
        uint32_t reg = descriptor[2 * i + 1] & 0xffffcu;
        uint32_t v = descriptor[2 * i];

        if (reg >= 0x92000u && reg < 0x92000u + 460u * 4u)
            continue;           /* CABAC range tables, not addresses */
        if (reg == 0x80198u || reg == 0x8002cu || reg == 0x80174u ||
            (reg >= 0x80800u && reg < 0x80980u) ||
            (reg >= 0x40040u && reg < 0x40100u))
            continue;           /* tables and packed parameters */
        assert(!(v >= 0x13200000u && v < 0x13300000u));
    }

    /* T23-only writes */
    assert(value_of(pairs, 0x40170, 0) == 0x00000330u);
    assert(value_of(pairs, 0x40174, 0) == 0x80000330u);
    assert(value_of(pairs, 0x801e0, 0) == 0x00400000u);
    assert(value_of(pairs, 0x801e4, 0) == 0x00400000u);
    assert(value_of(pairs, 0x90028, 0) == 0x00400000u);
    assert(value_of(pairs, 0x60004, 0) ==
           (0x80000000u | (h << 14) | w));

    /* frame, reference and output addresses pass through */
    assert(value_of(pairs, 0x40010, 0) == c.raw[0]);
    assert(value_of(pairs, 0x40014, 0) == c.raw[1]);
    assert(value_of(pairs, 0x60008, 0) == c.output_y);
    assert(value_of(pairs, 0x6000c, 0) == c.output_c);
    assert(value_of(pairs, 0x90024, 0) == c.bitstream);
    assert(value_of(pairs, 0x30004, 0) == (c.bitstream & ~0x7fu));
    assert(value_of(pairs, 0x30018, 0) == c.scratch_base);
    assert(value_of(pairs, 0x30040, 0) == 0x400u);  /* 1 MiB window */
    assert(value_of(pairs, 0x90008, 0) ==
           (((uint32_t)c.mb_height << 24) | ((uint32_t)c.mb_width << 16)));
    assert((value_of(pairs, 0x90018, 0) & 0xffu) == (p ? 0x32u : 0x31u));
    assert(((value_of(pairs, 0x90018, 0) >> 8) & 0x3fu) == c.qp);
    printf("T23 Helix %s %ux%u: %zu writes match the OEM order\n",
           p ? "P" : "I", w, h, pairs);
}

static void check_refusals(void)
{
    T21H264SliceConfig c;
    size_t pairs = 7;

    fill(&c, 1, 640, 360);
    c.descriptor_words = 2067;      /* one short of a T23 P list */
    memset(descriptor, 0, sizeof(descriptor));
    errno = 0;
    assert(T21_H264_BuildDescriptor(&c, &pairs) == -1 && errno == EINVAL);
    assert(descriptor[0] == 0u && pairs == 7u);
    fill(&c, 1, 640, 360);
    c.reference_y = 0;
    assert(T21_H264_BuildDescriptor(&c, &pairs) == -1);
    fill(&c, 0, 640, 360);
    c.scratch_base = 0;
    assert(T21_H264_BuildDescriptor(&c, &pairs) == -1);
    fill(&c, 2, 640, 360);
    assert(T21_H264_BuildDescriptor(&c, &pairs) == -1);
    fill(&c, 0, 640, 360);
    c.descriptor_words = 2030;      /* exactly an I list */
    assert(T21_H264_BuildDescriptor(&c, &pairs) == 0 && pairs == 1015u);
}

int main(void)
{
    unsigned int i;

    for (i = 0; i < sizeof(cabac_state); i++)
        cabac_state[i] = (uint8_t)(1u + (i * 37u) % 126u);
    check_list(0, 1920, 1080);
    check_list(1, 1920, 1080);
    check_list(0, 640, 360);
    check_list(1, 640, 360);
    check_refusals();
    printf("T23 Helix descriptor test passed\n");
    return 0;
}
