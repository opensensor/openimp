/*
 * t31_ivs_compare - compare OpenIMP's IVS move / base move algorithms with
 * a stock T31 1.1.6 libimp.so on identical synthetic frame sequences.
 *
 * The stock library is loaded with dlopen() and its algorithm entry points
 * (imp_alloc_move, imp_move_preprocess, imp_move_process and the base-move
 * counterparts) are called directly, one frame after the other, so there is
 * no FrameSource, no threading and no camera image involved. The OpenIMP
 * side is src/t31/openimp_t31_ivs_move.c compiled into this tool.
 *
 * The stock library picks its MXU2 SIMD code when /proc/cpuinfo lists
 * "mxu_v2" and /tmp/closesimd does not exist; otherwise its scalar code.
 * Run once without and once with /tmp/closesimd to compare both paths.
 *
 * Known difference: when the ROI bounding box starts below row 0 and
 * reaches the bottom row, the stock move reads one row past its buffer;
 * OpenIMP replicates the last row. Such configurations are counted
 * separately ("oob") and do not fail the run.
 *
 * The stock scalar base move prints "total_count" lines on stdout, so the
 * report goes to stderr and stdout is sent to /dev/null.
 *
 * Build (uclibc thingino toolchain, the stock lib is uclibc):
 *   mipsel-linux-gcc -O2 -march=mips32r2 -rdynamic -Isrc/t31 -o t31_ivs_compare \
 *       tools/t31_ivs_compare.c src/t31/openimp_t31_ivs_move.c -ldl
 * Run: ./t31_ivs_compare /tmp/libimp-stock.so
 * Exit status 0 when every non-oob result is identical.
 *
 * T23: add -DPLATFORM_T23 (T23 1.3.0 parameter and IMPFrameInfo layout)
 * and run it against the stock T23 1.3.0 libimp, which an OpenIMP T23
 * image keeps for the Helix worker:
 *   LD_LIBRARY_PATH=/opt/openimp-t23 ./t23_ivs_compare /opt/openimp-t23/libimp.so
 * The T23 library has only the scalar path (no MXU2 branch, see
 * openimp_t31_ivs.c), so /tmp/closesimd makes no difference there.
 */
#include <dlfcn.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "openimp_t31_ivs_move.h"

/* Imported by the stock libimp from the process (normally libsysutils). */
int IMP_Log_Get_Option(void) { return 0; }
int imp_log_fun(int level, int option, int count, ...)
{
    (void)level; (void)option; (void)count;
    return 0;
}

/* Stock base-move ring slot: public output plus the frame timestamp. */
typedef struct {
    int ret;
    uint8_t *data;
    int datalen;
    int reserved;
    int64_t timeStamp;
} StockBaseSlot;

static void *(*stock_alloc_move)(IMP_IVS_MoveParam *, void (*)(void *));
static int (*stock_move_pre)(void *, T31IVSFrameInfo *);
static int (*stock_move_proc)(void *, T31IVSFrameInfo *, IMP_IVS_MoveOutput *);
static void *(*stock_alloc_base)(IMP_IVS_BaseMoveParam *, void (*)(void *));
static int (*stock_base_pre)(void *, T31IVSFrameInfo *);
static int (*stock_base_proc)(void *, T31IVSFrameInfo *, StockBaseSlot *);

static void no_free(void *p) { (void)p; }

static uint32_t rng = 12345;
static uint32_t rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 8; }
static int sat8(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

/* Gradient with a moving inverted box, several noise / lighting modes. */
static void make_frame(uint8_t *f, int w, int h, int t, int mode)
{
    int x, y;

    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            int v = (x * 3 + y * 2) & 0xff;
            int bx = (t * 13) % w, by = (t * 7) % h;

            if (mode == 1)
                v = (v + (int)(rnd() % 7)) & 0xff;
            else if (mode == 2)
                v = rnd() & 0xff;
            else if (mode == 3)
                v = sat8(v / 2 + ((t / 5) % 3) * 60);
            if (mode != 2 && x >= bx && x < bx + w / 6 && y >= by && y < by + h / 5)
                v = 255 - v;
            f[y * w + x] = (uint8_t)v;
        }
    memset(f + w * h, 128, (size_t)w * h / 2);
}

static void frame_info(T31IVSFrameInfo *fi, uint8_t *f, int w, int h, int t)
{
    memset(fi, 0, sizeof(*fi));
    fi->width = (uint32_t)w;
    fi->height = (uint32_t)h;
    fi->pixfmt = T31_IVS_PIX_NV12;
    fi->size = (uint32_t)(w * h * 3 / 2);
    fi->virAddr = (uint32_t)(uintptr_t)f;
    fi->timeStamp = 1000000 + (int64_t)t * 40000;
}

static long move_same, move_diff, move_oob_diff, move_results;

static void compare_move(int w, int h, int skip, int layout, int mode)
{
    IMP_IVS_MoveParam p;
    T31IvsMove *mine;
    void *stock;
    uint8_t *f = malloc((size_t)w * h * 3 / 2);
    int rows = 3, cols = 4, n = 0, r, c, t, i;
    int oob = layout == 1;          /* box starts at row > 0, touches bottom */

    memset(&p, 0, sizeof(p));
    p.skipFrameCnt = skip;
    p.frameInfo.width = (uint32_t)w;
    p.frameInfo.height = (uint32_t)h;
    for (r = 0; r < rows; r++)
        for (c = 0; c < cols; c++) {
            if (layout == 1 && r == 0)
                continue;
            if (layout == 2 && (r == 0 || r == rows - 1))
                continue;
            p.sense[n] = (r * cols + c) % 5;
            p.roiRect[n].p0.x = c * w / cols;
            p.roiRect[n].p0.y = r * h / rows;
            p.roiRect[n].p1.x = (c + 1) * w / cols - 1;
            p.roiRect[n].p1.y = (r + 1) * h / rows - 1;
            n++;
        }
    p.roiRectCnt = n;
    stock = stock_alloc_move(&p, no_free);
    mine = t31_ivs_move_create(&p);
    if (!stock || !mine || !f) {
        fprintf(stderr, "move %dx%d: allocation failed\n", w, h);
        move_diff++;
        free(f);
        t31_ivs_move_destroy(mine);
        return;
    }
    for (t = 0; t < 40; t++) {
        IMP_IVS_MoveOutput so, mo;
        T31IVSFrameInfo fi;
        int sr, mr, bad;

        make_frame(f, w, h, t, mode);
        frame_info(&fi, f, w, h, t);
        memset(&so, 0x55, sizeof(so));
        memset(&mo, 0x55, sizeof(mo));
        stock_move_pre(stock, &fi);
        sr = stock_move_proc(stock, &fi, &so);
        t31_ivs_move_feed(mine, t31_ivs_move_needs_luma(mine) ? f : NULL,
                          (uint32_t)w);
        mr = t31_ivs_move_run(mine, mo.retRoi);
        bad = sr != mr;
        if (sr == 0) {
            move_results++;
            for (i = 0; i < n; i++)
                bad |= so.retRoi[i] != mo.retRoi[i];
        }
        if (!bad) {
            move_same++;
        } else if (oob) {
            move_oob_diff++;
        } else {
            if (++move_diff <= 10) {
                fprintf(stderr, "MOVE DIFF %dx%d skip=%d layout=%d mode=%d t=%d ret=%d/%d:",
                        w, h, skip, layout, mode, t, sr, mr);
                for (i = 0; i < n; i++)
                    fprintf(stderr, " %d/%d", so.retRoi[i], mo.retRoi[i]);
                fprintf(stderr, "\n");
            }
        }
    }
    /* The stock handle is not freed (its destructor is internal). */
    t31_ivs_move_destroy(mine);
    free(f);
}

static long base_same, base_diff, base_results;

static void compare_base(int w, int h, int skip, int refnum, int sense, int mode)
{
    IMP_IVS_BaseMoveParam p;
    T31IvsBaseMove *mine;
    void *stock;
    int len = (w / 8) * (h / 8), t;
    uint8_t *f = malloc((size_t)w * h * 3 / 2);
    uint8_t *sd = calloc((size_t)len + 1, 1), *md = calloc((size_t)len + 1, 1);

    memset(&p, 0, sizeof(p));
    p.skipFrameCnt = skip;
    p.referenceNum = refnum;
    p.sense = sense;
    p.frameInfo.width = (uint32_t)w;
    p.frameInfo.height = (uint32_t)h;
    stock = stock_alloc_base(&p, no_free);
    mine = t31_ivs_base_move_create(&p);
    if (!stock || !mine || !f || !sd || !md) {
        fprintf(stderr, "base %dx%d: allocation failed\n", w, h);
        base_diff++;
        goto out;
    }
    for (t = 0; t < 30; t++) {
        StockBaseSlot so;
        T31IVSFrameInfo fi;
        int sr, mret = 0, bad;

        make_frame(f, w, h, t, mode);
        frame_info(&fi, f, w, h, t);
        memset(&so, 0, sizeof(so));
        so.data = sd;
        stock_base_pre(stock, &fi);
        sr = stock_base_proc(stock, &fi, &so);
        t31_ivs_base_move_feed(mine, t31_ivs_base_move_needs_luma(mine) ? f : NULL,
                               (uint32_t)w);
        t31_ivs_base_move_run(mine, md, &mret);
        bad = sr != 0 || so.ret != mret || so.datalen != len;
        if (mret) {
            base_results++;
            bad |= memcmp(sd, md, (size_t)len) != 0;
        }
        if (!bad) {
            base_same++;
        } else if (++base_diff <= 10) {
            fprintf(stderr, "BASE DIFF %dx%d skip=%d ref=%d sense=%d mode=%d t=%d ret=%d/%d\n",
                    w, h, skip, refnum, sense, mode, t, so.ret, mret);
        }
    }
out:
    t31_ivs_base_move_destroy(mine);
    free(f);
    free(sd);
    free(md);
}

static int simd_expected(void)
{
    char line[256];
    int mxu = 0;
    FILE *cpu = fopen("/proc/cpuinfo", "r");

    if (cpu) {
        while (fgets(line, sizeof(line), cpu))
            if (strstr(line, "ASEs implemented") && strstr(line, "mxu_v2"))
                mxu = 1;
        fclose(cpu);
    }
    return mxu && access("/tmp/closesimd", F_OK) != 0;
}

int main(int argc, char **argv)
{
    static const int msize[][2] = { { 640, 360 }, { 320, 240 }, { 64, 48 } };
    static const int bsize[][2] = { { 320, 240 }, { 64, 48 } };
    static const int skips[] = { 0, 1, 2, 3, 5, 9 };
    void *lib;
    int s, k, l, m, r, e, devnull;

    if (argc < 2) {
        fprintf(stderr, "usage: %s /path/to/stock/libimp.so\n", argv[0]);
        return 2;
    }
    lib = dlopen(argv[1], RTLD_LAZY | RTLD_GLOBAL);
    if (!lib) {
        fprintf(stderr, "dlopen: %s\n", dlerror());
        return 2;
    }
    stock_alloc_move = (void *(*)(IMP_IVS_MoveParam *, void (*)(void *)))dlsym(lib, "imp_alloc_move");
    stock_move_pre = (int (*)(void *, T31IVSFrameInfo *))dlsym(lib, "imp_move_preprocess");
    stock_move_proc = (int (*)(void *, T31IVSFrameInfo *, IMP_IVS_MoveOutput *))dlsym(lib, "imp_move_process");
    stock_alloc_base = (void *(*)(IMP_IVS_BaseMoveParam *, void (*)(void *)))dlsym(lib, "imp_alloc_base_move");
    stock_base_pre = (int (*)(void *, T31IVSFrameInfo *))dlsym(lib, "imp_base_move_preprocess");
    stock_base_proc = (int (*)(void *, T31IVSFrameInfo *, StockBaseSlot *))dlsym(lib, "imp_base_move_process");
    if (!stock_alloc_move || !stock_move_pre || !stock_move_proc ||
        !stock_alloc_base || !stock_base_pre || !stock_base_proc) {
        fprintf(stderr, "%s: IVS entry points not found (not a stock %s libimp?)\n",
                argv[1],
#if defined(PLATFORM_T23)
                "T23"
#else
                "T31"
#endif
                );
        return 2;
    }
    fflush(stdout);
    devnull = open("/dev/null", O_WRONLY);
    if (devnull >= 0)
        dup2(devnull, 1);
    fprintf(stderr, "stock path: %s\n", simd_expected() ? "MXU2 SIMD" : "scalar");

    for (s = 0; s < 3; s++)
        for (k = 0; k < 6; k++)
            for (l = 0; l < 3; l++)
                for (m = 0; m < 4; m++)
                    compare_move(msize[s][0], msize[s][1], skips[k], l, m);
    fprintf(stderr, "move: frames same=%ld differ=%ld oob-differ=%ld (results %ld)\n",
            move_same, move_diff, move_oob_diff, move_results);

    for (s = 0; s < 2; s++)
        for (k = 0; k < 4; k++)
            for (r = 1; r <= 7; r += 3)
                for (e = 0; e < 4; e++)
                    for (m = 0; m < 4; m++)
                        compare_base(bsize[s][0], bsize[s][1], skips[k], r, e, m);
    fprintf(stderr, "base: frames same=%ld differ=%ld (detections %ld)\n",
            base_same, base_diff, base_results);
    return move_diff || base_diff;
}
