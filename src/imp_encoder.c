/**
 * IMP Encoder Module Implementation
 * Based on reverse engineering of libimp.so v1.1.6
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <imp/imp_encoder.h>
#include "fifo.h"

#define LOG_ENC(fmt, ...) fprintf(stderr, "[Encoder] " fmt "\n", ##__VA_ARGS__)
#define FIFO_SIZE 64  /* Size of Fifo structure */

/* External codec functions (stubs implemented at end of file) */
int AL_Codec_Encode_Create(void **codec, void *params);
int AL_Codec_Encode_Destroy(void *codec);
int AL_Codec_Encode_GetSrcFrameCntAndSize(void *codec, int *cnt, int *size);
int AL_Codec_Encode_GetSrcStreamCntAndSize(void *codec, int *cnt, int *size);
int AL_Codec_Encode_SetDefaultParam(void *params);

/* Encoder channel structure - 0x308 bytes per channel */
#define MAX_ENC_CHANNELS 9
#define ENC_CHANNEL_SIZE 0x308
#define MAX_ENC_GROUPS 3

typedef struct {
    int chn_id;                 /* 0x00: Channel ID (-1 = unused) */
    uint8_t data_04[0x4];       /* 0x04-0x07: Padding */
    void *codec;                /* 0x08: AL_Codec handle (offset arg1[2]) */
    int src_frame_cnt;          /* 0x0c: Source frame count (offset arg1[3]) */
    int src_frame_size;         /* 0x10: Source frame size (offset arg1[4]) */
    void *frame_buffers;        /* 0x14: Frame buffer array (offset arg1[5]) */
    uint8_t fifo[FIFO_SIZE];    /* 0x18: Fifo structure (offset arg1[6]) */
    uint8_t data_58[0x40];      /* 0x58-0x97: Padding to 0x98 */
    IMPEncoderCHNAttr attr;     /* 0x98: Channel attributes (0x70 bytes) */
    uint8_t data_108[0x8c];     /* 0x108-0x193: Padding */
    uint8_t data_194[0x44];     /* 0x194-0x1d7: Padding */
    pthread_mutex_t mutex_1d8;  /* 0x1d8: Mutex (offset arg1[0x6a]) */
    pthread_cond_t cond_1f0;    /* 0x1f0: Condition variable */
    uint8_t data_208[0x18];     /* 0x208-0x21f: Padding */
    int eventfd;                /* 0x220: Event FD */
    uint8_t data_224[0x70];     /* 0x224-0x293: Padding */
    void *group_ptr;            /* 0x294: Pointer to group */
    uint8_t data_298[0x100];    /* 0x298-0x397: More data */
    uint8_t registered;         /* 0x398: Registration flag */
    uint8_t data_399[0x7];      /* 0x399-0x39f: Padding */
    uint8_t data_3a0[0x4];      /* 0x3a0-0x3a3: More data */
    uint8_t enabled;            /* 0x3a4: Enable flag */
    uint8_t data_3a5[0x3];      /* 0x3a5-0x3a7: Padding */
    uint8_t data_3a8[0x4];      /* 0x3a8-0x3ab: More data */
    uint8_t started;            /* 0x3ac: Started flag */
    uint8_t data_3ad[0x53];     /* 0x3ad-0x3ff: More data */
    uint8_t recv_pic_enabled;   /* 0x400: Receive picture enabled */
    uint8_t data_401[0x3];      /* 0x401-0x403: Padding */
    uint8_t recv_pic_started;   /* 0x404: Receive picture started */
    uint8_t data_405[0x3];      /* 0x405-0x407: Padding */
    sem_t sem_408;              /* 0x408: Semaphore (offset arg1[0x102]) */
    sem_t sem_418;              /* 0x418: Semaphore (offset arg1[0x106]) */
    sem_t sem_428;              /* 0x428: Semaphore (offset arg1[0x10a]) */
    pthread_mutex_t mutex_438;  /* 0x438: Mutex (offset arg1[0x10e]) */
    pthread_mutex_t mutex_450;  /* 0x450: Mutex (offset arg1[0x114]) */
    pthread_t thread_encoder;   /* 0x468: Encoder thread (offset arg1[0x1a]) */
    pthread_t thread_stream;    /* 0x46c: Stream thread (offset arg1[0xf]) */
    uint8_t data_470[0x98];     /* 0x470-0x507: Padding */
    /* Total: 0x308 bytes (776 bytes) */
} EncChannel;

/* Encoder group structure */
typedef struct {
    int group_id;               /* 0x00: Group ID */
    uint32_t field_04;          /* 0x04: Field */
    uint32_t chn_count;         /* 0x08: Number of registered channels */
    EncChannel *channels[3];    /* 0x0c-0x14: Up to 3 channels per group */
} EncGroup;

/* Global encoder state */
typedef struct {
    void *module_ptr;           /* 0x00: Module pointer */
    EncGroup groups[6];         /* 0x04: Up to 6 groups */
} EncoderState;

/* Global variables */
static EncoderState *gEncoder = NULL;
static EncChannel g_EncChannel[MAX_ENC_CHANNELS];
static pthread_mutex_t encoder_mutex = PTHREAD_MUTEX_INITIALIZER;
static int encoder_initialized = 0;

/* Forward declarations for helper functions */
static int channel_encoder_init(EncChannel *chn);
static int channel_encoder_exit(EncChannel *chn);
static int channel_encoder_set_rc_param(void *dst, IMPEncoderRcAttr *src);
static void *encoder_thread(void *arg);
static void *stream_thread(void *arg);

/* Initialize encoder module */
static void encoder_init(void) {
    if (encoder_initialized) return;

    /* Initialize all channels as unused */
    for (int i = 0; i < MAX_ENC_CHANNELS; i++) {
        memset(&g_EncChannel[i], 0, ENC_CHANNEL_SIZE);
        g_EncChannel[i].chn_id = -1;
    }

    encoder_initialized = 1;
}

/* IMP_Encoder_CreateGroup - based on decompilation at 0x82658 */
int IMP_Encoder_CreateGroup(int encGroup) {
    if (encGroup < 0 || encGroup >= 6) {
        LOG_ENC("CreateGroup failed: invalid group %d", encGroup);
        return -1;
    }

    pthread_mutex_lock(&encoder_mutex);

    /* Initialize encoder module */
    encoder_init();

    /* Initialize gEncoder if needed */
    if (gEncoder == NULL) {
        gEncoder = (EncoderState*)calloc(1, sizeof(EncoderState));
        if (gEncoder == NULL) {
            pthread_mutex_unlock(&encoder_mutex);
            return -1;
        }

        /* Initialize all groups */
        for (int i = 0; i < 6; i++) {
            gEncoder->groups[i].group_id = -1;
            gEncoder->groups[i].chn_count = 0;
            gEncoder->groups[i].channels[0] = NULL;
            gEncoder->groups[i].channels[1] = NULL;
            gEncoder->groups[i].channels[2] = NULL;
        }
    }

    /* Check if group already exists */
    if (gEncoder->groups[encGroup].group_id >= 0) {
        LOG_ENC("CreateGroup: group %d already exists", encGroup);
        pthread_mutex_unlock(&encoder_mutex);
        return 0;
    }

    /* Initialize the group */
    gEncoder->groups[encGroup].group_id = encGroup;
    gEncoder->groups[encGroup].chn_count = 0;

    pthread_mutex_unlock(&encoder_mutex);

    LOG_ENC("CreateGroup: grp=%d", encGroup);
    return 0;
}

/* IMP_Encoder_DestroyGroup - based on decompilation at 0x827f4 */
int IMP_Encoder_DestroyGroup(int encGroup) {
    if (encGroup < 0 || encGroup >= 6) {
        LOG_ENC("DestroyGroup failed: invalid group %d", encGroup);
        return -1;
    }

    pthread_mutex_lock(&encoder_mutex);

    if (gEncoder == NULL || gEncoder->groups[encGroup].group_id < 0) {
        LOG_ENC("DestroyGroup: group %d doesn't exist", encGroup);
        pthread_mutex_unlock(&encoder_mutex);
        return -1;
    }

    /* Check if any channels are still registered */
    if (gEncoder->groups[encGroup].channels[0] != NULL ||
        gEncoder->groups[encGroup].channels[1] != NULL ||
        gEncoder->groups[encGroup].channels[2] != NULL) {
        LOG_ENC("DestroyGroup failed: group %d still has channels", encGroup);
        pthread_mutex_unlock(&encoder_mutex);
        return -1;
    }

    /* Destroy the group */
    gEncoder->groups[encGroup].group_id = -1;
    gEncoder->groups[encGroup].chn_count = 0;

    pthread_mutex_unlock(&encoder_mutex);

    LOG_ENC("DestroyGroup: grp=%d", encGroup);
    return 0;
}

/* IMP_Encoder_CreateChn - based on decompilation at 0x836e0 */
int IMP_Encoder_CreateChn(int encChn, IMPEncoderCHNAttr *attr) {
    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS) {
        LOG_ENC("CreateChn failed: invalid channel %d", encChn);
        return -1;
    }

    if (attr == NULL) {
        LOG_ENC("CreateChn failed: NULL attr");
        return -1;
    }

    pthread_mutex_lock(&encoder_mutex);

    EncChannel *chn = &g_EncChannel[encChn];

    /* Check if channel already exists */
    if (chn->chn_id >= 0) {
        LOG_ENC("CreateChn: channel %d already exists", encChn);
        pthread_mutex_unlock(&encoder_mutex);
        return -1;
    }

    /* Clear channel structure (from decompilation: memset 0x308 bytes) */
    memset(chn, 0, ENC_CHANNEL_SIZE);

    /* Copy attributes */
    memcpy(&chn->attr, attr, sizeof(IMPEncoderCHNAttr));

    /* Set channel ID and flags */
    chn->chn_id = encChn;
    chn->registered = 1;  /* Mark as created (offset 0x3ac in decompilation) */
    chn->enabled = 0;
    chn->started = 1;     /* Mark as initialized (offset 0x404 in decompilation) */

    /* Initialize semaphores (from decompilation at 0x83d18) */
    /* sem_init at offset 0x418 with value 0 */
    sem_init(&chn->sem_418, 0, 0);

    /* sem_init at offset 0x408 with buffer count value */
    int buf_count = 4; /* Default buffer count */
    sem_init(&chn->sem_408, 0, buf_count);

    /* Initialize mutexes (from decompilation at 0x83d30) */
    if (pthread_mutex_init(&chn->mutex_438, NULL) < 0) {
        LOG_ENC("CreateChn: failed to init mutex_438");
        chn->chn_id = -1;
        pthread_mutex_unlock(&encoder_mutex);
        return -1;
    }

    /* Initialize recursive mutex (from decompilation at 0x83d50) */
    pthread_mutexattr_t attr_recursive;
    if (pthread_mutexattr_init(&attr_recursive) < 0) {
        LOG_ENC("CreateChn: failed to init mutexattr");
        pthread_mutex_unlock(&encoder_mutex);
        return -1;
    }

    pthread_mutexattr_settype(&attr_recursive, PTHREAD_MUTEX_RECURSIVE);

    if (pthread_mutex_init(&chn->mutex_450, &attr_recursive) < 0) {
        LOG_ENC("CreateChn: failed to init mutex_450");
        chn->chn_id = -1;
        pthread_mutex_unlock(&encoder_mutex);
        return -1;
    }

    pthread_mutexattr_destroy(&attr_recursive);

    /* Initialize semaphore at 0x428 (from decompilation at 0x83d70) */
    if (sem_init(&chn->sem_428, 0, 0) < 0) {
        LOG_ENC("CreateChn: failed to init sem_428");
        chn->chn_id = -1;
        pthread_mutex_unlock(&encoder_mutex);
        return -1;
    }

    /* Initialize encoder (from decompilation: channel_encoder_init) */
    if (channel_encoder_init(chn) < 0) {
        LOG_ENC("CreateChn: channel_encoder_init failed");

        /* Cleanup on failure */
        if (chn->frame_buffers != NULL) {
            free(chn->frame_buffers);
            chn->frame_buffers = NULL;
        }

        chn->chn_id = -1;
        pthread_mutex_unlock(&encoder_mutex);
        return -1;
    }

    /* Allocate stream buffers (from decompilation at 0x83e10) */
    int stream_cnt = buf_count;
    int stream_size = 0x38; /* Size per stream buffer */
    size_t total_size = stream_cnt * stream_size * 7;

    void **buf_ptr = (void**)&chn->data_298[0];
    *buf_ptr = calloc(total_size, 1);
    if (*buf_ptr == NULL) {
        LOG_ENC("CreateChn: failed to allocate stream buffers");
        channel_encoder_exit(chn);
        chn->chn_id = -1;
        pthread_mutex_unlock(&encoder_mutex);
        return -1;
    }

    /* Initialize mutex and condition for frame handling */
    pthread_mutex_init(&chn->mutex_1d8, NULL);
    pthread_cond_init(&chn->cond_1f0, NULL);

    /* Create eventfd (from decompilation at 0x840a0) */
    chn->eventfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (chn->eventfd < 0) {
        LOG_ENC("CreateChn: failed to create eventfd");
        channel_encoder_exit(chn);
        if (chn->frame_buffers != NULL) {
            free(chn->frame_buffers);
        }
        chn->chn_id = -1;
        pthread_mutex_unlock(&encoder_mutex);
        return -1;
    }

    pthread_mutex_unlock(&encoder_mutex);

    LOG_ENC("CreateChn: chn=%d, profile=%d created successfully", encChn, attr->encAttr.profile);
    return 0;
}

/* IMP_Encoder_DestroyChn - based on decompilation at 0x85c30 */
int IMP_Encoder_DestroyChn(int encChn) {
    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS) {
        LOG_ENC("DestroyChn failed: invalid channel %d", encChn);
        return -1;
    }

    pthread_mutex_lock(&encoder_mutex);

    if (g_EncChannel[encChn].chn_id < 0) {
        LOG_ENC("DestroyChn: channel %d doesn't exist", encChn);
        pthread_mutex_unlock(&encoder_mutex);
        return -1;
    }

    /* Stop receiving pictures if started */
    if (g_EncChannel[encChn].recv_pic_enabled) {
        pthread_mutex_unlock(&encoder_mutex);
        IMP_Encoder_StopRecvPic(encChn);
        pthread_mutex_lock(&encoder_mutex);
    }

    /* Unregister if registered */
    if (g_EncChannel[encChn].registered) {
        pthread_mutex_unlock(&encoder_mutex);
        IMP_Encoder_UnRegisterChn(encChn);
        pthread_mutex_lock(&encoder_mutex);
    }

    /* TODO: Call channel_encoder_exit() */
    /* TODO: Free buffers */
    /* TODO: Destroy semaphores, mutexes */
    /* TODO: Close file descriptors */

    /* Clear the channel */
    memset(&g_EncChannel[encChn], 0, ENC_CHANNEL_SIZE);
    g_EncChannel[encChn].chn_id = -1;

    pthread_mutex_unlock(&encoder_mutex);

    LOG_ENC("DestroyChn: chn=%d", encChn);
    return 0;
}

/* IMP_Encoder_RegisterChn - based on decompilation at 0x84634 */
int IMP_Encoder_RegisterChn(int encGroup, int encChn) {
    if (encGroup < 0 || encGroup >= 6) {
        LOG_ENC("RegisterChn failed: invalid group %d", encGroup);
        return -1;
    }

    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS) {
        LOG_ENC("RegisterChn failed: invalid channel %d", encChn);
        return -1;
    }

    pthread_mutex_lock(&encoder_mutex);

    /* Check if channel exists */
    if (g_EncChannel[encChn].chn_id < 0) {
        LOG_ENC("RegisterChn failed: channel %d not created", encChn);
        pthread_mutex_unlock(&encoder_mutex);
        return -1;
    }

    if (gEncoder == NULL || gEncoder->groups[encGroup].group_id < 0) {
        LOG_ENC("RegisterChn failed: group %d not created", encGroup);
        pthread_mutex_unlock(&encoder_mutex);
        return -1;
    }

    /* Find empty slot in group (max 3 channels per group) */
    EncGroup *grp = &gEncoder->groups[encGroup];

    if (grp->channels[0] == NULL) {
        grp->channels[0] = &g_EncChannel[encChn];
    } else if (grp->channels[1] == NULL) {
        grp->channels[1] = &g_EncChannel[encChn];
    } else if (grp->channels[2] == NULL) {
        grp->channels[2] = &g_EncChannel[encChn];
    } else {
        LOG_ENC("RegisterChn failed: group %d is full", encGroup);
        pthread_mutex_unlock(&encoder_mutex);
        return -1;
    }

    grp->chn_count++;
    g_EncChannel[encChn].group_ptr = grp;
    g_EncChannel[encChn].registered = 1;

    pthread_mutex_unlock(&encoder_mutex);

    LOG_ENC("RegisterChn: grp=%d, chn=%d", encGroup, encChn);
    return 0;
}

/* IMP_Encoder_UnRegisterChn - based on decompilation at 0x84838 */
int IMP_Encoder_UnRegisterChn(int encChn) {
    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS) {
        LOG_ENC("UnRegisterChn failed: invalid channel %d", encChn);
        return -1;
    }

    pthread_mutex_lock(&encoder_mutex);

    if (g_EncChannel[encChn].chn_id < 0) {
        LOG_ENC("UnRegisterChn failed: channel %d not created", encChn);
        pthread_mutex_unlock(&encoder_mutex);
        return -1;
    }

    EncGroup *grp = (EncGroup*)g_EncChannel[encChn].group_ptr;

    if (grp == NULL) {
        LOG_ENC("UnRegisterChn: channel %d not registered", encChn);
        pthread_mutex_unlock(&encoder_mutex);
        return 0;
    }

    /* Remove from group */
    if (grp->channels[0] == &g_EncChannel[encChn]) {
        grp->channels[0] = NULL;
    } else if (grp->channels[1] == &g_EncChannel[encChn]) {
        grp->channels[1] = NULL;
    } else if (grp->channels[2] == &g_EncChannel[encChn]) {
        grp->channels[2] = NULL;
    }

    grp->chn_count--;
    g_EncChannel[encChn].registered = 0;

    pthread_mutex_unlock(&encoder_mutex);

    LOG_ENC("UnRegisterChn: chn=%d", encChn);
    return 0;
}

/* IMP_Encoder_StartRecvPic - based on decompilation at 0x849c8 */
int IMP_Encoder_StartRecvPic(int encChn) {
    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS) {
        LOG_ENC("StartRecvPic failed: invalid channel %d", encChn);
        return -1;
    }

    pthread_mutex_lock(&encoder_mutex);

    if (g_EncChannel[encChn].chn_id < 0) {
        LOG_ENC("StartRecvPic failed: channel %d not created", encChn);
        pthread_mutex_unlock(&encoder_mutex);
        return -1;
    }

    /* Set flags at offsets 0x404 and 0x400 */
    g_EncChannel[encChn].recv_pic_started = 1;
    g_EncChannel[encChn].recv_pic_enabled = 1;

    pthread_mutex_unlock(&encoder_mutex);

    LOG_ENC("StartRecvPic: chn=%d", encChn);
    return 0;
}

/* IMP_Encoder_StopRecvPic - based on decompilation at 0x8591c */
int IMP_Encoder_StopRecvPic(int encChn) {
    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS) {
        LOG_ENC("StopRecvPic failed: invalid channel %d", encChn);
        return -1;
    }

    pthread_mutex_lock(&encoder_mutex);

    if (g_EncChannel[encChn].chn_id < 0) {
        LOG_ENC("StopRecvPic failed: channel %d not created", encChn);
        pthread_mutex_unlock(&encoder_mutex);
        return -1;
    }

    /* Clear flag at offset 0x400 */
    g_EncChannel[encChn].recv_pic_enabled = 0;

    /* TODO: The real implementation has complex logic to drain the pipeline */

    pthread_mutex_unlock(&encoder_mutex);

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

int IMP_Encoder_GetFd(int encChn) {
    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS) {
        return -1;
    }

    EncChannel *chn = &g_EncChannel[encChn];
    if (chn->chn_id < 0) {
        return -1;
    }

    LOG_ENC("GetFd: chn=%d, fd=%d", encChn, chn->eventfd);
    return chn->eventfd;
}

/* ========== Helper Functions ========== */

/* channel_encoder_init - based on decompilation at 0x8098c */
static int channel_encoder_init(EncChannel *chn) {
    if (chn == NULL) {
        return -1;
    }

    /* Set default codec parameters */
    uint8_t codec_params[0x7c0];
    memset(codec_params, 0, sizeof(codec_params));

    /* Create codec instance */
    if (AL_Codec_Encode_Create(&chn->codec, codec_params) < 0 || chn->codec == NULL) {
        LOG_ENC("channel_encoder_init: AL_Codec_Encode_Create failed");
        return -1;
    }

    /* Get source frame count and size */
    if (AL_Codec_Encode_GetSrcFrameCntAndSize(chn->codec, &chn->src_frame_cnt, &chn->src_frame_size) < 0) {
        LOG_ENC("channel_encoder_init: GetSrcFrameCntAndSize failed");
        AL_Codec_Encode_Destroy(chn->codec);
        return -1;
    }

    LOG_ENC("channel_encoder_init: frame_cnt=%d, frame_size=%d",
            chn->src_frame_cnt, chn->src_frame_size);

    /* Allocate frame buffers */
    size_t buf_size = chn->src_frame_cnt * 0x458; /* 0x458 bytes per frame buffer */
    chn->frame_buffers = calloc(buf_size, 1);
    if (chn->frame_buffers == NULL) {
        LOG_ENC("channel_encoder_init: failed to allocate frame buffers");
        AL_Codec_Encode_Destroy(chn->codec);
        return -1;
    }

    /* Initialize fifo */
    Fifo_Init(chn->fifo, chn->src_frame_cnt);

    /* Queue all buffers to fifo */
    for (int i = 0; i < chn->src_frame_cnt; i++) {
        void *buf = (uint8_t*)chn->frame_buffers + (i * 0x458);
        Fifo_Queue(chn->fifo, buf, -1);
    }

    /* Create encoder thread (from decompilation at 0x80b40) */
    if (pthread_create(&chn->thread_encoder, NULL, encoder_thread, chn) < 0) {
        LOG_ENC("channel_encoder_init: failed to create encoder thread");
        Fifo_Deinit(chn->fifo);
        free(chn->frame_buffers);
        AL_Codec_Encode_Destroy(chn->codec);
        return -1;
    }

    /* Create stream thread (from decompilation at 0x80b60) */
    if (pthread_create(&chn->thread_stream, NULL, stream_thread, chn) < 0) {
        LOG_ENC("channel_encoder_init: failed to create stream thread");
        pthread_cancel(chn->thread_encoder);
        pthread_join(chn->thread_encoder, NULL);
        Fifo_Deinit(chn->fifo);
        free(chn->frame_buffers);
        AL_Codec_Encode_Destroy(chn->codec);
        return -1;
    }

    return 0;
}

/* channel_encoder_exit - based on decompilation at 0x806e8 */
static int channel_encoder_exit(EncChannel *chn) {
    if (chn == NULL || chn->codec == NULL) {
        return -1;
    }

    /* Cancel threads */
    if (chn->thread_encoder) {
        pthread_cancel(chn->thread_encoder);
        pthread_join(chn->thread_encoder, NULL);
    }

    if (chn->thread_stream) {
        pthread_cancel(chn->thread_stream);
        pthread_join(chn->thread_stream, NULL);
    }

    /* Cleanup fifo */
    Fifo_Deinit(chn->fifo);

    /* Free buffers */
    if (chn->frame_buffers) {
        free(chn->frame_buffers);
        chn->frame_buffers = NULL;
    }

    /* Destroy codec */
    AL_Codec_Encode_Destroy(chn->codec);
    chn->codec = NULL;

    return 0;
}

/* channel_encoder_set_rc_param - stub */
static int channel_encoder_set_rc_param(void *dst, IMPEncoderRcAttr *src) {
    if (dst == NULL || src == NULL) {
        return -1;
    }

    /* TODO: Implement rate control parameter conversion */
    memcpy(dst, src, sizeof(IMPEncoderRcAttr));
    return 0;
}

/* Encoder thread - processes frames */
static void *encoder_thread(void *arg) {
    EncChannel *chn = (EncChannel*)arg;

    LOG_ENC("encoder_thread: started for channel %d", chn->chn_id);

    /* TODO: Implement actual encoding loop */
    while (1) {
        sleep(1);
    }

    return NULL;
}

/* Stream thread - handles stream output */
static void *stream_thread(void *arg) {
    EncChannel *chn = (EncChannel*)arg;

    LOG_ENC("stream_thread: started for channel %d", chn->chn_id);

    /* TODO: Implement actual stream handling */
    while (1) {
        sleep(1);
    }

    return NULL;
}

/* ========== Stub Implementations for External Functions ========== */

/* Stub implementations for AL_Codec functions */
int AL_Codec_Encode_Create(void **codec, void *params) {
    (void)params;
    *codec = malloc(1); /* Dummy codec handle */
    LOG_ENC("AL_Codec_Encode_Create: stub");
    return (*codec != NULL) ? 0 : -1;
}

int AL_Codec_Encode_Destroy(void *codec) {
    if (codec) {
        free(codec);
    }
    LOG_ENC("AL_Codec_Encode_Destroy: stub");
    return 0;
}

int AL_Codec_Encode_GetSrcFrameCntAndSize(void *codec, int *cnt, int *size) {
    (void)codec;
    *cnt = 4;      /* Default 4 frames */
    *size = 0x100000; /* 1MB per frame */
    LOG_ENC("AL_Codec_Encode_GetSrcFrameCntAndSize: stub cnt=%d size=%d", *cnt, *size);
    return 0;
}

int AL_Codec_Encode_GetSrcStreamCntAndSize(void *codec, int *cnt, int *size) {
    (void)codec;
    *cnt = 4;
    *size = 0x10000; /* 64KB per stream */
    LOG_ENC("AL_Codec_Encode_GetSrcStreamCntAndSize: stub");
    return 0;
}

int AL_Codec_Encode_SetDefaultParam(void *params) {
    memset(params, 0, 0x7c0);
    LOG_ENC("AL_Codec_Encode_SetDefaultParam: stub");
    return 0;
}

/* Fifo stub implementations */
void Fifo_Init(void *fifo, int size) {
    (void)fifo;
    (void)size;
    LOG_ENC("Fifo_Init: stub size=%d", size);
}

void Fifo_Deinit(void *fifo) {
    (void)fifo;
    LOG_ENC("Fifo_Deinit: stub");
}

int Fifo_Queue(void *fifo, void *item, int timeout) {
    (void)fifo;
    (void)item;
    (void)timeout;
    return 0;
}

