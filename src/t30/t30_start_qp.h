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
 * auto: piecewise linear in log2(bpp), bpp = bitrate / (width * height * fps),
 * with one knot table per picture class, because the same bpp converges to
 * different QPs: the native 2048x1536 picture is about 6 QP dearer than the
 * scaled 1920x1080 one (T30X SC4236, sensor_resolution=300).  Knots are the
 * converged CBR 2000 kbit/s IDR QPs measured on the T30X ("eprc stats" lines
 * after the first 2 s; audit/t30/b39, b42, b43):
 *   large (> 1920x1088 pixels):
 *     2048x1536 25 fps  bpp 0.0254  IDR 41 (P 41-42)            b39
 *     2048x1536 15 fps  bpp 0.0424  IDR 38 (P 40-41)            b39, b42
 *   small (<= 1920x1088 pixels):
 *     1920x1080 25 fps  bpp 0.0386  IDR 32 (P 32; b39: IDR 30)  b43
 *     1920x1080 15 fps  bpp 0.0643  IDR 28 (P 31)               b43
 * From bpp 0.0868 (1280x720 25 fps 2000 kbit/s) upwards both classes keep
 * the vendor 26 (720p converges at 23-25, so 26 is no burst).  UNVERIFIED:
 * the knot below the lowest measured bpp of each class (6 QP per halving of
 * bpp, the H.264 rule of thumb), the large-class segment between 0.0424 and
 * 0.0868 and any sensor other than the SC4236.  Result clamp 24..46; the
 * controller clamps to the application's QP range afterwards.
 */
#define T30_START_QP_VENDOR (-1)
#define T30_START_QP_LARGE_PIXELS (1920u * 1088u)

static inline int t30_start_qp(const char *env, uint32_t width,
                               uint32_t height, uint32_t fps_num,
                               uint32_t fps_den, uint32_t bitrate_kbps)
{
    static const double knot_bpp[2][4] = {
        { 0.0193, 0.0386, 0.0643, 0.0868 },     /* small */
        { 0.0127, 0.0254, 0.0424, 0.0868 },     /* large */
    };
    static const double knot_qp[2][4] = {
        { 38.0, 32.0, 28.0, 26.0 },
        { 47.0, 41.0, 38.0, 26.0 },
    };
    const double *kb, *kq;
    double bpp, x, qp;
    char *end;
    long v;
    int i;

    if (!env || !env[0] || !strcmp(env, "vendor"))
        return T30_START_QP_VENDOR;
    if (!strcmp(env, "auto")) {
        if (!width || !height || !fps_num || !fps_den || !bitrate_kbps)
            return T30_START_QP_VENDOR;
        i = (uint64_t)width * height > T30_START_QP_LARGE_PIXELS;
        kb = knot_bpp[i];
        kq = knot_qp[i];
        bpp = (double)bitrate_kbps * 1000.0 * fps_den /
              ((double)width * height * fps_num);
        if (bpp >= kb[3])
            return 26;
        if (bpp <= kb[0])
            qp = kq[0];
        else {
            x = log2(bpp);
            for (i = 0; i < 3; i++)
                if (bpp <= kb[i + 1])
                    break;
            qp = kq[i] + (kq[i + 1] - kq[i]) * (x - log2(kb[i])) /
                 (log2(kb[i + 1]) - log2(kb[i]));
        }
        v = lround(qp);
        return (int)(v < 24 ? 24 : v > 46 ? 46 : v);
    }
    v = strtol(env, &end, 10);
    if (*end || v < 10 || v > 51)
        return T30_START_QP_VENDOR;
    return (int)v;
}

#endif
