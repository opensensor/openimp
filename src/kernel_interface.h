/**
 * Kernel Driver Interface for IMP
 * Handles ioctl calls to Ingenic kernel drivers
 */

#ifndef KERNEL_INTERFACE_H
#define KERNEL_INTERFACE_H

#ifdef __cplusplus
extern "C" {
#endif

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
     * Layout must match tisp_channel_attr_set expected indices:
     *   [0]=enable, [1]=width, [2]=height,
     *   [3]=crop_enable, [4]=crop_x, [5]=crop_y, [6]=crop_width, [7]=crop_height,
     *   [8]=scaler_enable, [9]=scaler_outwidth, [10]=scaler_outheight,
     *   [11]=picwidth, [12]=picheight, [13]=fps_num, [14]=fps_den
     * Note: pixel format is conveyed via the V4L2 pixelformat header field, not here. */
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

/* FrameSource device operations */
int fs_open_device(int chn);
int fs_get_format(int fd, fs_format_t *fmt);
int fs_set_format(int fd, fs_format_t *fmt);
int fs_set_buffer_count(int fd, int count);
int fs_set_depth(int fd, int depth);
int fs_stream_on(int fd);
int fs_stream_off(int fd);
void fs_close_device(int fd);

/* VBM (Video Buffer Manager) operations */
int VBMCreatePool(int chn, void *fmt, void *ops, void *priv);
int VBMDestroyPool(int chn);
int VBMFillPool(int chn);
int VBMFlushFrame(int chn);
int VBMGetFrame(int chn, void **frame);
int VBMReleaseFrame(int chn, void *frame);
int VBMFrame_GetBuffer(void *frame, void **virt, int *size);

/* FS buffer queueing to kernel (V4L2-style) */
int fs_qbuf(int fd, int index, unsigned long phys, unsigned int length);
int fs_dqbuf(int fd, int *index_out);

/* Bridge between VBM and kernel queue */
int VBMPrimeKernelQueue(int chn, int fd);
int VBMKernelDequeue(int chn, int fd, void **frame_out);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_INTERFACE_H */

