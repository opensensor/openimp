/* OSD types in the layout of the T31 1.1.6 vendor headers (also T20, T21,
 * T30 and C100), which streamers are compiled against. OpenIMP's generic
 * include/imp/imp_osd.h differs (72-byte region attribute, x/y/w/h rect,
 * PIX_FMT_BGRA = 12) and must not be used for the T31 ABI. */
#ifndef OPENIMP_T31_OSD_ABI_H
#define OPENIMP_T31_OSD_ABI_H

#include <stdint.h>

typedef int IMPRgnHandle;

typedef enum {
    OSD_REG_INV = 0,
    OSD_REG_LINE = 1,
    OSD_REG_RECT = 2,
    OSD_REG_BITMAP = 3,
    OSD_REG_COVER = 4,
    OSD_REG_PIC = 5,
    OSD_REG_PIC_RMEM = 6,
} IMPOsdRgnType;

/* Vendor IMPPixelFormat values used by the OSD path. */
enum {
    T31_OSD_PIX_MONOWHITE = 8,
    T31_OSD_PIX_NV12 = 10,
    T31_OSD_PIX_NV21 = 11,
    T31_OSD_PIX_ARGB = 14,
    T31_OSD_PIX_RGBA = 15,
    T31_OSD_PIX_ABGR = 16,
    T31_OSD_PIX_BGRA = 17,
    T31_OSD_PIX_RGB555LE = 21,
    T31_OSD_PIX_BGR555LE = 25,
};

typedef struct {
    int x;
    int y;
} T31OSDPoint;

/* Inclusive corners, unlike OpenIMP's IMPRect {x, y, width, height}. */
typedef struct {
    T31OSDPoint p0;
    T31OSDPoint p1;
} T31OSDRect;

typedef union {
    void *bitmapData;
    struct {
        uint32_t color;
        uint32_t linewidth;
    } lineRectData;
    struct {
        uint32_t color;             /* 0xAARRGGBB */
    } coverData;
    struct {
        void *pData;
    } picData;
} IMPOSDRgnAttrData;

typedef struct {
    IMPOsdRgnType type;
    T31OSDRect rect;
    int fmt;                        /* vendor IMPPixelFormat value */
    IMPOSDRgnAttrData data;
} IMPOSDRgnAttr;

typedef struct {
    int show;
    T31OSDPoint offPos;
    float scalex;
    float scaley;
    int gAlphaEn;
    int fgAlhpa;
    int bgAlhpa;
    int layer;
} IMPOSDGrpRgnAttr;

_Static_assert(sizeof(IMPOSDRgnAttrData) == 8, "vendor IMPOSDRgnAttrData is 8 bytes");
_Static_assert(sizeof(IMPOSDRgnAttr) == 32, "vendor IMPOSDRgnAttr is 32 bytes");
_Static_assert(sizeof(IMPOSDGrpRgnAttr) == 36, "vendor IMPOSDGrpRgnAttr is 36 bytes");

#endif
