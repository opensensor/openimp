/* OSD types in the layout of the Ingenic T23 SDK 1.3.0 headers, which T23
 * streamers are compiled against.  They differ from T31 1.1.6 (region type
 * numbering, a 428-byte region attribute with line, ISP-draw, font and mosaic
 * members, a 16-byte attribute data union) and from OpenIMP's generic
 * include/imp/imp_osd.h, so the T23 OSD implementation uses these instead.
 * Sizes and offsets were checked against the vendor headers with the T23
 * toolchain. */
#ifndef OPENIMP_T23_OSD_ABI_H
#define OPENIMP_T23_OSD_ABI_H

#include <stdint.h>

typedef int IMPRgnHandle;

/* vendor IMP_ERR_OSD_* (imp_osd.h) */
#define T23_OSD_ERR_CHNID      ((int)0x80020001)
#define T23_OSD_ERR_PARAM      ((int)0x80020002)
#define T23_OSD_ERR_EXIST      ((int)0x80020004)
#define T23_OSD_ERR_UNEXIST    ((int)0x80020008)
#define T23_OSD_ERR_NULL_PTR   ((int)0x80020010)
#define T23_OSD_ERR_NOT_CONFIG ((int)0x80020020)
#define T23_OSD_ERR_NOT_SUPPORT ((int)0x80020040)
#define T23_OSD_ERR_PERM       ((int)0x80020080)
#define T23_OSD_ERR_NOMEM      ((int)0x80020100)
#define T23_OSD_ERR_SYS_NOTREADY ((int)0x80022000)
#define T23_OSD_ERR_RESOURCE   ((int)0x80028000)

typedef enum {
    OSD_REG_INV = 0,
    OSD_REG_HORIZONTAL_LINE = 1,
    OSD_REG_VERTICAL_LINE = 2,
    OSD_REG_RECT = 3,
    OSD_REG_FOUR_CORNER_RECT = 4,
    OSD_REG_BITMAP = 5,
    OSD_REG_COVER = 6,
    OSD_REG_PIC = 7,
    OSD_REG_PIC_RMEM = 8,
    OSD_REG_SLASH = 9,
    OSD_REG_ISP_PIC = 10,
    OSD_REG_ISP_LINE_RECT = 11,
    OSD_REG_ISP_COVER = 12,
    OSD_REG_MOSAIC = 13,
} IMPOsdRgnType;

/* vendor IMPPixelFormat values used by the OSD path */
enum {
    T23_OSD_PIX_MONOWHITE = 8,
    T23_OSD_PIX_NV12 = 10,
    T23_OSD_PIX_NV21 = 11,
    T23_OSD_PIX_ARGB = 14,
    T23_OSD_PIX_RGBA = 15,
    T23_OSD_PIX_ABGR = 16,
    T23_OSD_PIX_BGRA = 17,
    T23_OSD_PIX_RGB555LE = 21,
    T23_OSD_PIX_BGR555LE = 25,
};

typedef struct {
    int x;
    int y;
} T23OSDPoint;

/* inclusive corners */
typedef struct {
    T23OSDPoint p0;
    T23OSDPoint p1;
} T23OSDRect;

typedef struct {
    T23OSDPoint p0;
} T23OSDLine;

typedef union {
    void *bitmapData;
    struct {
        uint32_t color;             /* 0xAARRGGBB */
        uint32_t linewidth;
        uint32_t linelength;
        uint32_t rectlinelength;
    } lineRectData;
    struct {
        uint32_t color;             /* 0xAARRGGBB */
    } coverData;
    struct {
        void *pData;
    } picData;
} IMPOSDRgnAttrData;

/* ISP block attributes (imp_isp.h), opaque here: they are handed to the ISP
 * tuning layer unchanged.  The OSD picture block is read for its address. */
typedef struct {
    uint32_t words[8];
} T23ISPDrawBlockAttr;

typedef struct {
    uint8_t pinum;
    uint8_t osd_enable;
    uint16_t osd_left;
    uint16_t osd_top;
    uint16_t osd_width;
    uint16_t osd_height;
    char *osd_image;
    uint16_t osd_stride;
} T23ISPOSDBlockAttr;

typedef struct {
    uint32_t words[5];
} T23ISPMaskBlockAttr;

typedef struct {
    int osd_type;                   /* IMPISPPICTYPE */
    int osd_argb_type;              /* IMPISPARGBType */
    int osd_pixel_alpha_disable;    /* IMPISPTuningOpsMode */
} T23ISPOSDAttr;

typedef struct {
    T23ISPDrawBlockAttr stDrawAttr;
    T23ISPOSDBlockAttr stpicAttr;
    T23ISPMaskBlockAttr stCoverAttr;
} IMPOSDIspDraw;

typedef struct {
    unsigned int fontWidth;
    unsigned int fontHeight;
} IMPOSDFontSizeAttrData;

typedef struct {
    unsigned int invertColorSwitch;
    unsigned int luminance;
    unsigned int length;
    IMPOSDFontSizeAttrData data;
    unsigned int istimestamp;
    unsigned int colType[64];
} IMPOSDFontAttrData;

typedef struct {
    int x;
    int y;
    int mosaic_width;
    int mosaic_height;
    int frame_width;
    int frame_height;
    int mosaic_min_size;
} IMPOSDMosaicAttr;

typedef struct {
    IMPOsdRgnType type;
    T23OSDRect rect;
    T23OSDLine line;
    int fmt;                        /* vendor IMPPixelFormat value */
    IMPOSDRgnAttrData data;
    IMPOSDIspDraw osdispdraw;
    IMPOSDFontAttrData fontData;
    IMPOSDMosaicAttr mosaicAttr;
} IMPOSDRgnAttr;

typedef struct {
    uint64_t ts;
    uint64_t minus;
    uint64_t plus;
} IMPOSDRgnTimestamp;

typedef struct {
    int show;
    T23OSDPoint offPos;
    float scalex;
    float scaley;
    int gAlphaEn;
    int fgAlhpa;
    int bgAlhpa;
    int layer;
} IMPOSDGrpRgnAttr;

typedef struct {
    int status;
} IMPOSDRgnCreateStat;

typedef struct {
    int status;
} IMPOSDRgnRegisterStat;

/* isp_osd.h / imp_isp.h ISP OSD region API */
typedef enum {
    ISP_OSD_REG_INV = 0,
    ISP_OSD_REG_PIC = 1,
} IMPISPOSDType;

typedef struct {
    int chx;
    int sensornum;
    T23ISPOSDAttr chnOSDAttr;
    T23ISPOSDBlockAttr pic;
} IMPISPOSDSingleAttr;

typedef struct {
    IMPISPOSDType type;
    union {
        IMPISPOSDSingleAttr stsinglepicAttr;
    };
} IMPIspOsdAttrAsm;

_Static_assert(sizeof(IMPOSDRgnAttrData) == 16, "T23 IMPOSDRgnAttrData");
_Static_assert(sizeof(IMPOSDIspDraw) == 72, "T23 IMPOSDIspDraw");
_Static_assert(sizeof(IMPOSDFontAttrData) == 280, "T23 IMPOSDFontAttrData");
_Static_assert(sizeof(IMPOSDMosaicAttr) == 28, "T23 IMPOSDMosaicAttr");
_Static_assert(__builtin_offsetof(IMPOSDRgnAttr, fmt) == 28, "T23 OSD fmt");
_Static_assert(__builtin_offsetof(IMPOSDRgnAttr, data) == 32, "T23 OSD data");
_Static_assert(__builtin_offsetof(IMPOSDRgnAttr, osdispdraw) == 48,
               "T23 OSD osdispdraw");
_Static_assert(__builtin_offsetof(IMPOSDRgnAttr, fontData) == 120,
               "T23 OSD fontData");
_Static_assert(__builtin_offsetof(IMPOSDRgnAttr, mosaicAttr) == 400,
               "T23 OSD mosaicAttr");
_Static_assert(sizeof(IMPOSDRgnAttr) == 428, "T23 IMPOSDRgnAttr");
_Static_assert(sizeof(IMPOSDGrpRgnAttr) == 36, "T23 IMPOSDGrpRgnAttr");
_Static_assert(sizeof(T23ISPOSDBlockAttr) == 20, "T23 IMPISPOSDBlockAttr");
_Static_assert(sizeof(IMPISPOSDSingleAttr) == 40, "T23 IMPISPOSDSingleAttr");
_Static_assert(sizeof(IMPIspOsdAttrAsm) == 44, "T23 IMPIspOsdAttrAsm");

#endif
