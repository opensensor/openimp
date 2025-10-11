/**
 * IMP Encoder Module Implementation (Stub)
 */

#include <stdio.h>
#include <string.h>
#include <imp/imp_encoder.h>

#define LOG_ENC(fmt, ...) fprintf(stderr, "[IMP_Encoder] " fmt "\n", ##__VA_ARGS__)

int IMP_Encoder_CreateGroup(int encGroup) {
    LOG_ENC("CreateGroup: grp=%d", encGroup);
    return 0;
}

int IMP_Encoder_DestroyGroup(int encGroup) {
    LOG_ENC("DestroyGroup: grp=%d", encGroup);
    return 0;
}

int IMP_Encoder_CreateChn(int encChn, IMPEncoderCHNAttr *attr) {
    if (attr == NULL) return -1;
    LOG_ENC("CreateChn: chn=%d", encChn);
    return 0;
}

int IMP_Encoder_DestroyChn(int encChn) {
    LOG_ENC("DestroyChn: chn=%d", encChn);
    return 0;
}

int IMP_Encoder_RegisterChn(int encGroup, int encChn) {
    LOG_ENC("RegisterChn: grp=%d, chn=%d", encGroup, encChn);
    return 0;
}

int IMP_Encoder_UnRegisterChn(int encChn) {
    LOG_ENC("UnRegisterChn: chn=%d", encChn);
    return 0;
}

int IMP_Encoder_StartRecvPic(int encChn) {
    LOG_ENC("StartRecvPic: chn=%d", encChn);
    return 0;
}

int IMP_Encoder_StopRecvPic(int encChn) {
    LOG_ENC("StopRecvPic: chn=%d", encChn);
    return 0;
}

int IMP_Encoder_GetStream(int encChn, IMPEncoderStream *stream, int block) {
    if (stream == NULL) return -1;
    LOG_ENC("GetStream: chn=%d, block=%d", encChn, block);
    /* Return no stream available */
    return -1;
}

int IMP_Encoder_ReleaseStream(int encChn, IMPEncoderStream *stream) {
    if (stream == NULL) return -1;
    LOG_ENC("ReleaseStream: chn=%d", encChn);
    return 0;
}

int IMP_Encoder_PollingStream(int encChn, uint32_t timeoutMsec) {
    LOG_ENC("PollingStream: chn=%d, timeout=%u", encChn, timeoutMsec);
    /* Return timeout */
    return -1;
}

int IMP_Encoder_Query(int encChn, IMPEncoderCHNStat *stat) {
    if (stat == NULL) return -1;
    LOG_ENC("Query: chn=%d", encChn);
    memset(stat, 0, sizeof(*stat));
    return 0;
}

int IMP_Encoder_RequestIDR(int encChn) {
    LOG_ENC("RequestIDR: chn=%d", encChn);
    return 0;
}

int IMP_Encoder_FlushStream(int encChn) {
    LOG_ENC("FlushStream: chn=%d", encChn);
    return 0;
}

int IMP_Encoder_SetDefaultParam(IMPEncoderChnAttr *attr, IMPEncoderProfile profile,
                                 IMPEncoderRcMode rcMode, int width, int height,
                                 int fpsNum, int fpsDen, int gopLen, int gopMode,
                                 int quality, int bitrate) {
    if (attr == NULL) return -1;
    LOG_ENC("SetDefaultParam: %dx%d, %d/%d fps, profile=%d, rc=%d", 
            width, height, fpsNum, fpsDen, profile, rcMode);
    
    memset(attr, 0, sizeof(*attr));
    attr->encAttr.profile = profile;
    
    /* Set basic parameters based on profile */
    if (profile == IMP_ENC_PROFILE_JPEG) {
        attr->encAttr.attrJpeg.maxPicWidth = width;
        attr->encAttr.attrJpeg.maxPicHeight = height;
        attr->encAttr.attrJpeg.bufSize = width * height * 2;
    } else {
        attr->encAttr.attrH264.maxPicWidth = width;
        attr->encAttr.attrH264.maxPicHeight = height;
        attr->encAttr.attrH264.bufSize = width * height * 2;
        attr->encAttr.attrH264.profile = profile;
    }
    
    attr->rcAttr.attrRcMode.rcMode = rcMode;
    attr->rcAttr.outFrmRate.frmRateNum = fpsNum;
    attr->rcAttr.outFrmRate.frmRateDen = fpsDen;
    attr->rcAttr.attrGop.gopLength = gopLen;
    
    return 0;
}

int IMP_Encoder_GetChnAttr(int encChn, IMPEncoderChnAttr *attr) {
    if (attr == NULL) return -1;
    LOG_ENC("GetChnAttr: chn=%d", encChn);
    memset(attr, 0, sizeof(*attr));
    return 0;
}

int IMP_Encoder_SetJpegeQl(int encChn, IMPEncoderJpegeQl *attr) {
    if (attr == NULL) return -1;
    LOG_ENC("SetJpegeQl: chn=%d", encChn);
    return 0;
}

int IMP_Encoder_SetbufshareChn(int srcChn, int dstChn) {
    LOG_ENC("SetbufshareChn: src=%d, dst=%d", srcChn, dstChn);
    return 0;
}

int IMP_Encoder_SetFisheyeEnableStatus(int encChn, int enable) {
    LOG_ENC("SetFisheyeEnableStatus: chn=%d, enable=%d", encChn, enable);
    return 0;
}

