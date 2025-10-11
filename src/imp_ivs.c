/**
 * IMP IVS Module Implementation (Stub)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <imp/imp_ivs.h>

#define LOG_IVS(fmt, ...) fprintf(stderr, "[IMP_IVS] " fmt "\n", ##__VA_ARGS__)

int IMP_IVS_CreateGroup(int grpNum) {
    LOG_IVS("CreateGroup: grp=%d", grpNum);
    return 0;
}

int IMP_IVS_DestroyGroup(int grpNum) {
    LOG_IVS("DestroyGroup: grp=%d", grpNum);
    return 0;
}

int IMP_IVS_CreateChn(int chnNum, IMPIVSInterface *handler) {
    if (handler == NULL) return -1;
    LOG_IVS("CreateChn: chn=%d", chnNum);
    return 0;
}

int IMP_IVS_DestroyChn(int chnNum) {
    LOG_IVS("DestroyChn: chn=%d", chnNum);
    return 0;
}

int IMP_IVS_RegisterChn(int grpNum, int chnNum) {
    LOG_IVS("RegisterChn: grp=%d, chn=%d", grpNum, chnNum);
    return 0;
}

int IMP_IVS_UnRegisterChn(int chnNum) {
    LOG_IVS("UnRegisterChn: chn=%d", chnNum);
    return 0;
}

int IMP_IVS_StartRecvPic(int chnNum) {
    LOG_IVS("StartRecvPic: chn=%d", chnNum);
    return 0;
}

int IMP_IVS_StopRecvPic(int chnNum) {
    LOG_IVS("StopRecvPic: chn=%d", chnNum);
    return 0;
}

int IMP_IVS_PollingResult(int chnNum, int timeoutMs) {
    /* Return timeout */
    return -1;
}

int IMP_IVS_GetResult(int chnNum, void **result) {
    if (result == NULL) return -1;
    /* No result available */
    return -1;
}

int IMP_IVS_ReleaseResult(int chnNum, void *result) {
    if (result == NULL) return -1;
    return 0;
}

int IMP_IVS_CreateMoveInterface(IMPIVSInterface **interface, ...) {
    if (interface == NULL) return -1;
    LOG_IVS("CreateMoveInterface");
    /* Allocate a dummy interface */
    *interface = (IMPIVSInterface *)malloc(sizeof(void*));
    return 0;
}

int IMP_IVS_DestroyMoveInterface(IMPIVSInterface *interface) {
    if (interface == NULL) return -1;
    LOG_IVS("DestroyMoveInterface");
    free(interface);
    return 0;
}

