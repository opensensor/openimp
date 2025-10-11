/**
 * Kernel Driver Interface for IMP
 * Handles ioctl calls to Ingenic kernel drivers
 */

#ifndef KERNEL_INTERFACE_H
#define KERNEL_INTERFACE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Format structure from decompilation */
typedef struct {
    int enable;                 /* 0x00: Enable flag */
    int width;                  /* 0x04: Width */
    int height;                 /* 0x08: Height */
    int pixfmt;                 /* 0x0c: Pixel format */
    /* More fields... */
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

