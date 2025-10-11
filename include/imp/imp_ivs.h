/**
 * IMP IVS (Intelligent Video System) Module
 * 
 * Motion detection and intelligent video analysis
 */

#ifndef __IMP_IVS_H__
#define __IMP_IVS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "imp_common.h"

/**
 * IVS interface structure
 * 
 * This is an opaque structure that contains algorithm-specific
 * parameters and callbacks
 */
typedef struct IMPIVSInterface IMPIVSInterface;

/**
 * Create IVS group
 * 
 * @param grpNum Group number
 * @return 0 on success, negative on error
 */
int IMP_IVS_CreateGroup(int grpNum);

/**
 * Destroy IVS group
 * 
 * @param grpNum Group number
 * @return 0 on success, negative on error
 */
int IMP_IVS_DestroyGroup(int grpNum);

/**
 * Create IVS channel
 * 
 * @param chnNum Channel number
 * @param handler IVS interface handler
 * @return 0 on success, negative on error
 */
int IMP_IVS_CreateChn(int chnNum, IMPIVSInterface *handler);

/**
 * Destroy IVS channel
 * 
 * @param chnNum Channel number
 * @return 0 on success, negative on error
 */
int IMP_IVS_DestroyChn(int chnNum);

/**
 * Register channel to group
 * 
 * @param grpNum Group number
 * @param chnNum Channel number
 * @return 0 on success, negative on error
 */
int IMP_IVS_RegisterChn(int grpNum, int chnNum);

/**
 * Unregister channel from group
 * 
 * @param chnNum Channel number
 * @return 0 on success, negative on error
 */
int IMP_IVS_UnRegisterChn(int chnNum);

/**
 * Start receiving pictures
 * 
 * @param chnNum Channel number
 * @return 0 on success, negative on error
 */
int IMP_IVS_StartRecvPic(int chnNum);

/**
 * Stop receiving pictures
 * 
 * @param chnNum Channel number
 * @return 0 on success, negative on error
 */
int IMP_IVS_StopRecvPic(int chnNum);

/**
 * Poll for result
 * 
 * @param chnNum Channel number
 * @param timeoutMs Timeout in milliseconds
 * @return 0 on success, negative on error
 */
int IMP_IVS_PollingResult(int chnNum, int timeoutMs);

/**
 * Get result
 * 
 * @param chnNum Channel number
 * @param result Pointer to result pointer
 * @return 0 on success, negative on error
 */
int IMP_IVS_GetResult(int chnNum, void **result);

/**
 * Release result
 * 
 * @param chnNum Channel number
 * @param result Result pointer
 * @return 0 on success, negative on error
 */
int IMP_IVS_ReleaseResult(int chnNum, void *result);

#ifdef __cplusplus
}
#endif

#endif /* __IMP_IVS_H__ */

