/**
 * Kernel Driver Interface for IMP
 * Handles ioctl calls to Ingenic kernel drivers
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <pthread.h>
#include <errno.h>
#include <sched.h>
#include <time.h>
#include <stdarg.h>
#include "dma_alloc.h"
#include "imp_log_int.h"
#include "kernel_interface.h"
#include "trace_control.h"
#include "vbm_dq_step.h"
/* T31: frame-ready events of the encoder pull path; T23 shares that VBM
 * block (IVS capture, idle drain) since claude/t23-stub-fixes. */
#if defined(PLATFORM_T31) || defined(PLATFORM_T23)
#include "openimp_ready_event.h"
#endif

extern int64_t IMP_System_GetTimeStamp(void);
extern int64_t OpenIMP_P0_NormalizeMonotonicTimeStamp(uint64_t timestamp);

static void ki_trace(const char *fmt, ...)
{
    static unsigned int dequeue_trace_count;

    if (!openimp_debug_trace_enabled()) return;
    /* A nonblocking capture queue can legitimately report EAGAIN while it
     * waits for its first completed buffer.  Keep that diagnostic bounded so
     * it cannot evict the QBUF/STREAMON setup evidence from the kernel log. */
    if ((strstr(fmt, "DQBUF") != NULL || strstr(fmt, "KernelDequeue") != NULL) &&
        __sync_fetch_and_add(&dequeue_trace_count, 1) >= 24)
        return;
    int fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
    if (fd < 0) return;

    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) write(fd, buf, (size_t)n);
    close(fd);
}

static long long ki_mono_us(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (long long)now.tv_sec * 1000000LL + (long long)now.tv_nsec / 1000LL;
}

/* ioctl command definitions from decompilation */

/* FrameSource ioctl commands */
#if defined(PLATFORM_T20)
/* T20 uses the kernel 3.10 standard V4L2 capture ABI. */
#define VIDIOC_GET_FMT      0xc0cc5604
#define VIDIOC_TRY_FMT      0xc0cc5640
#define VIDIOC_SET_FMT      0xc0cc5605
#define VIDIOC_CROPCAP      0xc02c563a
#define VIDIOC_SET_CROP     0x8014563c
#define VIDIOC_GET_SCALERCAP 0xc00856c3
#define VIDIOC_SET_SCALER   0x800456c4
#elif defined(PLATFORM_T21) || defined(PLATFORM_T30)
/* T21 and T30 use the original 0x4c-byte frame-channel format ABI. */
#define VIDIOC_GET_FMT      0x404c56c4  /* Get format */
#define VIDIOC_SET_FMT      0xc04c56c3  /* Set format */
#else
#define VIDIOC_GET_FMT      0x407056c4  /* Get format */
#define VIDIOC_SET_FMT      0xc07056c3  /* Set format */
#endif
#define VIDIOC_SET_BUFCNT   0xc0145608  /* Set buffer count */
#define VIDIOC_SET_DEPTH    0x800456c5  /* Set frame depth */
#define VIDIOC_STREAM_ON    0x80045612  /* Start streaming */
#define VIDIOC_STREAM_OFF   0x80045613  /* Stop streaming */
#define VIDIOC_POLL_FRAME   0x400456bf  /* OEM frame-ready wait */
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

/* fs_format_t (the 0x70-byte stock fs_set_format/fs_get_format argument)
 * is declared in kernel_interface.h. */

struct fs_ioctl_format70 {
    uint32_t type;             /* 0x00 */
    uint32_t width;            /* 0x04: tisp_pix_format starts here */
    uint32_t height;           /* 0x08 */
    uint32_t pixelformat;      /* 0x0c */
    uint32_t field;            /* 0x10 */
    uint32_t bytesperline;     /* 0x14 */
    uint32_t sizeimage;        /* 0x18 */
    uint32_t colorspace;       /* 0x1c */
    uint32_t priv;             /* 0x20 */
#if !defined(PLATFORM_T21) && !defined(PLATFORM_T30)
    uint32_t flags;            /* 0x24 */
    uint32_t ycbcr_enc;        /* 0x28 */
    uint32_t quantization;     /* 0x2c */
    uint32_t xfer_func;        /* 0x30 */
#endif
#if defined(PLATFORM_T21) || defined(PLATFORM_T30)
    uint32_t crop_enable;      /* 0x24 */
    uint32_t crop_top;         /* 0x28 */
    uint32_t crop_left;        /* 0x2c */
    uint32_t crop_width;       /* 0x30 */
    uint32_t crop_height;      /* 0x34 */
    uint32_t scaler_enable;    /* 0x38 */
    uint32_t scaler_outwidth;  /* 0x3c */
    uint32_t scaler_outheight; /* 0x40 */
    uint32_t rate_bits;        /* 0x44 */
    uint32_t rate_mask;        /* 0x48 */
#else
    uint32_t crop_enable;      /* 0x34 */
    uint32_t crop_top;         /* 0x38 */
    uint32_t crop_left;        /* 0x3c */
    uint32_t crop_width;       /* 0x40 */
    uint32_t crop_height;      /* 0x44 */
    uint32_t scaler_enable;    /* 0x48 */
    uint32_t scaler_outwidth;  /* 0x4c */
    uint32_t scaler_outheight; /* 0x50 */
    uint32_t rate_bits;        /* 0x54 */
    uint32_t rate_mask;        /* 0x58 */
    uint32_t fcrop_enable;     /* 0x5c */
    uint32_t fcrop_top;        /* 0x60 */
    uint32_t fcrop_left;       /* 0x64 */
    uint32_t fcrop_width;      /* 0x68 */
    uint32_t fcrop_height;     /* 0x6c */
#endif
};

#if defined(PLATFORM_T20)
struct t20_v4l2_pix_format {
    uint32_t width;
    uint32_t height;
    uint32_t pixelformat;
    uint32_t field;
    uint32_t bytesperline;
    uint32_t sizeimage;
    uint32_t colorspace;
    uint32_t priv;
};

struct t20_v4l2_format {
    uint32_t type;
    union {
        struct t20_v4l2_pix_format pix;
        uint8_t raw_data[200];
    } fmt;
};

struct t20_v4l2_crop {
    uint32_t type;
    int32_t left;
    int32_t top;
    int32_t width;
    int32_t height;
};

struct t20_v4l2_cropcap {
    uint32_t type;
    struct { int32_t left, top, width, height; } bounds;
    struct { int32_t left, top, width, height; } defrect;
    struct { uint32_t numerator, denominator; } pixelaspect;
};

struct t20_scalercap {
    uint16_t max_width;
    uint16_t max_height;
    uint16_t min_width;
    uint16_t min_height;
};

struct t20_scaler {
    uint16_t out_width;
    uint16_t out_height;
};

_Static_assert(sizeof(struct t20_v4l2_format) == 0xcc,
               "T20 v4l2_format size");
_Static_assert(sizeof(struct t20_v4l2_crop) == 0x14,
               "T20 v4l2_crop size");
_Static_assert(sizeof(struct t20_v4l2_cropcap) == 0x2c,
               "T20 v4l2_cropcap size");
#endif

#if defined(PLATFORM_T21) || defined(PLATFORM_T30)
_Static_assert(sizeof(struct fs_ioctl_format70) == 0x4c,
               "legacy fs ioctl format size");
#else
_Static_assert(sizeof(struct fs_ioctl_format70) == 0x70,
               "fs ioctl format size");
#endif

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
#if defined(PLATFORM_T20)
    snprintf(devname, sizeof(devname), "/dev/video%d", chn + 1);
#else
    snprintf(devname, sizeof(devname), "/dev/framechan%d", chn);
#endif

    /* Try to open with retries (from decompilation: 0x101 retries) */
    for (int i = 0; i < 257; i++) {
        int fd = open(devname,
#if defined(PLATFORM_T20)
                      O_RDWR | O_CLOEXEC,
#else
                      O_RDWR | O_NONBLOCK | O_CLOEXEC,
#endif
                      0);
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

#if defined(PLATFORM_T20)
    struct t20_v4l2_format g = {0};

    g.type = 1;
    if (ioctl(fd, VIDIOC_GET_FMT, &g) < 0) {
        fprintf(stderr, "[KernelIF] T20 VIDIOC_G_FMT failed: %s\n", strerror(errno));
        return -1;
    }
    fmt->type = (int)g.type;
    fmt->width = (int)g.fmt.pix.width;
    fmt->height = (int)g.fmt.pix.height;
    fmt->pixelformat = (int)g.fmt.pix.pixelformat;
    fmt->field = (int)g.fmt.pix.field;
    fmt->bytesperline = (int)g.fmt.pix.bytesperline;
    fmt->sizeimage = (int)g.fmt.pix.sizeimage;
    fmt->colorspace = (int)g.fmt.pix.colorspace;
    fmt->priv = (int)g.fmt.pix.priv;
    return 0;
#else
    struct fs_ioctl_format70 g = {0};

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
    fmt->field = (int)g.field;
    fmt->bytesperline = (int)g.bytesperline;
    fmt->sizeimage = (int)g.sizeimage;
    fmt->colorspace = (int)g.colorspace;
    fmt->priv = (int)g.priv;
    fmt->crop_enable = (int)g.crop_enable;
    fmt->crop_x = (int)g.crop_left;
    fmt->crop_y = (int)g.crop_top;
    fmt->crop_width = (int)g.crop_width;
    fmt->crop_height = (int)g.crop_height;
    fmt->scaler_enable = (int)g.scaler_enable;
    fmt->scaler_outwidth = (int)g.scaler_outwidth;
    fmt->scaler_outheight = (int)g.scaler_outheight;

    fprintf(stderr, "[KernelIF] Got format: %dx%d fmt=0x%x sizeimage=%d bytesperline=%d\n",
            fmt->width, fmt->height, fmt->pixelformat, fmt->sizeimage, fmt->bytesperline);
    return 0;
#endif
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

#if defined(PLATFORM_T20)
    struct t20_v4l2_crop crop = {
        .type = 1,
        .left = fmt->crop_enable ? fmt->crop_x : 0,
        .top = fmt->crop_enable ? fmt->crop_y : 0,
        .width = fmt->crop_enable ? fmt->crop_width : 0,
        .height = fmt->crop_enable ? fmt->crop_height : 0,
    };
    struct t20_scalercap cap = {0};
    struct t20_scaler scaler = {
        .out_width = fmt->scaler_enable ? (uint16_t)fmt->scaler_outwidth : 0,
        .out_height = fmt->scaler_enable ? (uint16_t)fmt->scaler_outheight : 0,
    };
    struct t20_v4l2_format s = {0};
    uint32_t fourcc = (fmt->pixelformat < 0x100) ? pixfmt_to_fourcc(fmt->pixelformat)
                                                 : (uint32_t)fmt->pixelformat;

    /* The T20 frame channel checks S_CROP against bounds that only
     * VIDIOC_CROPCAP fills in; until then they are 0x0 and any non-empty
     * crop is rejected with EINVAL.  The OEM T20 EnableChn therefore issues
     * CROPCAP first whenever crop is enabled and validates the rectangle
     * against the returned bounds before S_CROP. */
    if (fmt->crop_enable) {
        struct t20_v4l2_cropcap cc;

        memset(&cc, 0, sizeof(cc));
        cc.type = 1;
        if (ioctl(fd, VIDIOC_CROPCAP, &cc) < 0) {
            fprintf(stderr, "[KernelIF] T20 VIDIOC_CROPCAP failed: %s\n", strerror(errno));
            return -1;
        }
        if (crop.left < cc.bounds.left || crop.top < cc.bounds.top ||
            crop.left + crop.width > cc.bounds.left + cc.bounds.width ||
            crop.top + crop.height > cc.bounds.top + cc.bounds.height) {
            fprintf(stderr,
                    "[KernelIF] T20 crop %dx%d+%d+%d exceeds bounds %dx%d+%d+%d\n",
                    crop.width, crop.height, crop.left, crop.top,
                    cc.bounds.width, cc.bounds.height,
                    cc.bounds.left, cc.bounds.top);
            return -1;
        }
    }
    if (ioctl(fd, VIDIOC_SET_CROP, &crop) < 0) {
        fprintf(stderr, "[KernelIF] T20 VIDIOC_S_CROP failed: %s\n", strerror(errno));
        return -1;
    }
    /* The driver answers GET_SCALERCAP with EPERM for a channel without a
     * usable scaler (ISP bypass, YUV sensors, an FR channel) and would then
     * reject SET_SCALER.  The OEM T20 EnableChn skips SET_SCALER when
     * GET_SCALERCAP fails instead of failing the whole enable. */
    if (ioctl(fd, VIDIOC_GET_SCALERCAP, &cap) < 0) {
        if (fmt->scaler_enable) {
            fprintf(stderr, "[KernelIF] T20 GET_SCALERCAP failed with scaler requested: %s\n",
                    strerror(errno));
            return -1;
        }
        memset(&cap, 0, sizeof(cap));
    } else if (ioctl(fd, VIDIOC_SET_SCALER, &scaler) < 0) {
        fprintf(stderr, "[KernelIF] T20 SET_SCALER failed: %s\n", strerror(errno));
        return -1;
    }

    s.type = 1;
    s.fmt.pix.width = (uint32_t)fmt->width;
    s.fmt.pix.height = (uint32_t)fmt->height;
    s.fmt.pix.pixelformat = fourcc;
    s.fmt.pix.field = 0;
    if (ioctl(fd, VIDIOC_TRY_FMT, &s) < 0) {
        fprintf(stderr, "[KernelIF] T20 VIDIOC_TRY_FMT failed: %s\n", strerror(errno));
        return -1;
    }
    if (ioctl(fd, VIDIOC_SET_FMT, &s) < 0) {
        fprintf(stderr, "[KernelIF] T20 VIDIOC_S_FMT failed: %s\n", strerror(errno));
        return -1;
    }

    fmt->width = (int)s.fmt.pix.width;
    fmt->height = (int)s.fmt.pix.height;
    fmt->field = (int)s.fmt.pix.field;
    fmt->bytesperline = (int)s.fmt.pix.bytesperline;
    fmt->sizeimage = (int)s.fmt.pix.sizeimage;
    fmt->colorspace = (int)s.fmt.pix.colorspace;
    fmt->priv = (int)s.fmt.pix.priv;
    if (fmt->bytesperline <= 0)
        fmt->bytesperline = fmt->width;
    /* S_FMT returns the queue's authoritative allocation size.  NV12/NV21
     * include the chroma plane: 0x2fd000 for 1080p and 0x56400 for 360p.
     * Retain a format-aware fallback for kernels that leave sizeimage zero. */
    if (fmt->sizeimage <= 0) {
        int aligned_height = (fmt->height + 15) & ~15;
        int luma_size = fmt->bytesperline * aligned_height;

        if (fourcc == 0x3231564e || fourcc == 0x3132564e)
            fmt->sizeimage = luma_size * 3 / 2;
        else
            fmt->sizeimage = luma_size;
    }
    fprintf(stderr,
            "[KernelIF] T20 format %dx%d fourcc=0x%x bpl=%d queue-size=%d cap=%ux%u..%ux%u\n",
            fmt->width, fmt->height, fourcc, fmt->bytesperline, fmt->sizeimage,
            cap.min_width, cap.min_height, cap.max_width, cap.max_height);
    return 0;
#else
    struct fs_ioctl_format70 s = {0};
    /* Header */
    s.type = 1; /* V4L2_BUF_TYPE_VIDEO_CAPTURE */
    s.width = (uint32_t)fmt->width;
    s.height = (uint32_t)fmt->height;

    /* Convert enum pixfmt to fourcc if needed */
    uint32_t fourcc = (fmt->pixelformat < 0x100) ? pixfmt_to_fourcc(fmt->pixelformat)
                                                 : (uint32_t)fmt->pixelformat;
    s.pixelformat = fourcc;
    s.field = 0;         /* V4L2_FIELD_NONE */

    /* Stock leaves bytesperline/sizeimage for the driver to populate. */
    s.bytesperline = 0;
    s.sizeimage = 0;
    s.colorspace = 8;    /* V4L2_COLORSPACE_SRGB */
    s.priv = 0;

    /*
     * The 0x70-byte ABI contains the full 12-word tisp_pix_format, followed
     * by crop/scaler/rate/fcrop fields.  The prior nine-word pix header
     * shifted scaler_enable by 16 bytes, so the driver advertised the scaled
     * size while its DMA engine continued writing full-sensor frames.
     */
    s.crop_enable = (uint32_t)fmt->crop_enable;
    s.crop_top = (uint32_t)fmt->crop_y;
    s.crop_left = (uint32_t)fmt->crop_x;
    s.crop_width = (uint32_t)fmt->crop_width;
    s.crop_height = (uint32_t)fmt->crop_height;
    s.scaler_enable = (uint32_t)fmt->scaler_enable;
    s.scaler_outwidth = (uint32_t)fmt->scaler_outwidth;
    s.scaler_outheight = (uint32_t)fmt->scaler_outheight;
    /* These are mscaler loop/mask fields, not the public fps fraction. */
    s.rate_bits = 0;
    s.rate_mask = 1;

    /* Debug: dump structure before ioctl */
    OPENIMP_TRACE_STDERR("[KernelIF] SET_FMT before ioctl:\n");
    OPENIMP_TRACE_STDERR("[KernelIF]   width=%u height=%u pixelformat=0x%x\n",
            s.width, s.height, s.pixelformat);
    OPENIMP_TRACE_STDERR("[KernelIF]   bytesperline=%u sizeimage=%u colorspace=%u\n",
            s.bytesperline, s.sizeimage, s.colorspace);
    OPENIMP_TRACE_STDERR("[KernelIF]   attr enable=%d attr=%dx%d pic=%dx%d fps=%d/%d\n",
            fmt->enable, fmt->attr_width, fmt->attr_height,
            fmt->picwidth, fmt->picheight, fmt->fps_num, fmt->fps_den);
    OPENIMP_TRACE_STDERR("[KernelIF]   crop_enable=%d crop=%dx%d+%d+%d\n",
            fmt->crop_enable, fmt->crop_width, fmt->crop_height, fmt->crop_x, fmt->crop_y);
    OPENIMP_TRACE_STDERR("[KernelIF]   scaler_enable=%d scaler_outwidth=%d scaler_outheight=%d\n",
            fmt->scaler_enable, fmt->scaler_outwidth, fmt->scaler_outheight);

    int ret = ioctl(fd, VIDIOC_SET_FMT, &s);
    if (ret < 0) {
        fprintf(stderr, "[KernelIF] VIDIOC_SET_FMT failed: %s\n", strerror(errno));
        fprintf(stderr, "[KernelIF]   Requested: %dx%d fmt=0x%x (fourcc=0x%x) colorspace=%d\n",
                fmt->width, fmt->height, fmt->pixelformat, fourcc, s.colorspace);
        return -1;
    }

    /* Debug: dump structure after ioctl */
    OPENIMP_TRACE_STDERR("[KernelIF] SET_FMT after ioctl:\n");
    OPENIMP_TRACE_STDERR("[KernelIF]   width=%u height=%u sizeimage=%u bytesperline=%u\n",
            s.width, s.height, s.sizeimage, s.bytesperline);
    OPENIMP_TRACE_STDERR(
            "[KernelIF]   crop=%u %ux%u+%u+%u scaler=%u %ux%u rate=%u/0x%x\n",
            s.crop_enable, s.crop_width, s.crop_height, s.crop_left, s.crop_top,
            s.scaler_enable, s.scaler_outwidth, s.scaler_outheight,
            s.rate_bits, s.rate_mask);

    /* CRITICAL: The kernel modifies the structure in-place during SET_FMT and copies it back.
     * The remote ISP core may update sizeimage, bytesperline, and other fields.
     * We MUST read these modified values from 's' after the ioctl returns.
     * DO NOT call GET_FMT here - it queries the remote ISP again and may return garbage.
     */
    fprintf(stderr, "[KernelIF] Set format: %dx%d fmt=0x%x (fourcc=0x%x) sizeimage=%u bytesperline=%u colorspace=%d\n",
            fmt->width, fmt->height, fmt->pixelformat, fourcc,
            s.sizeimage, s.bytesperline, s.colorspace);

    /* Update fmt with kernel-modified values */
    fmt->width = (int)s.width;
    fmt->height = (int)s.height;
    fmt->sizeimage = (int)s.sizeimage;
    fmt->bytesperline = (int)s.bytesperline;
    fmt->field = (int)s.field;
    fmt->colorspace = (int)s.colorspace;
    fmt->priv = (int)s.priv;
    fmt->crop_enable = (int)s.crop_enable;
    fmt->crop_x = (int)s.crop_left;
    fmt->crop_y = (int)s.crop_top;
    fmt->crop_width = (int)s.crop_width;
    fmt->crop_height = (int)s.crop_height;
    fmt->scaler_enable = (int)s.scaler_enable;
    fmt->scaler_outwidth = (int)s.scaler_outwidth;
    fmt->scaler_outheight = (int)s.scaler_outheight;

    if (fmt->bytesperline <= 0)
        fmt->bytesperline = (int)fmt->width;
    /* The VBM/QBUF path uses fmt->sizeimage directly as the queued USERPTR
     * length. On T31, replacing the driver-returned aligned size with the
     * visible WxH payload size makes VIDIOC_QBUF fail with EINVAL. */

    return 0;
#endif
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
    req.capabilities = 0;    /* OEM uses 0 — verified via BN MCP decompilation */
    req.reserved[0] = 0;

    int ret = ioctl(fd, VIDIOC_SET_BUFCNT, &req);
    if (ret < 0) {
        fprintf(stderr, "[KernelIF] VIDIOC_SET_BUFCNT failed: %s\n", strerror(errno));
        return -1;
    }

    fprintf(stderr, "[KernelIF] REQBUFS: requested=%d actual=%u\n", count, req.count);
    return (int)req.count;
}

/**
 * Set frame depth
 * ioctl: 0x800456c5
 *
 * OEM passes pointer to int (verified via BN MCP decompilation)
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
 *
 * OEM passes pointer to int (verified via BN MCP decompilation)
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
 *
 * OEM passes pointer to int (verified via BN MCP decompilation)
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

#if defined(PLATFORM_T31)
/* STREAMOFF for the process exit paths: only the ioctl, no stdio, so it
 * can run from a signal handler. Errors ("not streaming") are ignored. */
int fs_stream_off_quiet(int fd) {
    int enable = 1;

    if (fd < 0) {
        return -1;
    }
    return ioctl(fd, VIDIOC_STREAM_OFF, &enable);
}
#endif

/*
 * VIDIOC_POLL_FRAME (0x400456bf) is the frame-ready wait of the stock libimp
 * FS(N)-tick thread (frame_pooling_thread at 0x99acc): it blocks in this
 * ioctl, then calls the group update that DQBUFs the frame.  In the driver
 * it is wait_for_completion_interruptible(&chan->frame_done) (OEM +0x2d4);
 * frame_chan_event() complete()s it once per finished frame, STREAMOFF and
 * release complete_all() it.  *ready_out receives the ready-frame count
 * (open tx-isp: frame_ready_count, at least 1) or the negative error.
 *
 * OpenIMP does not need it: the FrameSource worker waits in select() and
 * drains with DQBUF (frame_pooling_thread in framesource_tseries.c), and
 * nothing calls fs_poll_frame() at the moment.  It is kept, without the
 * helper thread it used to start per call (that thread only added a 10 ms
 * trace tick; the caller always waited for it with no timeout, so it bought
 * nothing and cost a pthread_create per frame), for a caller that wants the
 * OEM wait, e.g. a stock-compatible tick loop:
 *
 * - The fd is O_NONBLOCK (fs_open_device).  A driver that honours that for
 *   this ioctl (open tx-isp, claude/t31-isp-last-close, pending) returns
 *   EAGAIN when no frame completed; then this waits in poll() for up to
 *   timeout_ms (-1 = forever) and asks again.  POLLIN with the completion
 *   already taken still means a frame can be dequeued, reported as ready=1.
 *   Logged once when first seen.
 * - The stock driver and today's open driver ignore O_NONBLOCK here and
 *   block in the ioctl until a frame or STREAMOFF, exactly as before;
 *   timeout_ms has no effect then.
 *
 * Returns 0 (frame ready), -1 error or stream stopped (POLLERR), -2 EINTR,
 * -3 timeout.
 */
static int fs_poll_frame_nonblock_seen;

int fs_poll_frame(int fd, unsigned int *ready_out, int timeout_ms)
{
    uint32_t ready = 0xffffffffu;
    struct pollfd pfd;
    int ret;
    int err;

    if (fd < 0)
        return -1;

    ret = ioctl(fd, VIDIOC_POLL_FRAME, &ready);
    err = errno;
    if (ret < 0 && err == EAGAIN) {
        if (!__atomic_exchange_n(&fs_poll_frame_nonblock_seen, 1,
                                 __ATOMIC_RELAXED))
            fprintf(stderr, "[KernelIF] framechan POLL_FRAME (0x400456bf) "
                    "honours O_NONBLOCK: waiting in poll()\n");
        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        ret = poll(&pfd, 1, timeout_ms);
        if (ret == 0)
            return -3;
        if (ret < 0)
            return errno == EINTR ? -2 : -1;
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
            return -1; /* not streaming (any more) */
        ready = 0xffffffffu;
        ret = ioctl(fd, VIDIOC_POLL_FRAME, &ready);
        err = errno;
        if (ret < 0 && err == EAGAIN) {
            ready = 1u;
            ret = 0;
        }
    }
    if (ret < 0) {
        if (err == EINTR)
            return -2;
        fprintf(stderr, "[KernelIF] POLL_FRAME failed: fd=%d %s\n", fd,
                strerror(err));
        return -1;
    }
    if (ready_out)
        *ready_out = ready;
    ki_trace("libimp/KI: POLL_FRAME fd=%d ready=%u\n", fd, ready);
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
    size_t buf_sz = sizeof(struct v4l2_buf32) + 0x400; /* tolerate driver over-copy */
    void *raw = NULL;
    if (posix_memalign(&raw, 16, buf_sz) != 0 || !raw) {
        return -1;
    }
    memset(raw, 0, buf_sz);
    struct v4l2_buf32 *b = (struct v4l2_buf32 *)raw;
    b->index = (uint32_t)index;
    b->type = 1; /* V4L2_BUF_TYPE_VIDEO_CAPTURE */
    b->memory = 2; /* USERPTR */
    b->field = 0;  /* OEM uses 0 (from memset) */
    int ret = ioctl(fd, VIDIOC_QUERYBUF, b);
    if (ret < 0) {
        fprintf(stderr, "[KernelIF] QUERYBUF failed: idx=%d err=%s\n", index, strerror(errno));
        free(raw);
        return -1;
    }
    if (length_out) *length_out = b->length;
    free(raw);
    return 0;
}

/* Queue a userspace buffer to the framechan driver
 * Driver expects a 0x44-byte v4l2_buffer-like struct (32-bit layout).
 */
int fs_qbuf(int fd, int index, unsigned long phys, unsigned int length) {
    if (fd < 0 || index < 0) return -1;

    size_t buf_sz = sizeof(struct v4l2_buf32) + 0x400; /* large slack to tolerate driver over-copy */
    void *raw = NULL;
    if (posix_memalign(&raw, 16, buf_sz) != 0 || !raw) {
        return -1;
    }
    memset(raw, 0, buf_sz);
    struct v4l2_buf32 *b = (struct v4l2_buf32 *)raw;
    b->index = (uint32_t)index;
    b->type = 1;            /* V4L2_BUF_TYPE_VIDEO_CAPTURE */
    b->memory = 2;          /* Must match REQBUFS memory type (USERPTR) */
    b->field = 0;           /* OEM uses 0 (from memset); field=4 broke PHY_CHANNEL DQBUF */
    b->flags = 0;
    b->sequence = 0;
    b->m = (uint32_t)phys;  /* USERPTR carries DMA phys on this T31 variant */
    b->length = length;     /* Must equal kernel expected length */
    b->bytesused = length;  /* Stock helper mirrors payload length here */

    int ret = ioctl(fd, VIDIOC_QBUF, b);
    if (ret < 0) {
        fprintf(stderr, "[KernelIF] QBUF failed: idx=%d phys=0x%lx len=%u err=%s\n",
                index, phys, length, strerror(errno));
        ki_trace("libimp/KI: QBUF fail fd=%d idx=%d phys=0x%lx len=%u err=%d\n",
                 fd, index, phys, length, errno);
        free(raw);
        return -1;
    }
    {
        static int qbuf_log_count = 0;
        if (qbuf_log_count < 6) {
            OPENIMP_TRACE_STDERR("[KernelIF] QBUF OK: fd=%d idx=%d phys=0x%lx len=%u\n",
                    fd, index, phys, length);
            ki_trace("libimp/KI: QBUF ok fd=%d idx=%d phys=0x%lx len=%u bytesused=%u\n",
                     fd, index, phys, length, b->bytesused);
            qbuf_log_count++;
        }
    }
    free(raw);
    return 0;
}
/*
 * DQBUF O_NONBLOCK detection (see kernel_interface.h).  The fd is opened
 * O_NONBLOCK, so a driver that honours the flag answers an empty queue with
 * EAGAIN at once; the FrameSource worker drains until that happens, so the
 * open driver gives the first EAGAIN within the first frame period.  The
 * stock driver never returns EAGAIN: it sleeps in DQBUF until the next frame
 * (select() always reports the fd readable there), so each DQBUF after the
 * first frame blocks for most of a frame period.  Three DQBUFs that slept at
 * least FS_DQ_BLOCKED_US without any EAGAIN seen make it IGNORED.  An EAGAIN
 * seen later still switches to HONOURED (a long scheduling delay could fake
 * a blocking DQBUF, nothing fakes an EAGAIN).  Timing stops once decided.
 */
#define FS_DQ_BLOCKED_US 10000LL
#define FS_DQ_BLOCKED_HITS 3u

static int fs_dq_nonblock = FS_DQ_NONBLOCK_UNKNOWN;
static unsigned int fs_dq_blocked_hits;

int fs_dqbuf_nonblock_mode(void)
{
    return __atomic_load_n(&fs_dq_nonblock, __ATOMIC_RELAXED);
}

const char *fs_dqbuf_nonblock_name(int mode)
{
    switch (mode) {
    case FS_DQ_NONBLOCK_HONOURED:
        return "honours O_NONBLOCK";
    case FS_DQ_NONBLOCK_IGNORED:
        return "ignores O_NONBLOCK";
    default:
        return "O_NONBLOCK support not yet known";
    }
}

static void fs_dq_note_eagain(void)
{
    int old = __atomic_exchange_n(&fs_dq_nonblock, FS_DQ_NONBLOCK_HONOURED,
                                  __ATOMIC_RELAXED);

    if (old != FS_DQ_NONBLOCK_HONOURED)
        fprintf(stderr, "[KernelIF] framechan DQBUF honours O_NONBLOCK%s: "
                "capture waits in select(), DQBUF never sleeps\n",
                old == FS_DQ_NONBLOCK_IGNORED
                    ? " (EAGAIN after all; earlier slow DQBUFs were "
                      "scheduling delays)" : "");
}

static void fs_dq_note_slept(int fd, long long slept_us)
{
    int flags;
    int expected = FS_DQ_NONBLOCK_UNKNOWN;

    if (slept_us < FS_DQ_BLOCKED_US ||
        __sync_add_and_fetch(&fs_dq_blocked_hits, 1u) < FS_DQ_BLOCKED_HITS)
        return;
    /* Only meaningful if the fd really is non-blocking. */
    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || !(flags & O_NONBLOCK))
        return;
    if (__atomic_compare_exchange_n(&fs_dq_nonblock, &expected,
                                    FS_DQ_NONBLOCK_IGNORED, 0,
                                    __ATOMIC_RELAXED, __ATOMIC_RELAXED))
        fprintf(stderr, "[KernelIF] framechan DQBUF ignores O_NONBLOCK "
                "(%u DQBUFs slept, last %lld ms, no EAGAIN): capture waits "
                "in DQBUF, only STREAMOFF wakes it\n",
                FS_DQ_BLOCKED_HITS, slept_us / 1000LL);
}

/* Dequeue a filled buffer from the framechan driver */
int fs_dqbuf(int fd, int *index_out, uint64_t *timestamp_out) {
    void *raw = NULL;
    struct v4l2_buf32 *b;
    int ret;
    int saved_errno;
    int nonblock_mode;
    long long dq_start_us = 0;
    size_t buf_sz;

    if (fd < 0 || !index_out) return -1;

    buf_sz = sizeof(struct v4l2_buf32) + 0x400;
    if (posix_memalign(&raw, 16, buf_sz) != 0 || raw == NULL)
        return -1;
    memset(raw, 0, buf_sz);
    b = (struct v4l2_buf32 *)raw;
    b->type   = 1;
    b->memory = 2;
    b->field  = 0;

    {
        static int dqbuf_log_count = 0;
        if (dqbuf_log_count < 6) {
            OPENIMP_TRACE_STDERR("[KernelIF] DQBUF: fd=%d attempting...\n", fd);
            dqbuf_log_count++;
        }
    }
    ki_trace("libimp/KI: DQBUF enter fd=%d raw=%p\n", fd, raw);

    ki_trace("libimp/KI: DQBUF ioctl-call fd=%d\n", fd);
    nonblock_mode = fs_dqbuf_nonblock_mode();
    if (nonblock_mode == FS_DQ_NONBLOCK_UNKNOWN)
        dq_start_us = ki_mono_us();
    ret = ioctl(fd, VIDIOC_DQBUF, raw);
    saved_errno = errno;
    if (ret < 0 && saved_errno == EAGAIN) {
        if (nonblock_mode != FS_DQ_NONBLOCK_HONOURED)
            fs_dq_note_eagain();
    } else if (ret == 0 && nonblock_mode == FS_DQ_NONBLOCK_UNKNOWN) {
        fs_dq_note_slept(fd, ki_mono_us() - dq_start_us);
    }
    ki_trace("libimp/KI: DQBUF ioctl-ret fd=%d ret=%d errno=%d\n", fd, ret, saved_errno);

    if (ret < 0) {
        ki_trace("libimp/KI: DQBUF exit fd=%d ret=%d idx=-1 errno=%d\n",
                 fd, ret, saved_errno);
        if (saved_errno == EAGAIN || saved_errno == EINTR) {
            free(raw);
            return -2;
        }
        /* A DQBUF waiting in the driver when DisableChn stops the stream
         * ends with EINVAL (vb2: T20/T21/T30) or EPIPE (open tx-isp T23):
         * the normal end of a stop. The worker reports it when it happens
         * while the channel is meant to run. */
        if (saved_errno == EINVAL || saved_errno == EPIPE) {
            free(raw);
            return -3;
        }
        fprintf(stderr, "[KernelIF] DQBUF failed: fd=%d %s\n", fd, strerror(saved_errno));
        free(raw);
        return -1;
    }

    *index_out = (int)b->index;
    if (timestamp_out != NULL) {
        if (b->ts_usec < 1000000u)
            *timestamp_out = (uint64_t)b->ts_sec * 1000000u + b->ts_usec;
        else
            *timestamp_out = 0;
    }
    ki_trace("libimp/KI: DQBUF exit fd=%d ret=%d idx=%d errno=%d\n",
             fd, ret, *index_out, saved_errno);
    {
        static int dqbuf_ok_log = 0;
        if (dqbuf_ok_log < 6) {
            OPENIMP_TRACE_STDERR(
                    "[KernelIF] DQBUF: fd=%d OK idx=%d ts=%u.%06u seq=%u\n",
                    fd, *index_out, b->ts_sec, b->ts_usec, b->sequence);
            dqbuf_ok_log++;
        }
    }
    free(raw);
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
#if defined(PLATFORM_T23)
    uint32_t direct_phys_addr; /* 0x20: T23 direct physical address */
    uint32_t _timestamp_pad;   /* 0x24: o32 alignment */
    int64_t time_stamp;        /* 0x28: Public capture timestamp */
    int64_t time_stamp_ivdc;   /* 0x30: IVDC dequeue timestamp */
    uint8_t data[0x3f0];       /* 0x38-0x427: Private frame data */
#else
    uint8_t data[0x408];    /* 0x20-0x427: Frame data */
#endif
} VBMFrame;

#if defined(PLATFORM_T23)
_Static_assert(offsetof(VBMFrame, time_stamp) == 0x28,
               "T23 frame timestamp offset must remain 0x28");
#else
_Static_assert(offsetof(VBMFrame, data) == 0x20,
               "T31 frame timestamp offset must remain 0x20");
#endif
_Static_assert(sizeof(VBMFrame) == VBM_FRAME_SIZE,
               "VBM public/private frame record size mismatch");

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
    uint8_t *buf_in_userspace; /* Per-buffer: 1 if DQBUF'd, 0 if in kernel queue */
} VBMPool;

/* Global VBM state */
typedef struct {
    VBMFrame *frame;        /* 0x00: Frame pointer */
    uint32_t phys_addr;     /* 0x04: Physical address */
    uint32_t virt_addr;     /* 0x08: Virtual address */
    int ref_count;          /* 0x0c: Reference count */
} VBMVolume;

/* The frame array follows the pool header at the stock offset 0x180, or
 * after the whole header where it is larger (64-bit host builds). */
#define VBM_POOL_HEADER_SIZE \
    (sizeof(VBMPool) > 0x180 ? (sizeof(VBMPool) + 15u) & ~(size_t)15u \
                             : (size_t)0x180)

static VBMPool *vbm_instance[MAX_VBM_POOLS] = {NULL};
static VBMVolume g_framevolumes[30]; /* Global frame volumes array */

/*
 * Pool lifetime. vbm_instance[chn] is published, freed, and used by the
 * calls an application or encoder thread makes (GetFrame, ReleaseFrame,
 * Fill/FlushFrame) under vbm_pool_lock[chn], so DestroyPool can never free
 * a pool one of them is using. The capture worker's own calls
 * (KernelDequeue, RecycleIdleFrames) do not take it: DisableChn joins the
 * worker before it destroys the pool, and a DQBUF must not hold up the
 * readers. Lock order: vbm_pool_lock, then the pool's queue_mutex.
 */
static pthread_mutex_t vbm_pool_lock[MAX_VBM_POOLS] = {
    [0 ... MAX_VBM_POOLS - 1] = PTHREAD_MUTEX_INITIALIZER
};
/* The last destroyed pool of each channel, kept allocated (header and
 * frame records only; its buffers are freed). A consumer that still holds
 * a frame from it releases a pointer into this block: as long as the block
 * is not freed, the channel's next pool cannot be allocated at the same
 * address, so the stale pointer never passes for one of its frames. */
static VBMPool *vbm_retired[MAX_VBM_POOLS];

/* rmem bytes of each FrameSource pool taken from the arena (0: none) */
static size_t vbm_rmem_bytes[MAX_VBM_POOLS];

/*
 * Once per process, before a FrameSource pool is taken from rmem: warn when
 * it no longer fits next to what is already allocated - the other pools and
 * the fixed buffers (encoder, ISP, OSD, ...) - so an overcommitted
 * configuration (resolutions x nrVBs too large for the rmem= reservation)
 * is visible at start-up with its numbers, before the allocation failures or
 * skipped pictures it causes.  Diagnosis only: nothing is changed.
 */
static void vbm_check_rmem_budget(int chn, int need)
{
    static int warned;
    size_t used, total, largest, pools = 0;
    int i, count = 0;

    if (need <= 0 || __atomic_load_n(&warned, __ATOMIC_RELAXED) ||
        DMA_RmemStats(&used, &total, &largest) != 0 || !total)
        return;
    if ((size_t)need <= total - (used < total ? used : total) &&
        (size_t)need <= largest)
        return;
    if (__atomic_exchange_n(&warned, 1, __ATOMIC_RELAXED))
        return;
    for (i = 0; i < MAX_VBM_POOLS; i++) {
        size_t bytes = __atomic_load_n(&vbm_rmem_bytes[i], __ATOMIC_RELAXED);

        if (i != chn && bytes) {
            pools += bytes;
            count++;
        }
    }
    IMP_LOG_WARN("DMA", "rmem budget exceeded: FrameSource pool vbm_chn%d "
                 "needs %d bytes, but rmem has %zu of %zu bytes free "
                 "(largest free block %zu); in use: %d other pool(s) %zu "
                 "bytes, fixed buffers %zu bytes. Lower the resolutions or "
                 "nrVBs, or enlarge rmem: allocations will fail or pictures "
                 "be skipped",
                 chn, need, used < total ? total - used : 0u, total, largest,
                 count, pools, used > pools ? used - pools : 0u);
}

/* g_framevolumes: registration, lookup and reference counts */
static pthread_mutex_t vbm_volume_lock = PTHREAD_MUTEX_INITIALIZER;

/* Index of frame in pool's frame array, or -1 when it is not one of its
 * records (a frame of a pool destroyed since, or of another channel). Only
 * compares addresses: such a pointer may already be freed. */
static int vbm_frame_index(const VBMPool *pool, const void *frame)
{
    uintptr_t base = (uintptr_t)pool->frames;
    uintptr_t addr = (uintptr_t)frame;
    uintptr_t index;

    if (addr < base || (addr - base) % sizeof(VBMFrame) != 0)
        return -1;
    index = (addr - base) / sizeof(VBMFrame);
    return index < (uintptr_t)pool->frame_count ? (int)index : -1;
}

static void vbm_unregister_volumes(int chn)
{
    pthread_mutex_lock(&vbm_volume_lock);
    for (int i = 0; i < 30; i++) {
        if (g_framevolumes[i].frame != NULL &&
            g_framevolumes[i].frame->chn == chn)
            memset(&g_framevolumes[i], 0, sizeof(g_framevolumes[i]));
    }
    pthread_mutex_unlock(&vbm_volume_lock);
}

static VBMVolume *vbm_find_volume_by_vaddr(uint32_t vaddr)
{
    for (int i = 0; i < 30; ++i) {
        if (g_framevolumes[i].frame != NULL && g_framevolumes[i].virt_addr == vaddr) {
            return &g_framevolumes[i];
        }
    }
    return NULL;
}

/* External functions */
extern int IMP_FrameSource_GetPool(int chn);

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

static int vbm_create_pool(int chn, void *fmt, void *ops, void *priv);
static void vbm_log_rmem(const char *what, int chn, int size);

int VBMCreatePool(int chn, void *fmt, void *ops, void *priv) {
    int ret;

    if (chn < 0 || chn >= MAX_VBM_POOLS) {
        return -1;
    }
    pthread_mutex_lock(&vbm_pool_lock[chn]);
    ret = vbm_create_pool(chn, fmt, ops, priv);
    pthread_mutex_unlock(&vbm_pool_lock[chn]);
    return ret;
}

static int vbm_create_pool(int chn, void *fmt, void *ops, void *priv) {

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

    /* Allocate pool structure with proper alignment for MIPS */
    size_t pool_size = frame_count * VBM_FRAME_SIZE + VBM_POOL_HEADER_SIZE;
    OPENIMP_TRACE_STDERR("[VBM] CreatePool: allocating pool_size=%zu (frame_count=%d * 0x%x + 0x%zx)\n",
            pool_size, frame_count, VBM_FRAME_SIZE, VBM_POOL_HEADER_SIZE);

    VBMPool *pool = NULL;
    /* Use posix_memalign to ensure 16-byte alignment for MIPS */
    if (posix_memalign((void**)&pool, 16, pool_size) != 0 || pool == NULL) {
        fprintf(stderr, "[VBM] CreatePool: posix_memalign failed (size=%zu): %s\n",
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
    int fps_num, fps_den;
    uint32_t frame_fourcc;
    memcpy(&width, fmt_bytes + 0x0, sizeof(int));
    memcpy(&height, fmt_bytes + 0x4, sizeof(int));
    memcpy(&pixfmt, fmt_bytes + 0x8, sizeof(int));
    memcpy(&req_size, fmt_bytes + 0xc, sizeof(int));
    memcpy(&fps_num, fmt_bytes + 0x2c, sizeof(int));
    memcpy(&fps_den, fmt_bytes + 0x30, sizeof(int));
    frame_fourcc = (pixfmt < 0x100) ? pixfmt_to_fourcc(pixfmt) : (uint32_t)pixfmt;
    if (fps_num <= 0) fps_num = 1;
    if (fps_den <= 0) fps_den = 1;

    /* Calculate frame size and honor requested size from SET_FMT (kernel sizeimage) */
    int calc_size = calculate_frame_size(width, height, pixfmt);
    int raw_size = (req_size > 0) ? req_size : calc_size;

    /* Align frame size to 32-byte boundary for DMA (MIPS cache line alignment) */
    pool->frame_size = (raw_size + 31) & ~31;

    fprintf(stderr, "[VBM] CreatePool: chn=%d, %dx%d fmt=0x%x fourcc=0x%x, fps=%d/%d, %d frames, size=%d (req=%d calc=%d aligned=%d)\n",
            chn, width, height, pixfmt, frame_fourcc, fps_num, fps_den,
            frame_count, pool->frame_size, req_size, calc_size, pool->frame_size);

    /* Try to get pool from FrameSource */
    pool->pool_id = IMP_FrameSource_GetPool(chn);

    /* Allocate memory for frames via DMA allocator */
    int total_size = pool->frame_size * frame_count;
    IMPDMABufferInfo alloc_info;
    memset(&alloc_info, 0, sizeof(alloc_info));
    int ret;

    if (pool->pool_id < 0) {
        vbm_check_rmem_budget(chn, total_size);
        ret = DMA_AllocDescriptor(&alloc_info, total_size, pool->name);
        if (ret >= 0)
            __atomic_store_n(&vbm_rmem_bytes[chn], (size_t)total_size,
                             __ATOMIC_RELAXED);
    } else {
        ret = DMA_PoolAllocDescriptor(pool->pool_id, &alloc_info, total_size, pool->name);
    }

    if (ret < 0) {
        fprintf(stderr, "[VBM] CreatePool: allocation failed\n");
        free(pool);
        return -1;
    }

    uint32_t phys_base = alloc_info.phys_addr;
    uint32_t virt_base = alloc_info.virt_addr;

    pool->phys_base = phys_base;
    pool->virt_base = virt_base;

    /* Initialize frames array pointer */
    uint8_t *pool_bytes = (uint8_t*)pool;
    pool->frames = (VBMFrame*)(pool_bytes + VBM_POOL_HEADER_SIZE);

    /* Initialize frame queue */
    pool->available_queue = (int*)calloc(frame_count, sizeof(int));
    /* Per-buffer kernel ownership tracking: prevents double-QBUF when
     * VBMReleaseFrame is called multiple times for the same buffer
     * (OEM uses AL_Buffer refcounting; we track explicitly). */
    pool->buf_in_userspace = (uint8_t*)calloc(frame_count, sizeof(uint8_t));
    if (pool->available_queue == NULL || pool->buf_in_userspace == NULL) {
        fprintf(stderr, "[VBM] CreatePool: failed to allocate queue\n");
        free(pool->available_queue);
        free(pool->buf_in_userspace);
        DMA_FreePhys(pool->phys_base);
        free(pool);
        return -1;
    }

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

        /* OEM codec path checks frameInfo.pixfmt against m_SrcFourCC, so the
         * public frame record must carry the source FOURCC here, not the IMP
         * enum from IMPFSChnAttr. */
        memcpy(frame_bytes + 0x10, &frame_fourcc, sizeof(uint32_t));

        /* Write size at offset 0x14 */
        int frame_size = pool->frame_size;
        memcpy(frame_bytes + 0x14, &frame_size, sizeof(int));

        /* Write phys_addr at offset 0x18 */
        uint32_t phys = pool->phys_base + (i * pool->frame_size);
        memcpy(frame_bytes + 0x18, &phys, sizeof(uint32_t));

        /* Write virt_addr at offset 0x1c */
        uint32_t virt = pool->virt_base + (i * pool->frame_size);
        memcpy(frame_bytes + 0x1c, &virt, sizeof(uint32_t));

#if defined(PLATFORM_T23)
        /* The 1.3.0 public descriptor exposes a direct address followed by
         * two aligned timestamps.  OpenTX uses the same physical DMA window
         * for direct and normal capture on T23. */
        memcpy(frame_bytes + 0x20, &phys, sizeof(uint32_t));
#else

        /* OEM encoder callback reads framePriv->i_fps_num/den from
         * words 0xb/0xc (byte offsets 0x2c/0x30). Seed those fields
         * directly in the public frame record so on_encoder_group_data_update
         * can enter sub_8eea0 instead of rejecting the frame. */
        memcpy(frame_bytes + 0x2c, &fps_num, sizeof(int));
        memcpy(frame_bytes + 0x30, &fps_den, sizeof(int));
#endif

        OPENIMP_TRACE_STDERR("[VBM] Frame %d: phys=0x%x virt=0x%x fourcc=0x%x fps=%d/%d\n",
                i, phys, virt, frame_fourcc, fps_num, fps_den);

        /* Register in global frame volumes */
        pthread_mutex_lock(&vbm_volume_lock);
        for (int j = 0; j < 30; j++) {
            if (g_framevolumes[j].frame == NULL) {
                g_framevolumes[j].frame = frame;
                g_framevolumes[j].phys_addr = frame->phys_addr;
                g_framevolumes[j].virt_addr = frame->virt_addr;
                g_framevolumes[j].ref_count = 0;
                break;
            }
        }
        pthread_mutex_unlock(&vbm_volume_lock);
    }

    pool->queue_head = 0;
    pool->queue_tail = 0;
    pool->queue_count = 0;
    pthread_mutex_init(&pool->queue_mutex, NULL);

    vbm_instance[chn] = pool;
    vbm_log_rmem("after creating", chn, total_size);

    OPENIMP_TRACE_STDERR("[VBM] CreatePool: chn=%d created successfully\n", chn);
    return 0;
}

/* One line per FrameSource pool creation/release with the state of the
 * reserved arena: the pools are the allocations that come and go with every
 * DisableChn/EnableChn, so this is where fragmentation would show first.
 * OPENIMP_RMEM_MAP=1 also logs every live allocation. */
static void vbm_log_rmem(const char *what, int chn, int size)
{
    static int map = -1;
    size_t used, total, largest;

    if (DMA_RmemStats(&used, &total, &largest) != 0)
        return;
    IMP_LOG_INFO("DMA", "rmem %s vbm_chn%d (%d bytes): used %zu of %zu, "
                 "largest free block %zu", what, chn, size, used, total,
                 largest);
    if (map < 0) {
        const char *value = getenv("OPENIMP_RMEM_MAP");

        map = value && value[0] == '1';
    }
    if (map)
        DMA_LogRmem(what);
}

int VBMDestroyPool(int chn) {
    if (chn < 0 || chn >= MAX_VBM_POOLS) {
        return -1;
    }

    pthread_mutex_lock(&vbm_pool_lock[chn]);
    VBMPool *pool = vbm_instance[chn];
    if (pool == NULL) {
        pthread_mutex_unlock(&vbm_pool_lock[chn]);
        return -1;
    }
    /* for the log line after the release (the pool is retired by then) */
    int pool_bytes = pool->frame_size * pool->frame_count;
    vbm_instance[chn] = NULL;

    fprintf(stderr, "[VBM] DestroyPool: chn=%d\n", chn);

    /* Unregister frames from global volumes */
    vbm_unregister_volumes(chn);

    /* Destroy queue mutex */
    pthread_mutex_destroy(&pool->queue_mutex);

    /* Free queue */
    if (pool->available_queue != NULL) {
        free(pool->available_queue);
    }
    if (pool->buf_in_userspace != NULL) {
        free(pool->buf_in_userspace);
    }

    /* Free allocated memory */
    if (pool->phys_base != 0) {
        DMA_FreePhys(pool->phys_base);
    }

    /* Retire the pool structure (see vbm_retired) */
    pool->available_queue = NULL;
    pool->buf_in_userspace = NULL;
    pool->phys_base = 0;
    free(vbm_retired[chn]);
    vbm_retired[chn] = pool;
    __atomic_store_n(&vbm_rmem_bytes[chn], 0, __ATOMIC_RELAXED);
    pthread_mutex_unlock(&vbm_pool_lock[chn]);

    vbm_log_rmem("after releasing", chn, pool_bytes);
    OPENIMP_TRACE_STDERR("[VBM] DestroyPool: chn=%d destroyed\n", chn);
    return 0;
}

/* Prime kernel queue with up to 'limit' VBM frames (if limit<=0, use all) */
int VBMPrimeKernelQueue(int chn, int fd, int limit) {
    if (chn < 0 || chn >= MAX_VBM_POOLS) return -1;
    VBMPool *pool = vbm_instance[chn];
    if (!pool) return -1;
    pool->fd = fd;

    int to_queue = pool->frame_count;
    if (limit > 0 && limit < to_queue) to_queue = limit;

    /* DO NOT call GET_FMT here - it returns garbage from the remote ISP core.
     * Instead, use QUERYBUF to get the kernel's expected length for each buffer,
     * and fall back to NV12 calculation if QUERYBUF returns 0.
     *
     * Pop indices from the available_queue when priming the kernel so they are
     * no longer considered 'available' by the pool. If a QBUF fails, put the
     * frame back into the available queue so VBMGetFrame can still use it (the
     * capture thread's software fallback depends on this).
     */
    /* The open-tx-isp frame-channel driver expects V4L2_MEMORY_USERPTR
     * buffers to carry a physical DMA address in buffer.m.userptr:
     * tx-isp-module.c QBUF stores `buffer_phys_addr = buffer.m.userptr`.
     * Keep the virtual-address mode only as an explicit experiment. */
    const char* force_phys_ev = getenv("OPENIMP_QBUF_FORCE_PHYS");
    const char* force_virt_ev = getenv("OPENIMP_QBUF_USE_VIRT");
    int use_virt = 0;
    if (force_virt_ev && force_virt_ev[0] != '\0' && force_virt_ev[0] != '0') {
        use_virt = 1;
    } else if (force_phys_ev && force_phys_ev[0] == '0') {
        use_virt = 1;
    }
    int queued_ok = 0;

    pthread_mutex_lock(&pool->queue_mutex);
    for (int j = 0; j < to_queue; j++) {
        if (pool->queue_count <= 0) {
            break;
        }
        int idx = pool->available_queue[pool->queue_head];
        pool->queue_head = (pool->queue_head + 1) % pool->frame_count;
        pool->queue_count--;

        VBMFrame *f = &pool->frames[idx];
        unsigned long addr_m = use_virt ? (unsigned long)f->virt_addr
                                        : (unsigned long)f->phys_addr;
        if (j == 0) {
            OPENIMP_TRACE_STDERR("[VBM] PrimeKernelQueue: QBUF using %s address in .m\n",
                    use_virt ? "VIRT" : "PHYS");
        }

        /* Use VBM frame size directly — do NOT call fs_querybuf (ioctl 0xc0445609)
         * because the custom tx-isp driver maps that ioctl to DQBUF, not QUERYBUF.
         * Calling it here corrupts the driver's buffer state. */
        unsigned int qlen = (unsigned int)f->size;
        if (fs_qbuf(fd, idx, addr_m, qlen) < 0) {
            /* QBUF failed — return frame to the available queue so the software
             * fallback path (VBMGetFrame) can still use it.  Do NOT abort the
             * entire prime operation; some kernels reject pre-STREAMON QBUFs
             * but the capture thread can still fall back to software mode. */
            pool->available_queue[pool->queue_tail] = idx;
            pool->queue_tail = (pool->queue_tail + 1) % pool->frame_count;
            pool->queue_count++;
            fprintf(stderr, "[VBM] PrimeKernelQueue: qbuf failed for idx=%d (len=%u), returned to avail queue\n", idx, qlen);
            continue;
        }
        queued_ok++;
    }
    pthread_mutex_unlock(&pool->queue_mutex);

    fprintf(stderr, "[VBM] PrimeKernelQueue: queued %d/%d frames to kernel for chn=%d (limit=%d)\n",
            queued_ok, to_queue, chn, limit);
    return queued_ok;
}

/* Dequeue a kernel-filled frame and map to VBM frame pointer */

#if defined(PLATFORM_T31)
volatile int openimp_vbm_dq_step[MAX_VBM_POOLS];
#define VBM_DQ_STEP(chn, step) (openimp_vbm_dq_step[(chn)] = (step))
#else
#define VBM_DQ_STEP(chn, step) do { } while (0)
#endif

#if defined(PLATFORM_T31) || defined(PLATFORM_T23) || \
    defined(PLATFORM_T21) || defined(PLATFORM_T30)
extern void openimp_t31_ivs_capture(int fs_chn, const void *frame)
    __attribute__((weak));
#endif

#if defined(PLATFORM_T31)
/* IMP_FrameSource_SetChnRotate software rotation (framesource_tseries.c) */
extern void openimp_fs_rotate_capture(int chn, void *frame)
    __attribute__((weak));
#endif

#if defined(PLATFORM_T31) || defined(PLATFORM_T23) || \
    defined(PLATFORM_T21) || defined(PLATFORM_T30)

/*
 * The ready queue below is a pull queue: only IMP_FrameSource_GetFrame (the
 * encoder's PollingStream while it receives, or a streamer's own reader)
 * pops it and hands the buffer back through VBMReleaseFrame.  IVS takes its
 * copy in VBMKernelDequeue and never holds a capture buffer, so with IVS as
 * the only consumer every buffer ended up parked in the ready queue and the
 * next DQBUF never completed.  While nobody has pulled for VBM_PULL_IDLE_MS
 * a captured buffer therefore goes straight back to the driver after the
 * IVS copy, together with anything a stopped reader left queued.  A reader
 * that comes back marks itself with its first (empty) GetFrame, and the
 * next capture is published again; an active reader polls far more often
 * than this window, so its path is unchanged.  T20/T21/T30 need this as
 * well: their pools have two buffers, so two parked frames stop capture.
 */
#define VBM_PULL_IDLE_MS 1000u

int VBMReleaseFrame(int chn, void *frame);

static uint32_t vbm_last_pull_ms[MAX_VBM_POOLS];

static uint32_t vbm_now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)ts.tv_sec * 1000u + (uint32_t)(ts.tv_nsec / 1000000);
}

static int vbm_pull_idle(int chn)
{
    uint32_t last = __atomic_load_n(&vbm_last_pull_ms[chn], __ATOMIC_RELAXED);

    return vbm_now_ms() - last >= VBM_PULL_IDLE_MS;
}

#if defined(PLATFORM_T31) || defined(PLATFORM_T23)
/*
 * Frame-ready events. The encoder pulls frames with VBMGetFrame, which never
 * blocks; it used to sleep 1 ms between attempts, so every captured frame
 * cost it up to a frame interval of 1 ms wake-ups (plus a clock_gettime per
 * attempt here). The capture worker now advances a per-channel sequence
 * after publishing a frame and broadcasts; a reader takes the sequence
 * before its VBMGetFrame and sleeps until it moves (openimp_ready_event.h).
 * The events are static, so a waiter never touches a pool that DestroyPool
 * may free.
 */
static OpenIMPReadyEvent vbm_ready[MAX_VBM_POOLS];
static pthread_once_t vbm_ready_once = PTHREAD_ONCE_INIT;

static void vbm_ready_init_once(void)
{
    int i;

    for (i = 0; i < MAX_VBM_POOLS; i++)
        openimp_ready_event_init(&vbm_ready[i]);
}

static void vbm_ready_notify(int chn)
{
    pthread_once(&vbm_ready_once, vbm_ready_init_once);
    openimp_ready_event_notify(&vbm_ready[chn]);
}

void VBMWakeReaders(int chn)
{
    if (chn >= 0 && chn < MAX_VBM_POOLS)
        vbm_ready_notify(chn);
}

unsigned int VBMReadySequence(int chn)
{
    if (chn < 0 || chn >= MAX_VBM_POOLS)
        return 0u;
    pthread_once(&vbm_ready_once, vbm_ready_init_once);
    return openimp_ready_event_sequence(&vbm_ready[chn]);
}

int VBMWaitReady(int chn, unsigned int sequence, uint32_t timeout_us)
{
    if (chn < 0 || chn >= MAX_VBM_POOLS)
        return -1;
    pthread_once(&vbm_ready_once, vbm_ready_init_once);
    return openimp_ready_event_wait(&vbm_ready[chn], sequence, timeout_us);
}

#endif /* PLATFORM_T31 || PLATFORM_T23: frame-ready events */

/* Return every frame still waiting in chn's ready queue to the driver while
 * no reader is pulling it.  Each index is popped under queue_mutex, so a
 * reader that races in can never receive a frame that is being recycled. */
int VBMRecycleIdleFrames(int chn)
{
    VBMPool *pool;
    int recycled = 0;

    if (chn < 0 || chn >= MAX_VBM_POOLS || !vbm_pull_idle(chn))
        return 0;
    pool = vbm_instance[chn];
    /* A buffer goes back to the driver either by QBUF on the pool's fd or,
     * when the FrameSource owns the fd (T23: pool fd -1), through the
     * pool's release callback.  A pool with neither would only take the
     * frame into its own queue again. */
    if (!pool || (pool->fd < 0 &&
                  (!pool->buf_in_userspace || !pool->ops[1])))
        return 0;
    for (;;) {
        int idx;

        pthread_mutex_lock(&pool->queue_mutex);
        if (pool->queue_count <= 0) {
            pthread_mutex_unlock(&pool->queue_mutex);
            break;
        }
        idx = pool->available_queue[pool->queue_head];
        pool->queue_head = (pool->queue_head + 1) % pool->frame_count;
        pool->queue_count--;
        pthread_mutex_unlock(&pool->queue_mutex);
        if (idx < 0 || idx >= pool->frame_count)
            continue;
        VBMReleaseFrame(chn, &pool->frames[idx]);
        recycled++;
    }
    if (recycled)
        ki_trace("libimp/VBM: idle-recycle ch=%d frames=%d\n", chn, recycled);
    return recycled;
}
#endif

#if defined(PLATFORM_T31)
#include "imp/imp_framesource.h"

/*
 * Capture timestamps for the T31 frame channels.
 *
 * Both kernel drivers stamp a buffer when the ISP completes it:
 *  - stock tx-isp-t31.ko: frame_chan_event (buffer done, called from the ISP
 *    interrupt routine) calls private_getrawmonotonic() and stores tv_sec and
 *    tv_nsec / 1000 in vb+0x14/+0x18, which __fill_v4l2_buffer copies to the
 *    DQBUF timestamp (CLOCK_MONOTONIC_RAW, the P0 timestamp clock);
 *  - open-tx-isp: fill_timeval_mono in the frame-done interrupt
 *    (CLOCK_MONOTONIC).
 * Rather than trusting one layout per driver, work out which clock the
 * DQBUF value belongs to at dequeue time: it must be a little older than
 * "now" on that clock.  CLOCK_MONOTONIC_RAW is tried first and is converted
 * 1:1, so a plausible raw value takes exactly the old path; other clocks are
 * rebased with the per-frame offset between that clock and the raw clock.
 * The clock that last matched is tried first and stays preferred with a
 * wider window, so NTP slew between MONOTONIC and MONOTONIC_RAW cannot make
 * it flap.
 *
 * Only when no clock fits (or the result would go backwards) does the frame
 * get a synthetic time: previous timestamp + one frame period, never later
 * than dequeue time.  Plain dequeue time is what made the pts jitter by up to
 * one frame (34/55/88/101 ms steps at 15 fps) and repeat.
 */
enum {
    FS_TS_CLOCK_RAW,
    FS_TS_CLOCK_MONOTONIC,
    FS_TS_CLOCK_REALTIME,
    FS_TS_CLOCK_COUNT
};

#define FS_TS_MAX_AGE_US        1000000ull  /* new clock: at most 1 s old */
#define FS_TS_LOCKED_MAX_AGE_US 5000000ull  /* clock already in use */
#define FS_TS_FUTURE_SLACK_US   5000ull     /* stamp may lead "now" slightly */
#define FS_TS_LOG_INTERVAL_US   60000000ll
#define FS_TS_REBASE_US         1000000ll   /* P0 clock moved back this far */

static const clockid_t fs_ts_clock_id[FS_TS_CLOCK_COUNT] = {
    CLOCK_MONOTONIC_RAW, CLOCK_MONOTONIC, CLOCK_REALTIME
};
static const char *const fs_ts_clock_name[FS_TS_CLOCK_COUNT] = {
    "MONOTONIC_RAW", "MONOTONIC", "REALTIME"
};

typedef struct {
    int64_t last;           /* last published P0 timestamp, -1 = none */
    int64_t last_log;       /* P0 time of the last summary line */
    uint32_t period_us;     /* frame period from the channel attributes */
    uint32_t frames;
    uint32_t fallback_invalid;
    uint32_t fallback_order;
    uint32_t logged_fallbacks;
    uint32_t summarized;    /* fallback total in the last summary line */
} FsTsState;

static FsTsState fs_ts_state[MAX_VBM_POOLS] = {
    [0 ... MAX_VBM_POOLS - 1] = { .last = -1, .last_log = -1 }
};
static int fs_ts_clock = -1;    /* clock that matched last, -1 = none yet */

static uint64_t fs_ts_clock_us(clockid_t id)
{
    struct timespec ts;

    if (clock_gettime(id, &ts) != 0)
        return 0;
    return (uint64_t)(uint32_t)ts.tv_sec * 1000000ull +
           (uint64_t)(uint32_t)ts.tv_nsec / 1000ull;
}

/* Is stamp a plausible reading of clock c taken no more than max_age ago?
 * On success returns the same instant on CLOCK_MONOTONIC_RAW. */
static int fs_ts_try_clock(int c, uint64_t stamp, uint64_t raw_now,
                           uint64_t max_age, uint64_t *raw_out)
{
    uint64_t now = c == FS_TS_CLOCK_RAW ? raw_now
                                        : fs_ts_clock_us(fs_ts_clock_id[c]);
    uint64_t age;

    if (!now || stamp > now + FS_TS_FUTURE_SLACK_US)
        return 0;
    age = stamp < now ? now - stamp : 0;
    if (age > max_age)
        return 0;
    if (c == FS_TS_CLOCK_RAW)
        *raw_out = stamp;
    else if (age > raw_now)
        return 0;
    else
        *raw_out = raw_now - age;
    return 1;
}

static int fs_ts_to_raw(uint64_t stamp, uint64_t raw_now, uint64_t *raw_out)
{
    int locked = __atomic_load_n(&fs_ts_clock, __ATOMIC_RELAXED);
    int c;

    if (!stamp || !raw_now)
        return -1;
    if (locked >= 0 &&
        fs_ts_try_clock(locked, stamp, raw_now, FS_TS_LOCKED_MAX_AGE_US,
                        raw_out))
        return locked;
    for (c = 0; c < FS_TS_CLOCK_COUNT; ++c) {
        if (c == locked ||
            !fs_ts_try_clock(c, stamp, raw_now, FS_TS_MAX_AGE_US, raw_out))
            continue;
        __atomic_store_n(&fs_ts_clock, c, __ATOMIC_RELAXED);
        fprintf(stderr, "[KernelIF] frame timestamps use CLOCK_%s "
                "(DQBUF %llu us, raw now %llu us)\n", fs_ts_clock_name[c],
                (unsigned long long)stamp, (unsigned long long)raw_now);
        return c;
    }
    return -1;
}

static uint32_t fs_ts_period_us(int chn, FsTsState *st)
{
    if (!st->period_us || (st->frames & 63u) == 0) {
        IMPFSChnAttr attr;

        memset(&attr, 0, sizeof(attr));
        if (IMP_FrameSource_GetChnAttr(chn, &attr) == 0 &&
            attr.outFrmRateNum > 0 && attr.outFrmRateDen > 0) {
            uint64_t period = 1000000ull * (uint32_t)attr.outFrmRateDen /
                              (uint32_t)attr.outFrmRateNum;

            if (period)
                st->period_us = period > 2000000ull ? 2000000u
                                                     : (uint32_t)period;
        }
        if (!st->period_us)
            st->period_us = 40000u;
    }
    return st->period_us;
}

/* DQBUF timestamp (absolute, unknown clock) -> P0 frame timestamp.
 * Called only by the channel's own dequeue thread. */
static int64_t fs_frame_timestamp(int chn, uint64_t stamp)
{
    FsTsState *st = &fs_ts_state[chn];
    uint64_t raw_now = fs_ts_clock_us(CLOCK_MONOTONIC_RAW);
    int64_t now = IMP_System_GetTimeStamp();
    int64_t ts = -1;
    uint64_t raw;
    uint32_t period = fs_ts_period_us(chn, st);
    int clock = fs_ts_to_raw(stamp, raw_now, &raw);
    const char *why = NULL;

    st->frames++;
    /*
     * The P0 timebase itself went backwards: IMP_System_Init after an
     * IMP_System_Exit in the same process restarts it at 0, and
     * IMP_System_RebaseTimeStamp can move it anywhere.  The previous
     * session's last timestamp then no longer compares with anything;
     * keeping it would synthesize last + 1 us for every frame until the
     * new timebase caught up with the old one.  Start the channel over.
     */
    if (st->last >= 0 && now >= 0 && now + FS_TS_REBASE_US < st->last) {
        fprintf(stderr, "[KernelIF] ch%d timestamp base moved back "
                "(now %lld us, last %lld us), restarting the sequence\n",
                chn, (long long)now, (long long)st->last);
        st->last = -1;
        st->last_log = -1;
    }
    if (clock >= 0)
        ts = OpenIMP_P0_NormalizeMonotonicTimeStamp(raw);
    if (ts < 0) {
        why = "no usable capture time";
        st->fallback_invalid++;
    } else if (st->last >= 0 && ts <= st->last) {
        why = "capture time not increasing";
        st->fallback_order++;
    }
    if (why) {
        int64_t captured = ts;

        if (st->last < 0) {
            ts = now;
        } else {
            ts = st->last + period;
            if (ts > now)
                ts = now;
            if (ts <= st->last)
                ts = st->last + 1;
        }
        if (st->logged_fallbacks < 5) {
            st->logged_fallbacks++;
            fprintf(stderr, "[KernelIF] ch%d frame %u: %s (DQBUF %llu us, "
                    "P0 %lld us, last %lld us) -> %lld us\n", chn, st->frames,
                    why, (unsigned long long)stamp, (long long)captured,
                    (long long)st->last, (long long)ts);
        }
    }
    /* At most one summary a minute, and only when something new was
     * synthesized: one fallback at start-up must not turn into a line
     * every minute for the rest of a soak. */
    if (st->fallback_invalid + st->fallback_order != st->summarized &&
        now >= 0 &&
        (st->last_log < 0 || now - st->last_log >= FS_TS_LOG_INTERVAL_US)) {
        int locked = __atomic_load_n(&fs_ts_clock, __ATOMIC_RELAXED);

        st->last_log = now;
        st->summarized = st->fallback_invalid + st->fallback_order;
        fprintf(stderr, "[KernelIF] ch%d timestamps: %u frames, clock %s, "
                "synthesized %u (no capture time) + %u (not increasing)\n",
                chn, st->frames,
                locked >= 0 ? fs_ts_clock_name[locked] : "none",
                st->fallback_invalid, st->fallback_order);
    }
    st->last = ts;
    return ts;
}
#endif

int VBMKernelDequeue(int chn, int fd, void **frame_out) {
    if (chn < 0 || chn >= MAX_VBM_POOLS || !frame_out) return -1;
    VBMPool *pool = vbm_instance[chn];
    if (!pool) return -1;
    static int eagain_count[MAX_VBM_POOLS] = {0};
    static int err_count[MAX_VBM_POOLS] = {0};
    static int dbg_count[MAX_VBM_POOLS] = {0};

    int idx = -1;
    uint64_t absolute_timestamp = 0;
    int64_t frame_timestamp;
    ki_trace("libimp/VBM: KernelDequeue enter ch=%d fd=%d pool=%p\n", chn, fd, pool);
    if (dbg_count[chn] < 3) {
        OPENIMP_TRACE_STDERR("[VBM] VBMKernelDequeue chn=%d: attempting DQBUF...\n", chn);
    }

    VBM_DQ_STEP(chn, VBM_DQ_STEP_DQBUF);
    int ret = fs_dqbuf(fd, &idx, &absolute_timestamp);
    ki_trace("libimp/VBM: KernelDequeue post-dq ch=%d fd=%d ret=%d idx=%d\n",
             chn, fd, ret, idx);
    if (dbg_count[chn] < 3) {
        OPENIMP_TRACE_STDERR("[VBM] VBMKernelDequeue chn=%d: DQBUF ret=%d idx=%d\n", chn, ret, idx);
        dbg_count[chn]++;
    }

    if (ret == -2) {
        int c = ++eagain_count[chn];
        if (c <= 5 || (c % 50) == 0) {
            OPENIMP_TRACE_STDERR("[VBM] VBMKernelDequeue chn=%d: DQBUF EAGAIN (count=%d)\n", chn, c);
        }
        return -2; /* EAGAIN */
    }
    if (ret == -3)
        return -3; /* not streaming, see fs_dqbuf */
    if (ret != 0) {
        int c = ++err_count[chn];
        if (c <= 5 || (c % 50) == 0) {
            fprintf(stderr, "[VBM] VBMKernelDequeue chn=%d: DQBUF error ret=%d\n", chn, ret);
        }
        return -1;
    }
    if (idx < 0 || idx >= pool->frame_count) {
        fprintf(stderr, "[VBM] VBMKernelDequeue chn=%d: invalid idx=%d (frame_count=%d)\n", chn, idx, pool->frame_count);
        return -1;
    }
#if defined(PLATFORM_T31)
    frame_timestamp = fs_frame_timestamp(chn, absolute_timestamp);
#else
    frame_timestamp =
        OpenIMP_P0_NormalizeMonotonicTimeStamp(absolute_timestamp);
    if (frame_timestamp < 0)
        frame_timestamp = IMP_System_GetTimeStamp();
#endif
#if defined(PLATFORM_T23)
    pool->frames[idx].time_stamp = frame_timestamp;
    pool->frames[idx].time_stamp_ivdc = frame_timestamp;
#else
    memcpy(pool->frames[idx].data, &frame_timestamp,
           sizeof(frame_timestamp));
#endif
#if defined(PLATFORM_T31) || defined(PLATFORM_T23) || \
    defined(PLATFORM_T21) || defined(PLATFORM_T30)
    /* IVS groups bound to this channel copy the luma they need now, while
     * the buffer is still private to this thread (openimp_t31_ivs.c). */
#if defined(PLATFORM_T31)
    /* Rotate first, as the vendor does before notifying any consumer. */
    if (openimp_fs_rotate_capture)
        openimp_fs_rotate_capture(chn, &pool->frames[idx]);
#endif
    VBM_DQ_STEP(chn, VBM_DQ_STEP_IVS);
    if (openimp_t31_ivs_capture)
        openimp_t31_ivs_capture(chn, &pool->frames[idx]);
#endif
    /* Mark buffer as in userspace — VBMReleaseFrame will only QBUF it back
     * if this flag is set, preventing double-QBUF. */
    if (pool->buf_in_userspace)
        pool->buf_in_userspace[idx] = 1;
#if defined(PLATFORM_T31) || defined(PLATFORM_T23) || \
    defined(PLATFORM_T21) || defined(PLATFORM_T30)
    if (vbm_pull_idle(chn)) {
        VBM_DQ_STEP(chn, VBM_DQ_STEP_REQUEUE);
        VBMRecycleIdleFrames(chn);
        VBMReleaseFrame(chn, &pool->frames[idx]);
        *frame_out = NULL;
        return -1;
    }
#endif

    /*
     * The shared encoder is a public-API pull consumer: PollingStream calls
     * IMP_FrameSource_GetFrame, which in turn pops this ready queue.  The old
     * experimental T31 graph delivered the pointer through a Module observer
     * instead, so merely returning it to frame_pooling_thread stranded every
     * capture buffer after DQBUF.  Publish the completed index here and let
     * VBMReleaseFrame return it to the stock frame-channel driver after AVPU
     * accepts the source address.
     */
    VBM_DQ_STEP(chn, VBM_DQ_STEP_QUEUE);
    pthread_mutex_lock(&pool->queue_mutex);
    if (pool->queue_count >= pool->frame_count) {
        VBMFrame *captured = &pool->frames[idx];

        pthread_mutex_unlock(&pool->queue_mutex);
        VBM_DQ_STEP(chn, VBM_DQ_STEP_REQUEUE);
        ki_trace("libimp/VBM: ready-queue-full ch=%d idx=%d count=%d\n",
                 chn, idx, pool->queue_count);
        if (fs_qbuf(fd, idx, captured->phys_addr,
                    (unsigned int)captured->size) == 0 &&
            pool->buf_in_userspace)
            pool->buf_in_userspace[idx] = 0;
        *frame_out = NULL;
        return -1;
    }
    pool->available_queue[pool->queue_tail] = idx;
    pool->queue_tail = (pool->queue_tail + 1) % pool->frame_count;
    pool->queue_count++;
    pthread_mutex_unlock(&pool->queue_mutex);
#if defined(PLATFORM_T31)
    vbm_ready_notify(chn);
#endif

    *frame_out = &pool->frames[idx];
    ki_trace("libimp/VBM: ready ch=%d idx=%d count=%d frame=%p\n",
             chn, idx, pool->queue_count, *frame_out);
    return 0;
}


static int vbm_fill_pool(int chn);

int VBMFillPool(int chn) {
    int ret;

    if (chn < 0 || chn >= MAX_VBM_POOLS) {
        return -1;
    }
    pthread_mutex_lock(&vbm_pool_lock[chn]);
    ret = vbm_fill_pool(chn);
    pthread_mutex_unlock(&vbm_pool_lock[chn]);
    return ret;
}

static int vbm_fill_pool(int chn) {
    VBMPool *pool = vbm_instance[chn];
    if (pool == NULL) {
        return -1;
    }

    OPENIMP_TRACE_STDERR("[VBM] FillPool: chn=%d, filling %d frames\n", chn, pool->frame_count);

    pthread_mutex_lock(&pool->queue_mutex);
    pool->queue_head = 0;
    pool->queue_tail = 0;
    pool->queue_count = 0;
    pthread_mutex_unlock(&pool->queue_mutex);

    if (pool->ops[1] != NULL) {
        int queued_ok = 0;

        for (int i = 0; i < pool->frame_count; i++) {
            int ret = ((int (*)(void *, void *))pool->ops[1])(&pool->frames[i], pool->priv);
            if (ret == 0) {
                queued_ok++;
            } else {
                fprintf(stderr, "[VBM] FillPool: release callback failed chn=%d idx=%d ret=%d\n",
                        chn, i, ret);
            }
        }

        OPENIMP_TRACE_STDERR("[VBM] FillPool: seeded %d/%d frames via release callback\n",
                queued_ok, pool->frame_count);
        return queued_ok;
    }

    /* Queue all frames as available for the software/non-callback path. */
    pthread_mutex_lock(&pool->queue_mutex);

    for (int i = 0; i < pool->frame_count; i++) {
        pool->available_queue[pool->queue_tail] = i;
        pool->queue_tail = (pool->queue_tail + 1) % pool->frame_count;
        pool->queue_count++;
    }

    pthread_mutex_unlock(&pool->queue_mutex);

    OPENIMP_TRACE_STDERR("[VBM] FillPool: queued %d frames\n", pool->queue_count);

    return pool->queue_count;
}

int VBMFlushFrame(int chn) {
    if (chn < 0 || chn >= MAX_VBM_POOLS) {
        return -1;
    }

    pthread_mutex_lock(&vbm_pool_lock[chn]);
    VBMPool *pool = vbm_instance[chn];
    if (pool == NULL) {
        pthread_mutex_unlock(&vbm_pool_lock[chn]);
        return -1;
    }

    OPENIMP_TRACE_STDERR("[VBM] FlushFrame: chn=%d\n", chn);

    /* Clear the frame queue */
    pthread_mutex_lock(&pool->queue_mutex);

    pool->queue_head = 0;
    pool->queue_tail = 0;
    pool->queue_count = 0;

    pthread_mutex_unlock(&pool->queue_mutex);
    pthread_mutex_unlock(&vbm_pool_lock[chn]);

    OPENIMP_TRACE_STDERR("[VBM] FlushFrame: flushed all frames\n");

    return 0;
}

static int vbm_get_frame(int chn, void **frame);

void VBMWaitReleases(int chn)
{
    if (chn < 0 || chn >= MAX_VBM_POOLS)
        return;
    pthread_mutex_lock(&vbm_pool_lock[chn]);
    pthread_mutex_unlock(&vbm_pool_lock[chn]);
}

int VBMGetFrame(int chn, void **frame) {
    int ret;

    if (chn < 0 || chn >= MAX_VBM_POOLS) {
        return -1;
    }
    pthread_mutex_lock(&vbm_pool_lock[chn]);
    ret = vbm_get_frame(chn, frame);
    pthread_mutex_unlock(&vbm_pool_lock[chn]);
    return ret;
}

static int vbm_get_frame(int chn, void **frame) {
    VBMPool *pool = vbm_instance[chn];
    if (pool == NULL) {
        *frame = NULL;
        return -1;
    }
#if defined(PLATFORM_T31) || defined(PLATFORM_T23) || \
    defined(PLATFORM_T21) || defined(PLATFORM_T30)
    __atomic_store_n(&vbm_last_pull_ms[chn], vbm_now_ms(), __ATOMIC_RELAXED);
#endif

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

    OPENIMP_TRACE_STDERR("[VBM] GetFrame: chn=%d, frame=%p (idx=%d, %d remaining)\n",
            chn, *frame, frame_idx, pool->queue_count);
    return 0;
}

static int vbm_release_frame(int chn, void *frame);

int VBMReleaseFrame(int chn, void *frame) {
    int ret;

    if (chn < 0 || chn >= MAX_VBM_POOLS) {
        return -1;
    }
    pthread_mutex_lock(&vbm_pool_lock[chn]);
    ret = vbm_release_frame(chn, frame);
    pthread_mutex_unlock(&vbm_pool_lock[chn]);
    return ret;
}

static int vbm_release_frame(int chn, void *frame) {
    static int trace_budget = 96;
    VBMPool *pool = vbm_instance[chn];
    if (pool == NULL || frame == NULL) {
        return -1;
    }

    /* Only a record of this pool is looked at: a consumer may still hold a
     * frame of a pool DisableChn has destroyed (and EnableChn re-created),
     * whose memory is gone and whose buffer must not be queued again. */
    int frame_idx = vbm_frame_index(pool, frame);
    if (frame_idx < 0) {
        static int foreign_logged;

        if (!foreign_logged) {
            foreign_logged = 1;
            fprintf(stderr, "[VBM] ReleaseFrame: chn=%d frame %p is not in this channel's pool (released after DisableChn?), ignored\n",
                    chn, frame);
        }
        return -1;
    }

    if (trace_budget > 0) {
        VBMFrame *trace_frame = (VBMFrame*)frame;
        trace_budget--;
        ki_trace("libimp/VBMKI: release chn=%d frame=%p idx=%d frame_chn=%d vaddr=0x%x paddr=0x%x\n",
                 chn, frame,
                 trace_frame ? trace_frame->index : -1,
                 trace_frame ? trace_frame->chn : -1,
                 trace_frame ? trace_frame->virt_addr : 0,
                 trace_frame ? trace_frame->phys_addr : 0);
    }

    VBMFrame *vbm_frame = (VBMFrame*)frame;
    int kernel_backed = (pool->fd >= 0);

    pthread_mutex_lock(&pool->queue_mutex);

    if (pool->buf_in_userspace != NULL && pool->ops[1] != NULL) {
        int in_userspace = pool->buf_in_userspace[frame_idx];

        if (trace_budget > 0) {
            trace_budget--;
            ki_trace("libimp/VBMKI: release-cb chn=%d idx=%d in_userspace=%d cb=%p priv=%p\n",
                     chn, frame_idx, in_userspace, pool->ops[1], pool->priv);
        }

        if (!in_userspace) {
            pthread_mutex_unlock(&pool->queue_mutex);
            return 0;
        }

        if (((int (*)(void *, void *))pool->ops[1])(frame, pool->priv) == 0) {
            pool->buf_in_userspace[frame_idx] = 0;
            if (trace_budget > 0) {
                trace_budget--;
                ki_trace("libimp/VBMKI: release-cb-ok chn=%d idx=%d\n",
                         chn, frame_idx);
            }
            pthread_mutex_unlock(&pool->queue_mutex);
            return 0;
        }

        if (trace_budget > 0) {
            trace_budget--;
            ki_trace("libimp/VBMKI: release-cb-fail chn=%d idx=%d\n",
                     chn, frame_idx);
        }
        /* Callback-backed pools are driver-owned; if the callback fails,
         * fall through to the legacy local-queue fallback. */
        pool->buf_in_userspace[frame_idx] = 0;
    }

    if (kernel_backed) {
        int in_userspace = 0;

        if (pool->buf_in_userspace != NULL) {
            in_userspace = pool->buf_in_userspace[frame_idx];
        }
        if (trace_budget > 0) {
            trace_budget--;
            ki_trace("libimp/VBMKI: release-kernel chn=%d idx=%d in_userspace=%d q=%d/%d fd=%d\n",
                     chn, frame_idx, in_userspace,
                     pool->queue_count, pool->frame_count, pool->fd);
        }

        /* Duplicate releases are expected from higher layers; only the first
         * release after DQBUF should hand the buffer back to the kernel. */
        if (pool->buf_in_userspace == NULL || !in_userspace) {
            pthread_mutex_unlock(&pool->queue_mutex);
            return 0;
        }

        {
            unsigned long phys = vbm_frame->phys_addr;
            unsigned int qlen = (unsigned int)vbm_frame->size;

            if (fs_qbuf(pool->fd, frame_idx, phys, qlen) == 0) {
                pool->buf_in_userspace[frame_idx] = 0;
                if (trace_budget > 0) {
                    trace_budget--;
                    ki_trace("libimp/VBMKI: release-qbuf-ok chn=%d idx=%d phys=0x%lx len=%u\n",
                             chn, frame_idx, phys, qlen);
                }
                pthread_mutex_unlock(&pool->queue_mutex);
                return 0;
            }

            fprintf(stderr, "[VBM] ReleaseFrame: fs_qbuf failed for idx=%d (len=%u)\n", frame_idx, qlen);
            if (trace_budget > 0) {
                trace_budget--;
                ki_trace("libimp/VBMKI: release-qbuf-fail chn=%d idx=%d phys=0x%lx len=%u\n",
                         chn, frame_idx, phys, qlen);
            }
            /* Fall back to the software queue so callers are not left with a
             * permanently lost buffer when QBUF fails. */
            pool->buf_in_userspace[frame_idx] = 0;
        }
    }

    if (pool->queue_count >= pool->frame_count) {
        pool->queue_head = (pool->queue_head + 1) % pool->frame_count;
        pool->queue_count--;
    }

    pool->available_queue[pool->queue_tail] = frame_idx;
    pool->queue_tail = (pool->queue_tail + 1) % pool->frame_count;
    pool->queue_count++;

    pthread_mutex_unlock(&pool->queue_mutex);
#if defined(PLATFORM_T31)
    /* Software fallback queue: also readable by VBMGetFrame. */
    vbm_ready_notify(chn);
#endif

    /* fprintf throttled — high-frequency per-frame path */

    return 0;
}

int VBMLockFrameByVaddr(uint32_t vaddr)
{
    static int trace_budget = 64;
    VBMVolume *vol;

    pthread_mutex_lock(&vbm_volume_lock);
    vol = vbm_find_volume_by_vaddr(vaddr);
    if (vol == NULL) {
        pthread_mutex_unlock(&vbm_volume_lock);
        fprintf(stderr, "[VBM] LockFrameByVaddr: vaddr=0x%x not found\n", vaddr);
        return -1;
    }

    int old_ref = vol->ref_count;
    vol->ref_count++;
    int new_ref = vol->ref_count;
    VBMFrame *frame = vol->frame;
    int frame_chn = frame->chn;
    int frame_index = frame->index;
    /* fprintf throttled — high-frequency per-frame path */
    pthread_mutex_unlock(&vbm_volume_lock);
    if (trace_budget > 0) {
        trace_budget--;
        ki_trace("libimp/VBMKI: lock vaddr=0x%x ref=%d->%d frame=%p chn=%d idx=%d\n",
                 vaddr, old_ref, new_ref, frame, frame_chn, frame_index);
    }
    return 0;
}

int VBMUnlockFrameByVaddr(uint32_t vaddr)
{
    static int trace_budget = 96;
    VBMVolume *vol;

    pthread_mutex_lock(&vbm_volume_lock);
    vol = vbm_find_volume_by_vaddr(vaddr);
    if (vol == NULL) {
        pthread_mutex_unlock(&vbm_volume_lock);
        fprintf(stderr, "[VBM] UnlockFrameByVaddr: vaddr=0x%x not found\n", vaddr);
        return -1;
    }

    if (vol->ref_count <= 0) {
        pthread_mutex_unlock(&vbm_volume_lock);
        fprintf(stderr, "[VBM] UnlockFrameByVaddr: vaddr=0x%x already unlocked\n", vaddr);
        return -1;
    }

    int old_ref = vol->ref_count;
    vol->ref_count--;
    int ref_count = vol->ref_count;
    /* The record is only read while it is registered (DestroyPool
     * unregisters it under vbm_volume_lock before freeing it). */
    VBMFrame *frame = vol->frame;
    int frame_chn = frame->chn;
    int frame_index = frame->index;
    pthread_mutex_unlock(&vbm_volume_lock);

    OPENIMP_TRACE_STDERR("[VBM] UnlockFrameByVaddr: vaddr=0x%x ref=%d\n", vaddr, ref_count);
    if (trace_budget > 0) {
        trace_budget--;
        ki_trace("libimp/VBMKI: unlock vaddr=0x%x ref=%d->%d frame=%p chn=%d idx=%d\n",
                 vaddr, old_ref, ref_count, frame, frame_chn, frame_index);
    }

    if (ref_count == 0) {
        if (trace_budget > 0) {
            trace_budget--;
            ki_trace("libimp/VBMKI: unlock-release vaddr=0x%x frame=%p chn=%d idx=%d\n",
                     vaddr, frame, frame_chn, frame_index);
        }
        /* VBMReleaseFrame refuses the frame if its pool went away since. */
        return VBMReleaseFrame(frame_chn, frame);
    }

    return 0;
}

int VBMLockFrame(void *frame)
{
    if (frame == NULL) return -1;
    return VBMLockFrameByVaddr(((VBMFrame*)frame)->virt_addr);
}

int VBMUnLockFrame(void *frame)
{
    if (frame == NULL) return -1;
    return VBMUnlockFrameByVaddr(((VBMFrame*)frame)->virt_addr);
}


/* Get originating channel from VBM frame (offset 0x04) */
int VBMFrame_GetChannel(void *frame, int *chn_out) {
    if (!frame || !chn_out) return -1;
    VBMFrame *f = (VBMFrame*)frame;
    *chn_out = f->chn;
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
