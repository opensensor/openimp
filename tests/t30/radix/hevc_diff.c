/*
 * Differential test of the T30 H.265 syntax helpers against the vendor
 * libimp 1.0.5 (x265-derived i265e) under qemu-mipsel:
 *   hevc_diff <vendor libimp.so> [cases]
 * cabac: t30_radix_cabac_init vs Entropy::resetEntropy (0x6476c), every slice
 *        type and QP -10..60.
 * vps/sps/pps/slice: t30_hevc_write_* vs Entropy::codeVPS/codeSPS/codePPS/
 *        codeSliceHeader for random values (struct offsets from the vendor
 *        writers, see docs/T30_HEVC.md); the RBSP bits must be identical.
 * Desk tool, not part of libimp (docs/T30_HEVC.md).
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "t30/radix/t30_radix_cabac.h"
#include "t30/radix/t30_radix_hevc.h"
#include "hevc_case.h"

static void w32(uint8_t *b, unsigned o, uint32_t v) { memcpy(b + o, &v, 4); }

typedef void (*ctor_fn)(void *self);
typedef void (*reset_fn)(void *entropy, const void *slice);

static void *sym(void *h, const char *name)
{
    void *p = dlsym(h, name);

    if (!p) {
        fprintf(stderr, "missing %s\n", name);
        exit(2);
    }
    return p;
}


/* --- vendor layouts (offsets from the vendor writers) -------------------- */
static void v_ptl(uint8_t *b, const T30HevcPtl *p)
{
    w32(b, 0x00, p->profile_idc);
    w32(b, 0x04, p->level_idc);
    w32(b, 0x10, p->bit_depth_constraint);
    w32(b, 0x14, p->chroma_format_constraint);
    b[0x18] = p->tier_flag;
    b[0x19] = p->progressive_source;
    b[0x1a] = p->interlaced_source;
    b[0x1b] = p->non_packed_constraint;
    b[0x1c] = p->frame_only_constraint;
    memcpy(b + 0x1d, p->compat, 32);
    b[0x3d] = p->intra_constraint;
    b[0x3e] = p->one_picture_only;
    b[0x3f] = p->lower_bit_rate;
}

static void v_sps(uint8_t *b, const T30HevcSps *s)
{
    const T30HevcVui *v = &s->vui;
    uint8_t *u = b + 0x70, *h = u + 0x30;

    w32(b, 0x00, s->chroma_format_idc);
    w32(b, 0x04, s->width);
    w32(b, 0x08, s->height);
    w32(b, 0x20, s->log2_min_cb);
    w32(b, 0x24, s->log2_diff_max_min_cb);
    w32(b, 0x28, s->log2_max_poc_lsb);
    w32(b, 0x2c, s->log2_max_tb);
    w32(b, 0x30, s->log2_min_tb);
    w32(b, 0x34, s->max_tu_depth_inter);
    w32(b, 0x38, s->max_tu_depth_intra);
    w32(b, 0x40, s->max_sub_layers);
    w32(b, 0x44, s->max_dec_pic_buffering);
    w32(b, 0x48, s->max_latency_increase);
    w32(b, 0x4c, s->num_reorder_pics);
    b[0x54] = s->long_term_refs;
    b[0x55] = s->sao;
    b[0x56] = s->amp;
    b[0x57] = s->strong_intra_smoothing;
    b[0x58] = s->temporal_mvp;
    b[0x59] = s->timing_info;
    b[0x5a] = s->hrd_flag_written;
    w32(b, 0x5c, s->conf_left);
    w32(b, 0x60, s->conf_right);
    w32(b, 0x64, s->conf_top);
    w32(b, 0x68, s->conf_bottom);
    b[0x6c] = s->conformance_window;
    w32(u, 0x00, v->aspect_ratio_idc);
    w32(u, 0x04, v->sar_width);
    w32(u, 0x08, v->sar_height);
    w32(u, 0x0c, v->video_format);
    w32(u, 0x10, v->colour_primaries);
    w32(u, 0x14, v->transfer_characteristics);
    w32(u, 0x18, v->matrix_coeffs);
    w32(u, 0x1c, v->chroma_loc_top);
    w32(u, 0x20, v->chroma_loc_bottom);
    u[0x24] = v->aspect_ratio_info_present;
    u[0x25] = v->overscan_info_present;
    u[0x26] = v->overscan_appropriate;
    u[0x27] = v->video_signal_type_present;
    u[0x28] = v->video_full_range;
    u[0x29] = v->colour_description_present;
    u[0x2a] = v->chroma_loc_info_present;
    u[0x2b] = v->frame_field_info_present;
    u[0x2c] = v->field_seq;
    u[0x2d] = v->hrd_present;
    w32(h, 0x00, v->hrd.bit_rate_scale);
    w32(h, 0x04, v->hrd.cpb_size_scale);
    w32(h, 0x08, v->hrd.initial_cpb_removal_delay_length);
    w32(h, 0x0c, v->hrd.cpb_removal_delay_length);
    w32(h, 0x10, v->hrd.dpb_output_delay_length);
    w32(h, 0x14, v->hrd.bit_rate_value);
    w32(h, 0x18, v->hrd.cpb_size_value);
    h[0x1c] = v->hrd.cbr_flag;
    w32(u, 0x50, v->def_disp_left);
    w32(u, 0x54, v->def_disp_right);
    w32(u, 0x58, v->def_disp_top);
    w32(u, 0x5c, v->def_disp_bottom);
    u[0x60] = v->default_display_window;
    w32(u, 0x64, v->num_units_in_tick);
    w32(u, 0x68, v->time_scale);
}

static void v_pps(uint8_t *b, const T30HevcPps *p)
{
    w32(b, 0x00, p->diff_cu_qp_delta_depth);
    w32(b, 0x04, (uint32_t)p->cb_qp_offset);
    w32(b, 0x08, (uint32_t)p->cr_qp_offset);
    w32(b, 0x0c, (uint32_t)p->beta_offset_div2);
    w32(b, 0x10, (uint32_t)p->tc_offset_div2);
    b[0x14] = p->weighted_pred;
    b[0x15] = p->weighted_bipred;
    b[0x16] = p->cu_qp_delta_enabled;
    b[0x17] = p->constrained_intra_pred;
    b[0x18] = p->transquant_bypass;
    b[0x19] = p->transform_skip;
    b[0x1a] = p->entropy_coding_sync;
    b[0x1b] = p->sign_data_hiding;
    b[0x1c] = p->deblocking_control_present;
    b[0x1d] = p->deblocking_disabled;
    w32(b, 0x20, p->num_ref_idx_l0_default);
    w32(b, 0x24, p->num_ref_idx_l1_default);
    b[0x28] = p->slice_chroma_qp_offsets_present;
}

static void v_slice(uint8_t *b, const T30HevcSlice *s, int32_t init_qp_minus26)
{
    uint32_t n = s->num_negative + s->num_positive;

    w32(b, 0x600, n);
    w32(b, 0x604, s->num_negative);
    w32(b, 0x608, s->num_positive);
    for (uint32_t i = 0; i < n; i++) {
        w32(b, 0x650 + 4 * i, (uint32_t)s->delta_poc[i]);
        b[0x690 + i] = s->used[i];
    }
    w32(b, 0x770, s->nal_unit_type);
    w32(b, 0x774, s->slice_type);
    w32(b, 0x77c, (uint32_t)s->cb_qp_offset);
    w32(b, 0x780, (uint32_t)s->cr_qp_offset);
    w32(b, 0x784, (uint32_t)s->poc);
    w32(b, 0x788, (uint32_t)s->last_idr_poc);
    w32(b, 0x78c, (uint32_t)s->rps_idx);
    w32(b, 0x790, s->collocated_ref_idx);
    w32(b, 0x794, s->num_ref_idx_l0);
    w32(b, 0x798, s->num_ref_idx_l1);
    w32(b, 0x79c, s->max_num_merge_cand);
    b[0x7a5] = s->loop_filter_across_slices;
    b[0x7a6] = s->collocated_from_l0;
    w32(b, 0x7a8, (uint32_t)init_qp_minus26);
    w32(b, 0x7b4, s->num_sps_rps);
}

/* vendor Bitstream + Entropy */
typedef void (*fn_obj)(void *self);
static fn_obj bs_ctor, ent_ctor;
static uint8_t *bs_obj, *ent_obj;

static void vendor_begin(void)
{
    static uint8_t bs_mem[256], ent_mem[8192];

    memset(bs_mem, 0, sizeof(bs_mem));
    memset(ent_mem, 0, sizeof(ent_mem));
    bs_ctor(bs_mem);
    ent_ctor(ent_mem);
    memcpy(ent_mem, &(void *){ bs_mem }, sizeof(void *));
    bs_obj = bs_mem;
    ent_obj = ent_mem;
}

/* vendor bits vs ours: whole bytes and the pending bits */
static uint32_t vendor_hash(uint32_t h)
{
    uint8_t *fifo;
    uint32_t len, bits;

    memcpy(&fifo, bs_obj + 4, 4);
    memcpy(&len, bs_obj + 0xc, 4);
    memcpy(&bits, bs_obj + 0x10, 4);
    return hevc_hash(h, fifo, len, bits, bs_obj[0x14]);
}

static int same_bits(const T30HevcBits *b)
{
    uint8_t *fifo;
    uint32_t len, bits;
    uint8_t part;

    memcpy(&fifo, bs_obj + 4, 4);
    memcpy(&len, bs_obj + 0xc, 4);
    memcpy(&bits, bs_obj + 0x10, 4);
    part = bs_obj[0x14];
    if (len != b->len || bits != b->bits || b->overflow)
        return 0;
    if (len && memcmp(fifo, b->buf, len))
        return 0;
    return !bits || part == (uint8_t)(b->cur << (8 - bits));
}

static int test_headers(void *h, int cases)
{
    typedef void (*vps_fn)(void *, const void *);
    typedef void (*sps_fn)(void *, const void *, const void *, const void *);
    typedef void (*pps_fn)(void *, const void *, int, int);
    typedef void (*sh_fn)(void *, const void *, void *, unsigned, unsigned, int);
    vps_fn vps = (vps_fn)sym(h, "_ZN8I265E_NS7Entropy7codeVPSERKNS_8I265eVPSE");
    sps_fn sps = (sps_fn)sym(h, "_ZN8I265E_NS7Entropy7codeSPSERKNS_8I265eSPSERKNS_16I265eScalingListERKNS_21I265eProfileTierLevelE");
    pps_fn pps = (pps_fn)sym(h, "_ZN8I265E_NS7Entropy7codePPSERKNS_8I265ePPSEbi");
    sh_fn sh = (sh_fn)sym(h, "_ZN8I265E_NS7Entropy15codeSliceHeaderERKNS_10I265eSliceEPNS_16I265eEncodeFrameEjji");
    int bad[4] = { 0, 0, 0, 0 }, slices = 0;
    uint32_t hash = 2166136261u;

    bs_ctor = (fn_obj)sym(h, "_ZN8I265E_NS9BitstreamC1Ev");
    ent_ctor = (fn_obj)sym(h, "_ZN8I265E_NS7EntropyC1Ev");
    for (int c = 0; c < cases; c++) {
        static uint8_t vb[0x2000], vs[0x2000], sl[0x800], enc[0x2000], frame[0x100];
        static uint8_t out[4096];
        T30HevcVps V;
        T30HevcSps S;
        T30HevcPps Pp;
        T30HevcSlice L;
        T30HevcBits b;

        hevc_case(&V, &S, &Pp, &L);
        memset(vb, 0, sizeof(vb));
        v_ptl(vb + 0x20, &V.ptl);
        w32(vb, 0x60, V.max_sub_layers);
        w32(vb, 0x64, V.num_reorder_pics);
        w32(vb, 0x68, V.max_dec_pic_buffering);
        w32(vb, 0x6c, V.max_latency_increase);
        vendor_begin();
        vps(ent_obj, vb);
        t30_hevc_bits_init(&b, out, sizeof(out));
        t30_hevc_write_vps(&b, &V);
        bad[0] += !same_bits(&b);
        hash = vendor_hash(hash);

        /* SPS (no scaling list data) */
        memset(vs, 0, sizeof(vs));
        memset(sl, 0, sizeof(sl));
        v_sps(vs, &S);
        sl[0x540] = S.scaling_list_enabled;
        v_ptl(vb, &S.ptl);
        vendor_begin();
        sps(ent_obj, vs, sl, vb);
        t30_hevc_bits_init(&b, out, sizeof(out));
        t30_hevc_write_sps(&b, &S);
        bad[1] += !same_bits(&b);
        hash = vendor_hash(hash);

        /* PPS */
        memset(vb, 0, sizeof(vb));
        v_pps(vb, &Pp);
        vendor_begin();
        pps(ent_obj, vb, Pp.loop_filter_across_slices, Pp.init_qp_minus26);
        t30_hevc_bits_init(&b, out, sizeof(out));
        t30_hevc_write_pps(&b, &Pp);
        bad[2] += !same_bits(&b);
        hash = vendor_hash(hash);

        /* slice header */
        S.long_term_refs = 0;
        Pp.weighted_pred = Pp.weighted_bipred = 0;
        memset(enc, 0, sizeof(enc));
        memset(vs, 0, sizeof(vs));
        memset(frame, 0, sizeof(frame));
        v_sps(enc + 0x8c0, &S);
        v_pps(enc + 0x99c, &Pp);
        v_slice(vs, &L, Pp.init_qp_minus26);
        memcpy(frame, &(void *){ enc }, sizeof(void *));
        t30_hevc_bits_init(&b, out, sizeof(out));
        if (t30_hevc_write_slice_header(&b, &L, &S, &Pp) == 0) {
            vendor_begin();
            sh(ent_obj, vs, frame, L.segment_address, L.address_bits, L.slice_qp);
            slices++;
            hash = vendor_hash(hash);
            if (!same_bits(&b) && ++bad[3] <= 3)
                printf("slice case %d: nal %u type %u differs\n", c, L.nal_unit_type, L.slice_type);
        }
    }
    printf("headers: %d cases: vps %d, sps %d, pps %d differ; slice headers %d, %d differ; vendor hash 0x%08x\n",
           cases, bad[0], bad[1], bad[2], slices, bad[3], hash);
    return bad[0] + bad[1] + bad[2] + bad[3];
}

/* NAL framing: t30_hevc_nal vs NALList::serialize (0xa0520); prints the
 * vendor's NAL record so that the start code length can be seen */
static int test_nal(void *h, int cases)
{
    typedef void (*ser_fn)(void *, int, const void *);
    typedef void (*wr_fn)(void *, unsigned, unsigned);
    fn_obj nal_ctor = (fn_obj)sym(h, "_ZN8I265E_NS7NALListC1Ev");
    ser_fn ser = (ser_fn)sym(h, "_ZN8I265E_NS7NALList9serializeE16i265e_nal_type_tRKNS_9BitstreamE");
    wr_fn wr = (wr_fn)sym(h, "_ZN8I265E_NS9Bitstream5writeEjj");
    static const int types[] = { 1, 19, 32, 33, 34, 39 };
    int bad = 0;

    for (int c = 0; c < cases; c++) {
        static uint8_t list[0x1000], rbsp[600], mine[1400];
        int t = types[rnd() % 6];
        size_t len = rr(1, 500), n;
        uint32_t vsize, vtype;
        uint8_t *vp;

        for (size_t i = 0; i < len; i++)
            rbsp[i] = (rnd() & 1) ? 0 : (uint8_t)rr(0, 4);
        vendor_begin();
        for (size_t i = 0; i < len; i++)
            wr(bs_obj, rbsp[i], 8);
        memset(list, 0, sizeof(list));
        nal_ctor(list);
        ser(list, t, bs_obj);
        memcpy(&vtype, list + 0, 4);
        memcpy(&vsize, list + 4, 4);
        memcpy(&vp, list + 8, 4);
        n = t30_hevc_nal(mine, sizeof(mine), (uint32_t)t, rbsp, len, 1);
        if (c == 0)
            printf("nal: vendor record type %u size %u, first bytes %02x %02x %02x %02x %02x %02x\n",
                   vtype, vsize, vp[0], vp[1], vp[2], vp[3], vp[4], vp[5]);
        if (vtype != (uint32_t)t || vsize != n || memcmp(vp, mine, n)) {
            if (++bad <= 3)
                printf("nal case %d type %d len %zu: vendor %u bytes, ours %zu\n", c, t, len, vsize, n);
        }
    }
    printf("nal: %d cases, %d differ\n", cases, bad);
    return bad;
}

static int test_cabac(void *h)
{
    ctor_fn ent_ctor = (ctor_fn)sym(h, "_ZN8I265E_NS7EntropyC1Ev");
    reset_fn reset = (reset_fn)sym(h, "_ZN8I265E_NS7Entropy12resetEntropyERKNS_10I265eSliceE");
    int bad = 0, n = 0;
    uint32_t hash = 2166136261u;

    for (int t = 0; t < 3; t++)
        for (int qp = -10; qp <= 60; qp++) {
            static uint8_t ent[8192], slice[8192], mine[T30_RADIX_CABAC_CONTEXTS];

            memset(ent, 0, sizeof(ent));
            memset(slice, 0, sizeof(slice));
            ent_ctor(ent);
            memcpy(slice + 0x774, &t, 4);
            memcpy(slice + 0x778, &qp, 4);
            reset(ent, slice);
            t30_radix_cabac_init(mine, t, qp);
            for (int i = 0; i < T30_RADIX_CABAC_CONTEXTS; i++)
                hash = (hash ^ ent[0x10 + i]) * 16777619u;
            n++;
            if (memcmp(ent + 0x10, mine, sizeof(mine))) {
                if (++bad <= 3)
                    for (int i = 0; i < T30_RADIX_CABAC_CONTEXTS; i++)
                        if (ent[0x10 + i] != mine[i]) {
                            printf("cabac type %d qp %d ctx 0x%02x: vendor %u ours %u\n",
                                   t, qp, i, ent[0x10 + i], mine[i]);
                            break;
                        }
            }
        }
    printf("cabac: %d cases, %d differ, vendor hash 0x%08x\n", n, bad, hash);
    return bad;
}

int main(int argc, char **argv)
{
    void *h = dlopen(argc > 1 ? argv[1] : "libimp.so", RTLD_NOW | RTLD_LOCAL);
    int bad = 0;

    if (!h) {
        fprintf(stderr, "dlopen: %s\n", dlerror());
        return 2;
    }
    bad += test_cabac(h);
    bad += test_headers(h, argc > 2 ? atoi(argv[2]) : 5000);
    bad += test_nal(h, 2000);
    return bad ? 1 : 0;
}
