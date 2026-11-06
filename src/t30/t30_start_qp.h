#ifndef OPENIMP_T30_START_QP_H
#define OPENIMP_T30_START_QP_H

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * OPENIMP_T30_START_QP (OpenIMP extra, ON by default since #216 b44):
 * the QP the T30 OEM rate controller (eprc_t30.c) starts its first IDR with,
 * in every rate mode (CBR, VBR, SMART; FIXQP does not use it).
 *   unset, "", "auto"                       -> from bits per pixel (default)
 *   "vendor", anything else invalid         -> -1 (the vendor default QP 26)
 *   integer 10..51                          -> that QP
 *
 * auto: piecewise linear in log2(bpp), bpp = bitrate / (width * height * fps)
 * (VBR/SMART: the max bitrate), one knot table per picture class: the native
 * 2048x1536 picture converges about 6 QP higher than the ISP-scaled ones
 * (T30X SC4236, sensor_resolution=300).  Knots = converged IDR QPs of the
 * T30X ("eprc stats" lines after the first 2 s; P QPs in brackets):
 *   large (> 1920x1088 pixels):
 *     2048x1536 25 fps 1000 kbit/s bpp 0.0127  IDR 42 (P 43)      b45
 *     2048x1536 25 fps 2000        bpp 0.0254  IDR 41 (P 41)      b39, b44, b45
 *     2048x1536 15 fps 2000        bpp 0.0424  IDR 38-40 (P 40-41) b42, b44, b45
 *                                              -> map 40
 *     2048x1536 25 fps 4000        bpp 0.0509  IDR 40 (P 40)      b45 -> knot 39
 *     VBR 2048x1536 25 fps max 2000 started at 41: IDR 40 (P 42-43) b45
 *   small (<= 1920x1088 pixels):
 *     1920x1080 25 fps  512 kbit/s bpp 0.0099  IDR 33 (P 35)      b45 -> 34
 *     1920x1080 25 fps 2000        bpp 0.0386  IDR 32 (P 32)      b43, b44, b45
 *     1920x1080 15 fps 2000        bpp 0.0643  IDR 28-32 (P 30-31) b43, b45 -> 29
 *     640x360   15 fps  256        bpp 0.0741  IDR 27-28 (P 28-29) b45 (map 28)
 * The converged QP depends little on the bitrate at these points (the
 * controller meets the rate with the P pictures), hence the flat curves.
 * Below the first knot the QP stays at it (a floor: small 34, large 42).
 * Top knot 26 (vendor): small class from 0.0868 (1280x720 25 fps 2000,
 * converges at 23-25); large class from 0.1017, UNVERIFIED, as is the
 * large-class segment above 0.0509 and any sensor other than the SC4236.
 * Result clamp 24..46; the controller clamps to the application's QP range.
 */
#define T30_START_QP_VENDOR (-1)
#define T30_START_QP_LARGE_PIXELS (1920u * 1088u)

static inline int t30_start_qp(const char *env, uint32_t width,
                               uint32_t height, uint32_t fps_num,
                               uint32_t fps_den, uint32_t bitrate_kbps)
{
    static const double knot_bpp[2][4] = {
        { 0.0099, 0.0386, 0.0643, 0.0868 },     /* small */
        { 0.0127, 0.0254, 0.0509, 0.1017 },     /* large */
    };
    static const double knot_qp[2][4] = {
        { 34.0, 32.0, 29.0, 26.0 },
        { 42.0, 41.0, 39.0, 26.0 },
    };
    const double *kb, *kq;
    double bpp, x, qp;
    char *end;
    long v;
    int i;

    if (env && !strcmp(env, "vendor"))
        return T30_START_QP_VENDOR;
    if (!env || !env[0] || !strcmp(env, "auto")) {
        if (!width || !height || !fps_num || !fps_den || !bitrate_kbps)
            return T30_START_QP_VENDOR;
        /* i is the picture class here, the knot segment below */
        i = (uint64_t)width * height > T30_START_QP_LARGE_PIXELS;
        kb = knot_bpp[i];
        kq = knot_qp[i];
        bpp = (double)bitrate_kbps * 1000.0 * fps_den /
              ((double)width * height * fps_num);
        if (bpp >= kb[3])
            return 26;
        if (bpp <= kb[0])
            qp = kq[0];                 /* floor, no slope below kb[0] */
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
