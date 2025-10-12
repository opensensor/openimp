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
/* Buffer queue/dequeue ioctls (from decompilation notes) */
#define VIDIOC_QBUF         0xc044560f  /* Queue buffer */
#define VIDIOC_QUERYBUF     0xc0445609  /* Query buffer */
#define VIDIOC_DQBUF        0xc0445611  /* Dequeue buffer */

/* Encoder ioctl commands (to be discovered) */
#define ENCODER_CREATE_CHN  0x40000000  /* Placeholder */
#define ENCODER_START       0x40000001  /* Placeholder */

/* ISP ioctl commands (to be discovered) */
#define ISP_INIT            0x50000000  /* Placeholder */
#define ISP_SET_SENSOR      0x50000001  /* Placeholder */

/* V4L2 format structure with Ingenic extensions for VIDIOC_SET_FMT ioctl
 * This is a 0xc8 (200 byte) structure that gets passed to ioctl 0xc07056c3
 * Based on kernel driver tx-isp-module.c line 3896
 *
 * The raw_data area contains a copy of imp_channel_attr starting at offset 0x24
 * which the kernel extracts and passes to tisp_channel_attr_set
 */
typedef struct {
    /* V4L2 standard header */
    int type;                   /* 0x00: Buffer type (V4L2_BUF_TYPE_VIDEO_CAPTURE = 1) */
    /* V4L2 pix format */
    int width;                  /* 0x04: Width */
    int height;                 /* 0x08: Height */
    int pixelformat;            /* 0x0c: Pixel format (fourcc) */
    int field;                  /* 0x10: Field order */
    int bytesperline;           /* 0x14: Bytes per line */
    int sizeimage;              /* 0x18: Image size in bytes */
    int colorspace;             /* 0x1c: Colorspace (V4L2_COLORSPACE_SRGB = 8) */
    int priv;                   /* 0x20: Private data */
    /* Ingenic imp_channel_attr in raw_data area (starting at 0x24)
     * Must match tisp_channel_attr_set indices:
     *   [0]=enable, [1]=width, [2]=height,
     *   [3]=crop_enable, [4]=crop_x, [5]=crop_y, [6]=crop_width, [7]=crop_height,
     *   [8]=scaler_enable, [9]=scaler_outwidth, [10]=scaler_outheight,
     *   [11]=picwidth, [12]=picheight, [13]=fps_num, [14]=fps_den
     * Note: pixel format is conveyed via V4L2 pixelformat header field. */
    int enable;                 /* 0x24: Enable (arg2[0]) */
    int attr_width;             /* 0x28: Width (arg2[1]) */
    int attr_height;            /* 0x2c: Height (arg2[2]) */
    int crop_enable;            /* 0x30: Crop enable (arg2[3]) */
    int crop_x;                 /* 0x34: Crop X (arg2[4]) */
    int crop_y;                 /* 0x38: Crop Y (arg2[5]) */
    int crop_width;             /* 0x3c: Crop width (arg2[6]) */
    int crop_height;            /* 0x40: Crop height (arg2[7]) */
    int scaler_enable;          /* 0x44: Scaler enable (arg2[8]) */
    int scaler_outwidth;        /* 0x48: Scaler output width (arg2[9]) */
    int scaler_outheight;       /* 0x4c: Scaler output height (arg2[10]) */
    int picwidth;               /* 0x50: Picture width (arg2[11]) */
    int picheight;              /* 0x54: Picture height (arg2[12]) */
    int fps_num;                /* 0x58: FPS numerator (arg2[13]) */
    int fps_den;                /* 0x5c: FPS denominator (arg2[14]) */
    char padding[0x68];         /* 0x60-0xc7: Padding to 200 bytes */
} fs_format_t;

/* v4l2_requestbuffers (driver expects 5 x u32 = 0x14 bytes):
 * count, type, memory, capabilities, reserved[1]
 */
typedef struct {
    uint32_t count;        /* Buffer count */
    uint32_t type;         /* V4L2_BUF_TYPE_VIDEO_CAPTURE=1 */
    uint32_t memory;       /* 1=MMAP, 2=USERPTR */
    uint32_t capabilities; /* set to 0 unless using V4L2 capability flags */
    uint32_t reserved[1];  /* reserved */
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
/**
 * Get format from framechan device
 * ioctl: 0x407056c4 (VIDIOC_GET_FMT)
 * Based on decompilation at 0x9ecf8
 *
 * CRITICAL: The OEM code uses the same structure for both SET_FMT and GET_FMT.
 * The kernel modifies the structure in-place, updating computed fields like
 * sizeimage and bytesperline. We must use the same 0x70-byte structure layout.
 */
int fs_get_format(int fd, fs_format_t *fmt) {
    if (fd < 0 || fmt == NULL) {
        return -1;
    }

    /* Use the same 0x70-byte structure as SET_FMT - the kernel expects this exact layout */
    struct __attribute__((packed)) fs_format70_t {
        /* v4l2-like header (0x00..0x23) */
        uint32_t type;         /* 0x00 */
        uint32_t width;        /* 0x04 */
        uint32_t height;       /* 0x08 */
        uint32_t pixelformat;  /* 0x0c */
        uint32_t field;        /* 0x10 */
        uint32_t bytesperline; /* 0x14 */
        uint32_t sizeimage;    /* 0x18 */
        uint32_t colorspace;   /* 0x1c */
        uint32_t priv;         /* 0x20 */
        /* Ingenic IMPFSChnAttr slice (0x24..0x6F) - kernel may read/write these */
        uint32_t enable;           /* 0x24 */
        uint32_t attr_width;       /* 0x28 */
        uint32_t attr_height;      /* 0x2c */
        uint32_t crop_enable;      /* 0x30 */
        uint32_t crop_x;           /* 0x34 */
        uint32_t crop_y;           /* 0x38 */
        uint32_t crop_width;       /* 0x3c */
        uint32_t crop_height;      /* 0x40 */
        uint32_t scaler_enable;    /* 0x44 */
        uint32_t scaler_outwidth;  /* 0x48 */
        uint32_t scaler_outheight; /* 0x4c */
        uint32_t picwidth;         /* 0x50 */
        uint32_t picheight;        /* 0x54 */
        uint32_t fps_num;          /* 0x58 */
        uint32_t fps_den;          /* 0x5c */
        uint8_t  pad[0x70 - 0x24 - 15 * 4]; /* 16 bytes padding to 0x70 */
    } g = {0};

    g.type = 1; /* V4L2_BUF_TYPE_VIDEO_CAPTURE */
    int ret = ioctl(fd, VIDIOC_GET_FMT, &g);
    if (ret < 0) {
        fprintf(stderr, "[KernelIF] VIDIOC_GET_FMT failed: %s\n", strerror(errno));
        return -1;
    }

    /* Copy relevant fields back out */
    fmt->type = (int)g.type;
    fmt->width = (int)g.width;
    fmt->height = (int)g.height;
    fmt->pixelformat = (int)g.pixelformat;
    fmt->bytesperline = (int)g.bytesperline;
    fmt->sizeimage = (int)g.sizeimage;
    fmt->colorspace = (int)g.colorspace;

    fprintf(stderr, "[KernelIF] Got format: %dx%d fmt=0x%x sizeimage=%d bytesperline=%d\n",
            fmt->width, fmt->height, fmt->pixelformat, fmt->sizeimage, fmt->bytesperline);
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
 *
 * Based on decompilation at 0x9ecf8:
 * The format structure needs to be properly initialized with all fields
 */
int fs_set_format(int fd, fs_format_t *fmt) {
    if (fd < 0 || fmt == NULL) {
        return -1;
    }

    /* Use v4l2-like 0x70 struct for SET as well (matches driver expectations)
     * IMPORTANT: The driver expects Ingenic IMP channel attributes packed
     * immediately after the 0x24-byte v4l2-like header. BN MCP shows the OEM lib
     * fills these fields before issuing 0xc07056c3. If we leave them zero, the
     * remote handler computes bogus sizeimage/stride, causing QBUF to fail.
     */
    struct __attribute__((packed)) fs_format70_t {
        /* v4l2-like header (0x00..0x23) */
        uint32_t type;         /* 0x00 */
        uint32_t width;        /* 0x04 */
        uint32_t height;       /* 0x08 */
        uint32_t pixelformat;  /* 0x0c */
        uint32_t field;        /* 0x10 */
        uint32_t bytesperline; /* 0x14 */
        uint32_t sizeimage;    /* 0x18 */
        uint32_t colorspace;   /* 0x1c */
        uint32_t priv;         /* 0x20 */
        /* Ingenic IMPFSChnAttr slice (0x24..0x6F) */
        uint32_t enable;           /* 0x24 */
        uint32_t attr_width;       /* 0x28 */
        uint32_t attr_height;      /* 0x2c */
        uint32_t crop_enable;      /* 0x30 */
        uint32_t crop_x;           /* 0x34 */
        uint32_t crop_y;           /* 0x38 */
        uint32_t crop_width;       /* 0x3c */
        uint32_t crop_height;      /* 0x40 */
        uint32_t scaler_enable;    /* 0x44 */
        uint32_t scaler_outwidth;  /* 0x48 */
        uint32_t scaler_outheight; /* 0x4c */
        uint32_t picwidth;         /* 0x50 */
        uint32_t picheight;        /* 0x54 */
        uint32_t fps_num;          /* 0x58 */
        uint32_t fps_den;          /* 0x5c */
        uint8_t  pad[0x70 - 0x24 - 15 * 4]; /* 16 bytes padding to 0x70 */
    } s = {0};

    /* Header */
    s.type = 1; /* V4L2_BUF_TYPE_VIDEO_CAPTURE */
    s.width = (uint32_t)fmt->width;
    s.height = (uint32_t)fmt->height;

    /* Convert enum pixfmt to fourcc if needed */
    uint32_t fourcc = (fmt->pixelformat < 0x100) ? pixfmt_to_fourcc(fmt->pixelformat)
                                                 : (uint32_t)fmt->pixelformat;
    s.pixelformat = fourcc;
    s.field = 0;         /* V4L2_FIELD_NONE (driver forces to 4) */

    /* CRITICAL: Driver does NOT compute sizeimage properly - we must set it explicitly
     * For NV12/NV21: size = width * height * 3/2 (Y plane + UV plane)
     * The driver stores this at offset 0x58 and validates QBUF length against it.
     */
    uint32_t calc_sizeimage = 0;
    if (fourcc == 0x3231564e || fourcc == 0x3132564e) {  /* NV12 or NV21 */
        calc_sizeimage = (uint32_t)fmt->width * (uint32_t)fmt->height * 3 / 2;
    } else if (fourcc == 0x56595559 || fourcc == 0x59565955) {  /* YUYV or UYVY */
        calc_sizeimage = (uint32_t)fmt->width * (uint32_t)fmt->height * 2;
    } else {
        /* Fallback: assume 3/2 for planar formats */
        calc_sizeimage = (uint32_t)fmt->width * (uint32_t)fmt->height * 3 / 2;
    }

    s.bytesperline = 0;  /* Let driver compute */
    s.sizeimage = calc_sizeimage;  /* MUST set explicitly */
    s.colorspace = 8;    /* V4L2_COLORSPACE_SRGB */
    s.priv = 0;

    /* Extended IMP attributes (match fs_format_t layout in kernel_interface.h) */
    s.enable           = (uint32_t)fmt->enable;
    s.attr_width       = (uint32_t)fmt->attr_width;
    s.attr_height      = (uint32_t)fmt->attr_height;
    s.crop_enable      = (uint32_t)fmt->crop_enable;
    s.crop_x           = (uint32_t)fmt->crop_x;
    s.crop_y           = (uint32_t)fmt->crop_y;
    s.crop_width       = (uint32_t)fmt->crop_width;
    s.crop_height      = (uint32_t)fmt->crop_height;
    s.scaler_enable    = (uint32_t)fmt->scaler_enable;
    s.scaler_outwidth  = (uint32_t)fmt->scaler_outwidth;
    s.scaler_outheight = (uint32_t)fmt->scaler_outheight;
    s.picwidth         = (uint32_t)fmt->picwidth;
    s.picheight        = (uint32_t)fmt->picheight;
    s.fps_num          = (uint32_t)fmt->fps_num;
    s.fps_den          = (uint32_t)fmt->fps_den;

    int ret = ioctl(fd, VIDIOC_SET_FMT, &s);
    if (ret < 0) {
        fprintf(stderr, "[KernelIF] VIDIOC_SET_FMT failed: %s\n", strerror(errno));
        fprintf(stderr, "[KernelIF]   Requested: %dx%d fmt=0x%x (fourcc=0x%x) colorspace=%d\n",
                fmt->width, fmt->height, fmt->pixelformat, fourcc, s.colorspace);
        return -1;
    }

    /* CRITICAL: The kernel modifies the structure in-place during SET_FMT and copies it back.
     * The remote ISP core may update sizeimage, bytesperline, and other fields.
     * We MUST read these modified values from 's' after the ioctl returns.
     * DO NOT call GET_FMT here - it queries the remote ISP again and may return garbage.
     */
    fprintf(stderr, "[KernelIF] Set format: %dx%d fmt=0x%x (fourcc=0x%x) sizeimage=%u->%u bytesperline=%u colorspace=%d\n",
            fmt->width, fmt->height, fmt->pixelformat, fourcc,
            calc_sizeimage, s.sizeimage, s.bytesperline, s.colorspace);

    /* Update fmt with kernel-modified values */
    fmt->sizeimage = (int)s.sizeimage;
    fmt->bytesperline = (int)s.bytesperline;
    fmt->field = (int)s.field;

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

    fs_bufcnt_t req = {0};
    req.count = (uint32_t)count;
    req.type = 1;            /* V4L2_BUF_TYPE_VIDEO_CAPTURE */
    req.memory = 2;          /* V4L2_MEMORY_USERPTR (matches earlier success) */
    req.capabilities = 0;    /* no special caps */
    req.reserved[0] = 0;

    int ret = ioctl(fd, VIDIOC_SET_BUFCNT, &req);
    if (ret < 0) {
        fprintf(stderr, "[KernelIF] VIDIOC_SET_BUFCNT failed: %s\n", strerror(errno));
        return -1;
    }

    fprintf(stderr, "[KernelIF] Set buffer count: %d (actual: %u)\n", count, req.count);
    return (int)req.count;
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

/* 32-bit v4l2_buffer layout used by this driver */
struct v4l2_buf32 {
    uint32_t index;        /* 0x00 */
    uint32_t type;         /* 0x04 */
    uint32_t bytesused;    /* 0x08 */
    uint32_t flags;        /* 0x0C */
    uint32_t field;        /* 0x10 */
    uint32_t ts_sec;       /* 0x14 */
    uint32_t ts_usec;      /* 0x18 */
    uint32_t timecode[4];  /* 0x1C..0x28 */
    uint32_t sequence;     /* 0x2C */
    uint32_t memory;       /* 0x30 */
    uint32_t m;            /* 0x34: union userptr/offset/fd */
    uint32_t length;       /* 0x38 */
    uint32_t reserved2;    /* 0x3C */
    uint32_t reserved;     /* 0x40 */
} __attribute__((packed));

/* Query buffer to get the exact length the driver expects */
int fs_querybuf(int fd, int index, unsigned int *length_out) {
    if (fd < 0 || index < 0) return -1;
    struct v4l2_buf32 b;
    memset(&b, 0, sizeof(b));
    b.index = (uint32_t)index;
    b.type = 1; /* V4L2_BUF_TYPE_VIDEO_CAPTURE */
    int ret = ioctl(fd, VIDIOC_QUERYBUF, &b);
    if (ret < 0) {
        fprintf(stderr, "[KernelIF] QUERYBUF failed: idx=%d err=%s\n", index, strerror(errno));
        return -1;
    }
    if (length_out) *length_out = b.length;
    return 0;
}

/* Queue a userspace buffer to the framechan driver
 * Driver expects a 0x44-byte v4l2_buffer-like struct (32-bit layout).
 */
int fs_qbuf(int fd, int index, unsigned long phys, unsigned int length) {
    if (fd < 0 || index < 0) return -1;

    struct v4l2_buf32 b;
    memset(&b, 0, sizeof(b));
    b.index = (uint32_t)index;
    b.type = 1;            /* V4L2_BUF_TYPE_VIDEO_CAPTURE */
    b.memory = 2;          /* Must match REQBUFS memory type (USERPTR) */
    b.field = 0;           /* V4L2_FIELD_NONE */
    b.flags = 0;
    b.sequence = 0;
    b.m = (uint32_t)phys;  /* Driver uses this as DMA phys (driver-specific) */
    b.length = length;     /* Must equal kernel expected length */
    b.bytesused = length;  /* Some drivers check bytesused too */

    int ret = ioctl(fd, VIDIOC_QBUF, &b);
    if (ret < 0) {
        fprintf(stderr, "[KernelIF] QBUF failed: idx=%d phys=0x%lx len=%u err=%s\n", index, phys, length, strerror(errno));
        return -1;
    }
    return 0;
}

/* Dequeue a filled buffer from the framechan driver */
int fs_dqbuf(int fd, int *index_out) {
    if (fd < 0 || !index_out) return -1;

    struct v4l2_buf32 b;
    memset(&b, 0, sizeof(b));
    b.type = 1;      /* Required for type check in driver */

    int ret = ioctl(fd, VIDIOC_DQBUF, &b);
    if (ret < 0) {
        if (errno == EAGAIN) return -2; /* non-blocking */
        fprintf(stderr, "[KernelIF] DQBUF failed: %s\n", strerror(errno));
        return -1;
    }
    *index_out = (int)b.index;
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
    int pixfmt;             /* 0x10: Pixel format */
    int size;               /* 0x14: Frame size */
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
    int fd;                 /* Kernel framechan fd for qbuf/dqbuf (-1 if unused) */
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
        /* Enum values (PIX_FMT_*) */
        case 0xa:       /* PIX_FMT_NV12 (enum) */
        case 0xb:       /* PIX_FMT_NV21 (enum) */
        /* Fourcc values */
        case 0x3231564e: /* 'NV12' (fourcc) */
        case 0x3132564e: /* 'NV21' (fourcc) */
        case 0x32315559: /* 'YU12' (fourcc) */
            size = ((((height + 15) & 0xfffffff0) * 12) >> 3) * ((width + 15) & 0xfffffff0);
            break;

        case 0x23:      /* ARGB8888 */
        case 0xf:       /* RGBA8888 */
            size = ((width + 15) & 0xfffffff0) * (height << 2);
            break;

        case 0x32314742: /* BG12 */
        case 0x32314142: /* AB12 */
        case 0x32314247: /* GB12 */
        case 0x32314752: /* RG12 */
        case 0x50424752: /* RGBP */
        case 0x1:       /* PIX_FMT_YUYV422 (enum) */

        case 0x56595559: /* 'YUYV' (fourcc) */
        case 0x2:       /* PIX_FMT_UYVY422 (enum) */
        case 0x59565955: /* 'UYVY' (fourcc) */
            size = (width * height * 16) >> 3;
            break;

        case 0x33524742: /* BGR3 */
            size = (width * height * 24) >> 3;
            break;

        case 0x34524742: /* BGR4 */
            size = (width * height * 32) >> 3;
            break;

        default:
            fprintf(stderr, "[VBM] calculate_frame_size: unknown pixfmt=0x%x\n", pixfmt);
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

    /* Check if pool already exists - if so, return success */
    if (vbm_instance[chn] != NULL) {
        fprintf(stderr, "[VBM] CreatePool: pool for chn=%d already exists, skipping\n", chn);
        return 0;
    }

    /* ops can be NULL - we'll use default operations */
    (void)ops;

    /* Safe struct member access using byte offsets */
    uint8_t *fmt_bytes = (uint8_t*)fmt;

    /* Get frame count from IMPFSChnAttr structure
     * IMPFSChnAttr layout:
     *   0x00: picWidth
     *   0x04: picHeight
     *   0x08: pixFmt
     *   0x0c: crop (20 bytes)
     *   0x20: scaler (12 bytes)
     *   0x2c: outFrmRateNum
     *   0x30: outFrmRateDen
     *   0x34: nrVBs (number of video buffers = frame count)
     *   0x38: type
     *   0x3c: fcrop (T31)
     */



    int frame_count;
    memcpy(&frame_count, fmt_bytes + 0x34, sizeof(int));

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
    pool->fd = -1;

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

    /* Initialize each frame using safe member access */
    for (int i = 0; i < frame_count; i++) {
        VBMFrame *frame = &pool->frames[i];

        /* Use safe struct member access pattern */
        uint8_t *frame_bytes = (uint8_t*)frame;

        /* Write index at offset 0x00 */
        memcpy(frame_bytes + 0x00, &i, sizeof(int));

        /* Write chn at offset 0x04 */
        memcpy(frame_bytes + 0x04, &chn, sizeof(int));

        /* Write width at offset 0x08 */
        memcpy(frame_bytes + 0x08, &width, sizeof(int));

        /* Write height at offset 0x0c */
        memcpy(frame_bytes + 0x0c, &height, sizeof(int));

        /* Write pixfmt at offset 0x10 */
        memcpy(frame_bytes + 0x10, &pixfmt, sizeof(int));

        /* Write size at offset 0x14 */
        int frame_size = pool->frame_size;
        memcpy(frame_bytes + 0x14, &frame_size, sizeof(int));

        /* Write phys_addr at offset 0x18 */
        uint32_t phys = pool->phys_base + (i * pool->frame_size);
        memcpy(frame_bytes + 0x18, &phys, sizeof(uint32_t));

        /* Write virt_addr at offset 0x1c */
        uint32_t virt = pool->virt_base + (i * pool->frame_size);
        memcpy(frame_bytes + 0x1c, &virt, sizeof(uint32_t));

        fprintf(stderr, "[VBM] Frame %d: phys=0x%x virt=0x%x\n",
                i, phys, virt);

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

/* Prime kernel queue with all VBM frames */
int VBMPrimeKernelQueue(int chn, int fd) {
    if (chn < 0 || chn >= MAX_VBM_POOLS) return -1;
    VBMPool *pool = vbm_instance[chn];
    if (!pool) return -1;
    pool->fd = fd;
    /* Ask driver for sizeimage once (fallback if QUERYBUF returns 0) */
    fs_format_t kfmt;
    memset(&kfmt, 0, sizeof(kfmt));
    unsigned int drv_len = 0;
    if (fs_get_format(fd, &kfmt) == 0 && kfmt.sizeimage > 0) {
        drv_len = (unsigned int)kfmt.sizeimage;
    }
    for (int i = 0; i < pool->frame_count; i++) {
        VBMFrame *f = &pool->frames[i];
        /* Driver expects DMA physical address in .m (BN MCP shows KSEG1 writes) */
        unsigned long phys = (unsigned long)f->phys_addr;
        unsigned int klen = 0;
        if (fs_querybuf(fd, i, &klen) < 0) {
            fprintf(stderr, "[VBM] PrimeKernelQueue: querybuf failed for idx=%d\n", i);
            return -1;
        }
        if (klen == 0 && drv_len > 0) {
            klen = drv_len;
            fprintf(stderr, "[VBM] PrimeKernelQueue: idx=%d querybuf_len=0, using sizeimage=%u\n", i, klen);
        }
        /* If still unknown, prefer NV12 exact size w*h*3/2 first, then fall back to pool size */
        if (klen == 0) {
            unsigned int nv12 = (unsigned int)f->width * (unsigned int)f->height * 3 / 2;
            if (nv12 > 0) {
                klen = nv12;
                fprintf(stderr, "[VBM] PrimeKernelQueue: idx=%d fallback NV12 size=%u (w=%d h=%d)\n", i, klen, f->width, f->height);
            }
        }
        if (klen == 0) {
            klen = (unsigned int)f->size;
            fprintf(stderr, "[VBM] PrimeKernelQueue: idx=%d fallback use pool size=%u (w=%d h=%d)\n", i, klen, f->width, f->height);
        }
        if (klen > (unsigned int)f->size) {
            fprintf(stderr, "[VBM] PrimeKernelQueue: idx=%d driver_len=%u > our_len=%u -> clamping\n", i, klen, (unsigned int)f->size);
            klen = (unsigned int)f->size;
        }
        if (fs_qbuf(fd, i, phys, klen) < 0) {
            /* Probe alternate length if NV12 vs stride mismatch: try other of {3110400, pool_size} */
            unsigned int alt = (klen == (unsigned int)f->size) ? ((unsigned int)f->width * (unsigned int)f->height * 3 / 2) : (unsigned int)f->size;
            if (alt != klen && alt > 0 && alt <= (unsigned int)f->size) {
                fprintf(stderr, "[VBM] PrimeKernelQueue: idx=%d first qbuf len=%u failed, trying alt len=%u\n", i, klen, alt);
                if (fs_qbuf(fd, i, phys, alt) == 0) {
                    klen = alt;
                } else {
                    fprintf(stderr, "[VBM] PrimeKernelQueue: qbuf failed for idx=%d (len=%u)\n", i, alt);
                    return -1;
                }
            } else {
                fprintf(stderr, "[VBM] PrimeKernelQueue: qbuf failed for idx=%d (len=%u)\n", i, klen);
                return -1;
            }
        }
    }
    fprintf(stderr, "[VBM] PrimeKernelQueue: queued %d frames to kernel for chn=%d\n", pool->frame_count, chn);
    return 0;
}

/* Dequeue a kernel-filled frame and map to VBM frame pointer */
int VBMKernelDequeue(int chn, int fd, void **frame_out) {
    if (chn < 0 || chn >= MAX_VBM_POOLS || !frame_out) return -1;
    VBMPool *pool = vbm_instance[chn];
    if (!pool) return -1;
    int idx = -1;
    int ret = fs_dqbuf(fd, &idx);
    if (ret != 0) return -1;
    if (idx < 0 || idx >= pool->frame_count) return -1;
    *frame_out = &pool->frames[idx];
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

    /* Validate frame index */
    if (frame_idx < 0 || frame_idx >= pool->frame_count) {
        fprintf(stderr, "[VBM] GetFrame: invalid frame index %d (max %d)\n",
                frame_idx, pool->frame_count - 1);
        *frame = NULL;
        return -1;
    }

    *frame = &pool->frames[frame_idx];

    /* Validate frame pointer */
    if (*frame == NULL) {
        fprintf(stderr, "[VBM] GetFrame: NULL frame pointer for index %d\n", frame_idx);
        return -1;
    }

    /* Additional validation: check if pointer is reasonable */
    uintptr_t frame_addr = (uintptr_t)(*frame);
    if (frame_addr < 0x10000) {
        fprintf(stderr, "[VBM] GetFrame: invalid frame pointer %p (too small)\n", *frame);
        *frame = NULL;
        return -1;
    }

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

    /* If kernel-backed, re-queue to kernel immediately (QBUF) */
    if (pool->fd >= 0) {
        unsigned long phys = vbm_frame->phys_addr;
        unsigned int length = (unsigned int)vbm_frame->size;
        if (fs_qbuf(pool->fd, frame_idx, phys, length) < 0) {
            fprintf(stderr, "[VBM] ReleaseFrame: fs_qbuf failed for idx=%d\n", frame_idx);
        }
    }

    pool->available_queue[pool->queue_tail] = frame_idx;
    pool->queue_tail = (pool->queue_tail + 1) % pool->frame_count;
    pool->queue_count++;

    pthread_mutex_unlock(&pool->queue_mutex);

    fprintf(stderr, "[VBM] ReleaseFrame: returned frame idx=%d (%d available)\n",
            frame_idx, pool->queue_count);

    return 0;
}



/* Expose frame backing buffer to higher layers (safe accessor) */
int VBMFrame_GetBuffer(void *frame, void **virt, int *size) {
    if (!frame || !virt || !size) return -1;
    VBMFrame *f = (VBMFrame*)frame;
    *virt = (void*)(uintptr_t)f->virt_addr;
    *size = f->size;
    return 0;
}
