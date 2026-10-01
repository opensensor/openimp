/* T23 1.3.0 FrameSource/System odds and ends. */

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <imp/imp_framesource.h>

#include "imp_log_int.h"

#define T23_FS_CHANNELS 9
#define T23_MEM_POOLS   32

static pthread_mutex_t misc_lock = PTHREAD_MUTEX_INITIALIZER;
static int direct_threshold[T23_FS_CHANNELS];
static size_t pool_request[T23_MEM_POOLS];

static int fs_channel_created(int chn)
{
    IMPFSChnAttr attr;

    memset(&attr, 0, sizeof(attr));
    return chn >= 0 && chn < T23_FS_CHANNELS &&
           IMP_FrameSource_GetChnAttr(chn, &attr) == 0;
}

/* OEM: the cache threshold that decides between encoding and dropping in
 * the dual-sensor IVDC direct mode; it is only stored on the created
 * channel (and read back), which is all a single-sensor stack needs. */
int IMP_FrameSource_SetDirectModeAttr(int chn, int data_threshold)
{
    if (!fs_channel_created(chn))
        return -1;
    pthread_mutex_lock(&misc_lock);
    direct_threshold[chn] = data_threshold;
    pthread_mutex_unlock(&misc_lock);
    return 0;
}

int IMP_FrameSource_GetDirectModeAttr(int chn, int *data_threshold)
{
    if (!data_threshold || !fs_channel_created(chn))
        return -1;
    pthread_mutex_lock(&misc_lock);
    *data_threshold = direct_threshold[chn];
    pthread_mutex_unlock(&misc_lock);
    return 0;
}

/* OEM: IMP_MemPool_InitPool(poolId, size, name) - carve a pool out of rmem
 * for the channels later bound to it with IMP_*_SetPool.  OpenIMP channels
 * allocate from the common rmem arena whatever pool they are given, so a
 * reserved pool would only take rmem away from them: the request is
 * validated and recorded, nothing is reserved. */
int IMP_System_MemPoolRequest(int poolId, size_t size, char *name)
{
    if (poolId < 0 || poolId >= T23_MEM_POOLS || !size)
        return -1;
    pthread_mutex_lock(&misc_lock);
    pool_request[poolId] = size;
    pthread_mutex_unlock(&misc_lock);
    IMP_LOG_INFO("System", "mempool %d (%s, %u bytes) recorded; OpenIMP "
                 "allocates from the shared rmem arena", poolId,
                 name ? name : "", (unsigned int)size);
    return 0;
}
