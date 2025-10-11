/**
 * Kernel Driver Interface for IMP
 * Handles ioctl calls to Ingenic kernel drivers
 */

#ifndef KERNEL_INTERFACE_H
#define KERNEL_INTERFACE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Format structure from decompilation at 0x9ecf8
 * This is a 0x70 (112 byte) structure that gets passed to ioctl 0xc07056c3
 * It appears to be the full IMPFSChnAttr structure
 */
typedef struct {
    int enable;                 /* 0x00: Enable flag */
    int width;                  /* 0x04: Width */
    int height;                 /* 0x08: Height */
    int pixfmt;                 /* 0x0c: Pixel format (fourcc) */
    int crop_enable;            /* 0x10: Crop enable */
    int crop_top;               /* 0x14: Crop top */
    int crop_left;              /* 0x18: Crop left */
    int crop_width;             /* 0x1c: Crop width */
    int crop_height;            /* 0x20: Crop height */
    int scaler_enable;          /* 0x24: Scaler enable */
    int scaler_out_width;       /* 0x28: Scaler output width */
    int scaler_out_height;      /* 0x2c: Scaler output height */
    int fps_num;                /* 0x30: FPS numerator */
    int fps_den;                /* 0x34: FPS denominator */
    int buf_type;               /* 0x38: Buffer type */
    int buf_mode;               /* 0x3c: Buffer mode */
    int colorspace;             /* 0x40: Colorspace (V4L2_COLORSPACE_*) */
    char padding[0x30];         /* 0x44-0x70: Padding to 112 bytes */
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

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_INTERFACE_H */

