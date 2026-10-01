/* Remaining T31 libimp 1.1.6 entry points that no other OpenIMP module
 * covers: encoder fisheye/frame-release/pool helpers, the mempool request
 * API, IMP_OSD_AttachToGroup, the impdbg hooks of AI/AO and
 * IMPPixfmtToString.
 */

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <imp/imp_common.h>
#include <imp/imp_system.h>

#define EXTRAS_ENC_CHANNELS 9      /* NR_MAX_ENC_CHN */
#define EXTRAS_POOLS        32

/* Implemented by the encoder module; declared here with opaque pointers so
 * this file does not depend on OpenIMP's generic encoder header layout. */
extern int IMP_Encoder_Query(int encChn, void *stat);
extern int IMP_Encoder_GetStream(int encChn, void *stream, int block);
extern int IMP_Encoder_SetPool(int encChn, int poolId);
extern int IMP_Encoder_GetPool(int encChn);

static pthread_mutex_t extras_lock = PTHREAD_MUTEX_INITIALIZER;

static struct {
    int fisheye;
    int release_num;
    int release_den;
} extras_enc[EXTRAS_ENC_CHANNELS];

static struct {
    int requested;
    size_t size;
    char name[32];
} extras_pools[EXTRAS_POOLS];

static int extras_enc_valid(int channel)
{
    return channel >= 0 && channel < EXTRAS_ENC_CHANNELS;
}

static int extras_enc_created(int channel)
{
    /* IMPEncoderCHNStat is 24 bytes; Query fails for a missing channel */
    uint32_t stat[16];

    return IMP_Encoder_Query(channel, stat) == 0;
}

/* The fisheye correction lives in Ingenic's closed IVS fisheye module;
 * OpenIMP keeps the flag so applications can read back what they set. Like
 * libimp it must be set before the channel exists. */
int IMP_Encoder_SetFisheyeEnableStatus(int encChn, int enable)
{
    if (!extras_enc_valid(encChn) || extras_enc_created(encChn))
        return -1;
    pthread_mutex_lock(&extras_lock);
    extras_enc[encChn].fisheye = enable != 0;
    pthread_mutex_unlock(&extras_lock);
    return 0;
}

int IMP_Encoder_GetFisheyeEnableStatus(int encChn, int *enable)
{
    if (!extras_enc_valid(encChn) || !enable)
        return -1;
    pthread_mutex_lock(&extras_lock);
    *enable = extras_enc[encChn].fisheye;
    pthread_mutex_unlock(&extras_lock);
    return 0;
}

/* libimp starts a thread that hands the source frame back to FrameSource
 * num/den of a frame interval after it was queued, before the encoder is
 * done with it. OpenIMP's encoder returns every frame as soon as the AVPU
 * has consumed it, so the ratio is recorded but changes nothing. */
int IMP_Encoder_SetFrameRelease(int encChn, int num, int den)
{
    if (!extras_enc_valid(encChn) || !extras_enc_created(encChn))
        return -1;
    pthread_mutex_lock(&extras_lock);
    extras_enc[encChn].release_num = num;
    extras_enc[encChn].release_den = den;
    pthread_mutex_unlock(&extras_lock);
    return 0;
}

/* libimp frees its whole channel->pool table; the argument is unused. */
int IMP_Encoder_ClearPoolId(int encChn)
{
    int channel;

    (void)encChn;
    for (channel = 0; channel < EXTRAS_ENC_CHANNELS; channel++) {
        if (IMP_Encoder_GetPool(channel) >= 0)
            (void)IMP_Encoder_SetPool(channel, -1);
    }
    return 0;
}

/* IMP_Encoder_GetStream is GetStream_Impl(chn, stream, block, 0); "force"
 * only lets libimp's own drain path read a channel that stopped receiving. */
int IMP_Encoder_GetStream_Impl(int encChn, void *stream, int block, int force)
{
    (void)force;
    return IMP_Encoder_GetStream(encChn, stream, block);
}

/* libimp reserves "size" bytes of rmem per pool and channels bound with
 * IMP_FrameSource_SetPool/IMP_Encoder_SetPool allocate from it. OpenIMP
 * allocates every channel from the one rmem arena, so a pool is a name only:
 * reserving memory nothing would draw from would just shrink the arena. */
int IMP_System_MemPoolRequest(int poolId, size_t size, const char *name)
{
    if (poolId < 0 || poolId >= EXTRAS_POOLS || size == 0)
        return -1;
    pthread_mutex_lock(&extras_lock);
    if (extras_pools[poolId].requested) {
        pthread_mutex_unlock(&extras_lock);
        return -1;          /* libimp: "already request" */
    }
    extras_pools[poolId].requested = 1;
    extras_pools[poolId].size = size;
    memset(extras_pools[poolId].name, 0, sizeof(extras_pools[poolId].name));
    if (name)
        strncpy(extras_pools[poolId].name, name,
                sizeof(extras_pools[poolId].name) - 1);
    pthread_mutex_unlock(&extras_lock);
    return 0;
}

int IMP_System_MemPoolFree(int poolId)
{
    if (poolId < 0 || poolId >= EXTRAS_POOLS)
        return -1;
    pthread_mutex_lock(&extras_lock);
    if (!extras_pools[poolId].requested) {
        pthread_mutex_unlock(&extras_lock);
        return -1;          /* libimp: "pool null error" */
    }
    memset(&extras_pools[poolId], 0, sizeof(extras_pools[poolId]));
    pthread_mutex_unlock(&extras_lock);
    return 0;
}

/* libimp system_attach(): insert "from" between "to" and its current bind
 * source, i.e. src->to becomes src->from->to, rolling back on failure. The
 * encoder resolves its OSD group at RegisterChn/StartRecvPic, so attach the
 * OSD group before starting the channel (the usual order). */
int IMP_OSD_AttachToGroup(IMPCell *from, IMPCell *to)
{
    IMPCell source;

    if (!from || !to)
        return -1;
    if (IMP_System_GetBindbyDest(to, &source) != 0)
        return -1;
    if (IMP_System_UnBind(&source, to) != 0)
        return -1;
    if (IMP_System_Bind(&source, from) != 0) {
        (void)IMP_System_Bind(&source, to);
        return -1;
    }
    if (IMP_System_Bind(from, to) != 0) {
        (void)IMP_System_UnBind(&source, from);
        (void)IMP_System_Bind(&source, to);
        return -1;
    }
    return 0;
}

/* libimp registers impdbg shared-memory dumpers here (dsys); OpenIMP has
 * no impdbg server, so there is nothing to register. */
int IMP_AI_IMPDBG_Init(void)
{
    return 0;
}

int IMP_AO_IMPDBG_Init(void)
{
    return 0;
}

/* Vendor IMPPixelFormat numbering (T31 1.1.6 imp_common.h). */
static const char *const extras_pixfmt_names[] = {
    "YUV420Planar", "YUYV422", "UYVY422", "YUV422P", "YUV444P", "YUV410P",
    "YUV411P", "GRAY8", "MONOWHITE", "MONOBLACK", "NV12", "NV21", "RGB24",
    "BGR24", "ARGB", "RGBA", "ABGR", "BGRA", "RGB565BE", "RGB565LE",
    "RGB555BE", "RGB555LE", "BGR565BE", "BGR565LE", "BGR555BE", "BGR555LE",
    "0RGB", "RGB0", "0BGR", "BGR0", "BGGR8", "RGGB8", "GBRG8", "GRBG8",
};

char *IMPPixfmtToString(int pixfmt)
{
    if (pixfmt < 0 ||
        (size_t)pixfmt >= sizeof(extras_pixfmt_names) /
                              sizeof(extras_pixfmt_names[0]))
        return NULL;
    return (char *)extras_pixfmt_names[pixfmt];
}
