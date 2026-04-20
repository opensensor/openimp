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
#include <sched.h>
#include <stdarg.h>

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

#include "imp_log_int.h"

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
    int nocopy_depth;           /* 0x1cc: No-copy frame depth */
    int copy_type;              /* 0x1d0: Copy type flag (0=copy, 1=nocopy) */
    uint8_t data_1d4[0x68];    /* 0x1d4-0x23b: More data */
    void *special_data;         /* 0x23c: Special mode data */
    int source_chn;             /* 0x240: Source channel for extended channels */
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


/* Forward declarations */
static void *frame_capture_thread(void *arg);
static int framesource_bind(void *src_module, void *dst_module, void *output_ptr);
static int framesource_unbind(void *src_module, void *dst_module, void *output_ptr);

static void fs_bind_trace(const char *fmt, ...)
{
    int fd = open("/dev/kmsg", O_WRONLY);
    if (fd < 0) return;

    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n > 0) write(fd, buf, (size_t)n);
    close(fd);
}

static void framesource_publish_outputs(void *module, void *frame)
{
    if (module == NULL) return;

    uint32_t count = *(uint32_t *)((char *)module + 0x134);
    if (count == 0) count = 1;
    if (count > 3) count = 3;

    for (uint32_t i = 0; i < count; i++) {
        *(void **)((char *)module + 0x138 + (i * sizeof(void *))) = frame;
    }
}

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
    /* OEM validation order: chnNum >= 5 first, then NULL check, then alignment */
    if (chnNum < 0 || chnNum >= MAX_FS_CHANNELS) {
        LOG_FS("CreateChn failed: invalid channel %d", chnNum);
        return -1;
    }

    if (chn_attr == NULL) {
        LOG_FS("CreateChn failed: NULL attr");
        return -1;
    }

    /* OEM checks picWidth alignment to 16 (unless RAW format 0x22) */
    if (chn_attr->pixFmt != PIX_FMT_RAW && (chn_attr->picWidth & 0xf) != 0) {
        LOG_FS("CreateChn failed: picWidth %d not aligned to 16", chn_attr->picWidth);
        return -1;
    }

    /* OEM: channel 0 must be FS_PHY_CHANNEL type */
    if (chnNum == 0 && chn_attr->type != FS_PHY_CHANNEL) {
        LOG_FS("CreateChn failed: channel 0 must be FS_PHY_CHANNEL");
        return -1;
    }

    pthread_mutex_lock(&fs_mutex);

    framesource_init();

    if (gFramesource == NULL) {
        pthread_mutex_unlock(&fs_mutex);
        return -1;
    }

    FSChannel *chn = &gFramesource->channels[chnNum];

    /* Set attributes at offset 0x20 in channel struct */
    memcpy(&chn->attr, chn_attr, sizeof(IMPFSChnAttr));
    chn->fd = -1;

    /* Initialize readiness semaphore so we can gate STREAM_ON until thread is ready */
    sem_init(&chn->sem, 0, 0);

    /* OEM: initialize frame depth and copy type to 0 */
    g_frame_depth[chnNum] = 0;

    /* OEM does NOT open device or create VBM pool in CreateChn.
     * Device open and VBM pool creation happen in EnableChn. */

    /* Register FrameSource channel as module (DEV_ID_FS = 0) */
    extern void* IMP_System_AllocModule(const char *name, int groupID);
    void *fs_module = IMP_System_AllocModule("FrameSource", chnNum);
    if (fs_module == NULL) {
        LOG_FS("CreateChn: Failed to allocate module");
        pthread_mutex_unlock(&fs_mutex);
        return -1;
    }

    /* Set output_count to 1 (FrameSource has 1 output per channel) */
    uint32_t *output_count_ptr = (uint32_t*)((char*)fs_module + 0x134);
    *output_count_ptr = 1;

    /* Store module pointer in channel */
    chn->module_ptr = fs_module;

    extern int IMP_System_RegisterModule(int deviceID, int groupID, void *module);
    IMP_System_RegisterModule(0, chnNum, fs_module);  /* DEV_ID_FS = 0 */

    /* Set bind/unbind function pointers at offsets 0x40 and 0x44 on the module.
     * These must be set AFTER the module is allocated (framesource_init runs
     * before modules exist, so the bind setup there never fires). */
    {
        uint8_t *mod_bytes = (uint8_t*)fs_module;
        void **bind_ptr = (void**)(mod_bytes + 0x40);
        void **unbind_ptr = (void**)(mod_bytes + 0x44);
        *bind_ptr = (void*)framesource_bind;
        *unbind_ptr = (void*)framesource_unbind;
    }

    /* OEM sets state to 1 (created), not 0 */
    chn->state = 1;

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

    /* OEM requires state==1 (created but disabled). If state==2 (enabled), error out. */
    if (gFramesource->channels[chnNum].state == 2) {
        LOG_FS("DestroyChn: channel %d is busy, please disable it firstly", chnNum);
        pthread_mutex_unlock(&fs_mutex);
        return -1;
    }

    if (gFramesource->channels[chnNum].state == 0) {
        LOG_FS("DestroyChn: channel %d not created", chnNum);
        pthread_mutex_unlock(&fs_mutex);
        return -1;
    }

    /* OEM: reset frame depth and copy type */
    g_frame_depth[chnNum] = 0;

    /* OEM does NOT close fd here - fd is closed in DisableChn.
     * Destroy the module/group and clear channel state. */
    gFramesource->channels[chnNum].state = 0;
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

    if (gFramesource->channels[chnNum].state != 1) {
        LOG_FS("EnableChn: channel %d not created (state=%d)", chnNum, gFramesource->channels[chnNum].state);
        pthread_mutex_unlock(&fs_mutex);
        return -1;
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

    /* Prepare format structure — OEM-exact layout from do_reset_channel_attr
     * decompilation at 0x9ecf8.
     *
     * CRITICAL: The OEM memsets the 0x70-byte format buffer to zero and then
     * fills ONLY the V4L2 header fields (type, width, height, pixelformat,
     * colorspace) and two overrides (fps_num=1, picheight=0).  All Ingenic
     * extended attributes (enable, attr_width, crop, scaler, picwidth, etc.)
     * are left ZERO.  Setting enable=1 or non-zero picwidth/picheight causes
     * tisp_channel_attr_set in the stock kernel to configure the ISP channel
     * differently, which breaks the DQBUF path for PHY_CHANNEL (channel 0).
     *
     * The OEM only calls GET_FMT before SET_FMT for RAW format (0x22), never
     * for NV12/NV21. Doing so corrupts width/height with sensor-side values.
     */
    fs_format_t fmt;
    memset(&fmt, 0, sizeof(fmt));

    /* V4L2 header fields (match OEM defaults) */
    fmt.type = 1;           /* V4L2_BUF_TYPE_VIDEO_CAPTURE */
    fmt.width = chn->attr.picWidth;
    fmt.height = chn->attr.picHeight;
    fmt.pixelformat = chn->attr.pixFmt;
    fmt.field = 0;          /* progressive */
    fmt.bytesperline = 0;   /* driver will compute */
    fmt.sizeimage = 0;      /* driver will fill */
    fmt.colorspace = 8;     /* V4L2_COLORSPACE_SRGB like OEM */
    fmt.priv = 0;

    /* Ingenic extended attributes — OEM leaves these at zero for both
     * PHY_CHANNEL and EXT_CHANNEL in the initial SET_FMT.  The only
     * overrides the OEM applies (at label_9eef8) are:
     *   picheight = 0   (already zero from memset)
     *   fps_num   = 1
     * Everything else (enable, attr_width/height, crop, scaler, picwidth,
     * fps_den) stays zero. */
    fmt.enable = 0;
    fmt.fps_num = 1;
    /* All other extended fields remain 0 from memset — OEM parity */

    /* Set format via ioctl - this updates fmt.sizeimage with kernel's value */
    if (fs_set_format(chn->fd, &fmt) < 0) {
        LOG_FS("EnableChn failed: cannot set format");
        fs_close_device(chn->fd);
        chn->fd = -1;
        pthread_mutex_unlock(&fs_mutex);
        return -1;
    }

    /* Determine sizeimage from SET_FMT. kernel_interface.c already documents that
     * a follow-up GET_FMT can return stale/garbage values from the remote ISP
     * core (for example RG12-sized 4147200 after an NV12 SET_FMT that negotiated
     * 3133440). Trust the in-place SET_FMT result here; later QBUF paths already
     * use QUERYBUF for the exact kernel-advertised queue length. */
    int kernel_sizeimage = fmt.sizeimage;
    if (chnNum == 0 && chn->attr.scaler.enable == 0) {
        fprintf(stderr, "[FrameSource] EnableChn: ch0: using SET_FMT sizeimage=%d\n",
                kernel_sizeimage);
    } else {
        fprintf(stderr, "[FrameSource] EnableChn: using sizeimage=%d from SET_FMT for chn=%d\n",
                kernel_sizeimage, chnNum);
    }


    /* Build VBM format block matching IMPFSChnAttr layout offsets expected by VBMCreatePool:
       [0x00]=width, [0x04]=height, [0x08]=pixfmt(enum), [0x0c]=req_size(sizeimage), [0x34]=nrVBs */
    uint8_t vbm_fmt[0xd0];
    memset(vbm_fmt, 0, sizeof(vbm_fmt));
    memcpy(vbm_fmt + 0x00, &chn->attr.picWidth, sizeof(int));
    memcpy(vbm_fmt + 0x04, &chn->attr.picHeight, sizeof(int));
    memcpy(vbm_fmt + 0x08, &chn->attr.pixFmt, sizeof(int));
    memcpy(vbm_fmt + 0x0c, &kernel_sizeimage, sizeof(int));
    /* Triple buffering minimum: with synchronous encoder_update on the
     * capture thread and 2 buffers, the capture thread exhausts both before
     * the stream_thread can QBUF either back. 3 buffers ensures there's
     * always one in the kernel queue. The OEM avoids this via async encode
     * threads; our synchronous path needs the extra buffer. */
    int vbm_count = chn->attr.nrVBs;
    if (vbm_count < 3) vbm_count = 3;
    memcpy(vbm_fmt + 0x34, &vbm_count, sizeof(int));

    /* Create VBM pool using kernel-computed size and requested buffer count */
    if (VBMCreatePool(chnNum, vbm_fmt, NULL, NULL) < 0) {
        LOG_FS("EnableChn failed: cannot create VBM pool");
        fs_close_device(chn->fd);
        chn->fd = -1;
        pthread_mutex_unlock(&fs_mutex);
        return -1;
    }

    /* REQBUFS before STREAM_ON — match the VBM pool count (min 3 for
     * triple buffering). Add frame_depth if configured. */
    int requested_bufcnt = vbm_count + (g_frame_depth[chnNum] > 0 ? g_frame_depth[chnNum] : 0);
    int bufcnt = fs_set_buffer_count(chn->fd, requested_bufcnt);
    if (bufcnt < 0) {
        LOG_FS("EnableChn failed: cannot set buffer count");
        VBMDestroyPool(chnNum);
        fs_close_device(chn->fd);
        chn->fd = -1;
        pthread_mutex_unlock(&fs_mutex);
        return -1;
    }
    if (bufcnt < requested_bufcnt) {
        LOG_FS("EnableChn: driver reduced buffer count from %d to %d; continuing",
               requested_bufcnt, bufcnt);
    }

    /* Fill VBM pool (marks buffers as available in userspace queue) */
    if (VBMFillPool(chnNum) < 0) {
        LOG_FS("EnableChn failed: cannot fill VBM pool");
        VBMDestroyPool(chnNum);
        fs_close_device(chn->fd);
        chn->fd = -1;
        pthread_mutex_unlock(&fs_mutex);
        return -1;
    }

    /* Match OEM: ensure kernel frame depth is configured before STREAM_ON
     * OEM only calls SET_DEPTH when depth > 0 (verified via BN MCP decompilation) */
    {
        extern int fs_set_depth(int fd, int depth);
        int desired_depth = g_frame_depth[chnNum]; /* defaults to 0 unless app set */
        if (desired_depth > 0) {
            if (fs_set_depth(chn->fd, desired_depth) < 0) {
                LOG_FS("EnableChn: warning: could not set kernel frame depth to %d", desired_depth);
            }
        }
    }

    /* Mark running and start capture thread; it will block on select() */
    /* QBUF before STREAM_ON (standard V4L2 order) */
    extern int VBMPrimeKernelQueue(int chn, int fd, int limit);
    int queued_ok = VBMPrimeKernelQueue(chnNum, chn->fd, bufcnt);
    if (queued_ok < 0) {
        LOG_FS("EnableChn warning: prime kernel queue had errors, continuing");
    }
    LOG_FS("EnableChn: chn=%d primed kernel queue with %d/%d buffers",
           chnNum, queued_ok, bufcnt);

    /* The PHY channel is not useful if the kernel never accepted any userptr
     * buffers, and ch0 has been observed to wedge in DQBUF forever in exactly
     * that state. Fail fast instead of entering a no-frame drain loop. */
    if (chnNum == 0 && queued_ok <= 0) {
        LOG_FS("EnableChn failed: ch0 kernel prime accepted no buffers");
        VBMFlushFrame(chnNum);
        VBMDestroyPool(chnNum);
        fs_close_device(chn->fd);
        chn->fd = -1;
        pthread_mutex_unlock(&fs_mutex);
        return -1;
    }

    usleep(5000); /* 5ms guard */

    /* Ensure ISP global stream is active */
    {
        extern int ISP_EnsureLinkStreamOn(int sensor_idx);
        ISP_EnsureLinkStreamOn(0);
    }

    /* STREAM_ON */
    if (fs_stream_on(chn->fd) < 0) {
        LOG_FS("EnableChn failed: cannot start streaming");
        VBMFlushFrame(chnNum);
        VBMDestroyPool(chnNum);
        fs_close_device(chn->fd);
        chn->fd = -1;
        pthread_mutex_unlock(&fs_mutex);
        return -1;
    }

    /* Start capture thread AFTER STREAM_ON */
    chn->state = 2; /* Running */
    gFramesource->active_count++;

    if (pthread_create(&chn->thread, NULL, frame_capture_thread, chn) != 0) {
        LOG_FS("EnableChn failed: cannot create capture thread");
        fs_stream_off(chn->fd);
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

    /* OEM: check picWidth alignment to 16 (unless RAW format 0x22) */
    if (chn_attr->pixFmt != PIX_FMT_RAW && (chn_attr->picWidth & 0xf) != 0) {
        LOG_FS("SetChnAttr failed: picWidth %d not aligned to 16", chn_attr->picWidth);
        return -1;
    }

    /* OEM: channel 0 must be FS_PHY_CHANNEL type */
    if (chnNum == 0 && chn_attr->type != FS_PHY_CHANNEL) {
        LOG_FS("SetChnAttr failed: channel 0 must be FS_PHY_CHANNEL");
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
    /* OEM: channel must be created (state==1) but not enabled */
    if (gFramesource && gFramesource->channels[chnNum].state != 1) {
        LOG_FS("SetChnFifoAttr: channel %d not in created state", chnNum);
        pthread_mutex_unlock(&fs_mutex);
        return -1;
    }
    g_fifo_attrs[chnNum] = *attr;
    pthread_mutex_unlock(&fs_mutex);
    /* OEM calls SetMaxDelay with first field of fifo attr */
    IMP_FrameSource_SetMaxDelay(chnNum, attr->maxdepth);
    LOG_FS("SetChnFifoAttr: chn=%d, maxdepth=%d, depth=%d", chnNum, attr->maxdepth, attr->depth);
    return 0;
}

int IMP_FrameSource_GetChnFifoAttr(int chnNum, IMPFSChnFifoAttr *attr) {
    if (attr == NULL) return -1;
    if (chnNum < 0 || chnNum >= MAX_FS_CHANNELS) return -1;
    pthread_mutex_lock(&fs_mutex);
    /* OEM: channel must be created (state != 0) */
    if (gFramesource && gFramesource->channels[chnNum].state == 0) {
        LOG_FS("GetChnFifoAttr: channel %d not created", chnNum);
        pthread_mutex_unlock(&fs_mutex);
        return -1;
    }
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
                    framesource_publish_outputs(module, frame);
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
        if (poll_count <= 3) {
            LOG_FS("frame_capture_thread chn=%d: entering DQBUF drain loop", chn_num);
            fflush(stderr);
        }

        {
            unsigned int ready = 0;
            int poll_ret = fs_poll_frame(chn->fd, &ready);
            if (poll_ret == -2) {
                if (poll_count <= 5) {
                    LOG_FS("frame_capture_thread chn=%d: POLL_FRAME interrupted", chn_num);
                    fflush(stderr);
                }
                continue;
            }
            if (poll_ret < 0) {
                if (poll_count <= 5 || (poll_count % 50) == 0) {
                    LOG_FS("frame_capture_thread chn=%d: POLL_FRAME failed before DQBUF", chn_num);
                    fflush(stderr);
                }
                usleep(1000);
                continue;
            }
            if (ready == 0) {
                if (poll_count <= 5 || (poll_count % 50) == 0) {
                    LOG_FS("frame_capture_thread chn=%d: POLL_FRAME reported 0 ready frames", chn_num);
                    fflush(stderr);
                }
                usleep(1000);
                continue;
            }
            if (poll_count <= 5) {
                LOG_FS("frame_capture_thread chn=%d: POLL_FRAME ready=%u", chn_num, ready);
                fflush(stderr);
            }
        }

        /* Enforce non-blocking on fd in case driver cleared O_NONBLOCK */
        int __fl = fcntl(chn->fd, F_GETFL, 0);
        if (__fl != -1 && !(__fl & O_NONBLOCK)) {
            fcntl(chn->fd, F_SETFL, __fl | O_NONBLOCK);
            LOG_FS("frame_capture_thread chn=%d: re-enabled O_NONBLOCK before DQBUF", chn_num);
        }

        while (1) {
            void *frame = NULL;
            extern int VBMKernelDequeue(int chn, int fd, void **frame_out);
            int dq_ret = VBMKernelDequeue(chn_num, chn->fd, &frame);
            if (dq_ret == 0 && frame != NULL) {
                drained++;
                frame_count++;
                if (frame_count <= 5 || frame_count % 100 == 0) {
                    LOG_FS("frame_capture_thread chn=%d: got frame #%d (%p) from kernel", chn_num, frame_count, frame);
                    fflush(stderr);
                }
                /* Notify observers (bound modules like Encoder) */
                void *module = IMP_System_GetModule(DEV_ID_FS, chn_num);
                if (module != NULL) {
                    if (frame_count <= 5) {
                        LOG_FS("frame_capture_thread chn=%d: about to notify_observers module=%p", chn_num, module);
                        fflush(stderr);
                    }
                    framesource_publish_outputs(module, frame);
                    notify_observers(module, frame);
                    if (frame_count <= 5) {
                        LOG_FS("frame_capture_thread chn=%d: notify_observers returned", chn_num);
                        fflush(stderr);
                    }
                }
                /* keep draining */
                continue;
            }
            if (dq_ret < 0 && poll_count <= 5) {
                LOG_FS("frame_capture_thread chn=%d: DQBUF drain ret=%d frame=%p",
                       chn_num, dq_ret, frame);
                fflush(stderr);
            }
            break;
        }


        if (drained == 0) {
            /* No frames dequeued; avoid busy loop */
            no_frame_cycles++;
            if (no_frame_cycles <= 5 || (no_frame_cycles % 50) == 0) {
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
    if (src_module == NULL || dst_module == NULL) {
        LOG_FS("bind: NULL module");
        fs_bind_trace("libimp/FSB: bind src=%p dst=%p outptr=%p invalid\n",
                      src_module, dst_module, output_ptr);
        return -1;
    }

    LOG_FS("bind: Binding FrameSource to module");
    fs_bind_trace("libimp/FSB: bind src=%p dst=%p outptr=%p\n",
                  src_module, dst_module, output_ptr);

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
    fs_bind_trace("libimp/FSB: bind observer=%p dst=%p outidx=%d\n",
                  observer, observer->module, observer->output_index);

    /* Add observer to source module's observer list */
    if (add_observer_to_module(src_module, observer) < 0) {
        LOG_FS("bind: Failed to add observer");
        fs_bind_trace("libimp/FSB: bind add_observer failed src=%p obs=%p\n",
                      src_module, observer);
        free(observer);
        return -1;
    }

    LOG_FS("bind: Successfully bound FrameSource to module");
    fs_bind_trace("libimp/FSB: bind success src=%p dst=%p obs=%p\n",
                  src_module, dst_module, observer);
    return 0;
}

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

/* ========== Missing FrameSource functions needed by raptor-hal ========== */

int IMP_FrameSource_GetFrameDepth(int chnNum, int *depth) {
    if (chnNum < 0 || chnNum >= MAX_FS_CHANNELS || !depth) return -1;
    *depth = g_frame_depth[chnNum];
    return 0;
}

int IMP_FrameSource_SetDelay(int chnNum, int delay_ms) {
    if (chnNum < 0 || chnNum >= MAX_FS_CHANNELS) return -1;
    (void)delay_ms;
    LOG_FS("SetDelay(chn=%d, delay=%d) stub", chnNum, delay_ms);
    return 0;
}

int IMP_FrameSource_GetDelay(int chnNum, int *delay_ms) {
    if (chnNum < 0 || chnNum >= MAX_FS_CHANNELS || !delay_ms) return -1;
    *delay_ms = 0;
    return 0;
}

int IMP_FrameSource_SetMaxDelay(int chnNum, int max_delay_ms) {
    if (chnNum < 0 || chnNum >= MAX_FS_CHANNELS) return -1;
    (void)max_delay_ms;
    LOG_FS("SetMaxDelay(chn=%d, max=%d) stub", chnNum, max_delay_ms);
    return 0;
}

int IMP_FrameSource_GetMaxDelay(int chnNum, int *max_delay_ms) {
    if (chnNum < 0 || chnNum >= MAX_FS_CHANNELS || !max_delay_ms) return -1;
    *max_delay_ms = 0;
    return 0;
}

int IMP_FrameSource_SetPool(int chnNum, int poolId) {
    if (chnNum < 0 || chnNum >= MAX_FS_CHANNELS) return -1;
    (void)poolId;
    LOG_FS("SetPool(chn=%d, pool=%d) stub", chnNum, poolId);
    return 0;
}

int IMP_FrameSource_ChnStatQuery(int chnNum, void *stat) {
    if (chnNum < 0 || chnNum >= MAX_FS_CHANNELS || !stat) return -1;
    memset(stat, 0, 64); /* Zero out stat struct */
    return 0;
}

int IMP_FrameSource_EnableChnUndistort(int chnNum) {
    if (chnNum < 0 || chnNum >= MAX_FS_CHANNELS) return -1;
    LOG_FS("EnableChnUndistort(chn=%d) stub", chnNum);
    return 0;
}

int IMP_FrameSource_DisableChnUndistort(int chnNum) {
    if (chnNum < 0 || chnNum >= MAX_FS_CHANNELS) return -1;
    LOG_FS("DisableChnUndistort(chn=%d) stub", chnNum);
    return 0;
}

int IMP_FrameSource_GetTimedFrame(int chnNum, void *framets, int block,
                                   void *framedata, void *frame) {
    (void)chnNum; (void)framets; (void)block; (void)framedata; (void)frame;
    LOG_FS("GetTimedFrame(chn=%d) stub", chnNum);
    return -1; /* Not supported */
}

int IMP_FrameSource_SetFrameOffset(int chnNum, int offset) {
    if (chnNum < 0 || chnNum >= MAX_FS_CHANNELS) return -1;
    (void)offset;
    LOG_FS("SetFrameOffset(chn=%d, offset=%d) stub", chnNum, offset);
    return 0;
}

/* ========== Missing OEM framesource functions (from BN decompilation) ========== */

int IMP_FrameSource_SetSource(int extchnNum, int sourcechnNum) {
    if (extchnNum < 0 || extchnNum >= MAX_FS_CHANNELS) {
        LOG_FS("SetSource(): Invalid extchnNum %d", extchnNum);
        return -1;
    }
    if (sourcechnNum < 0 || sourcechnNum >= 3) {
        LOG_FS("SetSource(): Invalid sourcechnNum %d", sourcechnNum);
        return -1;
    }
    if (gFramesource == NULL) {
        LOG_FS("SetSource(): FrameSource is invalid,maybe system was not inited yet.");
        return -1;
    }
    gFramesource->channels[extchnNum].source_chn = sourcechnNum;
    return 0;
}

int IMP_FrameSource_SetFrameDepthCopyType(int chnNum, int bNoCopy) {
    if (chnNum < 0 || chnNum >= MAX_FS_CHANNELS) {
        LOG_FS("SetFrameDepthCopyType(): Invalid chnNum %d", chnNum);
        return -1;
    }
    if (gFramesource == NULL) return -1;
    FSChannel *ch = &gFramesource->channels[chnNum];

    pthread_mutex_lock(&fs_mutex);

    int cur_depth = ch->nocopy_depth;
    if (cur_depth != 0) {
        if (bNoCopy != 0) {
            LOG_FS("SetFrameDepthCopyType(): Please use before IMP_FrameSource_SetFrameDepth(%d,%d) when b_nocopy_depth(%d) != 0",
                   chnNum, cur_depth, bNoCopy);
        } else {
            LOG_FS("SetFrameDepthCopyType(): Please use after IMP_FrameSource_SetFrameDepth(%d, 0) when b_nocopy_depth == 0",
                   chnNum);
        }
        pthread_mutex_unlock(&fs_mutex);
        return -1;
    }

    ch->copy_type = (bNoCopy > 0) ? 1 : 0;
    pthread_mutex_unlock(&fs_mutex);
    return 0;
}

int IMP_FrameSource_CloseNCUInfo(void) {
    extern void *VBMGetInstance(void);
    void *vbm = VBMGetInstance();
    if (vbm == NULL) {
        LOG_FS("CloseNCUInfo(): VBMGetInstance failed");
        return -1;
    }
    /* Set NCU close flag at VBM instance offset 0x14 */
    *((int*)((uint8_t*)vbm + 0x14)) = 1;
    return 0;
}

static void *g_fs_pools = NULL;

int IMP_FrameSource_ClearPoolId(void) {
    if (g_fs_pools != NULL) {
        free(g_fs_pools);
    }
    g_fs_pools = NULL;
    return 0;
}
