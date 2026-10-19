/*
 * T40 frame-level rate control (host test, no camera).
 *
 * T40 used to code every picture with one open-loop QP, so the bitrate
 * followed the scene (Eufy T40XP 4K@20: 132 kbit/s for 6 Mbit/s CBR).  The
 * T40 codec now runs the T31 Allegro core: picture QP from
 * t31_al_rc_picture_qp, the published AU size fed back through
 * picture_start + update with no status registers (codec-t40.c
 * avpu_t40_rc_complete).  This test drives the core exactly that way against
 * a synthetic encoder (size halves every 6 QP, IDR 4x a P picture) and checks
 * that CBR and VBR reach the target over 30 s at the Eufy geometries, for a
 * detailed and for a nearly static scene.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "t40/t31_al_rc.h"

/* bits of a P picture at QP 26, per pixel (scene complexity) */
static uint32_t model_bits(double bpp26, uint32_t pixels, int qp, int idr)
{
    double b = bpp26 * pixels * pow(2.0, (26.0 - qp) / 6.0);

    if (idr)
        b *= 4.0;
    if (b < 200.0)
        b = 200.0;
    return (uint32_t)b;
}

/* avpu_t31_al_param as the T40 codec fills it */
static void fill_param(T31AlRcParam *p, T31AlGopParam *g, int cbr,
                       uint32_t bps, uint32_t w, uint32_t h, uint32_t fps)
{
    memset(p, 0, sizeof(*p));
    memset(g, 0, sizeof(*g));
    p->mode = cbr ? 1u : 2u;
    p->initial_rem_delay = 216000u;
    p->cpb_size = 270000u;
    t31_al_rc_frame_rate(fps, 1u, &p->frame_rate, &p->clk_ratio);
    p->target_bitrate = bps;
    p->max_bitrate = bps;
    p->initial_qp = 26;       /* iInitialQP -1 reads as 26 */
    p->min_qp = 15;
    p->max_qp = 45;
    p->ip_delta = -1;
    p->pb_delta = -1;
    p->num_pixels = w * h;
    p->max_psnr_x100 = 4200u;
    p->max_pel = 255u;
    g->mode = 2u;
    g->length = (uint16_t)(fps * 2u);
}

static int run(const char *name, int cbr, uint32_t kbps, uint32_t w,
               uint32_t h, double bpp26, double lo, double hi)
{
    const uint32_t fps = 20u, seconds = 30u;
    T31AlRcParam p;
    T31AlGopParam g;
    T31AlRc rc;
    uint64_t total = 0;
    uint32_t n, frames = fps * seconds;
    double ratio;

    fill_param(&p, &g, cbr, kbps * 1000u, w, h, fps);
    memset(&rc, 0, sizeof(rc));
    /* AL_EncChannel_Init: AL 1 (CBR) -> 0, AL 2 (VBR) -> 1 */
    if (t31_al_rc_init(&rc, cbr ? 0u : 1u, &p, &g) != 0) {
        fprintf(stderr, "%s: init failed\n", name);
        return 1;
    }
    for (n = 0; n < frames; ++n) {
        int idr = (n % g.length) == 0u;
        T31AlRcPicture pic;
        T31AlRcStatus st;
        int16_t qp;
        uint32_t bits;
        int32_t filler;

        memset(&pic, 0, sizeof(pic));
        pic.type = idr ? 2u : 1u;
        pic.flags = idr ? 3u : 2u;
        qp = t31_al_rc_picture_qp(&rc, &pic);
        if (qp < 0 || qp > 51) {
            fprintf(stderr, "%s: picture %u qp %d out of range\n", name, n, qp);
            return 1;
        }
        bits = model_bits(bpp26, w * h, qp, idr);
        /* the codec path: no status registers, used QP, AU size in bits */
        t31_al_rc_status_from_regs(&st, NULL, 0u);
        st.bits = bits;
        st.qp = qp;
        filler = t31_al_rc_picture_start(&rc, &pic, &st, bits);
        t31_al_rc_update(&rc, &pic, &st, bits, 0u,
                         filler > 0 ? (uint32_t)(filler < 8 ? 8 : filler) * 8u : 0u);
        total += bits;
    }
    ratio = (double)total / ((double)kbps * 1000.0 * seconds);
    printf("%-28s %4ux%-4u %s %5u kbit/s -> %7.1f kbit/s (%.2f) last qp %d\n",
           name, w, h, cbr ? "CBR" : "VBR", kbps,
           (double)total / 1000.0 / seconds, ratio, rc.st.qp);
    if (lo < 0.5 && rc.st.qp != p.min_qp) {
        fprintf(stderr, "%s: qp %d did not reach the minimum %d\n", name,
                rc.st.qp, p.min_qp);
        return 1;
    }
    if (ratio < lo || ratio > hi) {
        fprintf(stderr, "%s: bitrate ratio %.3f outside %.2f..%.2f\n",
                name, ratio, lo, hi);
        return 1;
    }
    return 0;
}

/* CappedVBR (AL 4 -> core mode 8) and CappedQuality (AL 8 -> 9), as the
 * T40 (libimp 1.3.1) and T41 (1.2.6) channels run them: the VBR core with
 * the uMaxPSNR cap fed from the picture SSE.  Synthetic picture quality:
 * PSNR = 52 - 0.45 * QP dB.  core_mode 1 = plain VBR for comparison. */
static int run_capped(const char *name, uint32_t core_mode, uint32_t kbps,
                      uint32_t max_kbps, double bpp26, uint32_t cap_db,
                      double *out_kbps, double *out_psnr)
{
    const uint32_t fps = 20u, seconds = 30u, w = 1920u, h = 1080u;
    T31AlRcParam p;
    T31AlGopParam g;
    T31AlRc rc;
    uint64_t total = 0;
    double psnr_sum = 0.0;
    uint32_t n, frames = fps * seconds;

    fill_param(&p, &g, 0, kbps * 1000u, w, h, fps);
    p.mode = core_mode == 8u ? 4u : core_mode == 9u ? 8u : 2u;
    p.max_bitrate = max_kbps * 1000u;
    p.max_psnr_x100 = (uint16_t)(cap_db * 100u);
    memset(&rc, 0, sizeof(rc));
    if (t31_al_rc_init(&rc, core_mode, &p, &g) != 0) {
        fprintf(stderr, "%s: init failed\n", name);
        return 1;
    }
    for (n = 0; n < frames; ++n) {
        int idr = (n % g.length) == 0u;
        T31AlRcPicture pic;
        T31AlRcStatus st;
        int16_t qp;
        uint32_t bits;
        double psnr, sse;
        uint64_t isse;
        int32_t filler;

        memset(&pic, 0, sizeof(pic));
        pic.type = idr ? 2u : 1u;
        pic.flags = idr ? 3u : 2u;
        qp = t31_al_rc_picture_qp(&rc, &pic);
        bits = model_bits(bpp26, w * h, qp, idr);
        psnr = 52.0 - 0.45 * qp;
        sse = (double)w * h * 255.0 * 255.0 / pow(10.0, psnr / 10.0);
        isse = (uint64_t)sse;
        memset(&st, 0, sizeof(st));
        st.sse_lo = (uint32_t)isse;
        st.sse_hi = (uint32_t)(isse >> 32);
        st.bits = bits;
        st.qp = qp;
        filler = t31_al_rc_picture_start(&rc, &pic, &st, bits);
        t31_al_rc_update(&rc, &pic, &st, bits, 0u,
                         filler > 0 ? (uint32_t)(filler < 8 ? 8 : filler) * 8u : 0u);
        total += bits;
        psnr_sum += psnr;
    }
    *out_kbps = (double)total / 1000.0 / seconds;
    *out_psnr = psnr_sum / frames;
    printf("%-30s target %5u max %5u cap %2u dB -> %7.1f kbit/s psnr %.2f dB last qp %d\n",
           name, kbps, max_kbps, cap_db, *out_kbps, *out_psnr, rc.st.qp);
    return 0;
}

static int capped_tests(void)
{
    double vbr_k, vbr_p, cv_k, cv_p, cq_k, cq_p;
    int fail = 0;

    /* static scene: VBR walks the QP down to iMinQP; both capped modes stop
     * where the picture reaches the cap and save bit rate */
    fail |= run_capped("static vbr", 1u, 2000, 2666, 0.01, 40, &vbr_k, &vbr_p);
    fail |= run_capped("static cappedvbr", 8u, 2000, 2666, 0.01, 40, &cv_k, &cv_p);
    fail |= run_capped("static cappedquality", 9u, 2000, 2666, 0.01, 40, &cq_k, &cq_p);
    if (!fail && (cv_k > 0.8 * vbr_k || cq_k > 0.8 * vbr_k ||
                  cv_p < 39.0 || cq_p < 39.0 || cv_p > 41.5 || cq_p > 41.5)) {
        fprintf(stderr, "capped static: no saving or quality off cap\n");
        fail = 1;
    }
    /* detailed scene: CappedVBR stays at or below uMaxBitRate */
    fail |= run_capped("detailed cappedvbr", 8u, 2000, 2666, 0.3, 45, &cv_k, &cv_p);
    if (!fail && cv_k > 2666.0 * 1.10) {
        fprintf(stderr, "capped detailed: %.1f kbit/s above max\n", cv_k);
        fail = 1;
    }
    return fail;
}

int main(void)
{
    int fail = 0;

    /* detailed scene: the target is reachable inside the QP bounds */
    fail |= run("ch1 cbr 500", 1, 500, 640, 360, 0.25, 0.90, 1.10);
    fail |= run("ch1 cbr 1000", 1, 1000, 640, 360, 0.25, 0.90, 1.10);
    fail |= run("ch1 cbr 2000", 1, 2000, 640, 360, 0.25, 0.90, 1.10);
    fail |= run("ch0 cbr 4000", 1, 4000, 3840, 2160, 0.02, 0.90, 1.10);
    fail |= run("ch0 cbr 6000", 1, 6000, 3840, 2160, 0.02, 0.90, 1.10);
    fail |= run("ch1 vbr 1000", 0, 1000, 640, 360, 0.25, 0.80, 1.10);
    fail |= run("ch0 vbr 6000", 0, 6000, 3840, 2160, 0.02, 0.80, 1.10);
    /* nearly static scene: the controller must walk the QP down from the
     * start to iMinQP (15); the bitrate is then bounded by the QP range, as
     * with the vendor library.  The old open-loop QP for this geometry was
     * 38, about 6 % of this model's 2.5 Mbit/s at QP 15. */
    fail |= run("ch0 cbr 6000 static", 1, 6000, 3840, 2160, 0.004, 0.35, 1.10);
    fail |= capped_tests();
    if (fail)
        return 1;
    printf("t40 frame rc: PASS\n");
    return 0;
}
