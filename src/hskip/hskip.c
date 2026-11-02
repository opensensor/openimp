/*
 * IMP_Encoder_SetChnHSkip reference structures (see hskip.h).
 *
 * OEM: libimp 3.12.0 (T10/T20, one binary) i264e_init_skip_header (0x32110)
 * and i264e_decide_slice_type_and_rd (0x322b4).
 */
#include <string.h>

#include "hskip/hskip.h"

static int32_t hs_clip3(int32_t x, int32_t lo, int32_t hi)
{
    return x < lo ? lo : x > hi ? hi : x;
}

void hskip_init(HSkip *s, uint32_t gop, uint32_t type, uint32_t m, uint32_t n,
                uint32_t same)
{
    int32_t a = 0, b = 0, c = 0, g = (int32_t)gop;

    memset(s, 0, sizeof(*s));
    s->gop = gop;
    s->same = same;
    s->type = type;
    s->counter = 0xffffffffu;
    switch (type) {
    case 0: a = 1; b = 0; c = 2; break;                 /* N1X */
    case 1: a = 1; b = 1; c = 2; break;                 /* N2X */
    case 2: a = 2; b = 1; c = 3; break;                 /* N4X */
    case 3: a = (g - 1) / 2; b = 1; c = 3; break;       /* HN1_FALSE */
    case 4: a = hs_clip3((int32_t)n, 1, (g - 1) / 2); b = 1; c = 3; break;  /* HN1_TRUE */
    case 5:
    case 6: a = 1; b = same ? g - 1 : hs_clip3((int32_t)m, 0, g - 1); c = 3; break;  /* H1M_* */
    default: return;
    }
    s->a = (uint32_t)a;
    s->b = (uint32_t)b;
    s->buffers = (uint32_t)c;
}

uint32_t hskip_buffers(uint32_t type)
{
    return type == 0 || type == 1 ? 2u : 3u;
}

/* the third buffer of a three-buffer structure: the lowest index that is neither the
 * reconstruction of the previous state (x) nor the current one (y) */
static int32_t hs_pick3(int32_t x, int32_t y)
{
    int32_t n;

    if (x == 0) {
        n = 1;
        if (y == 1)
            n = 2;
    } else {
        n = 0;
        if (y == 0) {
            n = 1;
            if (x == 1)
                n = 2;
        }
    }
    return n;
}

void hskip_decide(HSkip *s, int force_idr, int scene_idr, HSkipPic *pic)
{
    HSkipRef *cur = &s->cur, *prev = &s->prev;
    uint32_t cnt = s->counter;
    int32_t ref, rec;
    uint32_t ftype;
    int idr_path = 0;

    memset(pic, 0, sizeof(*pic));
    if ((int32_t)cnt < 0 || force_idr) {
        s->counter = 0;
        idr_path = 1;
    } else {
        if (s->gop == 0)
            return;
        if ((s->type == 0 || s->type == 5 || s->type == 6) && (int32_t)s->same > 0) {
            if (scene_idr && (cnt + 1u) % s->gop == 0) {
                cnt = 0;                                /* scene cut at a GOP boundary */
            } else {
                cnt = (cnt + 1u) % (s->same * s->gop);
            }
        } else {
            cnt = (cnt + 1u) % s->gop;
        }
        s->counter = cnt;
        if (cnt == 0)
            idr_path = 1;
    }
    if (idr_path) {
        memset(cur, 0, sizeof(*cur));
        memset(prev, 0, sizeof(*prev));
        cur->ref = -1;
        prev->ref = -1;
        prev->recon = -1;
        cur->ftype = 0;
        cur->flag = s->type != 0;
        if (s->type != 0)
            *prev = *cur;
        ref = cur->ref;
        rec = cur->recon;
        pic->idr = 1;
    } else {
        uint32_t spacing = (s->b + 1u) * s->a;

        if (spacing == 0)
            return;
        if (cnt % spacing != 0) {
            if (cnt % (s->b + 1u) == 0) {
                /* short-term base picture */
                cur->counter = cnt;
                cur->ref = cur->ftype < 3 ? cur->recon : prev->recon;
                if (s->buffers == 2) {
                    int32_t x = prev->flag == 0 ? cur->recon : prev->recon;

                    cur->recon = x == 0;
                } else {
                    cur->recon = hs_pick3(prev->recon, cur->recon);
                }
                cur->ftype = HSKIP_FT_SBASE;
                cur->flag = 0;
            } else {
                /* enhancement picture */
                int32_t r = cur->recon;

                cur->counter = cnt;
                cur->ref = r;
                if (s->buffers == 2) {
                    int32_t x = prev->flag != 0 ? prev->recon : r;

                    cur->recon = x == 0;
                } else {
                    cur->recon = hs_pick3(prev->recon, r);
                }
                cur->ftype = HSKIP_FT_ENHANCE;
                cur->flag = 0;
            }
        } else {
            /* long-term base picture (or the plain P picture of N1X) */
            int32_t r, x, y;

            cur->counter = cnt;
            if (prev->flag == 0) {
                r = cur->recon;
                cur->ref = r;
                x = prev->recon;
                y = r;
            } else {
                r = prev->recon;
                cur->ref = r;
                x = r;
                y = cur->recon;
            }
            if (s->buffers == 2)
                cur->recon = r == 0;
            else
                cur->recon = hs_pick3(x, y);
            if (s->type == 0) {
                cur->ftype = HSKIP_FT_SBASE;
                cur->flag = 0;
            } else if (s->type == 5) {
                cur->ftype = HSKIP_FT_SBASE;
                cur->flag = 0;
            } else {
                cur->ftype = cnt != 0;
                cur->flag = 1;
                *prev = *cur;
            }
        }
        ref = cur->ref;
        rec = cur->recon;
    }
    ftype = cur->ftype;
    pic->ftype = ftype;
    pic->counter = s->counter;
    pic->ref = ref;
    pic->recon = rec;
    if (ftype == 0 && cur->flag != 0) {
        pic->ltr_flag = 1;
        return;
    }
    if (s->type != 0 && ftype - 1u < 2u) {
        pic->reorder = 1;
        if (cur->flag != 0)
            pic->mmco = 1;
    }
}

int hskip_rc_cond(const HSkip *s, const HSkipPic *pic, int t10)
{
    switch (s->type) {
    case 0:
        return !t10 && s->gop && pic->counter % s->gop == 0 && pic->ftype == HSKIP_FT_SBASE;
    case 5:
        return pic->ftype == HSKIP_FT_SBASE;
    case 6:
        return pic->ftype == HSKIP_FT_LBASE;
    default:
        return 0;
    }
}
