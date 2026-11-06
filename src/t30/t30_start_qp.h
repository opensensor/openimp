#ifndef OPENIMP_T30_START_QP_H
#define OPENIMP_T30_START_QP_H

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * OPENIMP_T30_START_QP (OpenIMP extra, opt-in, NOT the default; #216):
 * the QP the T30 OEM rate controller (eprc_t30.c) starts its first IDR with.
 *   unset, "vendor", anything else invalid -> -1 (the vendor default QP 26)
 *   integer 10..51                          -> that QP
 *   "auto"                                  -> from bits per pixel
 *
 * auto: piecewise linear in log2(bpp), bpp = bitrate / (width * height * fps).
 * Knots are the converged CBR 2000 kbit/s QPs of the T30X run b39
 * (audit/t30/b39/e30-*.txt, "eprc stats" lines after the first 2 s):
 *   2048x1536 25 fps  bpp 0.0254  P qp 41-42, IDR 41
 *   2048x1536 15 fps  bpp 0.0424  P qp 40-41, IDR 38
 *   1920x1080 25 fps  bpp 0.0386  P qp 32,    IDR 30
 *   640x360   25 fps  bpp 0.347   P qp 25,    IDR 23
 * The two points near 0.04 differ by scene (UNVERIFIED which is typical),
 * so the knot there is a compromise (36, leaning to the larger frame).  From bpp 0.0868 (1280x720, 25 fps,
 * 2000 kbit/s) upwards the vendor value 26 is kept.  The knot below 0.0254
 * is an extrapolation (UNVERIFIED, no measurement).  Result clamp 24..46.
 */
#define T30_START_QP_VENDOR (-1)

static inline int t30_start_qp(const char *env, uint32_t width,
                               uint32_t height, uint32_t fps_num,
                               uint32_t fps_den, uint32_t bitrate_kbps)
{
    static const double knot_bpp[] = { 0.0127, 0.0254, 0.0400, 0.0868 };
    static const double knot_qp[]  = { 46.0, 41.0, 36.0, 26.0 };
    double bpp, x, qp;
    char *end;
    long v;
    int i;

    if (!env || !env[0] || !strcmp(env, "vendor"))
        return T30_START_QP_VENDOR;
    if (!strcmp(env, "auto")) {
        if (!width || !height || !fps_num || !fps_den || !bitrate_kbps)
            return T30_START_QP_VENDOR;
        bpp = (double)bitrate_kbps * 1000.0 * fps_den /
              ((double)width * height * fps_num);
        if (bpp >= knot_bpp[3])
            return 26;
        if (bpp <= knot_bpp[0])
            return 46;
        x = log2(bpp);
        for (i = 0; i < 3; i++)
            if (bpp <= knot_bpp[i + 1])
                break;
        qp = knot_qp[i] + (knot_qp[i + 1] - knot_qp[i]) *
             (x - log2(knot_bpp[i])) /
             (log2(knot_bpp[i + 1]) - log2(knot_bpp[i]));
        v = lround(qp);
        return (int)(v < 24 ? 24 : v > 46 ? 46 : v);
    }
    v = strtol(env, &end, 10);
    if (*end || v < 10 || v > 51)
        return T30_START_QP_VENDOR;
    return (int)v;
}

#endif
