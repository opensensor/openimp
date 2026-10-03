/* Host micro benchmark for the OSD line drawer: the stock lw x lw stamp
 * staircase (frozen copy in osd_draw_old.h) against the uniform band, on a
 * 1080p frame, full diagonal with lw 4, everything inside the frame. Not part
 * of `make check`; run it with `make -C tests/t31 bench`.
 *
 * Reported per line: the wall clock, the number of Y bytes written (the UV
 * plane is written once per chroma pair, i.e. half as often) and, for the
 * caller pattern in src/t31/openimp_t31_services.c, the dry pass plus the real
 * pass. */
#define _POSIX_C_SOURCE 200809L
#include "osd_draw_old.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BW 1920u
#define BH 1080u
#define BYTES ((size_t)BW * BH * 3 / 2)
#define ROWS ((size_t)BW * BH)
#define LINE_WORD 0xff102030u

static double now_us(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e6 + (double)ts.tv_nsec / 1e3;
}

static int count_y(const uint8_t *base)
{
    size_t i;
    int n = 0;

    for (i = 0; i < ROWS; i++)
        n += base[i] == 0x10;
    return n;
}

int main(int argc, char **argv)
{
    int iters = argc > 1 ? atoi(argv[1]) : 200;
    int x0 = 0, y0 = 0, x1 = (int)BW - 1, y1 = (int)BH - 1, lw = 4;
    uint8_t *mem = malloc(BYTES);
    struct osd_canvas c;
    volatile int sink = 0;
    double t0, old_1, old_2, new_1, new_2;
    int i, px_old, px_new, rows_old, rows_new;

    if (!mem) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    memset(mem, 0, BYTES);

    /* warm up + pixel counts */
    osd_canvas_init(&c, mem, BW, BH, BW, ROWS);
    osd_draw_line_old(&c, x0, y0, x1, y1, (uint32_t)lw, LINE_WORD, 0, 0);
    rows_old = c.ymax - c.ymin + 1;
    px_old = count_y(mem);
    memset(mem, 0, BYTES);
    osd_canvas_init(&c, mem, BW, BH, BW, ROWS);
    osd_draw_line(&c, x0, y0, x1, y1, (uint32_t)lw, LINE_WORD, 0, 0);
    rows_new = c.ymax - c.ymin + 1;
    px_new = count_y(mem);

    t0 = now_us();
    for (i = 0; i < iters; i++) {
        osd_canvas_init(&c, mem, BW, BH, BW, ROWS);
        osd_draw_line_old(&c, x0, y0, x1, y1, (uint32_t)lw, LINE_WORD, 0, 0);
        sink += c.ymax;
    }
    old_1 = (now_us() - t0) / iters;

    t0 = now_us();
    for (i = 0; i < iters; i++) {
        osd_canvas_init(&c, mem, BW, BH, BW, ROWS);
        c.dry = 1;
        osd_draw_line_old(&c, x0, y0, x1, y1, (uint32_t)lw, LINE_WORD, 0, 0);
        sink += c.ymax;
        c.dry = 0;
        osd_draw_line_old(&c, x0, y0, x1, y1, (uint32_t)lw, LINE_WORD, 0, 0);
        sink += c.ymax;
    }
    old_2 = (now_us() - t0) / iters;

    t0 = now_us();
    for (i = 0; i < iters; i++) {
        osd_canvas_init(&c, mem, BW, BH, BW, ROWS);
        osd_draw_line(&c, x0, y0, x1, y1, (uint32_t)lw, LINE_WORD, 0, 0);
        sink += c.ymax;
    }
    new_1 = (now_us() - t0) / iters;

    t0 = now_us();
    for (i = 0; i < iters; i++) {
        osd_canvas_init(&c, mem, BW, BH, BW, ROWS);
        c.dry = 1;
        osd_draw_line(&c, x0, y0, x1, y1, (uint32_t)lw, LINE_WORD, 0, 0);
        sink += c.ymax;
        c.dry = 0;
        osd_draw_line(&c, x0, y0, x1, y1, (uint32_t)lw, LINE_WORD, 0, 0);
        sink += c.ymax;
    }
    new_2 = (now_us() - t0) / iters;

    printf("%ux%u diagonal (%d,%d)-(%d,%d) lw %d, %d iterations per run\n",
           BW, BH, x0, y0, x1, y1, lw, iters);
    printf("  old staircase  one pass : %8.1f us  %7d Y bytes  %d rows\n",
           old_1, px_old, rows_old);
    printf("  new band       one pass : %8.1f us  %7d Y bytes  %d rows\n",
           new_1, px_new, rows_new);
    printf("  old staircase  dry+draw : %8.1f us\n", old_2);
    printf("  new band       dry+draw : %8.1f us\n", new_2);
    printf("  ratio band/staircase    : %.2f one pass, %.2f dry+draw\n",
           new_1 / old_1, new_2 / old_2);
    printf("  Y bytes                 : %.2f x of the stock staircase\n",
           (double)px_new / (double)px_old);
    free(mem);
    /* the volatile sink also doubles as a sanity check: a non-zero sum means
     * the timed calls really ran */
    return sink == 0;
}
