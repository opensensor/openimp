/* v2replay: host replay of a motion v2 input trace (OPENIMP_MOTION_V2_DUMPFD) through the current algorithm.
 *   v2replay TRACE [key=value ...]     keys: features learn k mind supp_ms jump cells frames move sense
 * Output per analysed frame:  F <seq> <now_ms> <flags> <objs> <cells moving> <supp>   (only frames with objs or supp)
 * and a summary line.  Build: gcc -O2 -I../../include -I../../src/t31 v2replay.c ../../src/t31/openimp_ivs_move_v2.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "openimp_ivs_move_v2.h"

int main(int argc, char **argv)
{
    FILE *f;
    int32_t hdr[8], r2[3];
    int64_t rec[3];
    OpenIMP_IVS_MoveConfigEx cfg;
    IvsMoveV2 *v2;
    uint16_t *cur, *tex;
    int i, n = 0, nobj_frames = 0, sup_frames = 0, quiet = 0;

    if (argc < 2 || !(f = fopen(argv[1], "rb")) || fread(hdr, 4, 8, f) != 8 || hdr[0] != 0x31443256) {
        fprintf(stderr, "usage: v2replay TRACE [k=v]\n");
        return 2;
    }
    ivs_move_v2_config_env(&cfg);
    for (i = 2; i < argc; i++) {
        int v;
        if (!strcmp(argv[i], "quiet")) { quiet = 1; continue; }
        if (sscanf(argv[i], "features=%i", &v) == 1) cfg.features = v;
        else if (sscanf(argv[i], "learn=%d", &v) == 1) cfg.learn_shift = v;
        else if (sscanf(argv[i], "k=%d", &v) == 1) cfg.thresh_k = v;
        else if (sscanf(argv[i], "mind=%d", &v) == 1) cfg.min_delta = v;
        else if (sscanf(argv[i], "supp_ms=%d", &v) == 1) cfg.suppress_ms = v;
        else if (sscanf(argv[i], "jump=%d", &v) == 1) cfg.jump_pct = v;
        else if (sscanf(argv[i], "cells=%d", &v) == 1) cfg.min_cells = v;
        else if (sscanf(argv[i], "frames=%d", &v) == 1) cfg.min_frames = v;
        else if (sscanf(argv[i], "move=%d", &v) == 1) cfg.min_move = v;
    }
    v2 = ivs_move_v2_create(hdr[1], hdr[2], 1, &cfg);
    cur = malloc(hdr[6] * 2); tex = malloc(hdr[6] * 2);
    while (fread(rec, 8, 3, f) == 3 && fread(r2, 4, 3, f) == 3 &&
           fread(cur, 2, hdr[6], f) == (size_t)hdr[6] && fread(tex, 2, hdr[6], f) == (size_t)hdr[6]) {
        OpenIMP_IVS_MoveOutputEx ex;
        int roix0[1] = { 0 }, roiy0[1] = { 0 }, roix1[1] = { hdr[1] - 1 }, roiy1[1] = { hdr[2] - 1 };
        int mc = 0;
        ivs_move_v2_feed_cells(v2, cur, tex, rec[1]);
        ivs_move_v2_run(v2, r2[0], r2[1], rec[2], r2[2]);
        memset(&ex, 0, sizeof ex);
        ivs_move_v2_result(v2, &ex, 1, roix0, roiy0, roix1, roiy1, NULL);
        for (i = 0; i < (int)ex.obj_cnt; i++) mc += ex.obj[i].cells;
        n++;
        if (ex.obj_cnt) nobj_frames++;
        if (ex.suppress) sup_frames++;
        if ((ex.obj_cnt || ex.suppress || getenv("V2R_ALL")) && !quiet)
            printf("F %lld %lld flags=%x objs=%u cells=%d supp=%u gain=%d box=%d,%d,%d,%d str=%u\n", (long long)rec[0], (long long)rec[2],
                   ex.flags, ex.obj_cnt, mc, ex.suppress, r2[1], ex.obj[0].x0, ex.obj[0].y0, ex.obj[0].x1, ex.obj[0].y1, ex.obj[0].strength);
    }
    printf("SUM frames=%d with_objs=%d suppressed=%d\n", n, nobj_frames, sup_frames);
    return 0;
}
