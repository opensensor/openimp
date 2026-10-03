/* Host test of the T21/T23 Helix shared reference ring (OPENIMP_REF_SHARE,
 * OEM BUF_SHARE_CFG): ring layout and sequence, and the command list
 * registers with sharing on and off. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "t21/t21_h264_descriptor.h"
#include "t21/t21_ref_ring.h"

#define WORDS 8192u
static uint32_t descriptor[WORDS];
static uint8_t cabac_state[1024];

static int find(size_t pairs, uint32_t reg, unsigned int nth, uint32_t *v)
{
    size_t i;

    for (i = 0; i < pairs; i++)
        if ((descriptor[2 * i + 1] & 0xffffcu) == reg && nth-- == 0u) {
            *v = descriptor[2 * i];
            return 1;
        }
    return 0;
}

static uint32_t get(size_t pairs, uint32_t reg)
{
    uint32_t v = 0;

    assert(find(pairs, reg, 0, &v));
    return v;
}

static void test_layout(uint32_t mbw, uint32_t mbh)
{
    T21RefRing r;
    T21RefRingPos prev, cur;
    uint32_t stride = mbw * 16u;
    uint64_t n;

    t21_ref_ring_init(&r, 0x03000000u, mbw, mbh);
    assert(r.ring_y == (mbh * 16u + 256u) * stride);
    assert(r.ring_c == (mbh * 8u + 128u) * stride);
    assert(r.base_c == r.base_y + r.ring_y);
    assert(t21_ref_ring_bytes(mbw, mbh) == r.ring_y + r.ring_c);
    t21_ref_ring_pos(&r, 0, &prev);
    assert(prev.recon_y == r.base_y && prev.recon_c == r.base_c);
    assert(prev.ref_y == prev.recon_y && prev.ref_c == prev.recon_c);
    for (n = 1; n < 5000; n++) {
        uint32_t d;

        t21_ref_ring_pos(&r, n, &cur);
        /* predicts from the previous picture */
        assert(cur.ref_y == prev.recon_y && cur.ref_c == prev.recon_c);
        /* always inside the ring, 256 (128) lines below the reference
         * modulo the ring */
        assert(cur.recon_y >= r.base_y && cur.recon_y <= cur.end_y);
        assert(cur.recon_c >= r.base_c && cur.recon_c <= cur.end_c);
        d = (cur.ref_y - cur.recon_y + r.ring_y) % r.ring_y;
        assert(d == r.step_y % r.ring_y);
        d = (cur.ref_c - cur.recon_c + r.ring_c) % r.ring_c;
        assert(d == r.step_c % r.ring_c);
        /* recon + picture wraps at most once (picture < ring) */
        assert(cur.start_y == r.base_y && cur.end_y == r.base_y + r.ring_y);
        prev = cur;
    }
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

static void test_registers(int p)
{
    T21H264SliceConfig c;
    size_t off_pairs = 0, on_pairs = 0, i;
    static uint32_t off[WORDS];
    uint32_t t = 0;
    uint32_t base4 = 0;

    fill(&c, p, 1920, 1080);
    assert(T21_H264_BuildDescriptor(&c, &off_pairs) == 0);
    memcpy(off, descriptor, off_pairs * 2u * sizeof(uint32_t));
    /* off: raw addresses in the MC/LF source registers, no ring */
    if (p) {
        assert(get(off_pairs, 0x50110) == c.raw[0]);
        assert(get(off_pairs, 0x50114) == c.raw[1]);
    }
    assert(get(off_pairs, 0xb0030) == c.raw[0]);
    assert(get(off_pairs, 0x6001c) == 0 && get(off_pairs, 0x60020) == 0);
    base4 = get(off_pairs, 0x60004) & 0x40000000u;
    assert(base4 == 0);

    fill(&c, p, 1920, 1080);
    c.ref_share = 1;
    c.ring_start_y = 0x03a00000u;
    c.ring_start_c = 0x03a00000u + 0x27d800u;
    c.ring_end_y = c.ring_start_y + 0x27d800u;
    c.ring_end_c = c.ring_start_c + 0x13ec00u;
    assert(T21_H264_BuildDescriptor(&c, &on_pairs) == 0);
    /* same register sequence, only values differ */
    assert(on_pairs == off_pairs);
    for (i = 0; i < on_pairs; i++)
        assert(descriptor[2 * i + 1] == off[2 * i + 1]);
    assert(get(on_pairs, 0x60004) & 0x40000000u);
    assert(get(on_pairs, 0x60008) == c.output_y);
    assert(get(on_pairs, 0x6000c) == c.output_c);
    assert(get(on_pairs, 0x60014) == c.ring_start_y);
    assert(get(on_pairs, 0x60018) == c.ring_start_c);
    assert(get(on_pairs, 0x6001c) == c.ring_end_y);
    assert(get(on_pairs, 0x60020) == c.ring_end_c);
    assert(get(on_pairs, 0xb0030) == c.ring_start_y);
    assert(get(on_pairs, 0xb0034) == c.ring_start_c);
    assert(get(on_pairs, 0xb0014) == c.reference_y);
    assert(get(on_pairs, 0xb0018) == c.reference_c);
    assert(get(on_pairs, 0xb0008) == c.raw[0]);
    if (p) {
        assert(get(on_pairs, 0x5006c) == c.reference_y);
        assert(get(on_pairs, 0x50070) == c.reference_c);
        assert(get(on_pairs, 0x50110) == c.ring_start_y);
        assert(get(on_pairs, 0x50114) == c.ring_start_c);
        assert(get(on_pairs, 0x10014) == c.reference_y);
    }
    /* everything else unchanged */
    for (i = 0; i < on_pairs; i++) {
        uint32_t reg = descriptor[2 * i + 1] & 0xffffcu;

        if (descriptor[2 * i] != off[2 * i]) {
            assert(reg == 0x60004 || reg == 0x60014 || reg == 0x60018 ||
                   reg == 0x6001c || reg == 0x60020 || reg == 0xb0030 ||
                   reg == 0xb0034 || reg == 0x50110 || reg == 0x50114);
        }
    }
    (void)t;
}

int main(void)
{
    /* 1080p: 3.7 MiB instead of 2 x 3.0 MiB */
    assert(t21_ref_ring_bytes(120, 68) == 3870720u);
    assert(t21_ref_ring_bytes(120, 68) < 2u * (1920u * 1088u * 3u / 2u));
    /* the sizes the device uses: 1080p and 360p both share (ring 3.69 MiB
     * against 6.0 MiB, 585 KiB against 690 KiB), tiny pictures do not */
    assert(t21_ref_ring_saves(120, 68));
    assert(t21_ref_pair_bytes(120, 68) == 6266880u);
    assert(t21_ref_ring_saves(40, 23));
    assert(t21_ref_ring_bytes(40, 23) == 599040u);
    assert(t21_ref_pair_bytes(40, 23) == 706560u);
    assert(t21_ref_ring_saves(80, 45));
    assert(!t21_ref_ring_saves(20, 12));     /* 320x180: H <= 256 */
    assert(!t21_ref_ring_saves(1, 1));
    assert(!t21_ref_ring_saves(40, 16));     /* 640x256: ring == pair */
    test_layout(120, 68);
    test_layout(80, 45);
    test_layout(40, 23);
    test_layout(1, 1);
    test_registers(0);
    test_registers(1);
    puts("ref_ring_test: ok");
    return 0;
}
