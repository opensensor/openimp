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
#include <sys/select.h>

#include <sys/time.h>
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
/* Per-channel FIFO attributes and frame depth (for API parity) */
static IMPFSChnFifoAttr g_fifo_attrs[MAX_FS_CHANNELS];
static int g_frame_depth[MAX_FS_CHANNELS];


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

/* Public init to match OEM SystemInit calling pattern */
int FrameSourceInit(void) {
    pthread_mutex_lock(&fs_mutex);
    framesource_init();
    int ret = (gFramesource != NULL) ? 0 : -1;
    pthread_mutex_unlock(&fs_mutex);
    return ret;
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
    int fd = open(dev_path, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        LOG_FS("CreateChn: Failed to open %s: %s", dev_path, strerror(errno));
        /* Continue anyway - device may not exist on all platforms */
    } else {
        gFramesource->channels[chnNum].fd = fd;
        LOG_FS("CreateChn: Opened %s (fd=%d, nonblock)", dev_path, fd);
    }

    /* Initialize readiness semaphore so we can gate STREAM_ON until thread is ready */
    sem_init(&gFramesource->channels[chnNum].sem, 0, 0);

    /* Do NOT create VBM pool here. The pool is created during EnableChn
     * after the kernel format is negotiated, to match OEM behavior. */

    /* Register FrameSource channel as module (DEV_ID_FS = 0) */
    /* Allocate a proper Module structure for this channel */
    extern void* IMP_System_AllocModule(const char *name, int groupID);
    void *fs_module = IMP_System_AllocModule("FrameSource", chnNum);
    if (fs_module == NULL) {
        LOG_FS("CreateChn: Failed to allocate module");
        pthread_mutex_unlock(&fs_mutex);
        return -1;
    }

    /* Set output_count to 1 (FrameSource has 1 output per channel) */
    /* Module structure has output_count at offset 0x134 */
    uint32_t *output_count_ptr = (uint32_t*)((char*)fs_module + 0x134);
    *output_count_ptr = 1;

    /* Store module pointer in channel */
    gFramesource->channels[chnNum].module_ptr = fs_module;

    extern int IMP_System_RegisterModule(int deviceID, int groupID, void *module);
    IMP_System_RegisterModule(0, chnNum, fs_module);  /* DEV_ID_FS = 0 */
    LOG_FS("CreateChn: registered FrameSource module [0,%d] with 1 output", chnNum);

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

    /* Prepare format structure - copy all fields from IMPFSChnAttr
     * Based on decompilation at 0x9ecf8, the entire structure is copied */
    fs_format_t fmt;
    memset(&fmt, 0, sizeof(fmt));

    /* V4L2 fields (for validation) */
    fmt.width = chn->attr.picWidth;
    fmt.height = chn->attr.picHeight;
    fmt.pixelformat = chn->attr.pixFmt;

    /* Ingenic imp_channel_attr fields (for tisp_channel_attr_set) */
    fmt.enable = 1; /* Enable explicit channel dimensions like prudynt */
    fmt.attr_width = chn->attr.picWidth;
    fmt.attr_height = chn->attr.picHeight;

    /* Copy crop settings */
    fmt.crop_enable = chn->attr.crop.enable;
    fmt.crop_x = chn->attr.crop.left;
    fmt.crop_y = chn->attr.crop.top;
    fmt.crop_width = chn->attr.crop.width;
    fmt.crop_height = chn->attr.crop.height;

    /* Copy scaler settings */
    fmt.scaler_enable = chn->attr.scaler.enable;
    fmt.scaler_outwidth = chn->attr.scaler.outwidth;
    fmt.scaler_outheight = chn->attr.scaler.outheight;

    /* Picture dimensions (same as channel dimensions) */
    fmt.picwidth = chn->attr.picWidth;
    fmt.picheight = chn->attr.picHeight;

    /* Copy FPS settings */
    fmt.fps_num = chn->attr.outFrmRateNum;
    fmt.fps_den = chn->attr.outFrmRateDen;

    /* Set format via ioctl - this updates fmt.sizeimage with kernel's value */
    if (fs_set_format(chn->fd, &fmt) < 0) {
        LOG_FS("EnableChn failed: cannot set format");
        fs_close_device(chn->fd);
        chn->fd = -1;
        pthread_mutex_unlock(&fs_mutex);
        return -1;
    }

    /* Use the sizeimage that SET_FMT returned (kernel modified it in-place).
     * DO NOT call GET_FMT - the remote ISP core returns garbage on GET_FMT.
     * The OEM library uses the sizeimage from SET_FMT's modified structure.
     */
    int kernel_sizeimage = fmt.sizeimage;

    fprintf(stderr, "[FrameSource] EnableChn: using sizeimage=%d from SET_FMT for chn=%d\n",
            kernel_sizeimage, chnNum);

    /* CRITICAL: Must call REQBUFS *before* creating VBM pool!
     * The kernel's REQBUFS handler allocates internal buffer structures.
     * This matches the OEM libimp.so sequence: SET_FMT -> REQBUFS -> VBMCreatePool.
     */
    /* Use channel attr nrVBs; align with prudynt (nrVBs=1) but let driver adjust actual count */
    int requested_bufcnt = chn->attr.nrVBs > 0 ? chn->attr.nrVBs : 1;
    if (requested_bufcnt < 1) requested_bufcnt = 1;
    int bufcnt = fs_set_buffer_count(chn->fd, requested_bufcnt);
    if (bufcnt < 0) {
        LOG_FS("EnableChn failed: cannot set buffer count");
        fs_close_device(chn->fd);
        chn->fd = -1;
        pthread_mutex_unlock(&fs_mutex);
        return -1;
    }

    /* Build VBM format block matching IMPFSChnAttr layout offsets expected by VBMCreatePool:
       [0x00]=width, [0x04]=height, [0x08]=pixfmt(enum), [0x0c]=req_size(sizeimage), [0x34]=nrVBs */
    uint8_t vbm_fmt[0xd0];
    memset(vbm_fmt, 0, sizeof(vbm_fmt));
    memcpy(vbm_fmt + 0x00, &chn->attr.picWidth, sizeof(int));
    memcpy(vbm_fmt + 0x04, &chn->attr.picHeight, sizeof(int));
    memcpy(vbm_fmt + 0x08, &chn->attr.pixFmt, sizeof(int));
    memcpy(vbm_fmt + 0x0c, &kernel_sizeimage, sizeof(int));
    memcpy(vbm_fmt + 0x34, &bufcnt, sizeof(int));

    /* Create VBM pool using kernel-computed size and requested buffer count */
    if (VBMCreatePool(chnNum, vbm_fmt, NULL, NULL) < 0) {
        LOG_FS("EnableChn failed: cannot create VBM pool");
        fs_close_device(chn->fd);
        chn->fd = -1;
        pthread_mutex_unlock(&fs_mutex);
        return -1;
    }

    /* Prime kernel with QBUF for all frames before streaming */
    if (VBMFillPool(chnNum) < 0) {
        LOG_FS("EnableChn failed: cannot fill VBM pool");
        VBMDestroyPool(chnNum);
        fs_close_device(chn->fd);
        chn->fd = -1;
        pthread_mutex_unlock(&fs_mutex);
        return -1;
    }
    extern int VBMPrimeKernelQueue(int chn, int fd);
    if (VBMPrimeKernelQueue(chnNum, chn->fd) < 0) {
        LOG_FS("EnableChn failed: cannot prime kernel queue");
        VBMDestroyPool(chnNum);
        fs_close_device(chn->fd);
        chn->fd = -1;
        pthread_mutex_unlock(&fs_mutex);
        return -1;
    }

    /* Small guard delay before STREAM_ON to mirror vendor pacing */
    usleep(5000); /* 5ms */

    /* Do not DQBUF before STREAM_ON; some drivers return EINVAL and may abort later */

    /* Match OEM: ensure kernel frame depth is configured (default 0) before STREAM_ON */
    {
        extern int fs_set_depth(int fd, int depth);
        int desired_depth = g_frame_depth[chnNum]; /* defaults to 0 unless app set */
        if (fs_set_depth(chn->fd, desired_depth) < 0) {
            LOG_FS("EnableChn: warning: could not set kernel frame depth to %d", desired_depth);
        }
    }


    /* Mark running and start capture thread first (match OEM ordering) */
    chn->state = 2; /* Running */
    gFramesource->active_count++;

    if (pthread_create(&chn->thread, NULL, frame_capture_thread, chn) != 0) {
        LOG_FS("EnableChn failed: cannot create capture thread");
        chn->state = 0;
        gFramesource->active_count--;
        VBMFlushFrame(chnNum);
        VBMDestroyPool(chnNum);
        fs_close_device(chn->fd);
        chn->fd = -1;
        pthread_mutex_unlock(&fs_mutex);
        return -1;
    }

    /* Wait briefly for capture thread to be running before STREAM_ON */
    {
        struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
        /* 100ms timeout */
        ts.tv_nsec += 100 * 1000000;
        if (ts.tv_nsec >= 1000000000) { ts.tv_sec += 1; ts.tv_nsec -= 1000000000; }
        int sem_rc = sem_timedwait(&chn->sem, &ts);
        if (sem_rc != 0) {
            LOG_FS("EnableChn: capture thread readiness wait timed out (%s) — continuing", strerror(errno));
        } else {
            LOG_FS("EnableChn: capture thread signaled readiness");
        }
    }

    /* Start streaming after thread is ready */
    if (fs_stream_on(chn->fd) < 0) {
        LOG_FS("EnableChn failed: cannot start streaming");
        /* Stop and join capture thread before teardown */
        pthread_cancel(chn->thread);
        pthread_join(chn->thread, NULL);
        chn->state = 0;
        gFramesource->active_count--;
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
    /* Accept known IMPPixelFormat enum range; format conversion to fourcc occurs later */
    if (chn_attr->pixFmt > PIX_FMT_RAW) {
        LOG_FS("SetChnAttr failed: invalid pixFmt enum %d", chn_attr->pixFmt);
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
    if (chnNum < 0 || chnNum >= MAX_FS_CHANNELS) return -1;
    pthread_mutex_lock(&fs_mutex);
    g_fifo_attrs[chnNum] = *attr;
    pthread_mutex_unlock(&fs_mutex);
    LOG_FS("SetChnFifoAttr: chn=%d, maxdepth=%d, depth=%d", chnNum, attr->maxdepth, attr->depth);
    return 0;
}

int IMP_FrameSource_GetChnFifoAttr(int chnNum, IMPFSChnFifoAttr *attr) {
    if (attr == NULL) return -1;
    if (chnNum < 0 || chnNum >= MAX_FS_CHANNELS) return -1;
    pthread_mutex_lock(&fs_mutex);
    *attr = g_fifo_attrs[chnNum];
    pthread_mutex_unlock(&fs_mutex);
    LOG_FS("GetChnFifoAttr: chn=%d -> maxdepth=%d, depth=%d", chnNum, attr->maxdepth, attr->depth);
    return 0;
}

int IMP_FrameSource_SetFrameDepth(int chnNum, int depth) {
    if (chnNum < 0 || chnNum >= MAX_FS_CHANNELS) return -1;
    pthread_mutex_lock(&fs_mutex);
    g_frame_depth[chnNum] = depth;
    int fd = (gFramesource) ? gFramesource->channels[chnNum].fd : -1;
    pthread_mutex_unlock(&fs_mutex);
    if (fd >= 0) {
        extern int fs_set_depth(int fd, int depth);
        int ret = fs_set_depth(fd, depth);
        if (ret < 0) return ret;
    }
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
    /* Signal readiness to EnableChn so STREAM_ON waits until thread is live */
    sem_post(&chn->sem);


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

    LOG_FS("frame_capture_thread: started for channel %d, state=%d, fd=%d",
           chn_num, chn->state, chn->fd);
    fflush(stderr);

    int frame_count = 0;
    int poll_count = 0;
    int state_wait_count = 0;
    int software_mode = 0;  /* Try hardware mode first; fallback to SOFTWARE MODE on errors */
    int no_frame_cycles = 0;    /* Counts consecutive hardware waits with no frames */
    int ioctl_fail_count = 0;    /* Counts consecutive ioctl failures */
    const int NO_FRAME_THRESHOLD = 20;  /* ~2s: 20 x 100ms select timeouts */
    const int IOCTL_FAIL_THRESHOLD = 5; /* switch after repeated ioctl errors */

    LOG_FS("frame_capture_thread chn=%d: entering main loop", chn_num);
    fflush(stderr);

    /* Main capture loop */
    while (1) {
        /* Check for thread cancellation */
        pthread_testcancel();

        poll_count++;
        if (poll_count == 1 || poll_count == 5 || poll_count == 10 || (poll_count % 50) == 0) {
            LOG_FS("frame_capture_thread chn=%d: poll iteration %d, state=%d, fd=%d, software_mode=%d",
                   chn_num, poll_count, chn->state, chn->fd, software_mode);
            fflush(stderr);
        }

        /* Check if channel is still running */
        if (chn->state != 2) {
            state_wait_count++;
            if (state_wait_count % 100 == 0) {
                LOG_FS("frame_capture_thread chn=%d: waiting for state=2 (current state=%d, waited %d times)",
                       chn_num, chn->state, state_wait_count);
                fflush(stderr);
            }
            usleep(10000); /* 10ms */
            continue;
        }

        if (state_wait_count > 0) {
            LOG_FS("frame_capture_thread chn=%d: state is now 2, starting capture", chn_num);
            fflush(stderr);
            state_wait_count = 0;
        }

        /* Check if we should use software mode
         * The hardware /dev/framechanX devices don't work properly without kernel modules,
         * so we skip hardware polling and go straight to software frame generation. */

        /* Enter software mode only if device isn't open or hardware polling is unavailable */
        if (!software_mode && chn->fd < 0) {
            LOG_FS("frame_capture_thread chn=%d: no device open, using SOFTWARE FRAME GENERATION mode", chn_num);
            software_mode = 1;
        }

        if (software_mode) {
            /* SOFTWARE MODE: Generate frames at ~20fps */
            usleep(50000); /* 50ms = 20fps */

            /* Get a frame buffer from VBM pool */
            void *frame = NULL;
            if (VBMGetFrame(chn_num, &frame) == 0 && frame != NULL) {
                frame_count++;
                if (frame_count <= 5 || frame_count % 100 == 0) {
                    LOG_FS("frame_capture_thread chn=%d: SOFTWARE MODE - generated frame #%d (%p)",
                           chn_num, frame_count, frame);
                }

                /* Notify observers (bound modules like Encoder) */
                void *module = IMP_System_GetModule(DEV_ID_FS, chn_num);
                if (module != NULL) {
                    notify_observers(module, frame);
                } else {
                    LOG_FS("frame_capture_thread chn=%d: WARNING - no module found for FrameSource", chn_num);
                }

                /* Frame will be released when user calls IMP_FrameSource_ReleaseFrame */
            } else {
                if (frame_count == 0 || frame_count % 100 == 0) {
                    LOG_FS("frame_capture_thread chn=%d: SOFTWARE MODE - VBMGetFrame failed (frame_count=%d)", chn_num, frame_count);
                }
                usleep(10000); /* 10ms */
            }
            continue;
        }

        /* HARDWARE MODE: Wait using select(2) on framechan fd, then drain with DQBUF */
        int drained = 0;
        {
            if (poll_count <= 3) {
                LOG_FS("frame_capture_thread chn=%d: about to select() (poll #%d)", chn_num, poll_count);
                fflush(stderr);
            }

            fd_set rfds; FD_ZERO(&rfds); FD_SET(chn->fd, &rfds);
            struct timeval tv; tv.tv_sec = 0; tv.tv_usec = 25000; /* 25ms */

            pthread_testcancel();
            int rc = select(chn->fd + 1, &rfds, NULL, NULL, &tv);
            pthread_testcancel();

            if (poll_count <= 3) {
                LOG_FS("frame_capture_thread chn=%d: select() returned %d (errno=%d)", chn_num, rc, errno);
                fflush(stderr);
            }

            if (rc < 0) {
                if (errno == EINTR) {
                    /* interrupted by signal, retry */
                } else {
                    ioctl_fail_count++;
                    if (ioctl_fail_count % 5 == 0) {
                        LOG_FS("frame_capture_thread chn=%d: select() error: %s (count=%d)", chn_num, strerror(errno), ioctl_fail_count);
                        fflush(stderr);
                    }
                }
            } else {
                ioctl_fail_count = 0;
            }
        }

        while (1) {
            void *frame = NULL;
            extern int VBMKernelDequeue(int chn, int fd, void **frame_out);
            if (VBMKernelDequeue(chn_num, chn->fd, &frame) == 0 && frame != NULL) {
                drained++;
                frame_count++;
                if (frame_count <= 5 || frame_count % 100 == 0) {
                    LOG_FS("frame_capture_thread chn=%d: got frame #%d (%p) from kernel", chn_num, frame_count, frame);
                }
                /* Notify observers (bound modules like Encoder) */
                void *module = IMP_System_GetModule(DEV_ID_FS, chn_num);
                if (module != NULL) {
                    notify_observers(module, frame);
                }
                /* keep draining */
                continue;
            }
            break;
        }

        if (drained == 0) {
            /* No frames dequeued; avoid busy loop */
            no_frame_cycles++;
            if ((no_frame_cycles % 50) == 0) {
                LOG_FS("frame_capture_thread chn=%d: DQBUF yielded no frames x%d", chn_num, no_frame_cycles);
            }
            if (no_frame_cycles >= NO_FRAME_THRESHOLD) {
                LOG_FS("frame_capture_thread chn=%d: no frames after %d polls, switching to SOFTWARE MODE", chn_num, no_frame_cycles);
                software_mode = 1;
                no_frame_cycles = 0;
            }
            usleep(1000); /* 1ms */
        } else {
            no_frame_cycles = 0;
            ioctl_fail_count = 0;
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
/* Minimal GetFrame/SnapFrame for thingino-streamer API parity */
int IMP_FrameSource_GetFrame(int chnNum, void **frame) {
    if (!frame) return -1;
    return VBMGetFrame(chnNum, frame);
}

int IMP_FrameSource_SnapFrame(int chnNum, IMPPixelFormat fmt, int width, int height,
                              void *out_buffer, IMPFrameInfo *info) {
    if (!out_buffer || !info) return -1;
    if (chnNum < 0 || chnNum >= MAX_FS_CHANNELS) return -1;

    /* Only support direct copy when requested format/size matches channel output */
    IMPFSChnAttr attr;
    if (IMP_FrameSource_GetChnAttr(chnNum, &attr) < 0) return -1;
    if (attr.picWidth != width || attr.picHeight != height || attr.pixFmt != fmt) {
        LOG_FS("SnapFrame: unsupported conversion req %dx%d fmt=0x%x (chn %dx%d fmt=0x%x)",
               width, height, fmt, attr.picWidth, attr.picHeight, attr.pixFmt);
        return -1;
    }

    void *frame = NULL;
    /* Try a few times to get a frame */
    for (int i = 0; i < 5; i++) {
        if (VBMGetFrame(chnNum, &frame) == 0 && frame) break;
        usleep(5000);
    }
    if (!frame) {
        LOG_FS("SnapFrame: no frame available");
        return -1;
    }

    /* Get backing buffer */
    extern int VBMFrame_GetBuffer(void *frame, void **virt, int *size);
    void *src = NULL; int src_size = 0;
    if (VBMFrame_GetBuffer(frame, &src, &src_size) < 0 || !src) {
        LOG_FS("SnapFrame: VBMFrame_GetBuffer failed");
        VBMReleaseFrame(chnNum, frame);
        return -1;
    }

    /* Compute expected size for NV12/YUYV422 */
    size_t expected = 0;
    if (fmt == PIX_FMT_NV12 || fmt == PIX_FMT_NV21) {
        expected = (size_t)width * height * 3 / 2;
    } else if (fmt == PIX_FMT_YUYV422 || fmt == PIX_FMT_UYVY422) {
        expected = (size_t)width * height * 2;
    } else {
        LOG_FS("SnapFrame: unsupported fmt=0x%x", fmt);
        VBMReleaseFrame(chnNum, frame);
        return -1;
    }
    if (src_size < (int)expected) {
        LOG_FS("SnapFrame: src_size=%d smaller than expected=%zu", src_size, expected);
        VBMReleaseFrame(chnNum, frame);
        return -1;
    }

    memcpy(out_buffer, src, expected);

    info->width = width;
    info->height = height;

    VBMReleaseFrame(chnNum, frame);
    return 0;
}

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

/**
 * IMP_FrameSource_ReleaseFrame - Release a frame back to the VBM pool
 *
 * This function releases a frame that was obtained from the FrameSource
 * back to the Video Buffer Manager (VBM) pool so it can be reused.
 *
 * @param chnNum: Channel number (0-4)
 * @param frame: Pointer to the frame to release
 * @return: 0 on success, -1 on error
 */
int IMP_FrameSource_ReleaseFrame(int chnNum, void *frame) {
    extern int VBMReleaseFrame(int chn, void *frame);

    if (chnNum < 0 || chnNum >= MAX_FS_CHANNELS) {
        LOG_FS("ReleaseFrame: invalid channel %d", chnNum);
        return -1;
    }

    if (frame == NULL) {
        LOG_FS("ReleaseFrame: NULL frame pointer");
        return -1;
    }

    /* Release the frame back to the VBM pool */
    int ret = VBMReleaseFrame(chnNum, frame);
    if (ret < 0) {
        LOG_FS("ReleaseFrame: VBMReleaseFrame failed for chn=%d, frame=%p", chnNum, frame);
        return -1;
    }

    LOG_FS("ReleaseFrame: Released frame %p from channel %d", frame, chnNum);
    return 0;
}

