/*
 * Host test of the T30 HEVC header writers (t30_radix_hevc.c): 500 random
 * VPS/SPS/PPS/slice-header cases (hevc_case.h) must hash like the output of
 * the vendor libimp 1.0.5 Entropy::codeVPS/codeSPS/codePPS/codeSliceHeader
 * for the same values (hash recorded with tests/t30/radix/hevc_diff under
 * qemu-mipsel, 2026-11-05; 20000 cases there, 0 differ).  Also writes a
 * realistic 1920x1080 Main header set (radix_hevc_headers.265) for ffprobe.
 */
#include <stdio.h>

#include "hevc_case.h"

static uint32_t mine_hash(uint32_t h, const T30HevcBits *b)
{
    return hevc_hash(h, b->buf, (uint32_t)b->len, b->bits,
                     b->bits ? (uint8_t)(b->cur << (8 - b->bits)) : 0);
}

static size_t put_nal(uint8_t *out, size_t cap, uint32_t type, T30HevcBits *b)
{
    t30_hevc_trailing(b);
    return t30_hevc_nal(out, cap, type, b->buf, b->len, 1);
}

int main(void)
{
    uint32_t hash = 2166136261u;
    static uint8_t buf[4096], file[8192];
    T30HevcVps v;
    T30HevcSps s;
    T30HevcPps p;
    T30HevcSlice l;
    T30HevcBits b;
    size_t n = 0;
    FILE *f;

    for (int c = 0; c < 500; c++) {
        hevc_case(&v, &s, &p, &l);
        t30_hevc_bits_init(&b, buf, sizeof(buf));
        t30_hevc_write_vps(&b, &v);
        hash = mine_hash(hash, &b);
        t30_hevc_bits_init(&b, buf, sizeof(buf));
        t30_hevc_write_sps(&b, &s);
        hash = mine_hash(hash, &b);
        t30_hevc_bits_init(&b, buf, sizeof(buf));
        t30_hevc_write_pps(&b, &p);
        hash = mine_hash(hash, &b);
        s.long_term_refs = 0;
        p.weighted_pred = p.weighted_bipred = 0;
        t30_hevc_bits_init(&b, buf, sizeof(buf));
        if (t30_hevc_write_slice_header(&b, &l, &s, &p) == 0)
            hash = mine_hash(hash, &b);
    }

    /* a plausible Main 1080p set for a decoder check */
    memset(&v, 0, sizeof(v));
    memset(&s, 0, sizeof(s));
    memset(&p, 0, sizeof(p));
    v.ptl.profile_idc = 1;
    v.ptl.level_idc = 120;
    v.ptl.compat[1] = v.ptl.compat[2] = 1;
    v.ptl.progressive_source = v.ptl.frame_only_constraint = 1;
    v.max_sub_layers = 1;
    v.max_dec_pic_buffering = 2;
    s.ptl = v.ptl;
    s.max_sub_layers = 1;
    s.chroma_format_idc = 1;
    s.width = 1920;
    s.height = 1080;
    s.log2_max_poc_lsb = 8;
    s.max_dec_pic_buffering = 2;
    s.log2_min_cb = 3;
    s.log2_diff_max_min_cb = 3;
    s.log2_min_tb = 2;
    s.log2_max_tb = 5;
    s.max_tu_depth_inter = s.max_tu_depth_intra = 1;
    s.sao = 1;
    s.timing_info = 1;
    s.hrd_flag_written = 1;
    s.vui.num_units_in_tick = 1;
    s.vui.time_scale = 25;
    p.num_ref_idx_l0_default = p.num_ref_idx_l1_default = 1;
    p.cu_qp_delta_enabled = 1;
    p.loop_filter_across_slices = 1;
    t30_hevc_bits_init(&b, buf, sizeof(buf));
    t30_hevc_write_vps(&b, &v);
    n += put_nal(file + n, sizeof(file) - n, 32, &b);
    t30_hevc_bits_init(&b, buf, sizeof(buf));
    t30_hevc_write_sps(&b, &s);
    n += put_nal(file + n, sizeof(file) - n, 33, &b);
    t30_hevc_bits_init(&b, buf, sizeof(buf));
    t30_hevc_write_pps(&b, &p);
    n += put_nal(file + n, sizeof(file) - n, 34, &b);
    if ((f = fopen("radix_hevc_headers.265", "wb"))) {
        fwrite(file, 1, n, f);
        fclose(f);
    }

    printf("radix_hevc_test: %s (hash 0x%08x, vendor 0x114a9aba)\n",
           hash == 0x114a9abau ? "PASS" : "FAIL", hash);
    return hash != 0x114a9abau;
}
