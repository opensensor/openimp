/**
 * Hardware Encoder Implementation
 * Interface to Ingenic hardware H.264/H.265 encoder
 * Based on reverse engineering of libimp.so v1.1.6
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include "hw_encoder.h"

#define LOG_HW(fmt, ...) fprintf(stderr, "[HW_Encoder] " fmt "\n", ##__VA_ARGS__)

/**
 * Initialize hardware encoder
 */
int HW_Encoder_Init(int *fd, HWEncoderParams *params) {
    if (fd == NULL || params == NULL) {
        return -1;
    }

    /* Try to open hardware encoder device */
    int dev_fd = open(HW_ENCODER_DEVICE, O_RDWR);
    if (dev_fd < 0) {
        LOG_HW("Failed to open %s: %s", HW_ENCODER_DEVICE, strerror(errno));
        LOG_HW("Hardware encoder not available, using software fallback");
        *fd = -1;
        return -1;
    }

    LOG_HW("Opened hardware encoder device: %s (fd=%d)", HW_ENCODER_DEVICE, dev_fd);

    /* Initialize encoder with parameters */
    if (ioctl(dev_fd, VENC_IOCTL_INIT, params) < 0) {
        LOG_HW("VENC_IOCTL_INIT failed: %s", strerror(errno));
        close(dev_fd);
        *fd = -1;
        return -1;
    }

    LOG_HW("Hardware encoder initialized:");
    LOG_HW("  Codec: %s", params->codec_type == HW_CODEC_H264 ? "H.264" :
                          params->codec_type == HW_CODEC_H265 ? "H.265" : "JPEG");
    LOG_HW("  Resolution: %ux%u", params->width, params->height);
    LOG_HW("  FPS: %u/%u", params->fps_num, params->fps_den);
    LOG_HW("  GOP: %u", params->gop_length);
    LOG_HW("  Bitrate: %u bps", params->bitrate);

    *fd = dev_fd;
    return 0;
}

/**
 * Deinitialize hardware encoder
 */
int HW_Encoder_Deinit(int fd) {
    if (fd < 0) {
        return 0; /* Already closed or not initialized */
    }

    /* Deinitialize encoder */
    if (ioctl(fd, VENC_IOCTL_DEINIT, NULL) < 0) {
        LOG_HW("VENC_IOCTL_DEINIT failed: %s", strerror(errno));
    }

    close(fd);
    LOG_HW("Hardware encoder deinitialized");
    return 0;
}

/**
 * Encode a frame
 */
int HW_Encoder_Encode(int fd, HWFrameBuffer *frame) {
    if (fd < 0 || frame == NULL) {
        return -1;
    }

    /* Submit frame for encoding */
    if (ioctl(fd, VENC_IOCTL_ENCODE, frame) < 0) {
        LOG_HW("VENC_IOCTL_ENCODE failed: %s", strerror(errno));
        return -1;
    }

    LOG_HW("Frame submitted for encoding: %ux%u, phys=0x%x, ts=%llu",
           frame->width, frame->height, frame->phys_addr, 
           (unsigned long long)frame->timestamp);

    return 0;
}

/**
 * Get encoded stream
 */
int HW_Encoder_GetStream(int fd, HWStreamBuffer *stream, int timeout_ms) {
    if (fd < 0 || stream == NULL) {
        return -1;
    }

    /* Clear stream buffer */
    memset(stream, 0, sizeof(HWStreamBuffer));

    /* Get encoded stream from hardware */
    /* Note: timeout_ms is passed via stream->reserved[0] */
    stream->reserved[0] = timeout_ms;

    if (ioctl(fd, VENC_IOCTL_GET_STREAM, stream) < 0) {
        if (errno == EAGAIN || errno == ETIMEDOUT) {
            /* No stream available */
            return -1;
        }
        LOG_HW("VENC_IOCTL_GET_STREAM failed: %s", strerror(errno));
        return -1;
    }

    LOG_HW("Got encoded stream: length=%u, type=%s, ts=%llu",
           stream->length,
           stream->frame_type == HW_FRAME_TYPE_I ? "I" :
           stream->frame_type == HW_FRAME_TYPE_P ? "P" : "B",
           (unsigned long long)stream->timestamp);

    return 0;
}

/**
 * Release stream buffer
 */
int HW_Encoder_ReleaseStream(int fd, HWStreamBuffer *stream) {
    if (fd < 0 || stream == NULL) {
        return -1;
    }

    /* Release stream buffer back to hardware */
    if (ioctl(fd, VENC_IOCTL_RELEASE, stream) < 0) {
        LOG_HW("VENC_IOCTL_RELEASE failed: %s", strerror(errno));
        return -1;
    }

    LOG_HW("Stream buffer released");
    return 0;
}

/**
 * Set encoder parameters
 */
int HW_Encoder_SetParams(int fd, HWEncoderParams *params) {
    if (fd < 0 || params == NULL) {
        return -1;
    }

    /* Set encoder parameters */
    if (ioctl(fd, VENC_IOCTL_SET_PARAM, params) < 0) {
        LOG_HW("VENC_IOCTL_SET_PARAM failed: %s", strerror(errno));
        return -1;
    }

    LOG_HW("Encoder parameters updated");
    return 0;
}

/**
 * Software fallback encoder (simple simulation)
 * Used when hardware encoder is not available
 */
int HW_Encoder_Encode_Software(HWFrameBuffer *frame, HWStreamBuffer *stream) {
    if (frame == NULL || stream == NULL) {
        return -1;
    }

    /* Simulate encoding by creating a dummy H.264 NAL unit */
    /* This is just for testing - real encoding would use libx264 or similar */
    
    static uint8_t dummy_stream[4096];
    static uint32_t frame_counter = 0;
    
    /* H.264 start code */
    dummy_stream[0] = 0x00;
    dummy_stream[1] = 0x00;
    dummy_stream[2] = 0x00;
    dummy_stream[3] = 0x01;
    
    /* NAL unit header (I-frame every 30 frames) */
    if (frame_counter % 30 == 0) {
        dummy_stream[4] = 0x65; /* IDR slice */
        stream->frame_type = HW_FRAME_TYPE_I;
    } else {
        dummy_stream[4] = 0x41; /* P slice */
        stream->frame_type = HW_FRAME_TYPE_P;
    }
    
    /* Fill with dummy data */
    for (int i = 5; i < 4096; i++) {
        dummy_stream[i] = (uint8_t)(frame_counter + i);
    }
    
    /* Populate stream buffer */
    stream->virt_addr = (uint32_t)(uintptr_t)dummy_stream;
    stream->phys_addr = 0; /* No physical address in software mode */
    stream->length = 4096;
    stream->timestamp = frame->timestamp;
    stream->slice_type = (frame_counter % 30 == 0) ? 0 : 1;
    
    frame_counter++;
    
    LOG_HW("Software encoding: frame %u, type=%s, length=%u",
           frame_counter,
           stream->frame_type == HW_FRAME_TYPE_I ? "I" : "P",
           stream->length);
    
    return 0;
}

