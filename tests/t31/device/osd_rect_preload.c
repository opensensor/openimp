/*
 * osd_rect_preload - LD_PRELOAD shim that adds OSD_REG_LINE / OSD_REG_RECT /
 * OSD_REG_BITMAP regions to a streamer's OSD groups (T31, T20, T21, T30; the
 * vendor T31 1.1.6 OSD ABI, src/t31/openimp_t31_osd_abi.h).
 *
 * timps only creates PIC and COVER regions, so this hooks IMP_OSD_Start:
 * before the real call it creates and registers, in that group,
 *   - RECT  lw 4 red     at ( w/8,  h/8) - (3w/8, 3h/8)
 *   - RECT  lw 1 green   at (3w/16, 3h/16) - (5w/16, 5h/16)
 *   - RECT  lw 6 blue    fmt BGRA: must NOT be drawn (stock needs MONOWHITE)
 *   - LINE  lw 3 white   horizontal at y = h/2, x w/16 .. 15w/16
 *   - LINE  lw 6 yellow  diagonal (w/2, h/8) -> (7w/8, 7h/8)
 *   - LINE  lw 2 cyan    rising diagonal (w/2, 7h/8) -> (7w/8, h/8)
 *   - RECT  lw 8 magenta hanging over the bottom-right corner (clipping)
 *   - BITMAP 64x32 grey gradient with a hole, at (5w/8, h/16); the source
 *     buffer is wiped and freed right after SetRgnAttr (libimp must keep
 *     its own copy, as stock does)
 * With OSD_PRELOAD_ANIMATE=1 a thread moves a lw 3 white RECT across and
 * past the frame edges every 40 ms, and rewrites the bitmap every second
 * (UpdateRgnAttrData), to exercise the locking against the encoder thread.
 *
 * Frame size: IMP_FrameSource_GetChnAttr(group) (picWidth/picHeight are the
 * first two ints on every T-series ABI), else OSD_PRELOAD_W/H, else 640x360.
 *
 * Build (any Thingino mipsel toolchain, nothing vendor is linked):
 *   $CC -std=gnu99 -O2 -Wall -fPIC -shared -I../../../src \
 *       -o osd_rect_preload.so osd_rect_preload.c -ldl -lpthread
 * Run on the camera (OSD must be enabled in timps.conf so the group exists):
 *   /etc/init.d/S95timps stop
 *   LD_PRELOAD=/tmp/osd_rect_preload.so /usr/bin/timpsd -c /etc/timps.conf
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "t31/openimp_t31_osd_abi.h"

int IMP_OSD_Start(int group);
extern IMPRgnHandle IMP_OSD_CreateRgn(IMPOSDRgnAttr *attr);
extern int IMP_OSD_RegisterRgn(IMPRgnHandle handle, int group, IMPOSDGrpRgnAttr *attr);
extern int IMP_OSD_SetRgnAttr(IMPRgnHandle handle, IMPOSDRgnAttr *attr);
extern int IMP_OSD_UpdateRgnAttrData(IMPRgnHandle handle, IMPOSDRgnAttrData *data);
extern int IMP_FrameSource_GetChnAttr(int chn, void *attr) __attribute__((weak));

#define BMP_W 64
#define BMP_H 32

static void frame_size(int group, int *w, int *h)
{
    uint32_t attr[128];
    const char *ew = getenv("OSD_PRELOAD_W"), *eh = getenv("OSD_PRELOAD_H");

    *w = 640;
    *h = 360;
    memset(attr, 0, sizeof(attr));
    if (IMP_FrameSource_GetChnAttr && IMP_FrameSource_GetChnAttr(group, attr) == 0 &&
        attr[0] >= 64 && attr[0] <= 4096 && attr[1] >= 64 && attr[1] <= 4096) {
        *w = (int)attr[0];
        *h = (int)attr[1];
    }
    if (ew && eh) {
        *w = atoi(ew);
        *h = atoi(eh);
    }
}

static int add(int group, IMPOSDRgnAttr *a, int layer, int offx, int offy)
{
    IMPOSDGrpRgnAttr g;
    IMPRgnHandle h = IMP_OSD_CreateRgn(NULL);

    if (h < 0) {
        fprintf(stderr, "osd_preload: CreateRgn failed\n");
        return -1;
    }
    memset(&g, 0, sizeof(g));
    g.show = 1;
    g.layer = layer;
    g.offPos.x = offx;
    g.offPos.y = offy;
    g.scalex = g.scaley = 1.0f;
    if (IMP_OSD_RegisterRgn(h, group, &g) != 0 || IMP_OSD_SetRgnAttr(h, a) != 0) {
        fprintf(stderr, "osd_preload: register/set rgn %d failed\n", h);
        return -1;
    }
    return h;
}

static void line_attr(IMPOSDRgnAttr *a, int type, int x0, int y0, int x1, int y1,
                      uint32_t argb, uint32_t lw, int fmt)
{
    memset(a, 0, sizeof(*a));
    a->type = (IMPOsdRgnType)type;
    a->rect.p0.x = x0;
    a->rect.p0.y = y0;
    a->rect.p1.x = x1;
    a->rect.p1.y = y1;
    a->fmt = fmt;
    a->data.lineRectData.color = argb;
    a->data.lineRectData.linewidth = lw;
}

static void fill_bitmap(uint8_t *b, int phase)
{
    int x, y;

    for (y = 0; y < BMP_H; y++)
        for (x = 0; x < BMP_W; x++) {
            int hole = x > 24 && x < 40 && y > 8 && y < 24;

            b[y * BMP_W + x] = hole ? 0 : (uint8_t)(32 + ((x * 3 + phase) & 0xbf));
        }
}

struct anim {
    int group, w, h, rect, bitmap;
};

static void *animate(void *arg)
{
    struct anim *an = arg;
    IMPOSDRgnAttr a;
    uint8_t *b = malloc(BMP_W * BMP_H);
    int x = -100, y = -50, step = 0;

    for (;;) {
        usleep(40000);
        x += 7;
        y += 3;
        if (x > an->w + 50)
            x = -150;
        if (y > an->h + 50)
            y = -80;
        line_attr(&a, OSD_REG_RECT, x, y, x + 120, y + 70, 0xffffffffu, 3,
                  T31_OSD_PIX_MONOWHITE);
        IMP_OSD_SetRgnAttr(an->rect, &a);
        if (b && an->bitmap >= 0 && ++step % 25 == 0) {
            IMPOSDRgnAttrData d;

            fill_bitmap(b, step);
            d.bitmapData = b;
            IMP_OSD_UpdateRgnAttrData(an->bitmap, &d);
            memset(b, 0, BMP_W * BMP_H);    /* the library must have copied */
        }
    }
    return NULL;
}

int IMP_OSD_Start(int group)
{
    static int (*real)(int);
    IMPOSDRgnAttr a;
    uint8_t *bmp;
    int w, h, bitmap = -1;

    if (!real)
        real = (int (*)(int))dlsym(RTLD_NEXT, "IMP_OSD_Start");
    if (!real)
        return -1;
    frame_size(group, &w, &h);
    fprintf(stderr, "osd_preload: group %d, frame %dx%d\n", group, w, h);

    line_attr(&a, OSD_REG_RECT, w / 8, h / 8, 3 * w / 8, 3 * h / 8, 0xffff0000u, 4,
              T31_OSD_PIX_MONOWHITE);
    add(group, &a, 1, 0, 0);
    line_attr(&a, OSD_REG_RECT, 3 * w / 16, 3 * h / 16, 5 * w / 16, 5 * h / 16,
              0xff00ff00u, 1, T31_OSD_PIX_MONOWHITE);
    add(group, &a, 2, 0, 0);
    line_attr(&a, OSD_REG_RECT, w / 4, h / 2 + 10, w / 2, 3 * h / 4, 0xff0000ffu, 6,
              T31_OSD_PIX_BGRA);
    add(group, &a, 1, 0, 0);
    line_attr(&a, OSD_REG_LINE, w / 16, h / 2, 15 * w / 16, h / 2, 0xffffffffu, 3,
              T31_OSD_PIX_MONOWHITE);
    add(group, &a, 1, 0, 0);
    line_attr(&a, OSD_REG_LINE, w / 2, h / 8, 7 * w / 8, 7 * h / 8, 0xffffff00u, 6,
              T31_OSD_PIX_MONOWHITE);
    add(group, &a, 1, 0, 0);
    line_attr(&a, OSD_REG_LINE, w / 2, 7 * h / 8, 7 * w / 8, h / 8, 0xff00ffffu, 2,
              T31_OSD_PIX_MONOWHITE);
    add(group, &a, 1, 0, 0);
    /* in-frame rect pushed over the corner by offPos */
    line_attr(&a, OSD_REG_RECT, w - 100, h - 60, w - 20, h - 10, 0xffff00ffu, 8,
              T31_OSD_PIX_MONOWHITE);
    add(group, &a, 1, 60, 40);

    bmp = malloc(BMP_W * BMP_H);
    if (bmp) {
        memset(&a, 0, sizeof(a));
        a.type = OSD_REG_BITMAP;
        a.rect.p0.x = 5 * w / 8;
        a.rect.p0.y = h / 16;
        a.rect.p1.x = a.rect.p0.x + BMP_W - 1;
        a.rect.p1.y = a.rect.p0.y + BMP_H - 1;
        a.fmt = T31_OSD_PIX_MONOWHITE;
        fill_bitmap(bmp, 0);
        a.data.bitmapData = bmp;
        bitmap = add(group, &a, 3, 0, 0);
        memset(bmp, 0, BMP_W * BMP_H);
        free(bmp);
    }

    if (getenv("OSD_PRELOAD_ANIMATE") && atoi(getenv("OSD_PRELOAD_ANIMATE"))) {
        struct anim *an = calloc(1, sizeof(*an));
        pthread_t t;

        line_attr(&a, OSD_REG_RECT, 0, 0, 120, 70, 0xffffffffu, 3, T31_OSD_PIX_MONOWHITE);
        if (an) {
            an->group = group;
            an->w = w;
            an->h = h;
            an->bitmap = bitmap;
            an->rect = add(group, &a, 4, 0, 0);
            if (an->rect >= 0 && pthread_create(&t, NULL, animate, an) == 0)
                pthread_detach(t);
        }
    }
    return real(group);
}
