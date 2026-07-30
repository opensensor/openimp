/*
 * Thin T31 stock-driver compatibility surface for the shared encoder.
 *
 * The T31 FrameSource adapter uses the lightweight VBM
 * implementation in kernel_interface.c rather than the incomplete port in
 * core/vbm.c.  Keep the few public/private bookkeeping entry points required
 * by Raptor and the FrameSource lifecycle here so the two VBM
 * implementations do not need to be linked together.
 */

#include <pthread.h>
#include <stdint.h>
#include <string.h>

#define T31_FS_CHANNELS 33

static int t31_fs_pool_ids[T31_FS_CHANNELS];
static pthread_once_t t31_fs_pool_once = PTHREAD_ONCE_INIT;
static uint8_t t31_vbm_compat_state[0x20];

static void t31_init_pool_ids(void)
{
    unsigned int i;

    for (i = 0; i < T31_FS_CHANNELS; ++i)
        t31_fs_pool_ids[i] = -1;
}

int IMP_FrameSource_SetPool(int channel, int pool_id)
{
    pthread_once(&t31_fs_pool_once, t31_init_pool_ids);
    if (channel < 0 || channel >= T31_FS_CHANNELS || pool_id < 0)
        return -1;
    if (t31_fs_pool_ids[channel] >= 0)
        return -1;
    t31_fs_pool_ids[channel] = pool_id;
    return 0;
}

int IMP_FrameSource_ClearPoolId(void)
{
    pthread_once(&t31_fs_pool_once, t31_init_pool_ids);
    t31_init_pool_ids();
    return 0;
}

/*
 * FrameSource only uses this vendor-global view to set the NCU shutdown flag
 * at offset 0x14.  Buffer ownership itself remains in kernel_interface.c.
 */
void *VBMGetInstance(void)
{
    return t31_vbm_compat_state;
}

int32_t VBMDumpPoolInfo(void)
{
    return 0;
}
