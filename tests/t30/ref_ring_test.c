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

/* Per-picture values of the stock T21 libimp 1.0.33 BUF_SHARE_CFG for
 * 1080p (120 x 68 macroblocks, ring at 0x03000000), worked out from the
 * disassembly: ring_y = (1088 + 256) * 1920 = 0x276000, ring_c =
 * (544 + 128) * 1920 = 0x13b000, step 0x78000 / 0x3c000, recon(n) = end -
 * (n * step mod ring) or the start, reference = recon(n - 1), byte output
 * (0xb0000 bits 8..15) = ((end_y - ref_y) / stride >> 4) - 1, 255 for the
 * first P after the IDR. */
static const struct {
    uint64_t n;
    uint32_t recon_y, recon_c, ref_y, ref_c;
    uint8_t wrap;
} oem_1080p[] = {
    { 0, 0x03000000u, 0x03276000u, 0x03000000u, 0x03276000u, 83 },
    { 1, 0x031fe000u, 0x03375000u, 0x03000000u, 0x03276000u, 255 },
    { 2, 0x03186000u, 0x03339000u, 0x031fe000u, 0x03375000u, 15 },
    { 3, 0x0310e000u, 0x032fd000u, 0x03186000u, 0x03339000u, 31 },
    { 4, 0x03096000u, 0x032c1000u, 0x0310e000u, 0x032fd000u, 47 },
    { 5, 0x0301e000u, 0x03285000u, 0x03096000u, 0x032c1000u, 63 },
    { 6, 0x0321c000u, 0x03384000u, 0x0301e000u, 0x03285000u, 79 },
    { 20, 0x03078000u, 0x032b2000u, 0x030f0000u, 0x032ee000u, 51 },
    { 21, 0x03000000u, 0x03276000u, 0x03078000u, 0x032b2000u, 67 },
    { 22, 0x031fe000u, 0x03375000u, 0x03000000u, 0x03276000u, 83 },
};

static void test_oem_values(void)
{
    T21RefRing r;
    T21RefRingPos p;
    size_t i;

    t21_ref_ring_init(&r, 0x03000000u, 120, 68);
    assert(r.ring_y == 0x276000u && r.ring_c == 0x13b000u);
    assert(r.step_y == 0x78000u && r.step_c == 0x3c000u);
    assert(r.stride == 1920u);
    for (i = 0; i < sizeof(oem_1080p) / sizeof(oem_1080p[0]); i++) {
        t21_ref_ring_pos(&r, oem_1080p[i].n, &p);
        assert(p.recon_y == oem_1080p[i].recon_y);
        assert(p.recon_c == oem_1080p[i].recon_c);
        assert(p.ref_y == oem_1080p[i].ref_y);
        assert(p.ref_c == oem_1080p[i].ref_c);
        assert(p.wrap_rows == oem_1080p[i].wrap);
        assert(p.start_y == 0x03000000u && p.end_y == 0x03276000u);
        assert(p.start_c == 0x03276000u && p.end_c == 0x033b1000u);
    }
    /* the wrap row is where the reference picture leaves the ring: the
     * reference rows after it continue at the ring start (84 rows ring,
     * 68 rows picture: wraps for 4 of every 5.25 pictures) */
    for (i = 2; i < 200; i++) {
        uint32_t rows;

        t21_ref_ring_pos(&r, i, &p);
        rows = (p.end_y - p.ref_y) / r.stride / 16u;
        assert(p.wrap_rows == rows - 1u);
        assert((rows < 68u) == (p.ref_y + 68u * 16u * r.stride > p.end_y));
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
    assert(get(off_pairs, 0xb0000) == 0x0002ffbdu);
    base4 = get(off_pairs, 0x60004) & 0x40000000u;
    assert(base4 == 0);

    fill(&c, p, 1920, 1080);
    c.ref_share = 1;
    c.ring_start_y = 0x03a00000u;
    c.ring_start_c = 0x03a00000u + 0x27d800u;
    c.ring_end_y = c.ring_start_y + 0x27d800u;
    c.ring_end_c = c.ring_start_c + 0x13ec00u;
    c.ring_wrap_rows = 15;
    assert(T21_H264_BuildDescriptor(&c, &on_pairs) == 0);
    assert(get(on_pairs, 0xb0000) == 0x00020fbdu);
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
                   reg == 0xb0034 || reg == 0x50110 || reg == 0x50114 ||
                   reg == 0xb0000);
        }
    }
    (void)t;
}

/* The command list of picture n of the 1080p ring, register by register
 * against the OEM table (what t30_fill_slice programs). */
static void test_picture(size_t i)
{
    T21H264SliceConfig c;
    T21RefRing r;
    T21RefRingPos p;
    size_t pairs = 0;
    int is_p = oem_1080p[i].n != 0;

    t21_ref_ring_init(&r, 0x03000000u, 120, 68);
    t21_ref_ring_pos(&r, oem_1080p[i].n, &p);
    fill(&c, is_p, 1920, 1080);
    c.ref_share = 1;
    c.reference_y = p.ref_y;
    c.reference_c = p.ref_c;
    c.output_y = p.recon_y;
    c.output_c = p.recon_c;
    c.ring_start_y = p.start_y;
    c.ring_start_c = p.start_c;
    c.ring_end_y = p.end_y;
    c.ring_end_c = p.end_c;
    c.ring_wrap_rows = p.wrap_rows;
    assert(T21_H264_BuildDescriptor(&c, &pairs) == 0);
    /* T23 adds bit 31 */
    assert((get(pairs, 0x60004) & 0x7fffffffu) ==
           (0x40000000u | (1080u << 14) | 1920u));
    assert(get(pairs, 0x60008) == oem_1080p[i].recon_y);
    assert(get(pairs, 0x6000c) == oem_1080p[i].recon_c);
    assert(get(pairs, 0x60014) == 0x03000000u);
    assert(get(pairs, 0x60018) == 0x03276000u);
    assert(get(pairs, 0x6001c) == 0x03276000u);
    assert(get(pairs, 0x60020) == 0x033b1000u);
    assert(get(pairs, 0xb0014) == oem_1080p[i].ref_y);
    assert(get(pairs, 0xb0018) == oem_1080p[i].ref_c);
    assert(get(pairs, 0xb0030) == 0x03000000u);
    assert(get(pairs, 0xb0034) == 0x03276000u);
    assert(get(pairs, 0xb0000) ==
           (0x000200bdu | ((uint32_t)oem_1080p[i].wrap << 8)));
    if (is_p) {
        assert(get(pairs, 0x5006c) == oem_1080p[i].ref_y);
        assert(get(pairs, 0x50070) == oem_1080p[i].ref_c);
        assert(get(pairs, 0x50110) == 0x03000000u);
        assert(get(pairs, 0x50114) == 0x03276000u);
    }
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
    test_oem_values();
    {
        size_t i;

        for (i = 0; i < sizeof(oem_1080p) / sizeof(oem_1080p[0]); i++)
            test_picture(i);
    }
    puts("ref_ring_test: ok");
    return 0;
}
