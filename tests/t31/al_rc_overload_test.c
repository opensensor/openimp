/*
 * CBR under a scene the encoder cannot bring down to the target (device log
 * 2026-10-12, 1080p25, 6.9 Mbit/s, QP 15..45: P pictures of ~1.3 Mbit at
 * QP 45, ~4.5 Mbit at QP 15).  Before the debt bound in hrd_add_picture the
 * buffer debt grew without limit, the 32-bit level arithmetic wrapped after
 * ~700 pictures and the QP fell from 45 to the minimum (pictures 16x the
 * target), and once the scene was easy again the QP stayed at 45 for
 * minutes.  Checks: the QP stays at the maximum for 20 minutes of such
 * pictures, for several GOP lengths and macroblock statistics, and comes
 * down again within 20 s once the pictures are small.
 */
#include "t40/t31_al_rc.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int run(unsigned gop, unsigned intra_pct)
{
    T31AlRc rc;
    T31AlRcParam p;
    T31AlGopParam g;
    const int hard = 30000, easy = 1000;
    int i, recovered = -1;

    memset(&rc, 0, sizeof(rc));
    memset(&p, 0, sizeof(p));
    memset(&g, 0, sizeof(g));
    p.mode = 1u;
    p.initial_rem_delay = 216000u;
    p.cpb_size = 270000u;
    t31_al_rc_frame_rate(25u, 1u, &p.frame_rate, &p.clk_ratio);
    p.target_bitrate = p.max_bitrate = 6899950u;
    p.initial_qp = 25;
    p.min_qp = 15;
    p.max_qp = 45;
    p.ip_delta = -1;
    p.pb_delta = -1;
    p.options = 1u;
    p.num_pixels = 1920u * 1080u;
    p.max_psnr_x100 = 4200u;
    p.max_pel = 255u;
    g.mode = 2u;
    g.length = (uint16_t)gop;
    if (t31_al_rc_init(&rc, 0u, &p, &g) != 0) {
        fprintf(stderr, "init failed\n");
        return 1;
    }
    for (i = 0; i < hard + easy; ++i) {
        T31AlRcPicture pic;
        T31AlRcStatus st;
        int idr = i % (int)gop == 0;
        int16_t qp;
        double bits;
        uint32_t size;
        int32_t filler;

        memset(&pic, 0, sizeof(pic));
        memset(&st, 0, sizeof(st));
        pic.type = idr ? 2u : 1u;
        pic.flags = idr ? 3u : 2u;
        qp = t31_al_rc_picture_qp(&rc, &pic);
        if (i < hard)
            bits = 1.3e6 * pow(4.5 / 1.3, (45.0 - qp) / 30.0) * (idr ? 1.45 : 1.0);
        else
            bits = 600.0 * pow(1.12, 45 - qp) * (idr ? 800.0 : 1.0);
        size = (uint32_t)bits;
        st.stat36 = 8160u;
        st.stat28 = 8160u * intra_pct / 100u;
        st.bits = size;
        st.qp = qp;
        filler = t31_al_rc_picture_start(&rc, &pic, &st, size);
        t31_al_rc_update(&rc, &pic, &st, size, 0u,
                         filler > 0 ? (uint32_t)(filler < 8 ? 8 : filler) * 8u : 0u);
        /* the first GOPs converge from the initial QP */
        if (i >= 300 && i < hard && qp < (idr ? 41 : 45)) {
            fprintf(stderr, "gop %u intra %u%%: picture %d %s at QP %d, want 45\n",
                    gop, intra_pct, i, idr ? "I" : "P", qp);
            return 1;
        }
        if (i >= hard && recovered < 0 && qp <= 30)
            recovered = i - hard;
    }
    if (recovered < 0 || recovered > 500) {
        fprintf(stderr, "gop %u intra %u%%: QP not down after the scene got easy (%d)\n",
                gop, intra_pct, recovered);
        return 1;
    }
    printf("gop %u intra %u%%: QP 45 held for %d pictures, down after %d\n",
           gop, intra_pct, hard - 300, recovered);
    return 0;
}

int main(void)
{
    static const unsigned gops[] = {20u, 30u, 50u, 75u};
    static const unsigned intra[] = {0u, 50u, 90u};
    unsigned a, b;
    int bad = 0;

    for (a = 0; a < sizeof(gops) / sizeof(gops[0]); ++a)
        for (b = 0; b < sizeof(intra) / sizeof(intra[0]); ++b)
            bad |= run(gops[a], intra[b]);
    if (bad)
        return 1;
    printf("t31 al rc overload: ok\n");
    return 0;
}
