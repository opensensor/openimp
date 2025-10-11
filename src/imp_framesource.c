/**
 * IMP FrameSource Module Implementation
 * Based on reverse engineering of libimp.so v1.1.6
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <semaphore.h>
#include <imp/imp_framesource.h>
#include <imp/imp_system.h>
#include "kernel_interface.h"

/* External system functions */
extern void* IMP_System_GetModule(int deviceID, int groupID);
extern int IMP_System_RegisterModule(int deviceID, int groupID, void *module);
extern int notify_observers(void *module, void *frame);

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

/* ioctl command for frame polling - from decompilation at 0x99acc */
#define VIDIOC_POLL_FRAME   0x400456bf  /* Poll for frame availability */

/* Forward declarations */
static void *frame_capture_thread(void *arg);
static int framesource_bind(void *src_module, void *dst_module, void *output_ptr);
static int framesource_unbind(void *src_module, void *dst_module, void *output_ptr);

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

        /* Register module with system for each channel */
        void *module = IMP_System_GetModule(DEV_ID_FS, i);
        if (module != NULL) {
            /* Set bind/unbind function pointers at offsets 0x40 and 0x44 */
            uint8_t *mod_bytes = (uint8_t*)module;
            void **bind_ptr = (void**)(mod_bytes + 0x40);
            void **unbind_ptr = (void**)(mod_bytes + 0x44);

            *bind_ptr = (void*)framesource_bind;
            *unbind_ptr = (void*)framesource_unbind;

            LOG_FS("Registered bind/unbind for FS channel %d", i);
        }
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

    /* Open /dev/framechanN device */
    char dev_path[32];
    snprintf(dev_path, sizeof(dev_path), "/dev/framechan%d", chnNum);
    int fd = open(dev_path, O_RDWR);
    if (fd < 0) {
        LOG_FS("CreateChn: Failed to open %s: %s", dev_path, strerror(errno));
        /* Continue anyway - device may not exist on all platforms */
    } else {
        gFramesource->channels[chnNum].fd = fd;
        LOG_FS("CreateChn: Opened %s (fd=%d)", dev_path, fd);
    }

    /* Create VBM pool for this channel */
    /* VBMCreatePool expects (chn, fmt, ops, priv) */
    if (VBMCreatePool(chnNum, (void*)chn_attr, NULL, NULL) < 0) {
        LOG_FS("CreateChn: Failed to create VBM pool");
        if (fd >= 0) {
            close(fd);
            gFramesource->channels[chnNum].fd = -1;
        }
        pthread_mutex_unlock(&fs_mutex);
        return -1;
    }

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

    /* Close device */
    if (gFramesource->channels[chnNum].fd >= 0) {
        close(gFramesource->channels[chnNum].fd);
        LOG_FS("DestroyChn: Closed device fd=%d", gFramesource->channels[chnNum].fd);
    }

    /* Destroy VBM pool */
    if (VBMDestroyPool(chnNum) < 0) {
        LOG_FS("DestroyChn: Failed to destroy VBM pool");
    }

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

    FSChannel *chn = &gFramesource->channels[chnNum];

    /* Open device if not already open */
    if (chn->fd < 0) {
        chn->fd = fs_open_device(chnNum);
        if (chn->fd < 0) {
            LOG_FS("EnableChn failed: cannot open device");
            pthread_mutex_unlock(&fs_mutex);
            return -1;
        }
    }

    /* Prepare format structure */
    fs_format_t fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.enable = 1;
    fmt.width = chn->attr.picWidth;
    fmt.height = chn->attr.picHeight;
    fmt.pixfmt = chn->attr.pixFmt;

    /* Set format via ioctl */
    if (fs_set_format(chn->fd, &fmt) < 0) {
        LOG_FS("EnableChn failed: cannot set format");
        fs_close_device(chn->fd);
        chn->fd = -1;
        pthread_mutex_unlock(&fs_mutex);
        return -1;
    }

    /* Create VBM pool */
    if (VBMCreatePool(chnNum, &fmt, NULL, gFramesource) < 0) {
        LOG_FS("EnableChn failed: cannot create VBM pool");
        fs_close_device(chn->fd);
        chn->fd = -1;
        pthread_mutex_unlock(&fs_mutex);
        return -1;
    }

    /* Set buffer count */
    int bufcnt = fs_set_buffer_count(chn->fd, 4); /* Default 4 buffers */
    if (bufcnt < 0) {
        LOG_FS("EnableChn failed: cannot set buffer count");
        VBMDestroyPool(chnNum);
        fs_close_device(chn->fd);
        chn->fd = -1;
        pthread_mutex_unlock(&fs_mutex);
        return -1;
    }

    /* Fill VBM pool */
    if (VBMFillPool(chnNum) < 0) {
        LOG_FS("EnableChn failed: cannot fill VBM pool");
        VBMDestroyPool(chnNum);
        fs_close_device(chn->fd);
        chn->fd = -1;
        pthread_mutex_unlock(&fs_mutex);
        return -1;
    }

    /* Start streaming */
    if (fs_stream_on(chn->fd) < 0) {
        LOG_FS("EnableChn failed: cannot start streaming");
        VBMFlushFrame(chnNum);
        VBMDestroyPool(chnNum);
        fs_close_device(chn->fd);
        chn->fd = -1;
        pthread_mutex_unlock(&fs_mutex);
        return -1;
    }

    chn->state = 2; /* Running */
    gFramesource->active_count++;

    /* Create frame capture thread */
    if (pthread_create(&chn->thread, NULL, frame_capture_thread, chn) != 0) {
        LOG_FS("EnableChn failed: cannot create capture thread");
        chn->state = 0;
        gFramesource->active_count--;
        fs_stream_off(chn->fd);
        VBMFlushFrame(chnNum);
        VBMDestroyPool(chnNum);
        fs_close_device(chn->fd);
        chn->fd = -1;
        pthread_mutex_unlock(&fs_mutex);
        return -1;
    }

    pthread_mutex_unlock(&fs_mutex);

    LOG_FS("EnableChn: chn=%d enabled successfully", chnNum);
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

    FSChannel *chn = &gFramesource->channels[chnNum];

    if (chn->state != 2) {
        LOG_FS("DisableChn: channel %d not enabled", chnNum);
        pthread_mutex_unlock(&fs_mutex);
        return 0;
    }

    /* Set state to stopping */
    chn->state = 0;

    /* Cancel and wait for capture thread */
    if (chn->thread != 0) {
        pthread_cancel(chn->thread);
        pthread_join(chn->thread, NULL);
        chn->thread = 0;
    }

    /* Stop streaming */
    if (chn->fd >= 0) {
        fs_stream_off(chn->fd);
    }

    /* Flush and destroy VBM pool */
    VBMFlushFrame(chnNum);
    VBMDestroyPool(chnNum);

    /* Close device */
    if (chn->fd >= 0) {
        fs_close_device(chn->fd);
        chn->fd = -1;
    }

    chn->state = 1; /* Disabled but created */
    gFramesource->active_count--;

    pthread_mutex_unlock(&fs_mutex);

    LOG_FS("DisableChn: chn=%d disabled successfully", chnNum);
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

/**
 * Frame capture thread - based on decompilation at 0x99acc
 * Polls kernel driver for frames and queues them
 */
static void *frame_capture_thread(void *arg) {
    FSChannel *chn = (FSChannel*)arg;
    int chn_num = -1;

    /* Find channel number */
    for (int i = 0; i < MAX_FS_CHANNELS; i++) {
        if (&gFramesource->channels[i] == chn) {
            chn_num = i;
            break;
        }
    }

    if (chn_num < 0) {
        LOG_FS("frame_capture_thread: invalid channel");
        return NULL;
    }

    LOG_FS("frame_capture_thread: started for channel %d", chn_num);

    /* Main capture loop */
    while (1) {
        /* Check for thread cancellation */
        pthread_testcancel();

        /* Check if channel is still running */
        if (chn->state != 2) {
            usleep(10000); /* 10ms */
            continue;
        }

        /* Poll for frame availability using ioctl */
        int frame_ready = -1;
        if (ioctl(chn->fd, VIDIOC_POLL_FRAME, &frame_ready) < 0) {
            LOG_FS("frame_capture_thread: ioctl POLL_FRAME failed: %s", strerror(errno));
            usleep(10000); /* 10ms */
            continue;
        }

        if (frame_ready <= 0) {
            /* No frame available yet */
            usleep(1000); /* 1ms */
            continue;
        }

        /* Frame is available - get it from VBM */
        void *frame = NULL;
        if (VBMGetFrame(chn_num, &frame) == 0 && frame != NULL) {
            LOG_FS("frame_capture_thread: got frame %p from VBM", frame);

            /* Notify observers (bound modules like Encoder) */
            void *module = IMP_System_GetModule(DEV_ID_FS, chn_num);
            if (module != NULL) {
                notify_observers(module, frame);
            }

            /* Frame will be released when user calls IMP_FrameSource_ReleaseFrame */
        } else {
            LOG_FS("frame_capture_thread: VBMGetFrame failed");
            usleep(10000); /* 10ms */
        }
    }

    return NULL;
}

/* ========== Module Binding Functions ========== */

/* Observer structure - matches system module definition */
typedef struct Observer {
    struct Observer *next;      /* 0x00: Next observer in list */
    void *module;               /* 0x04: Observer module pointer */
    void *frame;                /* 0x08: Frame pointer */
    int output_index;           /* 0x0c: Output index */
} Observer;

/* External functions for observer management */
extern int add_observer_to_module(void *module, Observer *observer);
extern int remove_observer_from_module(void *src_module, void *dst_module);

/**
 * framesource_bind - Bind FrameSource to another module (e.g., Encoder)
 * This is called when IMP_System_Bind() is invoked
 */
static int framesource_bind(void *src_module, void *dst_module, void *output_ptr) {
    (void)output_ptr;

    if (src_module == NULL || dst_module == NULL) {
        LOG_FS("bind: NULL module");
        return -1;
    }

    LOG_FS("bind: Binding FrameSource to module");

    /* Create observer structure */
    Observer *observer = (Observer*)calloc(1, sizeof(Observer));
    if (observer == NULL) {
        LOG_FS("bind: Failed to allocate observer");
        return -1;
    }

    /* Initialize observer */
    observer->next = NULL;
    observer->module = dst_module;
    observer->frame = NULL;
    observer->output_index = 0;

    /* Add observer to source module's observer list */
    if (add_observer_to_module(src_module, observer) < 0) {
        LOG_FS("bind: Failed to add observer");
        free(observer);
        return -1;
    }

    LOG_FS("bind: Successfully bound FrameSource to module");
    return 0;
}

/**
 * framesource_unbind - Unbind FrameSource from another module
 */
static int framesource_unbind(void *src_module, void *dst_module, void *output_ptr) {
    (void)output_ptr;

    if (src_module == NULL || dst_module == NULL) {
        LOG_FS("unbind: NULL module");
        return -1;
    }

    LOG_FS("unbind: Unbinding FrameSource from module");

    /* Remove observer from source module's observer list */
    if (remove_observer_from_module(src_module, dst_module) < 0) {
        LOG_FS("unbind: Failed to remove observer");
        return -1;
    }

    return 0;
}

