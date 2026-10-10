/* Loader-completeness shims for RVD/RAD features outside the active P3 gate.
 * Real P0-P3 implementations live in their subsystem translation units.
 * These return ENOTSUP instead of pretending that inactive P4 features work.
 * OSD and IVS are real on T40 and T41 (src/t23/openimp_t23_osd.c,
 * src/t31/openimp_t31_ivs*.c); SnapFrame is in openimp_p1.c. */

#include <errno.h>
#include <stddef.h>
#include <stdio.h>

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


P3_UNSUPPORTED(IMP_ISP_Tuning_CreateOsdRgn)
P3_UNSUPPORTED(IMP_ISP_Tuning_DestroyOsdRgn)
/* T40 IMP_ISP_Tuning_Get/SetMask: openimp_p3_controls.c (no T41 export,
 * the vendor T41 has SetMaskBlock instead) */
P3_UNSUPPORTED(IMP_ISP_Tuning_SetOsdRgnAttr)
P3_UNSUPPORTED(IMP_ISP_Tuning_ShowOsdRgn)
