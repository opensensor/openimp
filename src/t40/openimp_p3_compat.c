/* Loader-completeness shims for RVD/RAD features outside the active P3 gate.
 * Real P0-P3 implementations live in their subsystem translation units.
 * These return ENOTSUP instead of pretending that inactive P4 features work.
 * OSD and IVS are real on T40 and T41 (src/t23/openimp_t23_osd.c,
 * src/t31/openimp_t31_ivs*.c); SnapFrame is in openimp_p1.c. */

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

#define P3_REPORT_UNSUPPORTED(name)                                           \
    fprintf(stderr, "openimp-p3: unsupported call: %s\n", #name)

#define P3_UNSUPPORTED(name)                                                  \
    int name(void)                                                            \
    {                                                                         \
        P3_REPORT_UNSUPPORTED(name);                                          \
        errno = ENOTSUP;                                                      \
        return -1;                                                            \
    }

#define P3_UNSUPPORTED_PTR(name)                                              \
    void *name(void)                                                          \
    {                                                                         \
        P3_REPORT_UNSUPPORTED(name);                                          \
        errno = ENOTSUP;                                                      \
        return NULL;                                                          \
    }

P3_UNSUPPORTED(IMP_DMIC_Disable)
P3_UNSUPPORTED(IMP_DMIC_DisableAec)
P3_UNSUPPORTED(IMP_DMIC_DisableAecRefFrame)
P3_UNSUPPORTED(IMP_DMIC_DisableChn)
P3_UNSUPPORTED(IMP_DMIC_Enable)
P3_UNSUPPORTED(IMP_DMIC_EnableAec)
P3_UNSUPPORTED(IMP_DMIC_EnableAecRefFrame)
P3_UNSUPPORTED(IMP_DMIC_EnableChn)
P3_UNSUPPORTED(IMP_DMIC_GetChnParam)
P3_UNSUPPORTED(IMP_DMIC_GetFrame)
P3_UNSUPPORTED(IMP_DMIC_GetFrameAndRef)
P3_UNSUPPORTED(IMP_DMIC_GetGain)
P3_UNSUPPORTED(IMP_DMIC_GetPubAttr)
P3_UNSUPPORTED(IMP_DMIC_GetVol)
P3_UNSUPPORTED(IMP_DMIC_PollingFrame)
P3_UNSUPPORTED(IMP_DMIC_ReleaseFrame)
P3_UNSUPPORTED(IMP_DMIC_SetChnParam)
P3_UNSUPPORTED(IMP_DMIC_SetGain)
P3_UNSUPPORTED(IMP_DMIC_SetPubAttr)
P3_UNSUPPORTED(IMP_DMIC_SetUserInfo)
P3_UNSUPPORTED(IMP_DMIC_SetVol)


/* T40 IMP_ISP_Tuning_Get/SetMask: openimp_p3_controls.c (no T41 export,
 * the vendor T41 has SetMaskBlock instead).  T40 ISP-drawn OSD:
 * src/t40/openimp_t40_isp_osd.c. */
#if !defined(PLATFORM_T40)
P3_UNSUPPORTED(IMP_ISP_Tuning_CreateOsdRgn)
P3_UNSUPPORTED(IMP_ISP_Tuning_DestroyOsdRgn)
P3_UNSUPPORTED(IMP_ISP_Tuning_SetOsdRgnAttr)
P3_UNSUPPORTED(IMP_ISP_Tuning_ShowOsdRgn)
#endif

/* IMP_System_MemPoolRequest (T40 1.3.x imp_system.h): libimp reserves
 * "size" bytes of rmem per pool id and channels bound with
 * IMP_FrameSource_SetPool / IMP_Encoder_SetPool draw from it.  OpenIMP
 * allocates every channel from the one rmem arena, so a pool is a name only
 * (reserving memory nothing draws from would just shrink the arena).  Same
 * argument rules as the stock call: bad id or zero size fail, a second
 * request for the same id fails ("already request"). */
#define P3_MEMPOOLS 32
static struct { int requested; size_t size; char name[32]; } p3_pools[P3_MEMPOOLS];
static pthread_mutex_t p3_pool_lock = PTHREAD_MUTEX_INITIALIZER;

int IMP_System_MemPoolRequest(int poolId, size_t size, const char *name)
{
    if (poolId < 0 || poolId >= P3_MEMPOOLS || size == 0)
        return -1;
    pthread_mutex_lock(&p3_pool_lock);
    if (p3_pools[poolId].requested) {
        pthread_mutex_unlock(&p3_pool_lock);
        return -1;
    }
    p3_pools[poolId].requested = 1;
    p3_pools[poolId].size = size;
    memset(p3_pools[poolId].name, 0, sizeof(p3_pools[poolId].name));
    if (name)
        strncpy(p3_pools[poolId].name, name, sizeof(p3_pools[poolId].name) - 1);
    pthread_mutex_unlock(&p3_pool_lock);
    return 0;
}
