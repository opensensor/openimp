/* NV12 90/270 rotation benchmark for the device (and the host): times
 * nv12_rotate() per frame with the C kernels (plain 32x32 tiles and the
 * cache-line banded one) and, only with --mxu, the MXU2 path, plus a
 * memcpy of the same bytes (the cost of the copy back over the capture
 * buffer). Source frames cycle over several buffers larger than the L2, so
 * every frame starts cold as a freshly captured one does. On the device
 * every rotated frame is also compared between the kernels (bit-exact).
 *
 * NEVER pass --mxu on a T23 (or any core without a verified MXU2): the T23
 * kernel oopses on a COP2 instruction and the watchdog resets the board.
 * The binary refuses --mxu when /proc/cpuinfo names the T23 ("Pike").
 *
 * usage: nv12_rotate_bench [--mxu] [--exp] [-n frames] [WxH ...]
 *   --exp adds the banded kernel with one-line bands for unaligned rows
 *   build (device): <toolchain>-gcc -O2 -march=mips32r2 -DOPENIMP_MXU2_MODEL
 *   -Isrc tests/t31/nv12_rotate_bench.c src/framesource/nv12_rotate.c
 *   src/core/openimp_mxu2.c -lpthread   (drop -DOPENIMP_MXU2_MODEL for a
 *   real MXU2 build, T10/T20/T30/T31 only)
 */
#include "framesource/nv12_rotate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NBUF 4

static double now_us(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

static int is_t23(void)
{
    char line[256];
    FILE *f = fopen("/proc/cpuinfo", "r");
    int hit = 0;

    if (!f)
        return 0;
    while (fgets(line, sizeof(line), f))
        if (strstr(line, "system type") && strstr(line, "Pike"))
            hit = 1;
    fclose(f);
    return hit;
}

int main(int argc, char **argv)
{
    static const uint32_t def[][2] = {
        { 640, 352 }, { 704, 576 }, { 960, 544 }, { 1280, 720 },
        { 1920, 1080 },
    };
    uint32_t dims[32][2];
    int ndims = 0, frames = 40, mxu = 0, exp = 0, i, k, m, f;
    uint8_t *src[NBUF], *dst[4][2];
    size_t maxsz = 0;

    /* C first, before anything could run the MXU2 selection */
    nv12_rotate_set_impl(0);
    for (i = 1; i < argc; i++) {
        unsigned w, h;

        if (!strcmp(argv[i], "--mxu"))
            mxu = 1;
        else if (!strcmp(argv[i], "--exp"))
            exp = 1;
        else if (!strcmp(argv[i], "-n") && i + 1 < argc)
            frames = atoi(argv[++i]);
        else if (sscanf(argv[i], "%ux%u", &w, &h) == 2 && ndims < 32 &&
                 w && h && w <= 4096 && h <= 4096 && !((w | h) & 1)) {
            dims[ndims][0] = w;
            dims[ndims][1] = h;
            ndims++;
        }
    }
    if (mxu && is_t23()) {
        fprintf(stderr, "refusing --mxu on a T23 (no MXU: COP2 resets the board)\n");
        return 2;
    }
    if (!ndims) {
        for (i = 0; i < (int)(sizeof(def) / sizeof(def[0])); i++) {
            dims[i][0] = def[i][0];
            dims[i][1] = def[i][1];
        }
        ndims = i;
    }
    for (i = 0; i < ndims; i++) {
        size_t s = nv12_rotate_frame_size(dims[i][0], dims[i][1]);

        if (s > maxsz)
            maxsz = s;
    }
    for (i = 0; i < NBUF; i++) {
        void *p = NULL;

        if (posix_memalign(&p, 64, maxsz))
            return 1;
        src[i] = p;
        for (size_t j = 0; j < maxsz; j++)
            src[i][j] = (uint8_t)(j * 2654435761u >> 13 ^ (unsigned)i);
    }
    for (k = 0; k < 4; k++)
        for (i = 0; i < 2; i++) {
            void *p = NULL;

            if (posix_memalign(&p, 64, maxsz))
                return 1;
            dst[k][i] = p;
            memset(p, 0, maxsz);
        }
    if (mxu)
        printf("MXU2 selection: %d\n", nv12_rotate_set_impl(-1));

    printf("%-10s %-4s %-7s %9s %9s %8s\n", "size", "mode", "kernel",
           "avg_us", "min_us", "MB/s");
    for (i = 0; i < ndims; i++) {
        uint32_t w = dims[i][0], h = dims[i][1];
        size_t sz = nv12_rotate_frame_size(w, h);

        for (m = NV12_ROT_90_CCW; m <= NV12_ROT_90_CW; m++) {
            int mism = 0;

            for (k = 0; k < (mxu ? 3 : 2) + exp; k++) {
                double sum = 0, mn = 1e18;
                /* 0 tiles, 1 banded, 2 mxu2, 3 banded one-line bands */
                int kk = exp && k == (mxu ? 3 : 2) ? 3 : k;

                nv12_rotate_set_impl(kk == 2);
                nv12_rotate_set_c_kernel(kk == 3 ? 2 : kk == 1);
                for (f = -3; f < frames; f++) {
                    const uint8_t *s = src[(f + 8) % NBUF];
                    uint8_t *d = dst[k][(f + 8) & 1];
                    double t0 = now_us(), dt;

                    nv12_rotate(s, d, w, h, m);
                    dt = now_us() - t0;
                    if (f >= 0) {
                        sum += dt;
                        if (dt < mn)
                            mn = dt;
                    }
                    if (k && f == frames - 1 &&
                        memcmp(d, dst[0][(f + 8) & 1], sz) != 0)
                        mism = 1;
                }
                printf("%4ux%-5u %-4s %-7s %9.0f %9.0f %8.1f\n", w, h,
                       m == NV12_ROT_90_CW ? "cw" : "ccw",
                       kk == 0 ? "tiles" : kk == 1 ? "banded" : kk == 3 ? "band32" : "mxu2",
                       sum / frames, mn, sz / (sum / frames));
            }
            if (mism)
                printf("MISMATCH %ux%u mode %d\n", w, h, m);
        }
        /* the planes alone (C kernels): luma, then CbCr */
        for (k = 0; k < 2; k++) {
            int p;

            nv12_rotate_set_impl(0);
            nv12_rotate_set_c_kernel(k);
            for (p = 0; p < 2; p++) {
                double sum = 0;
                uint32_t sp = nv12_rotate_pitch(w), dp = nv12_rotate_pitch(h);
                size_t so = p ? (size_t)sp * nv12_rotate_lines(h) : 0;
                size_t dof = p ? (size_t)dp * nv12_rotate_lines(w) : 0;

                for (f = -3; f < frames; f++) {
                    double t0 = now_us(), dt;

                    nv12_rotate_plane(src[(f + 8) % NBUF] + so, sp,
                                      dst[k][(f + 8) & 1] + dof, dp,
                                      p ? w / 2u : w, p ? h / 2u : h,
                                      p ? 2 : 1, NV12_ROT_90_CW);
                    dt = now_us() - t0;
                    if (f >= 0)
                        sum += dt;
                }
                printf("%4ux%-5u %-4s %-7s %9.0f %9s %8s\n", w, h, "cw",
                       p ? (k ? "bandUV" : "tileUV") : (k ? "bandY" : "tileY"),
                       sum / frames, "-", "-");
            }
        }
        {
            double sum = 0, mn = 1e18;

            for (f = -3; f < frames; f++) {
                double t0 = now_us(), dt;

                memcpy(dst[0][(f + 8) & 1], src[(f + 8) % NBUF], sz);
                dt = now_us() - t0;
                if (f >= 0) {
                    sum += dt;
                    if (dt < mn)
                        mn = dt;
                }
            }
            printf("%4ux%-5u %-4s %-7s %9.0f %9.0f %8.1f\n", w, h, "-",
                   "memcpy", sum / frames, mn, sz / (sum / frames));
        }
    }
    nv12_rotate_set_impl(0);
    nv12_rotate_set_c_kernel(1);
    return 0;
}
