/**
 * IMP OSD (On-Screen Display) Module
 * 
 * On-screen display region management
 */

#ifndef __IMP_OSD_H__
#define __IMP_OSD_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "imp_common.h"

/**
 * OSD region type
 */
typedef enum {
    OSD_REG_INV = 0,                    /**< Invalid */
    OSD_REG_LINE = 1,                   /**< Line */
    OSD_REG_RECT = 2,                   /**< Rectangle */
    OSD_REG_BITMAP = 3,                 /**< Bitmap */
    OSD_REG_COVER = 4,                  /**< Cover */
    OSD_REG_PIC = 5                     /**< Picture */
} IMPOSDRgnType;

/**
 * OSD pixel format
 */
typedef enum {
    OSD_PIX_FMT_MONOWHITE = 0,          /**< Monochrome white */
    OSD_PIX_FMT_MONOBLACK = 1,          /**< Monochrome black */
    OSD_PIX_FMT_BGRA = 2                /**< BGRA */
} IMPOSDPixelFormat;

/**
 * Line rectangle data
 */
typedef struct {
    uint32_t color;                     /**< Line color */
    uint32_t linewidth;                 /**< Line width */
    IMPRect rect;                       /**< Rectangle */
} IMPOSDRgnAttrLineRectData;

/**
 * Cover data
 */
typedef struct {
    uint32_t color;                     /**< Cover color */
    IMPRect rect;                       /**< Rectangle */
} IMPOSDRgnAttrCoverData;

/**
 * Picture data
 */
typedef struct {
    void *pData;                        /**< Picture data */
    IMPPixelFormat pixelFormat;         /**< Pixel format */
    IMPRect rect;                       /**< Rectangle */
} IMPOSDRgnAttrPicData;

/**
 * OSD region attribute data
 */
typedef struct {
    IMPOSDRgnType type;                 /**< Region type */
    IMPRect rect;                       /**< Rectangle */
    IMPPixelFormat fmt;                 /**< Pixel format */
    union {
        void *bitmapData;               /**< Bitmap data */
        IMPOSDRgnAttrLineRectData lineRectData;  /**< Line/rect data */
        IMPOSDRgnAttrCoverData coverData;        /**< Cover data */
        IMPOSDRgnAttrPicData picData;            /**< Picture data */
    } data;
} IMPOSDRgnAttrData;

/**
 * OSD region attributes
 */
typedef struct {
    IMPOSDRgnType type;                 /**< Region type */
    IMPRect rect;                       /**< Rectangle */
    IMPPixelFormat fmt;                 /**< Pixel format */
    IMPOSDRgnAttrData data;             /**< Region data */
} IMPOSDRgnAttr;

/**
 * OSD group region attributes
 */
typedef struct {
    int show;                           /**< Show flag */
    IMPPoint offPos;                    /**< Offset position */
    float scalex;                       /**< X scale */
    float scaley;                       /**< Y scale */
    int gAlphaEn;                       /**< Global alpha enable */
    int fgAlhpa;                        /**< Foreground alpha */
    int bgAlhpa;                        /**< Background alpha */
    int layer;                          /**< Layer */
} IMPOSDGrpRgnAttr;

/**
 * Set OSD pool size
 * 
 * Must be called before IMP_System_Init()
 * 
 * @param size Pool size in bytes
 * @return 0 on success, negative on error
 */
int IMP_OSD_SetPoolSize(int size);

/**
 * Create OSD group
 * 
 * @param grpNum Group number
 * @return 0 on success, negative on error
 */
int IMP_OSD_CreateGroup(int grpNum);

/**
 * Destroy OSD group
 * 
 * @param grpNum Group number
 * @return 0 on success, negative on error
 */
int IMP_OSD_DestroyGroup(int grpNum);

/**
 * Create OSD region
 * 
 * @param handle Region handle
 * @param prAttr Region attributes
 * @return 0 on success, negative on error
 */
int IMP_OSD_CreateRgn(IMPRgnHandle handle, IMPOSDRgnAttr *prAttr);

/**
 * Destroy OSD region
 * 
 * @param handle Region handle
 * @return 0 on success, negative on error
 */
int IMP_OSD_DestroyRgn(IMPRgnHandle handle);

/**
 * Register region to group
 * 
 * @param handle Region handle
 * @param grpNum Group number
 * @param pgrAttr Group region attributes
 * @return 0 on success, negative on error
 */
int IMP_OSD_RegisterRgn(IMPRgnHandle handle, int grpNum, IMPOSDGrpRgnAttr *pgrAttr);

/**
 * Unregister region from group
 * 
 * @param handle Region handle
 * @param grpNum Group number
 * @return 0 on success, negative on error
 */
int IMP_OSD_UnRegisterRgn(IMPRgnHandle handle, int grpNum);

/**
 * Set region attributes
 * 
 * @param handle Region handle
 * @param prAttr Region attributes
 * @return 0 on success, negative on error
 */
int IMP_OSD_SetRgnAttr(IMPRgnHandle handle, IMPOSDRgnAttr *prAttr);

/**
 * Get region attributes
 * 
 * @param handle Region handle
 * @param prAttr Pointer to region attributes
 * @return 0 on success, negative on error
 */
int IMP_OSD_GetRgnAttr(IMPRgnHandle handle, IMPOSDRgnAttr *prAttr);

/**
 * Set group region attributes
 * 
 * @param handle Region handle
 * @param grpNum Group number
 * @param pgrAttr Group region attributes
 * @return 0 on success, negative on error
 */
int IMP_OSD_SetGrpRgnAttr(IMPRgnHandle handle, int grpNum, IMPOSDGrpRgnAttr *pgrAttr);

/**
 * Get group region attributes
 * 
 * @param handle Region handle
 * @param grpNum Group number
 * @param pgrAttr Pointer to group region attributes
 * @return 0 on success, negative on error
 */
int IMP_OSD_GetGrpRgnAttr(IMPRgnHandle handle, int grpNum, IMPOSDGrpRgnAttr *pgrAttr);

/**
 * Update region attribute data
 * 
 * @param handle Region handle
 * @param prAttrData Region attribute data
 * @return 0 on success, negative on error
 */
int IMP_OSD_UpdateRgnAttrData(IMPRgnHandle handle, IMPOSDRgnAttrData *prAttrData);

/**
 * Show or hide region
 * 
 * @param handle Region handle
 * @param grpNum Group number
 * @param showFlag Show flag (1=show, 0=hide)
 * @return 0 on success, negative on error
 */
int IMP_OSD_ShowRgn(IMPRgnHandle handle, int grpNum, int showFlag);

/**
 * Start OSD group
 * 
 * @param grpNum Group number
 * @return 0 on success, negative on error
 */
int IMP_OSD_Start(int grpNum);

/**
 * Stop OSD group
 * 
 * @param grpNum Group number
 * @return 0 on success, negative on error
 */
int IMP_OSD_Stop(int grpNum);

#ifdef __cplusplus
}
#endif

#endif /* __IMP_OSD_H__ */

