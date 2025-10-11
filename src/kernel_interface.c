/**
 * Kernel Driver Interface for IMP
 * Handles ioctl calls to Ingenic kernel drivers
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <errno.h>
#include "dma_alloc.h"

/* ioctl command definitions from decompilation */

/* FrameSource ioctl commands */
#define VIDIOC_GET_FMT      0x407056c4  /* Get format */
#define VIDIOC_SET_FMT      0xc07056c3  /* Set format */
#define VIDIOC_SET_BUFCNT   0xc0145608  /* Set buffer count */
#define VIDIOC_SET_DEPTH    0x800456c5  /* Set frame depth */
#define VIDIOC_STREAM_ON    0x80045612  /* Start streaming */
#define VIDIOC_STREAM_OFF   0x80045613  /* Stop streaming */

/* Encoder ioctl commands (to be discovered) */
#define ENCODER_CREATE_CHN  0x40000000  /* Placeholder */
#define ENCODER_START       0x40000001  /* Placeholder */

/* ISP ioctl commands (to be discovered) */
#define ISP_INIT            0x50000000  /* Placeholder */
#define ISP_SET_SENSOR      0x50000001  /* Placeholder */

/* Format structure from decompilation */
typedef struct {
    int enable;                 /* 0x00: Enable flag */
    int width;                  /* 0x04: Width */
    int height;                 /* 0x08: Height */
    int pixfmt;                 /* 0x0c: Pixel format */
    /* More fields... */
} fs_format_t;

/* Buffer count structure */
typedef struct {
    int count;                  /* Buffer count */
    int type;                   /* Buffer type */
    int mode;                   /* Mode */
} fs_bufcnt_t;

/**
 * Open framechan device
 * Based on decompilation at 0x9ecf8
 */
int fs_open_device(int chn) {
    char devname[64];
    snprintf(devname, sizeof(devname), "/dev/framechan%d", chn);
    
    /* Try to open with retries (from decompilation: 0x101 retries) */
    for (int i = 0; i < 257; i++) {
        int fd = open(devname, O_RDWR | O_NONBLOCK, 0);
        if (fd >= 0) {
            fprintf(stderr, "[KernelIF] Opened %s (fd=%d)\n", devname, fd);
            return fd;
        }
        
        if (i < 256) {
            usleep(10000); /* 10ms delay between retries */
        }
    }
    
    fprintf(stderr, "[KernelIF] Failed to open %s: %s\n", devname, strerror(errno));
    return -1;
}

/**
 * Get format from framechan device
 * ioctl: 0x407056c4
 */
int fs_get_format(int fd, fs_format_t *fmt) {
    if (fd < 0 || fmt == NULL) {
        return -1;
    }
    
    int ret = ioctl(fd, VIDIOC_GET_FMT, fmt);
    if (ret < 0) {
        fprintf(stderr, "[KernelIF] VIDIOC_GET_FMT failed: %s\n", strerror(errno));
        return -1;
    }
    
    fprintf(stderr, "[KernelIF] Got format: %dx%d fmt=0x%x\n", 
            fmt->width, fmt->height, fmt->pixfmt);
    return 0;
}

/**
 * Convert IMPPixelFormat enum to fourcc code
 */
static uint32_t pixfmt_to_fourcc(int pixfmt) {
    switch (pixfmt) {
        case 0xa:  /* PIX_FMT_NV12 */
            return 0x3231564e; /* 'NV12' */
        case 0xb:  /* PIX_FMT_NV21 */
            return 0x3132564e; /* 'NV21' */
        case 0x1:  /* PIX_FMT_YUYV422 */
            return 0x56595559; /* 'YUYV' */
        case 0x2:  /* PIX_FMT_UYVY422 */
            return 0x59565955; /* 'UYVY' */
        default:
            return pixfmt; /* Already fourcc or unknown */
    }
}

/**
 * Set format on framechan device
 * ioctl: 0xc07056c3
 */
int fs_set_format(int fd, fs_format_t *fmt) {
    if (fd < 0 || fmt == NULL) {
        return -1;
    }

    /* Convert enum pixfmt to fourcc if needed */
    fs_format_t kernel_fmt = *fmt;
    if (kernel_fmt.pixfmt < 0x100) {
        kernel_fmt.pixfmt = pixfmt_to_fourcc(kernel_fmt.pixfmt);
    }

    int ret = ioctl(fd, VIDIOC_SET_FMT, &kernel_fmt);
    if (ret < 0) {
        fprintf(stderr, "[KernelIF] VIDIOC_SET_FMT failed: %s\n", strerror(errno));
        fprintf(stderr, "[KernelIF]   Requested: %dx%d fmt=0x%x (fourcc=0x%x)\n",
                fmt->width, fmt->height, fmt->pixfmt, kernel_fmt.pixfmt);
        return -1;
    }

    fprintf(stderr, "[KernelIF] Set format: %dx%d fmt=0x%x (fourcc=0x%x)\n",
            fmt->width, fmt->height, fmt->pixfmt, kernel_fmt.pixfmt);
    return 0;
}

/**
 * Set buffer count
 * ioctl: 0xc0145608
 */
int fs_set_buffer_count(int fd, int count) {
    if (fd < 0) {
        return -1;
    }
    
    fs_bufcnt_t bufcnt = {
        .count = count,
        .type = 1,
        .mode = 2
    };
    
    int ret = ioctl(fd, VIDIOC_SET_BUFCNT, &bufcnt);
    if (ret < 0) {
        fprintf(stderr, "[KernelIF] VIDIOC_SET_BUFCNT failed: %s\n", strerror(errno));
        return -1;
    }
    
    fprintf(stderr, "[KernelIF] Set buffer count: %d (actual: %d)\n", count, bufcnt.count);
    return bufcnt.count;
}

/**
 * Set frame depth
 * ioctl: 0x800456c5
 */
int fs_set_depth(int fd, int depth) {
    if (fd < 0) {
        return -1;
    }
    
    int ret = ioctl(fd, VIDIOC_SET_DEPTH, &depth);
    if (ret < 0) {
        fprintf(stderr, "[KernelIF] VIDIOC_SET_DEPTH failed: %s\n", strerror(errno));
        return -1;
    }
    
    fprintf(stderr, "[KernelIF] Set frame depth: %d\n", depth);
    return 0;
}

/**
 * Start streaming
 * ioctl: 0x80045612
 */
int fs_stream_on(int fd) {
    if (fd < 0) {
        return -1;
    }
    
    int enable = 1;
    int ret = ioctl(fd, VIDIOC_STREAM_ON, &enable);
    if (ret < 0) {
        fprintf(stderr, "[KernelIF] VIDIOC_STREAM_ON failed: %s\n", strerror(errno));
        return -1;
    }
    
    fprintf(stderr, "[KernelIF] Stream started\n");
    return 0;
}

/**
 * Stop streaming
 * ioctl: 0x80045613
 */
int fs_stream_off(int fd) {
    if (fd < 0) {
        return -1;
    }
    
    int enable = 1;
    int ret = ioctl(fd, VIDIOC_STREAM_OFF, &enable);
    if (ret < 0) {
        fprintf(stderr, "[KernelIF] VIDIOC_STREAM_OFF failed: %s\n", strerror(errno));
        return -1;
    }
    
    fprintf(stderr, "[KernelIF] Stream stopped\n");
    return 0;
}

/**
 * Close device
 */
void fs_close_device(int fd) {
    if (fd >= 0) {
        close(fd);
        fprintf(stderr, "[KernelIF] Closed device (fd=%d)\n", fd);
    }
}

/* VBM (Video Buffer Manager) implementation - based on decompilation at 0x1efe4 */

#define MAX_VBM_POOLS 6
#define VBM_FRAME_SIZE 0x428

/* VBM Frame structure */
typedef struct {
    int index;              /* 0x00: Frame index */
    int chn;                /* 0x04: Channel */
    int width;              /* 0x08: Width */
    int height;             /* 0x0c: Height */
    int size;               /* 0x14: Frame size */
    int pixfmt;             /* 0x10: Pixel format */
    uint32_t phys_addr;     /* 0x18: Physical address */
    uint32_t virt_addr;     /* 0x1c: Virtual address */
    uint8_t data[0x408];    /* 0x20-0x427: Frame data */
} VBMFrame;

/* VBM Pool structure */
typedef struct {
    int chn;                /* 0x00: Channel ID */
    void *priv;             /* 0x04: Private data */
    uint8_t fmt[0xd0];      /* 0x08-0xd7: Format data */
    char name[64];          /* 0xd8-0x117: Pool name */
    uint32_t phys_base;     /* 0x16c: Physical base address */
    uint32_t virt_base;     /* 0x170: Virtual base address */
    void *ops[2];           /* 0x174-0x17b: Operations */
    int pool_id;            /* 0x17c: Pool ID from IMP_FrameSource_GetPool */
    VBMFrame *frames;       /* 0x180: Frame array */
    int frame_count;        /* Frame count (from fmt offset 0xcc) */
    int frame_size;         /* Calculated frame size */

    /* Extended fields for frame queue management */
    int *available_queue;   /* Queue of available frame indices */
    int queue_head;         /* Head of queue (next to dequeue) */
    int queue_tail;         /* Tail of queue (next to enqueue) */
    int queue_count;        /* Number of frames in queue */
    pthread_mutex_t queue_mutex; /* Mutex for queue access */
} VBMPool;

/* Global VBM state */
typedef struct {
    VBMPool *pool;          /* 0x00: Pool pointer */
    uint32_t phys_addr;     /* 0x04: Physical address */
    uint32_t virt_addr;     /* 0x08: Virtual address */
    int ref_count;          /* 0x0c: Reference count */
    pthread_mutex_t mutex;  /* 0x10: Mutex */
} VBMVolume;

static VBMPool *vbm_instance[MAX_VBM_POOLS] = {NULL};
static VBMVolume g_framevolumes[30]; /* Global frame volumes array */

/* External functions */
extern int IMP_FrameSource_GetPool(int chn);
extern int IMP_Alloc(char *name, int size, char *tag);
extern int IMP_PoolAlloc(int pool_id, char *name, int size, char *tag);
extern int IMP_Free(uint32_t phys_addr);

/* Calculate frame size based on pixel format */
static int calculate_frame_size(int width, int height, int pixfmt) {
    int size;

    switch (pixfmt) {
        case 0x23:      /* ARGB8888 */
        case 0xf:       /* RGBA8888 */
            size = ((width + 15) & 0xfffffff0) * (height << 2);
            break;

        case 0x3231564e: /* NV12 */
        case 0x32315559: /* YU12 */
            size = ((((height + 15) & 0xfffffff0) * 12) >> 3) * ((width + 15) & 0xfffffff0);
            break;

        case 0x32314742: /* BG12 */
        case 0x32314142: /* AB12 */
        case 0x32314247: /* GB12 */
        case 0x32314752: /* RG12 */
        case 0x50424752: /* RGBP */
        case 0x56595559: /* YUYV */
        case 0x59565955: /* UYVY */
            size = (width * height * 16) >> 3;
            break;

        case 0x33524742: /* BGR3 */
            size = (width * height * 24) >> 3;
            break;

        case 0x34524742: /* BGR4 */
            size = (width * height * 32) >> 3;
            break;

        default:
            size = -1;
            break;
    }

    return size;
}

int VBMCreatePool(int chn, void *fmt, void *ops, void *priv) {
    if (chn < 0 || chn >= MAX_VBM_POOLS) {
        return -1;
    }

    if (fmt == NULL) {
        fprintf(stderr, "[VBM] CreatePool: NULL format\n");
        return -1;
    }

    /* ops can be NULL - we'll use default operations */
    (void)ops;

    /* Safe struct member access using byte offsets */
    uint8_t *fmt_bytes = (uint8_t*)fmt;

    /* Debug: dump first part of format structure */
    fprintf(stderr, "[VBM] CreatePool: chn=%d, fmt structure dump:\n", chn);
    fprintf(stderr, "[VBM]   Offset 0x00-0x0f: ");
    for (int i = 0; i < 16; i++) {
        fprintf(stderr, "%02x ", fmt_bytes[i]);
    }
    fprintf(stderr, "\n[VBM]   Offset 0xc0-0xcf: ");
    for (int i = 0xc0; i < 0xd0; i++) {
        fprintf(stderr, "%02x ", fmt_bytes[i]);
    }
    fprintf(stderr, "\n");

    /* Get frame count from format structure at offset 0xcc */
    int frame_count;
    memcpy(&frame_count, fmt_bytes + 0xcc, sizeof(int));

    fprintf(stderr, "[VBM] CreatePool: raw frame_count at 0xcc = %d (0x%x)\n", frame_count, frame_count);

    /* Also check offset 0xd4 which is used in the decompilation */
    int frame_count_d4;
    memcpy(&frame_count_d4, fmt_bytes + 0xd4, sizeof(int));
    fprintf(stderr, "[VBM] CreatePool: value at 0xd4 = %d (0x%x)\n", frame_count_d4, frame_count_d4);

    /* Sanity check frame count - default to 4 if invalid */
    if (frame_count <= 0 || frame_count > 32) {
        fprintf(stderr, "[VBM] CreatePool: invalid frame_count=%d, using default 4\n", frame_count);
        frame_count = 4;
    }

    /* Allocate pool structure */
    size_t pool_size = frame_count * VBM_FRAME_SIZE + 0x180;
    fprintf(stderr, "[VBM] CreatePool: allocating pool_size=%zu (frame_count=%d * 0x%x + 0x180)\n",
            pool_size, frame_count, VBM_FRAME_SIZE);

    VBMPool *pool = (VBMPool*)malloc(pool_size);
    if (pool == NULL) {
        fprintf(stderr, "[VBM] CreatePool: malloc failed (size=%zu): %s\n",
                pool_size, strerror(errno));
        return -1;
    }

    memset(pool, 0, pool_size);

    /* Initialize pool with safe member access */
    pool->chn = chn;
    pool->priv = priv;
    pool->frame_count = frame_count;

    /* Copy format data (0xd0 bytes) */
    memcpy(pool->fmt, fmt, 0xd0);

    /* Copy ops pointers if provided */
    if (ops != NULL) {
        void **ops_array = (void**)ops;
        pool->ops[0] = ops_array[0];
        pool->ops[1] = ops_array[1];
    } else {
        pool->ops[0] = NULL;
        pool->ops[1] = NULL;
    }

    pool->pool_id = -1;

    /* Create pool name */
    snprintf(pool->name, sizeof(pool->name), "vbm_chn%d", chn);

    /* Get format parameters with safe access
     * Based on actual structure layout from prudynt:
     * Offset 0x00: width
     * Offset 0x04: height
     * Offset 0x08: pixfmt
     * The decompilation shows pool offsets, not format structure offsets
     */
    int width, height, pixfmt, req_size;
    memcpy(&width, fmt_bytes + 0x0, sizeof(int));
    memcpy(&height, fmt_bytes + 0x4, sizeof(int));
    memcpy(&pixfmt, fmt_bytes + 0x8, sizeof(int));
    memcpy(&req_size, fmt_bytes + 0xc, sizeof(int));

    /* Calculate frame size */
    int calc_size = calculate_frame_size(width, height, pixfmt);
    pool->frame_size = (req_size >= calc_size) ? req_size : calc_size;

    fprintf(stderr, "[VBM] CreatePool: chn=%d, %dx%d fmt=0x%x, %d frames, size=%d\n",
            chn, width, height, pixfmt, frame_count, pool->frame_size);

    /* Try to get pool from FrameSource */
    pool->pool_id = IMP_FrameSource_GetPool(chn);

    /* Allocate memory for frames via DMA allocator */
    int total_size = pool->frame_size * frame_count;
    char alloc_name[256];
    int ret;

    if (pool->pool_id < 0) {
        ret = IMP_Alloc(alloc_name, total_size, pool->name);
    } else {
        ret = IMP_PoolAlloc(pool->pool_id, alloc_name, total_size, pool->name);
    }

    if (ret < 0) {
        fprintf(stderr, "[VBM] CreatePool: allocation failed\n");
        free(pool);
        return -1;
    }

    /* Get physical and virtual addresses from DMA buffer */
    /* DMA buffer structure is returned in alloc_name */
    uint32_t phys_base, virt_base;
    memcpy(&virt_base, alloc_name + 0x80, sizeof(uint32_t));
    memcpy(&phys_base, alloc_name + 0x84, sizeof(uint32_t));

    pool->phys_base = phys_base;
    pool->virt_base = virt_base;

    /* Initialize frames array pointer */
    uint8_t *pool_bytes = (uint8_t*)pool;
    pool->frames = (VBMFrame*)(pool_bytes + 0x180);

    /* Initialize each frame */
    for (int i = 0; i < frame_count; i++) {
        VBMFrame *frame = &pool->frames[i];
        frame->index = i;
        frame->chn = chn;
        frame->width = width;
        frame->height = height;
        frame->pixfmt = pixfmt;
        frame->size = pool->frame_size;
        frame->phys_addr = pool->phys_base + (i * pool->frame_size);
        frame->virt_addr = pool->virt_base + (i * pool->frame_size);

        fprintf(stderr, "[VBM] Frame %d: phys=0x%x virt=0x%x\n",
                i, frame->phys_addr, frame->virt_addr);

        /* Register in global frame volumes */
        for (int j = 0; j < 30; j++) {
            if (g_framevolumes[j].pool == NULL) {
                g_framevolumes[j].pool = (VBMPool*)frame;
                g_framevolumes[j].phys_addr = frame->phys_addr;
                g_framevolumes[j].virt_addr = frame->virt_addr;
                g_framevolumes[j].ref_count = 0;
                pthread_mutex_init(&g_framevolumes[j].mutex, NULL);
                break;
            }
        }
    }

    /* Initialize frame queue */
    pool->available_queue = (int*)calloc(frame_count, sizeof(int));
    if (pool->available_queue == NULL) {
        fprintf(stderr, "[VBM] CreatePool: failed to allocate queue\n");
        IMP_Free(pool->phys_base);
        free(pool);
        return -1;
    }

    pool->queue_head = 0;
    pool->queue_tail = 0;
    pool->queue_count = 0;
    pthread_mutex_init(&pool->queue_mutex, NULL);

    vbm_instance[chn] = pool;

    fprintf(stderr, "[VBM] CreatePool: chn=%d created successfully\n", chn);
    return 0;
}

int VBMDestroyPool(int chn) {
    if (chn < 0 || chn >= MAX_VBM_POOLS) {
        return -1;
    }

    VBMPool *pool = vbm_instance[chn];
    if (pool == NULL) {
        return -1;
    }

    fprintf(stderr, "[VBM] DestroyPool: chn=%d\n", chn);

    /* Unregister frames from global volumes */
    for (int i = 0; i < 30; i++) {
        if (g_framevolumes[i].pool != NULL) {
            VBMFrame *frame = (VBMFrame*)g_framevolumes[i].pool;
            if (frame->chn == chn) {
                pthread_mutex_destroy(&g_framevolumes[i].mutex);
                g_framevolumes[i].pool = NULL;
                g_framevolumes[i].phys_addr = 0;
                g_framevolumes[i].virt_addr = 0;
                g_framevolumes[i].ref_count = 0;
            }
        }
    }

    /* Destroy queue mutex */
    pthread_mutex_destroy(&pool->queue_mutex);

    /* Free queue */
    if (pool->available_queue != NULL) {
        free(pool->available_queue);
    }

    /* Free allocated memory */
    if (pool->phys_base != 0) {
        IMP_Free(pool->phys_base);
    }

    /* Free pool structure */
    free(pool);
    vbm_instance[chn] = NULL;

    fprintf(stderr, "[VBM] DestroyPool: chn=%d destroyed\n", chn);
    return 0;
}

int VBMFillPool(int chn) {
    if (chn < 0 || chn >= MAX_VBM_POOLS) {
        return -1;
    }

    VBMPool *pool = vbm_instance[chn];
    if (pool == NULL) {
        return -1;
    }

    fprintf(stderr, "[VBM] FillPool: chn=%d, filling %d frames\n", chn, pool->frame_count);

    /* Queue all frames as available */
    pthread_mutex_lock(&pool->queue_mutex);

    for (int i = 0; i < pool->frame_count; i++) {
        pool->available_queue[pool->queue_tail] = i;
        pool->queue_tail = (pool->queue_tail + 1) % pool->frame_count;
        pool->queue_count++;
    }

    pthread_mutex_unlock(&pool->queue_mutex);

    fprintf(stderr, "[VBM] FillPool: queued %d frames\n", pool->queue_count);

    return 0;
}

int VBMFlushFrame(int chn) {
    if (chn < 0 || chn >= MAX_VBM_POOLS) {
        return -1;
    }

    VBMPool *pool = vbm_instance[chn];
    if (pool == NULL) {
        return -1;
    }

    fprintf(stderr, "[VBM] FlushFrame: chn=%d\n", chn);

    /* Clear the frame queue */
    pthread_mutex_lock(&pool->queue_mutex);

    pool->queue_head = 0;
    pool->queue_tail = 0;
    pool->queue_count = 0;

    pthread_mutex_unlock(&pool->queue_mutex);

    fprintf(stderr, "[VBM] FlushFrame: flushed all frames\n");

    return 0;
}

int VBMGetFrame(int chn, void **frame) {
    if (chn < 0 || chn >= MAX_VBM_POOLS) {
        return -1;
    }

    VBMPool *pool = vbm_instance[chn];
    if (pool == NULL) {
        *frame = NULL;
        return -1;
    }

    /* Get next available frame from queue */
    pthread_mutex_lock(&pool->queue_mutex);

    if (pool->queue_count == 0) {
        /* No frames available */
        pthread_mutex_unlock(&pool->queue_mutex);
        *frame = NULL;
        return -1;
    }

    /* Dequeue frame */
    int frame_idx = pool->available_queue[pool->queue_head];
    pool->queue_head = (pool->queue_head + 1) % pool->frame_count;
    pool->queue_count--;

    pthread_mutex_unlock(&pool->queue_mutex);

    *frame = &pool->frames[frame_idx];
    fprintf(stderr, "[VBM] GetFrame: chn=%d, frame=%p (idx=%d, %d remaining)\n",
            chn, *frame, frame_idx, pool->queue_count);
    return 0;
}

int VBMReleaseFrame(int chn, void *frame) {
    if (chn < 0 || chn >= MAX_VBM_POOLS) {
        return -1;
    }

    VBMPool *pool = vbm_instance[chn];
    if (pool == NULL || frame == NULL) {
        return -1;
    }

    fprintf(stderr, "[VBM] ReleaseFrame: chn=%d, frame=%p\n", chn, frame);

    /* Get frame index */
    VBMFrame *vbm_frame = (VBMFrame*)frame;
    int frame_idx = vbm_frame->index;

    /* Return frame to available queue */
    pthread_mutex_lock(&pool->queue_mutex);

    if (pool->queue_count >= pool->frame_count) {
        /* Queue is full - shouldn't happen */
        pthread_mutex_unlock(&pool->queue_mutex);
        fprintf(stderr, "[VBM] ReleaseFrame: queue full!\n");
        return -1;
    }

    pool->available_queue[pool->queue_tail] = frame_idx;
    pool->queue_tail = (pool->queue_tail + 1) % pool->frame_count;
    pool->queue_count++;

    pthread_mutex_unlock(&pool->queue_mutex);

    fprintf(stderr, "[VBM] ReleaseFrame: returned frame idx=%d (%d available)\n",
            frame_idx, pool->queue_count);

    return 0;
}

