/**
 * IMP FrameSource Module Implementation
 * Based on reverse engineering of libimp.so v1.1.6
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <imp/imp_framesource.h>

#define LOG_FS(fmt, ...) fprintf(stderr, "[FrameSource] " fmt "\n", ##__VA_ARGS__)

/* FrameSource channel structure - 0x2e8 bytes per channel */
#define MAX_FS_CHANNELS 5
#define FS_CHANNEL_SIZE 0x2e8

typedef struct {
    uint8_t data_00[0x1c];      /* 0x00-0x1b: Initial data */
    uint32_t state;             /* 0x1c: Channel state (0=disabled, 1=enabled, 2=running) */
    IMPFSChnAttr attr;          /* 0x20: Channel attributes (0x50 bytes) */
    uint8_t data_70[0xe8];      /* 0x70-0x157: More data */
    uint32_t mode;              /* 0x58: Mode (0=normal, 1=special) */
    uint8_t data_15c[0x64];     /* 0x15c-0x1bf: More data */
    pthread_t thread;           /* 0x1c0: Channel thread */
    int fd;                     /* 0x1c4: File descriptor for /dev/framechanN */
    void *module_ptr;           /* 0x1c8: Pointer to module */
    uint8_t data_1cc[0x70];     /* 0x1cc-0x23b: More data */
    void *special_data;         /* 0x23c: Special mode data */
    uint8_t data_240[0x4];      /* 0x240-0x243: Padding */
    sem_t sem;                  /* 0x244: Semaphore */
    uint8_t data_254[0x94];     /* 0x254-end: Rest of structure */
} FSChannel;

/* Global framesource state */
typedef struct {
    uint8_t data_00[0x10];      /* 0x00-0x0f: Header */
    uint32_t active_count;      /* 0x10: Number of active channels */
    uint32_t special_count;     /* 0x14: Number of special mode channels */
    uint8_t data_18[0x8];       /* 0x18-0x1f: More data */
    FSChannel channels[MAX_FS_CHANNELS]; /* 0x20: Channel array */
} FrameSourceState;

/* Global variables */
static FrameSourceState *gFramesource = NULL;
static pthread_mutex_t fs_mutex = PTHREAD_MUTEX_INITIALIZER;
static int fs_initialized = 0;

/* Initialize framesource module */
static void framesource_init(void) {
    if (fs_initialized) return;

    gFramesource = (FrameSourceState*)calloc(1, sizeof(FrameSourceState));
    if (gFramesource == NULL) {
        LOG_FS("Failed to allocate gFramesource");
        return;
    }

    /* Initialize all channels */
    for (int i = 0; i < MAX_FS_CHANNELS; i++) {
        gFramesource->channels[i].state = 0;
        gFramesource->channels[i].fd = -1;
    }

    fs_initialized = 1;
}

/* IMP_FrameSource_CreateChn - based on decompilation at 0x9d5d4 */
int IMP_FrameSource_CreateChn(int chnNum, IMPFSChnAttr *chn_attr) {
    if (chn_attr == NULL) {
        LOG_FS("CreateChn failed: NULL attr");
        return -1;
    }

    if (chnNum < 0 || chnNum >= MAX_FS_CHANNELS) {
        LOG_FS("CreateChn failed: invalid channel %d", chnNum);
        return -1;
    }

    pthread_mutex_lock(&fs_mutex);

    framesource_init();

    if (gFramesource == NULL) {
        pthread_mutex_unlock(&fs_mutex);
        return -1;
    }

    /* Set attributes */
    memcpy(&gFramesource->channels[chnNum].attr, chn_attr, sizeof(IMPFSChnAttr));
    gFramesource->channels[chnNum].state = 0; /* Disabled */
    gFramesource->channels[chnNum].fd = -1;

    /* TODO: Open /dev/framechanN device */
    /* TODO: Configure via ioctl */
    /* TODO: Create VBM pool */
    /* TODO: Spawn thread */

    pthread_mutex_unlock(&fs_mutex);

    LOG_FS("CreateChn: chn=%d, %dx%d, fmt=0x%x", chnNum,
           chn_attr->picWidth, chn_attr->picHeight, chn_attr->pixFmt);
    return 0;
}

/* IMP_FrameSource_DestroyChn - based on decompilation at 0x9dfdc */
int IMP_FrameSource_DestroyChn(int chnNum) {
    if (chnNum < 0 || chnNum >= MAX_FS_CHANNELS) {
        LOG_FS("DestroyChn failed: invalid channel %d", chnNum);
        return -1;
    }

    pthread_mutex_lock(&fs_mutex);

    if (gFramesource == NULL) {
        pthread_mutex_unlock(&fs_mutex);
        return 0;
    }

    /* Disable if enabled */
    if (gFramesource->channels[chnNum].state != 0) {
        pthread_mutex_unlock(&fs_mutex);
        IMP_FrameSource_DisableChn(chnNum);
        pthread_mutex_lock(&fs_mutex);
    }

    /* TODO: Close device */
    /* TODO: Destroy VBM pool */
    /* TODO: Free resources */

    memset(&gFramesource->channels[chnNum], 0, FS_CHANNEL_SIZE);
    gFramesource->channels[chnNum].fd = -1;

    pthread_mutex_unlock(&fs_mutex);

    LOG_FS("DestroyChn: chn=%d", chnNum);
    return 0;
}

/* IMP_FrameSource_EnableChn - based on decompilation at 0x9ecf8 */
int IMP_FrameSource_EnableChn(int chnNum) {
    if (chnNum < 0 || chnNum >= MAX_FS_CHANNELS) {
        LOG_FS("EnableChn failed: invalid channel %d", chnNum);
        return -1;
    }

    pthread_mutex_lock(&fs_mutex);

    if (gFramesource == NULL) {
        LOG_FS("EnableChn failed: not initialized");
        pthread_mutex_unlock(&fs_mutex);
        return -1;
    }

    if (gFramesource->channels[chnNum].state == 2) {
        LOG_FS("EnableChn: channel %d already enabled", chnNum);
        pthread_mutex_unlock(&fs_mutex);
        return 0;
    }

    /* TODO: Configure via ioctl */
    /* TODO: Start thread */
    /* TODO: Create VBM pool */

    gFramesource->channels[chnNum].state = 2; /* Running */
    gFramesource->active_count++;

    pthread_mutex_unlock(&fs_mutex);

    LOG_FS("EnableChn: chn=%d", chnNum);
    return 0;
}

/* IMP_FrameSource_DisableChn - based on decompilation at 0xa03bc */
int IMP_FrameSource_DisableChn(int chnNum) {
    if (chnNum < 0 || chnNum >= MAX_FS_CHANNELS) {
        LOG_FS("DisableChn failed: invalid channel %d", chnNum);
        return -1;
    }

    pthread_mutex_lock(&fs_mutex);

    if (gFramesource == NULL) {
        LOG_FS("DisableChn failed: not initialized");
        pthread_mutex_unlock(&fs_mutex);
        return -1;
    }

    if (gFramesource->channels[chnNum].state != 2) {
        LOG_FS("DisableChn: channel %d not enabled", chnNum);
        pthread_mutex_unlock(&fs_mutex);
        return 0;
    }

    /* TODO: Cancel thread */
    /* TODO: Flush pipeline */
    /* TODO: Disable via ioctl */
    /* TODO: Destroy VBM pool */

    gFramesource->channels[chnNum].state = 1; /* Disabled but created */
    gFramesource->active_count--;

    pthread_mutex_unlock(&fs_mutex);

    LOG_FS("DisableChn: chn=%d", chnNum);
    return 0;
}

/* IMP_FrameSource_SetChnAttr - based on decompilation at 0x9c890 */
int IMP_FrameSource_SetChnAttr(int chnNum, IMPFSChnAttr *chn_attr) {
    if (chn_attr == NULL) {
        LOG_FS("SetChnAttr failed: NULL attr");
        return -1;
    }

    if (chnNum < 0 || chnNum >= MAX_FS_CHANNELS) {
        LOG_FS("SetChnAttr failed: invalid channel %d", chnNum);
        return -1;
    }

    /* Check format - must be aligned and valid */
    if ((chn_attr->pixFmt & 0xf) != 0 && chn_attr->pixFmt != 0x22) {
        LOG_FS("SetChnAttr failed: invalid format 0x%x", chn_attr->pixFmt);
        return -1;
    }

    /* Channel 0 cannot have crop enabled */
    if (chnNum == 0 && chn_attr->crop.enable != 0) {
        LOG_FS("SetChnAttr failed: channel 0 cannot have crop enabled");
        return -1;
    }

    pthread_mutex_lock(&fs_mutex);

    framesource_init();

    if (gFramesource == NULL) {
        pthread_mutex_unlock(&fs_mutex);
        return -1;
    }

    /* Copy attributes - from decompilation, copies 0x50 bytes */
    memcpy(&gFramesource->channels[chnNum].attr, chn_attr, sizeof(IMPFSChnAttr));

    pthread_mutex_unlock(&fs_mutex);

    LOG_FS("SetChnAttr: chn=%d, %dx%d, fmt=0x%x", chnNum,
           chn_attr->picWidth, chn_attr->picHeight, chn_attr->pixFmt);
    return 0;
}

/* IMP_FrameSource_GetChnAttr - based on decompilation at 0x9cb20 */
int IMP_FrameSource_GetChnAttr(int chnNum, IMPFSChnAttr *chn_attr) {
    if (chn_attr == NULL) {
        LOG_FS("GetChnAttr failed: NULL attr");
        return -1;
    }

    if (chnNum < 0 || chnNum >= MAX_FS_CHANNELS) {
        LOG_FS("GetChnAttr failed: invalid channel %d", chnNum);
        return -1;
    }

    pthread_mutex_lock(&fs_mutex);

    if (gFramesource == NULL) {
        LOG_FS("GetChnAttr failed: not initialized");
        pthread_mutex_unlock(&fs_mutex);
        return -1;
    }

    /* Check if channel has attributes set */
    if (gFramesource->channels[chnNum].attr.picWidth == 0) {
        LOG_FS("GetChnAttr failed: channel %d not configured", chnNum);
        pthread_mutex_unlock(&fs_mutex);
        return -1;
    }

    /* Copy attributes - from decompilation, copies 0x50 bytes */
    memcpy(chn_attr, &gFramesource->channels[chnNum].attr, sizeof(IMPFSChnAttr));

    pthread_mutex_unlock(&fs_mutex);

    LOG_FS("GetChnAttr: chn=%d", chnNum);
    return 0;
}

int IMP_FrameSource_SetChnFifoAttr(int chnNum, IMPFSChnFifoAttr *attr) {
    if (attr == NULL) return -1;
    LOG_FS("SetChnFifoAttr: chn=%d", chnNum);
    return 0;
}

int IMP_FrameSource_GetChnFifoAttr(int chnNum, IMPFSChnFifoAttr *attr) {
    if (attr == NULL) return -1;
    LOG_FS("GetChnFifoAttr: chn=%d", chnNum);
    memset(attr, 0, sizeof(*attr));
    return 0;
}

int IMP_FrameSource_SetFrameDepth(int chnNum, int depth) {
    LOG_FS("SetFrameDepth: chn=%d, depth=%d", chnNum, depth);
    return 0;
}

int IMP_FrameSource_SetChnRotate(int chnNum, int rotation, int height, int width) {
    LOG_FS("SetChnRotate: chn=%d, rotation=%d, %dx%d", chnNum, rotation, width, height);
    return 0;
}

