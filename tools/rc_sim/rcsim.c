/*
 * Host wrapper around OpenIMP's vendor-identical rate controllers
 * (src/eprc: T21/T23, src/rc_t20: T20, src/rc_t10: T10) with one uniform
 * interface for tools/rc_sim/rc_sim.py (ctypes).  The controller sources
 * are taken unchanged from their branches by the Makefile; nothing here
 * changes their decisions.  The "beyond vendor" variants live in
 * rc_sim.py and only touch the controller through this interface and the
 * raw state blocks (rcs_block), as an opt-in OpenIMP layer would.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "eprc/eprc.h"
#include "rc_t20/rc_t20.h"
#include "rc_t10/rc_t10.h"

enum { SOC_T23 = 0, SOC_T20 = 1, SOC_T10 = 2 };

typedef struct {
    int soc, mode, gop, idr_gops;
    uint32_t mbs, frame;
    Eprc eprc;
    uint8_t slice[EPRC_SLICE_SIZE];
    RcT20 t20;
    RcT10 t10;
    int last_qp;
    /* study options (rcs_set_opt), all 0 = vendor */
    int opt_iaware;             /* 1: I-aware per-picture budget */
    int opt_floor;              /* floor = nominal / opt_floor */
    int64_t last_i_bits;
} Rcs;

static Rcs *rcs_cur;            /* instance inside rcs_start */

/* Hook in the P-picture QP of rc_t20/rc_t10 (Makefile): the per-picture
 * nominal budget.  Vendor: bit rate / fps (or GOP target / GOP); I-aware:
 * (nominal x GOP - bits of the last I picture) / (GOP - 1). */
int rcsim_hook_per(int per)
{
    Rcs *r = rcs_cur;
    int64_t v;

    if (!r || !r->opt_iaware || r->last_i_bits <= 0 || r->gop < 2)
        return per;
    v = ((int64_t)per * r->gop - r->last_i_bits) / (r->gop - 1);
    if (v < per / (r->opt_floor > 0 ? r->opt_floor : 8))
        v = per / (r->opt_floor > 0 ? r->opt_floor : 8);
    return (int)v;
}

void rcs_set_opt(void *h, int key, int value)
{
    Rcs *r = h;
    if (key == 1)
        r->opt_iaware = value;
    else if (key == 2)
        r->opt_floor = value;
}

void rcs_note_bits(void *h, int idr, uint32_t bits)
{
    Rcs *r = h;
    if (idr)
        r->last_i_bits = bits;
}

typedef struct {
    int32_t mode;               /* 1 CBR, 2 VBR, 3 SMART */
    int32_t width, height, gop, fps, kbps, min_qp, max_qp;
    int32_t frm_step, gop_step, i_bias, change_pos, quality, idr_gops;
} RcsParams;

/* per-picture statistics as the hardware reports them */
typedef struct {
    uint32_t cmpx;              /* SAD-like complexity (channel node +44, 0x80080) */
    uint32_t mv_sum;            /* motion vector magnitude sum (0x800e8 + 0x800ec) */
    uint32_t moving_mbs;        /* moving macroblocks (0x800e4 halves) */
    uint32_t intra_mbs;         /* intra / changed macroblocks (0x800e0) */
} RcsStats;

void *rcs_new(const RcsParams *q)
{
    Rcs *r = calloc(1, sizeof(*r));
    if (!r)
        return NULL;
    r->mode = q->mode;
    r->gop = q->gop;
    r->idr_gops = q->idr_gops > 0 ? q->idr_gops : 1;
    r->mbs = ((q->width + 15) / 16) * ((q->height + 15) / 16);
    r->soc = -1;
    return r;
}

int rcs_init(void *h, int soc, const RcsParams *q)
{
    Rcs *r = h;
    r->soc = soc;
    if (soc == SOC_T23) {
        EprcParams p;
        memset(&p, 0, sizeof(p));
        p.width = q->width; p.height = q->height;
        p.rc_mode = q->mode == 1 ? EPRC_MODE_CBR : q->mode == 3 ? EPRC_MODE_SMART : EPRC_MODE_VBR;
        p.gop = q->gop; p.fps_num = q->fps; p.fps_den = 1;
        p.min_qp = q->min_qp; p.max_qp = q->max_qp;
        p.bitrate = q->kbps; p.max_bitrate = q->kbps < 128 ? 128 : q->kbps;
        p.i_bias = q->i_bias; p.frm_qp_step = q->frm_step; p.gop_qp_step = q->gop_step;
        p.static_time = 2; p.change_pos = q->change_pos; p.quality = q->quality;
        p.init_qp = -1;
        p.bg_interval_gops = q->idr_gops;
        return EPRC_Init(&r->eprc, &p, r->slice);
    }
    if (soc == SOC_T20) {
        RcT20Params p;
        RCT20_DefaultParams(&p);
        p.method = q->mode; p.width = q->width; p.height = q->height; p.gop = q->gop;
        p.fps_num = q->fps; p.fps_den = 1; p.min_qp = q->min_qp; p.max_qp = q->max_qp;
        p.bitrate = q->kbps; p.max_bitrate = q->kbps < 128 ? 128 : q->kbps;
        p.i_bias = q->i_bias; p.frm_qp_step = q->frm_step; p.gop_qp_step = q->gop_step;
        p.static_time = 2; p.change_pos = q->change_pos; p.quality = q->quality;
        p.mb_rc = 0;            /* OpenIMP default (OPENIMP_T20_MBRC unset) */
        return RCT20_Init(&r->t20, &p);
    }
    if (soc == SOC_T10) {
        RcT10Params p;
        RCT10_DefaultParams(&p);
        p.method = q->mode; p.width = q->width; p.height = q->height; p.gop = q->gop;
        p.fps_num = q->fps; p.fps_den = 1; p.min_qp = q->min_qp; p.max_qp = q->max_qp;
        p.bitrate = q->kbps; p.max_bitrate = q->kbps < 128 ? 128 : q->kbps;
        p.i_bias = q->i_bias; p.frm_qp_step = q->frm_step; p.gop_qp_step = q->gop_step;
        p.static_time = 2; p.change_pos = q->change_pos; p.quality = q->quality;
        return RCT10_Init(&r->t10, &p);
    }
    return -1;
}

void rcs_free(void *h)
{
    Rcs *r = h;
    if (!r)
        return;
    if (r->soc == SOC_T23)
        EPRC_Free(&r->eprc);
    else if (r->soc == SOC_T20)
        RCT20_Free(&r->t20);
    free(r);
}

/* Picture start: returns the QP; *type 1 = IDR, 0 = P (2 = SMART GOP-start P). */
int rcs_start(void *h, int idr, int frames_since_idr, int *type)
{
    Rcs *r = h;
    int qp = 0;
    *type = idr ? 1 : 0;
    rcs_cur = r;
    if (r->soc == SOC_T23) {
        EprcFrameIn in;
        EprcPicture pic;
        memset(&in, 0, sizeof(in));
        in.frames_since_idr = idr ? 0 : (uint32_t)frames_since_idr;
        EPRC_FrameStart(&r->eprc, &in, &pic);
        qp = pic.qp;
        *type = pic.type == 2 ? 1 : pic.type == 6 ? 2 : 0;
    } else if (r->soc == SOC_T20) {
        RcT20Picture pic;
        RCT20_Start(&r->t20, idr, NULL, 0, &pic);
        qp = pic.qp;
        *type = pic.idr ? 1 : 0;
    } else if (r->soc == SOC_T10) {
        RcT10Picture pic;
        RCT10_Start(&r->t10, idr, &pic);
        qp = pic.qp;
        *type = pic.idr ? 1 : 0;
    }
    rcs_cur = NULL;
    r->last_qp = qp;
    return qp;
}

/* Picture end: returns 1 when the controller wants the picture coded again
 * at *qp.  may_repeat 0: finish without re-encode judging where the
 * controller supports it (eprc; T20/T10 have no such entry, the caller
 * then ignores the request, see rc_sim.py). */
int rcs_end(void *h, uint32_t bits, const RcsStats *st, int may_repeat, int *qp)
{
    Rcs *r = h;
    if (r->soc == SOC_T23) {
        uint32_t regs[EPRC_STAT_REGS];
        EprcPicture pic;
        int rep;
        memset(regs, 0, sizeof(regs));
        regs[15] = st->cmpx;
        regs[20] = (st->mv_sum / 2) & 0x7ffffffu;
        regs[21] = (st->mv_sum - st->mv_sum / 2) & 0x7ffffffu;
        regs[22] = ((st->moving_mbs / 2) & 0x7fff) | (((st->moving_mbs - st->moving_mbs / 2) & 0x7fff) << 16);
        regs[23] = st->intra_mbs & 0x7fff;
        rep = EPRC_FrameEndEx(&r->eprc, (bits + 7) / 8, regs, &pic, may_repeat);
        if (rep)
            *qp = pic.qp;
        return rep;
    }
    if (r->soc == SOC_T20) {
        RcT20Stats s;
        RcT20Picture pic;
        int rep;
        s.cmpx = st->cmpx;
        s.bits = bits;
        s.reg[0] = ((st->moving_mbs / 2) & 0x7fff) | (((st->moving_mbs - st->moving_mbs / 2) & 0x7fff) << 16);
        s.reg[1] = (st->mv_sum / 2) & 0x7ffffffu;
        s.reg[2] = (st->mv_sum - st->mv_sum / 2) & 0x7ffffffu;
        pic.qp = (uint8_t)r->last_qp;
        pic.idr = 0;
        rep = RCT20_End(&r->t20, &s, &pic);
        if (rep)
            *qp = pic.qp;
        return rep;
    }
    if (r->soc == SOC_T10) {
        RcT10Stats s;
        RcT10Picture pic;
        int rep;
        s.cmpx = st->cmpx;
        s.bits = bits;
        pic.qp = (uint8_t)r->last_qp;
        pic.idr = 0;
        rep = RCT10_End(&r->t10, &s, &pic);
        if (rep)
            *qp = pic.qp;
        return rep;
    }
    return 0;
}

/* Raw OEM-layout blocks: 0 = E, 1 = P (eprc: state block), 2 = S. */
uint8_t *rcs_block(void *h, int which)
{
    Rcs *r = h;
    if (r->soc == SOC_T23)
        return which == 0 ? r->eprc.e : which == 1 ? r->eprc.p : r->eprc.p + 352;
    if (r->soc == SOC_T20)
        return which == 0 ? r->t20.e : which == 1 ? r->t20.p : r->t20.s;
    if (r->soc == SOC_T10)
        return which == 0 ? r->t10.e : which == 1 ? r->t10.p : r->t10.s;
    return NULL;
}
