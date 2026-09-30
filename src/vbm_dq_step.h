/*
 * vbm_dq_step.h -- T31 stop diagnostics shared by kernel_interface.c and
 * the FrameSource pooling worker.
 */
#ifndef VBM_DQ_STEP_H
#define VBM_DQ_STEP_H

#if defined(PLATFORM_T31)
/* Where VBMKernelDequeue for a channel currently is, for
 * IMP_FrameSource_DisableChn's stop timeout report. The caller resets it to
 * VBM_DQ_STEP_NONE before each call. */
enum {
    VBM_DQ_STEP_NONE,
    VBM_DQ_STEP_DQBUF,      /* in the DQBUF ioctl */
    VBM_DQ_STEP_IVS,        /* IVS luma capture */
    VBM_DQ_STEP_QUEUE,      /* taking the ready-queue mutex */
    VBM_DQ_STEP_REQUEUE,    /* ready queue full: QBUF back */
};
extern volatile int openimp_vbm_dq_step[];
#endif

#endif /* VBM_DQ_STEP_H */
