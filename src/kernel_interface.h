/**
 * Kernel Driver Interface for IMP
 * Handles ioctl calls to Ingenic kernel drivers
 */

#ifndef KERNEL_INTERFACE_H
#define KERNEL_INTERFACE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* High-level frame-source format description used by OpenIMP callers.
 * fs_set_format marshals it into the stock 0x70-byte tisp_frame_format ABI:
 * type + 12-word pixel format + crop/scaler/rate/fcrop fields. */
typedef struct {
    int type;                   /* 0x00: Buffer type (V4L2_BUF_TYPE_VIDEO_CAPTURE = 1) */
    int width;                  /* 0x04: Width */
    int height;                 /* 0x08: Height */
    int pixelformat;            /* 0x0c: Pixel format (fourcc) */
    int field;                  /* 0x10: Field order */
    int bytesperline;           /* 0x14: Bytes per line */
    int sizeimage;              /* 0x18: Image size in bytes */
    int colorspace;             /* 0x1c: Colorspace (V4L2_COLORSPACE_SRGB = 8) */
    int priv;                   /* 0x20: Private data */
    /* Semantic channel fields marshalled after the full pixel-format block. */
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
    char padding[0x10];         /* 0x60-0x6f: Reserved/padding */
} fs_format_t;

/* FrameSource device operations */
int fs_open_device(int chn);
int fs_get_format(int fd, fs_format_t *fmt);
int fs_set_format(int fd, fs_format_t *fmt);
int fs_set_buffer_count(int fd, int count);
int fs_set_depth(int fd, int depth);
int fs_stream_on(int fd);
int fs_stream_off(int fd);
#if defined(PLATFORM_T31)
int fs_stream_off_quiet(int fd); /* ioctl only, for the exit paths */
#endif
/* OEM frame-ready wait (0x400456bf), unused by OpenIMP; see the .c file. */
int fs_poll_frame(int fd, unsigned int *ready_out, int timeout_ms);

/* Whether the frame-channel driver honours O_NONBLOCK in DQBUF, learnt from
 * the DQBUFs fs_dqbuf() has issued (all /dev/framechanN fds are opened
 * O_NONBLOCK, see fs_open_device):
 * - HONOURED: a DQBUF returned EAGAIN (open tx-isp since 85fbd1fd). The
 *   reader waits in select()/poll(), never in DQBUF.
 * - IGNORED: DQBUFs on the O_NONBLOCK fd slept until a frame arrived, and
 *   no EAGAIN was ever seen (stock tx-isp). Only STREAMOFF wakes them.
 * - UNKNOWN: too few DQBUFs yet. Treat as IGNORED. */
enum {
    FS_DQ_NONBLOCK_UNKNOWN = 0,
    FS_DQ_NONBLOCK_HONOURED = 1,
    FS_DQ_NONBLOCK_IGNORED = 2,
};
int fs_dqbuf_nonblock_mode(void);
const char *fs_dqbuf_nonblock_name(int mode);
void fs_close_device(int fd);

/* VBM (Video Buffer Manager) operations */
int VBMCreatePool(int chn, void *fmt, void *ops, void *priv);
int VBMDestroyPool(int chn);
int VBMFillPool(int chn);
int VBMFlushFrame(int chn);
/* Returns once no VBMReleaseFrame of chn is running: one that started
 * earlier may still have read the channel's previous frame-channel fd. */
void VBMWaitReleases(int chn);
int VBMGetFrame(int chn, void **frame);
int VBMReleaseFrame(int chn, void *frame);
int VBMLockFrame(void *frame);
int VBMUnLockFrame(void *frame);
int VBMLockFrameByVaddr(uint32_t vaddr);
int VBMUnlockFrameByVaddr(uint32_t vaddr);
int VBMFrame_GetBuffer(void *frame, void **virt, int *size);
/* Get originating channel from VBM frame (reads offset 0x04) */
int VBMFrame_GetChannel(void *frame, int *chn_out);

/* FS buffer queueing to kernel (V4L2-style) */
int fs_querybuf(int fd, int index, unsigned int *length_out);
int fs_qbuf(int fd, int index, unsigned long phys, unsigned int length);
int fs_dqbuf(int fd, int *index_out, uint64_t *timestamp_out);

/* Bridge between VBM and kernel queue */
int VBMPrimeKernelQueue(int chn, int fd, int limit);
int VBMKernelDequeue(int chn, int fd, void **frame_out);
#if defined(PLATFORM_T31) || defined(PLATFORM_T23) || \
    defined(PLATFORM_T21) || defined(PLATFORM_T30)
/* Return ready frames to the driver while no reader pulls the channel. */
int VBMRecycleIdleFrames(int chn);
#endif
#if defined(PLATFORM_T31) || defined(PLATFORM_T23)
/* Frame-ready events for a pull reader (the encoder's PollingStream):
 * read the sequence before VBMGetFrame and, when that found nothing, wait
 * for it to move instead of polling. Returns 0 when it moved (a frame was
 * published), -1 on timeout or a bad channel. */
unsigned int VBMReadySequence(int chn);
int VBMWaitReady(int chn, unsigned int sequence, uint32_t timeout_us);
/* End every VBMWaitReady on chn now (the reader rechecks its own state). */
void VBMWakeReaders(int chn);
#endif

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_INTERFACE_H */
