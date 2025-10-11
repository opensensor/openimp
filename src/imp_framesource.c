/**
 * IMP FrameSource Module Implementation (Stub)
 */

#include <stdio.h>
#include <string.h>
#include <imp/imp_framesource.h>

#define LOG_FS(fmt, ...) fprintf(stderr, "[IMP_FrameSource] " fmt "\n", ##__VA_ARGS__)

int IMP_FrameSource_CreateChn(int chnNum, IMPFSChnAttr *chn_attr) {
    if (chn_attr == NULL) return -1;
    LOG_FS("CreateChn: chn=%d, %dx%d, fmt=%d", chnNum, 
           chn_attr->picWidth, chn_attr->picHeight, chn_attr->pixFmt);
    return 0;
}

int IMP_FrameSource_DestroyChn(int chnNum) {
    LOG_FS("DestroyChn: chn=%d", chnNum);
    return 0;
}

int IMP_FrameSource_EnableChn(int chnNum) {
    LOG_FS("EnableChn: chn=%d", chnNum);
    return 0;
}

int IMP_FrameSource_DisableChn(int chnNum) {
    LOG_FS("DisableChn: chn=%d", chnNum);
    return 0;
}

int IMP_FrameSource_SetChnAttr(int chnNum, IMPFSChnAttr *chn_attr) {
    if (chn_attr == NULL) return -1;
    LOG_FS("SetChnAttr: chn=%d", chnNum);
    return 0;
}

int IMP_FrameSource_GetChnAttr(int chnNum, IMPFSChnAttr *chn_attr) {
    if (chn_attr == NULL) return -1;
    LOG_FS("GetChnAttr: chn=%d", chnNum);
    memset(chn_attr, 0, sizeof(*chn_attr));
    return 0;
}

int IMP_FrameSource_SetChnFifoAttr(int chnNum, IMPFSChnFifoAttr *attr) {
    if (attr == NULL) return -1;
    LOG_FS("SetChnFifoAttr: chn=%d", chnNum);
    return 0;
}

int IMP_FrameSource_GetChnFifoAttr(int chnNum, IMPFSChnFifoAttr *attr) {
    if (attr == NULL) return -1;
    LOG_FS("GetChnFifoAttr: chn=%d", chnNum);
    memset(attr, 0, sizeof(*attr));
    return 0;
}

int IMP_FrameSource_SetFrameDepth(int chnNum, int depth) {
    LOG_FS("SetFrameDepth: chn=%d, depth=%d", chnNum, depth);
    return 0;
}

int IMP_FrameSource_SetChnRotate(int chnNum, int rotation, int height, int width) {
    LOG_FS("SetChnRotate: chn=%d, rotation=%d, %dx%d", chnNum, rotation, width, height);
    return 0;
}

