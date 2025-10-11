/**
 * IMP OSD Module Implementation (Stub)
 */

#include <stdio.h>
#include <string.h>
#include <imp/imp_osd.h>

#define LOG_OSD(fmt, ...) fprintf(stderr, "[IMP_OSD] " fmt "\n", ##__VA_ARGS__)

int IMP_OSD_SetPoolSize(int size) {
    LOG_OSD("SetPoolSize: %d bytes", size);
    return 0;
}

int IMP_OSD_CreateGroup(int grpNum) {
    LOG_OSD("CreateGroup: grp=%d", grpNum);
    return 0;
}

int IMP_OSD_DestroyGroup(int grpNum) {
    LOG_OSD("DestroyGroup: grp=%d", grpNum);
    return 0;
}

int IMP_OSD_CreateRgn(IMPRgnHandle handle, IMPOSDRgnAttr *prAttr) {
    if (prAttr == NULL) return -1;
    LOG_OSD("CreateRgn: handle=%d, type=%d", handle, prAttr->type);
    return 0;
}

int IMP_OSD_DestroyRgn(IMPRgnHandle handle) {
    LOG_OSD("DestroyRgn: handle=%d", handle);
    return 0;
}

int IMP_OSD_RegisterRgn(IMPRgnHandle handle, int grpNum, IMPOSDGrpRgnAttr *pgrAttr) {
    if (pgrAttr == NULL) return -1;
    LOG_OSD("RegisterRgn: handle=%d, grp=%d", handle, grpNum);
    return 0;
}

int IMP_OSD_UnRegisterRgn(IMPRgnHandle handle, int grpNum) {
    LOG_OSD("UnRegisterRgn: handle=%d, grp=%d", handle, grpNum);
    return 0;
}

int IMP_OSD_SetRgnAttr(IMPRgnHandle handle, IMPOSDRgnAttr *prAttr) {
    if (prAttr == NULL) return -1;
    LOG_OSD("SetRgnAttr: handle=%d", handle);
    return 0;
}

int IMP_OSD_GetRgnAttr(IMPRgnHandle handle, IMPOSDRgnAttr *prAttr) {
    if (prAttr == NULL) return -1;
    LOG_OSD("GetRgnAttr: handle=%d", handle);
    memset(prAttr, 0, sizeof(*prAttr));
    return 0;
}

int IMP_OSD_SetGrpRgnAttr(IMPRgnHandle handle, int grpNum, IMPOSDGrpRgnAttr *pgrAttr) {
    if (pgrAttr == NULL) return -1;
    LOG_OSD("SetGrpRgnAttr: handle=%d, grp=%d", handle, grpNum);
    return 0;
}

int IMP_OSD_GetGrpRgnAttr(IMPRgnHandle handle, int grpNum, IMPOSDGrpRgnAttr *pgrAttr) {
    if (pgrAttr == NULL) return -1;
    LOG_OSD("GetGrpRgnAttr: handle=%d, grp=%d", handle, grpNum);
    memset(pgrAttr, 0, sizeof(*pgrAttr));
    return 0;
}

int IMP_OSD_UpdateRgnAttrData(IMPRgnHandle handle, IMPOSDRgnAttrData *prAttrData) {
    if (prAttrData == NULL) return -1;
    LOG_OSD("UpdateRgnAttrData: handle=%d", handle);
    return 0;
}

int IMP_OSD_ShowRgn(IMPRgnHandle handle, int grpNum, int showFlag) {
    LOG_OSD("ShowRgn: handle=%d, grp=%d, show=%d", handle, grpNum, showFlag);
    return 0;
}

int IMP_OSD_Start(int grpNum) {
    LOG_OSD("Start: grp=%d", grpNum);
    return 0;
}

int IMP_OSD_Stop(int grpNum) {
    LOG_OSD("Stop: grp=%d", grpNum);
    return 0;
}

