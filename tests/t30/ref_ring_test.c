/* Host test of the T21/T23 Helix shared reference ring (OPENIMP_REF_SHARE,
 * OEM BUF_SHARE_CFG): ring layout and sequence, and the command list
 * registers with sharing on and off. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "t21/t21_h264_descriptor.h"
#include "t21/t21_ref_ring.h"

#define WORDS 8192u
#if defined(T21_HELIX_T23_DELTAS)
#define RING_B0_LOW 0x00020063u      /* vendor T23 ring mode, P */
#define RING_B0_IDR 0x00020021u      /* vendor T23 ring mode, IDR */
#else
#define RING_B0_LOW 0x000200ffu      /* T21 template plus the two flags */
#define RING_B0_IDR 0x000200bdu
#endif
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
    assert(r.ring_y == (((mbh * 16u + 63u) & ~63u) + 256u) * stride);
    assert(r.ring_c == (((mbh * 16u + 63u) & ~63u) / 2u + 128u) * stride);
    assert(r.base_c == r.base_y + r.ring_y + 0x100u);
    assert(t21_ref_ring_bytes(mbw, mbh) == r.ring_y + 0x100u + r.ring_c);
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
        /* vendor granularity: the reference is a multiple of 64 lines
         * from the ring end, wrap byte 3 mod 4 (360p: 640-line ring) */
        assert((cur.end_y - cur.ref_y) % (64u * stride) == 0u);
        assert((cur.wrap_rows & 3u) == 3u);
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
 * (0xb0000 bits 8..15) = ((end_y - ref_y) / stride >> 4) - 1 (T21 1.0.33:
 * 255 for the first P after the IDR).  The chroma ring starts 0x100 after
 * the luma ring end (vendor T23 layout). */
static const struct {
    uint64_t n;
    uint32_t recon_y, recon_c, ref_y, ref_c;
    uint8_t wrap;
} oem_1080p[] = {
    { 0, 0x03000000u, 0x03276100u, 0x03000000u, 0x03276100u, 83 },
    { 1, 0x031fe000u, 0x03375100u, 0x03000000u, 0x03276100u, 83 },
    { 2, 0x03186000u, 0x03339100u, 0x031fe000u, 0x03375100u, 15 },
    { 3, 0x0310e000u, 0x032fd100u, 0x03186000u, 0x03339100u, 31 },
    { 4, 0x03096000u, 0x032c1100u, 0x0310e000u, 0x032fd100u, 47 },
    { 5, 0x0301e000u, 0x03285100u, 0x03096000u, 0x032c1100u, 63 },
    { 6, 0x0321c000u, 0x03384100u, 0x0301e000u, 0x03285100u, 79 },
    { 20, 0x03078000u, 0x032b2100u, 0x030f0000u, 0x032ee100u, 51 },
    { 21, 0x03000000u, 0x03276100u, 0x03078000u, 0x032b2100u, 67 },
    { 22, 0x031fe000u, 0x03375100u, 0x03000000u, 0x03276100u, 83 },
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
        assert(p.wrap_rows_t21 == (oem_1080p[i].n == 1 ? 255u
                                                        : oem_1080p[i].wrap));
        assert(p.start_y == 0x03000000u && p.end_y == 0x03276000u);
        assert(p.start_c == 0x03276100u && p.end_c == 0x033b1100u);
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
    assert((get(off_pairs, 0x80030) & 0x4000u) == 0);
    if (p)
        assert(get(off_pairs, 0x50000) == 0x547fdfb1u);
    base4 = get(off_pairs, 0x60004) & 0x40000000u;
    assert(base4 == 0);

    fill(&c, p, 1920, 1080);
    c.ref_share = 1;
    c.ring_start_y = 0x03a00000u;
    c.ring_start_c = 0x03a00000u + 0x27d800u;
    c.ring_end_y = c.ring_start_y + 0x27d800u;
    c.ring_end_c = c.ring_start_c + 0x13ec00u;
    c.ring_wrap_rows = 15;
    c.ring_flags = 0;
    assert(T21_H264_BuildDescriptor(&c, &on_pairs) == 0);
    /* vendor T23 lists: 0xb0000 byte, the same wrap row in 0x50000 bits
     * 14..21, 0x10014/18 = 0 (IDR) / ring start (P) */
    assert(get(on_pairs, 0xb0000) == (p ? (RING_B0_LOW | 0x0f00u)
                                        : (RING_B0_IDR | 0x0f00u)));
    if (p) {
        assert(get(on_pairs, 0x50000) == (0x5440dfb1u | (15u << 14)));
        assert(get(on_pairs, 0x50074) == 0 && get(on_pairs, 0x50078) == 0);
    }
    assert(get(on_pairs, 0x10014) == (p ? c.ring_start_y : 0u));
    assert(get(on_pairs, 0x10018) == (p ? c.ring_start_c : 0u));
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
        assert(get(on_pairs, 0x10014) == c.ring_start_y);
    }
    /* everything else unchanged */
    for (i = 0; i < on_pairs; i++) {
        uint32_t reg = descriptor[2 * i + 1] & 0xffffcu;

        if (descriptor[2 * i] != off[2 * i]) {
            assert(reg == 0x60004 || reg == 0x60014 || reg == 0x60018 ||
                   reg == 0x6001c || reg == 0x60020 || reg == 0xb0030 ||
                   reg == 0xb0034 || reg == 0x50110 || reg == 0x50114 ||
                   reg == 0xb0000 || reg == 0x50000 || reg == 0x10014 ||
                   reg == 0x10018 || reg == 0x50040 || reg == 0x50048 ||
                   reg == 0x5004c);
        }
    }
    /* experiment switches */
    if (p) {
        c.ring_flags = T21_RING_NO_WRAP;
        assert(T21_H264_BuildDescriptor(&c, &on_pairs) == 0);
        assert(get(on_pairs, 0xb0000) == (RING_B0_LOW | 0xff00u));
        assert(get(on_pairs, 0x50000) == 0x547fdfb1u);
        c.ring_flags = T21_RING_VENDOR_MISC;
        assert(T21_H264_BuildDescriptor(&c, &on_pairs) == 0);
        assert(get(on_pairs, 0x4010c) == 0x03400000u);
        assert(get(on_pairs, 0x801c0) == 0x30000006u);
        /* vendor T23 MCE words */
        c.ring_flags = T21_RING_VENDOR_ME;
        assert(T21_H264_BuildDescriptor(&c, &on_pairs) == 0);
        assert(get(on_pairs, 0x50000) == (0x5440d9b1u | (15u << 14)));
        assert(get(on_pairs, 0x50040) == 0x871f5008u);
        assert(get(on_pairs, 0x50048) == 0x02000200u);
        assert(get(on_pairs, 0x5004c) == 0x08080303u);
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
    c.ring_flags = 0;
    assert(T21_H264_BuildDescriptor(&c, &pairs) == 0);
    assert(get(pairs, 0x10014) == (is_p ? 0x03000000u : 0u));
    /* T23 adds bit 31 */
    assert((get(pairs, 0x60004) & 0x7fffffffu) ==
           (0x40000000u | (1080u << 14) | 1920u));
    assert(get(pairs, 0x60008) == oem_1080p[i].recon_y);
    assert(get(pairs, 0x6000c) == oem_1080p[i].recon_c);
    assert(get(pairs, 0x60014) == 0x03000000u);
    assert(get(pairs, 0x60018) == 0x03276100u);
    assert(get(pairs, 0x6001c) == 0x03276000u);
    assert(get(pairs, 0x60020) == 0x033b1100u);
    assert(get(pairs, 0xb0014) == oem_1080p[i].ref_y);
    assert(get(pairs, 0xb0018) == oem_1080p[i].ref_c);
    assert(get(pairs, 0xb0030) == 0x03000000u);
    assert(get(pairs, 0xb0034) == 0x03276100u);
    assert(get(pairs, 0xb0000) ==
           ((is_p ? RING_B0_LOW : RING_B0_IDR) |
            ((uint32_t)oem_1080p[i].wrap << 8)));
    if (is_p) {
        assert(get(pairs, 0x5006c) == oem_1080p[i].ref_y);
        assert(get(pairs, 0x50070) == oem_1080p[i].ref_c);
        assert(get(pairs, 0x50110) == 0x03000000u);
        assert(get(pairs, 0x50114) == 0x03276100u);
        assert(get(pairs, 0x50000) ==
               (0x5440dfb1u | ((uint32_t)oem_1080p[i].wrap << 14)));
    }
}

/* Live registers of the vendor T23 (libimp 1.3.0, 1080p, ring always on,
 * devmem samples): luma ring 0x02ff6000..0x0326c000, chroma ring
 * 0x0326c100..0x033a7100, recon (0x60008) only at multiples of 64 lines
 * from the ring start, 0xb0000 = 0x0002xx62 at rest (start bit cleared)
 * with xx in {0x47, 0x0f, 0x1b, 0x27, 0x23, 0x2f, 0x2b, 0x37, 0x33, 0x3f,
 * 0x4b, 0x07, 0x13, 0x1f}: all 3 mod 4, as 64-line positions give. */
static void test_vendor_t23(void)
{
    static const uint8_t seen[] = { 0x47, 0x0f, 0x1b, 0x27, 0x23, 0x2f,
                                    0x2b, 0x37, 0x33, 0x3f, 0x4b, 0x07,
                                    0x13, 0x1f };
    T21RefRing r;
    T21RefRingPos p;
    uint32_t hit = 0, lines_seen = 0;
    size_t i, n;

    t21_ref_ring_init(&r, 0x02ff6000u, 120, 68);
    t21_ref_ring_pos(&r, 0, &p);
    assert(p.start_y == 0x02ff6000u && p.end_y == 0x0326c000u);
    assert(p.start_c == 0x0326c100u && p.end_c == 0x033a7100u);
    assert(t21_ref_ring_bytes(120, 68) == 0x033a7100u - 0x02ff6000u);
    for (n = 0; n < 21; n++) {
        uint32_t lines;

        t21_ref_ring_pos(&r, n, &p);
        lines = (p.recon_y - r.base_y) / 1920u;
        assert(lines % 64u == 0 && lines < 1344u);
        lines_seen |= 1u << (lines / 64u);
        assert((p.recon_c - r.base_c) / 1920u == lines / 2u);
        if (n >= 2)
            assert(p.wrap_rows % 4u == 3u);
        for (i = 0; i < sizeof(seen); i++)
            if (n >= 2 && p.wrap_rows == seen[i])
                hit |= 1u << i;
    }
    assert(lines_seen == 0x1fffffu);     /* all 21 positions */
    assert(hit == (1u << sizeof(seen)) - 1u);  /* every observed byte */
}

/* QP window and lambda as the OEM h264_api_enc sets them from the picture
 * QP, in every rate-control mode (FIXQP included), on T21 1.0.33 and T23
 * 1.3.0 alike (vendor values from tools/eprc_oracle.py runs): lambda
 * 384 + 48 / 96 + 12 per QP above 33, window [QP - 12, min(QP + 13, 51)];
 * QP - 12 has the floor 1 on T23 and 0 on T21 (whose OEM wraps to 51 below
 * QP 12, not reproduced). */
static void test_qp_fields(void)
{
    static const struct { uint8_t qp; uint16_t l0, l1; uint8_t lo, hi; }
    v[] = {
        { 30, 384, 96, 18, 43 },
        { 33, 384, 96, 21, 46 },
        { 36, 528, 132, 24, 49 },
        { 40, 720, 180, 28, 51 },
        { 48, 1104, 276, 36, 51 },
#if defined(T21_HELIX_T23_DELTAS)
        { 8, 384, 96, 1, 21 },
        { 12, 384, 96, 1, 25 },
#else
        { 12, 384, 96, 0, 25 },
#endif
    };
    unsigned int i;

    for (i = 0; i < sizeof(v) / sizeof(v[0]); i++) {
        T21H264SliceConfig c;
        size_t pairs = 0;
        uint32_t w;

        fill(&c, 1, 640, 360);
        c.qp = v[i].qp;
        assert(T21_H264_BuildDescriptor(&c, &pairs) == 0);
        assert(get(pairs, 0xb001c) == v[i].l0);
        assert(get(pairs, 0xb0020) == ((uint32_t)v[i].l1 << 16 | v[i].l1));
        assert(get(pairs, 0x40040) == v[i].hi);
        w = get(pairs, 0x40074);
        assert(((w >> 24) & 0xffu) == v[i].hi);
        assert(((w >> 16) & 0xffu) == v[i].lo);
    }
}

int main(void)
{
    test_qp_fields();
    /* 1080p: 3.7 MiB instead of 2 x 3.0 MiB */
    assert(t21_ref_ring_bytes(120, 68) == 3870976u);
    assert(t21_ref_ring_bytes(120, 68) < 2u * (1920u * 1088u * 3u / 2u));
    /* the sizes the device uses: 1080p and 360p both share (ring 3.69 MiB
     * against 6.0 MiB, 600 KiB against 690 KiB, 360p ring rounded to 384 lines), tiny pictures do not */
    assert(t21_ref_ring_saves(120, 68));
    assert(t21_ref_pair_bytes(120, 68) == 6266880u);
    assert(t21_ref_ring_saves(40, 23));
    assert(t21_ref_ring_bytes(40, 23) == 614656u);
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
    test_vendor_t23();
    {
        size_t i;

        for (i = 0; i < sizeof(oem_1080p) / sizeof(oem_1080p[0]); i++)
            test_picture(i);
    }
    puts("ref_ring_test: ok");
    return 0;
}
