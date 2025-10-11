/**
 * Hardware Encoder Interface
 * Interface to Ingenic hardware H.264/H.265 encoder
 * Based on reverse engineering of libimp.so v1.1.6
 */

#ifndef HW_ENCODER_H
#define HW_ENCODER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Encoder device path */
#define HW_ENCODER_DEVICE "/dev/jz-venc"

/* Encoder ioctl commands */
#define VENC_IOCTL_INIT         0xc0104501
#define VENC_IOCTL_DEINIT       0xc0104502
#define VENC_IOCTL_ENCODE       0xc0104503
#define VENC_IOCTL_GET_STREAM   0xc0104504
#define VENC_IOCTL_RELEASE      0xc0104505
#define VENC_IOCTL_SET_PARAM    0xc0104506
#define VENC_IOCTL_GET_PARAM    0xc0104507

/* Encoder codec types */
#define HW_CODEC_H264           0
#define HW_CODEC_H265           1
#define HW_CODEC_JPEG           4

/* Encoder profile */
#define HW_PROFILE_BASELINE     0
#define HW_PROFILE_MAIN         1
#define HW_PROFILE_HIGH         2

/* Rate control modes */
#define HW_RC_MODE_FIXQP        0
#define HW_RC_MODE_CBR          1
#define HW_RC_MODE_VBR          2

/* Hardware encoder parameters */
typedef struct {
    uint32_t codec_type;        /* 0x00: Codec type (H264/H265/JPEG) */
    uint32_t profile;           /* 0x04: Profile */
    uint32_t width;             /* 0x08: Frame width */
    uint32_t height;            /* 0x0c: Frame height */
    uint32_t fps_num;           /* 0x10: FPS numerator */
    uint32_t fps_den;           /* 0x14: FPS denominator */
    uint32_t gop_length;        /* 0x18: GOP length */
    uint32_t rc_mode;           /* 0x1c: Rate control mode */
    uint32_t bitrate;           /* 0x20: Target bitrate */
    uint32_t qp;                /* 0x24: QP value (for FIXQP) */
    uint32_t max_qp;            /* 0x28: Max QP */
    uint32_t min_qp;            /* 0x2c: Min QP */
    uint32_t reserved[16];      /* 0x30-0x6f: Reserved */
} HWEncoderParams;

/* Hardware frame buffer */
typedef struct {
    uint32_t phys_addr;         /* 0x00: Physical address */
    uint32_t virt_addr;         /* 0x04: Virtual address */
    uint32_t size;              /* 0x08: Buffer size */
    uint32_t width;             /* 0x0c: Frame width */
    uint32_t height;            /* 0x10: Frame height */
    uint32_t pixfmt;            /* 0x14: Pixel format */
    uint64_t timestamp;         /* 0x18: Timestamp */
} HWFrameBuffer;

/* Hardware stream buffer */
typedef struct {
    uint32_t phys_addr;         /* 0x00: Physical address */
    uint32_t virt_addr;         /* 0x04: Virtual address */
    uint32_t length;            /* 0x08: Stream length */
    uint64_t timestamp;         /* 0x0c: Timestamp */
    uint32_t frame_type;        /* 0x14: Frame type (I/P/B) */
    uint32_t slice_type;        /* 0x18: Slice type */
    uint32_t reserved[8];       /* 0x1c-0x3b: Reserved */
} HWStreamBuffer;

/* Frame types */
#define HW_FRAME_TYPE_I         0
#define HW_FRAME_TYPE_P         1
#define HW_FRAME_TYPE_B         2

/**
 * Initialize hardware encoder
 * @param fd File descriptor (output)
 * @param params Encoder parameters
 * @return 0 on success, -1 on failure
 */
int HW_Encoder_Init(int *fd, HWEncoderParams *params);

/**
 * Deinitialize hardware encoder
 * @param fd File descriptor
 * @return 0 on success, -1 on failure
 */
int HW_Encoder_Deinit(int fd);

/**
 * Encode a frame
 * @param fd File descriptor
 * @param frame Frame buffer
 * @return 0 on success, -1 on failure
 */
int HW_Encoder_Encode(int fd, HWFrameBuffer *frame);

/**
 * Get encoded stream
 * @param fd File descriptor
 * @param stream Stream buffer (output)
 * @param timeout_ms Timeout in milliseconds (-1 = infinite)
 * @return 0 on success, -1 on failure
 */
int HW_Encoder_GetStream(int fd, HWStreamBuffer *stream, int timeout_ms);

/**
 * Release stream buffer
 * @param fd File descriptor
 * @param stream Stream buffer
 * @return 0 on success, -1 on failure
 */
int HW_Encoder_ReleaseStream(int fd, HWStreamBuffer *stream);

/**
 * Set encoder parameters
 * @param fd File descriptor
 * @param params Encoder parameters
 * @return 0 on success, -1 on failure
 */
int HW_Encoder_SetParams(int fd, HWEncoderParams *params);

#ifdef __cplusplus
}
#endif

#endif /* HW_ENCODER_H */

