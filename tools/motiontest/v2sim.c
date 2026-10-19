/* v2sim: host sweep of motion v2 on the mode 3 test pattern (objects of size/contrast/speed in all grid cells).
 *   v2sim [fps=20] [skip=5] [noise=1] [key=value as v2replay] [quiet]
 * Every object is shown 2 s (speed 0: appears, 40/200 px/s: bounces), 3 s of empty picture between;
 * a hit = v2 reports an object in the 3x3 cells around the object while it is shown (+1 s).
 * Prints hits/events per (size, contrast, speed) and the false reports in the empty picture. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../../include/openimp_testpattern.h"
#include "../../src/openimp_testpattern_gen.h"
#include "openimp_ivs_move_v2.h"

#define W 640
#define H 360
int main(int argc, char **argv)
{
    static const int sizes[] = { 16, 24, 32, 48, 56 }, cons[] = { 16, 32, 48, 96 }, speeds[] = { 0, 40, 200 };
    OpenIMP_IVS_MoveConfigEx cfg;
    IvsMoveV2 *v2;
    static uint8_t y[W * H], tmp[W * H / 2];
    int fps = 20, skip = 5, noise = 1, i, sz, co, sp, cell, quiet = 0, frame = 0, falses = 0, empty = 0;
    int hit[5][4][3] = {{{0}}}, tot[5][4][3] = {{{0}}};
    uint32_t rng = 777;

    ivs_move_v2_config_env(&cfg);
    for (i = 1; i < argc; i++) {
        int v;
        if (!strcmp(argv[i], "quiet")) quiet = 1;
        else if (sscanf(argv[i], "fps=%d", &v) == 1) fps = v;
        else if (sscanf(argv[i], "skip=%d", &v) == 1) skip = v;
        else if (sscanf(argv[i], "noise=%d", &v) == 1) noise = v;
        else if (sscanf(argv[i], "features=%i", &v) == 1) cfg.features = v;
        else if (sscanf(argv[i], "learn=%d", &v) == 1) cfg.learn_shift = v;
        else if (sscanf(argv[i], "k=%d", &v) == 1) cfg.thresh_k = v;
        else if (sscanf(argv[i], "mind=%d", &v) == 1) cfg.min_delta = v;
        else if (sscanf(argv[i], "cells=%d", &v) == 1) cfg.min_cells = v;
        else if (sscanf(argv[i], "frames=%d", &v) == 1) cfg.min_frames = v;
        else if (sscanf(argv[i], "move=%d", &v) == 1) cfg.min_move = v;
    }
    v2 = ivs_move_v2_create(W, H, skip + 1, &cfg);
    {
        /* warm up 6 s of empty picture */
        int n = 6 * fps;
        for (i = 0; i < n; i++, frame++) {
            memset(y, 110, sizeof y);
            for (int k = 0; k < W * H; k++) { rng = rng * 1664525u + 1013904223u; y[k] = 110 + (int)((rng >> 24) % (2u * noise + 1u)) - noise; }
            if (ivs_move_v2_next_frame(v2)) { ivs_move_v2_feed(v2, y, W, frame * 1000000LL / fps); }
            ivs_move_v2_run(v2, -1, 0, frame * 1000LL / fps, 2);
        }
    }
    for (sp = 0; sp < 3; sp++) for (sz = 0; sz < 5; sz++) for (co = 0; co < 4; co++) for (cell = 0; cell < 48; cell += 5) {
        int cx = cell % 8, cy = cell / 8, ph, nfr = 5 * fps, hitf = 0, falsef = 0;
        for (ph = 0; ph < nfr; ph++, frame++) {
            double ts = (double)ph / fps;
            int shown = ts < 2.0;
            OpenIMP_IVS_MoveOutputEx ex;
            int rx[1] = { 0 }, ry[1] = { 0 }, rx1[1] = { W - 1 }, ry1[1] = { H - 1 };
            for (int k = 0; k < W * H; k++) { rng = rng * 1664525u + 1013904223u; y[k] = 110 + (int)((rng >> 24) % (2u * noise + 1u)) - noise; }
            if (shown) openimp_tp_object(y, W, H, W, cx, cy, sizes[sz], cons[co], speeds[sp], ts);
            if (ivs_move_v2_next_frame(v2)) ivs_move_v2_feed(v2, y, W, frame * 1000000LL / fps);
            ivs_move_v2_run(v2, -1, 0, frame * 1000LL / fps, 2);
            memset(&ex, 0, sizeof ex);
            ivs_move_v2_result(v2, &ex, 1, rx, ry, rx1, ry1, NULL);
            if (ex.obj_cnt) {
                int near = 0;
                for (i = 0; i < (int)ex.obj_cnt; i++) {
                    int ox = ((int)ex.obj[i].x0 + (int)ex.obj[i].x1) / 2, oy = ((int)ex.obj[i].y0 + (int)ex.obj[i].y1) / 2;
                    if (abs(ox - (cx * 80 + 40)) <= 160 && abs(oy - (cy * 60 + 30)) <= 120) near = 1;
                }
                if (ts < 3.0 && near) hitf = 1;
                else if (ts >= 3.0 && ts > 3.0 + 0.0) falsef++;
                else if (!near) falsef++;
            }
        }
        (void)tmp;
        tot[sz][co][sp]++; hit[sz][co][sp] += hitf; falses += falsef; empty += nfr * 2 / 5;
    }
    printf("v2sim fps %d skip %d noise %d: false reports %d (frames after objects, in 5 s windows)\n", fps, skip, noise, falses);
    if (!quiet)
        for (sp = 0; sp < 3; sp++) {
            printf("speed %3d:", speeds[sp]);
            for (sz = 0; sz < 5; sz++) { printf("  size %2d [", sizes[sz]); for (co = 0; co < 4; co++) printf(" c%d %d/%d", cons[co], hit[sz][co][sp], tot[sz][co][sp]); printf(" ]"); }
            printf("\n");
        }
    return 0;
}
