/**
 * AL_Codec Implementation
 * Based on reverse engineering of libimp.so v1.1.6
 * Decompiled from addresses 0x7950c (Create), 0x7a180 (Destroy), etc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
#include <imp/imp_encoder.h>
#include <imp/imp_system.h>
#include "fifo.h"
#include "hw_encoder.h"

#define LOG_CODEC(fmt, ...) fprintf(stderr, "[Codec] " fmt "\n", ##__VA_ARGS__)

/* Codec structure - based on decompilation at 0x7950c */
/* Size: 0x924 bytes */
typedef struct {
    void *g_pCodec;                 /* 0x000: Global codec pointer */
    uint8_t codec_param[0x794];     /* 0x004: Codec parameters */
    void *encoder;                  /* 0x798: AL_Encoder handle */
    void *event;                    /* 0x79c: Event handle */
    void *callback;                 /* 0x7a0: Callback function */
    void *callback_arg;             /* 0x7a4: Callback argument */
    int channel_id;                 /* 0x7a8: Channel ID + 1 */

    /* Stream buffer pool config - offsets from decompilation */
    int stream_buf_count;           /* 0x7ac: Stream buffer count */
    int stream_buf_size;            /* 0x7b0: Stream buffer size */
    uint8_t stream_pool[0x44];      /* 0x7b4-0x7f7: Stream buffer pool */

    /* FIFOs (control structures allocated dynamically to ensure proper size) */
    void *fifo_frames;              /* Frame FIFO control block */
    void *fifo_streams;             /* Stream FIFO control block */

    /* Frame buffer pool config */
    uint8_t frame_pool_config[0x60]; /* 0x840-0x89f: Frame pool config */
    int frame_buf_count;            /* 0x840: Frame buffer count (from GetSrcFrameCntAndSize) */
    uint8_t frame_pool_data[0x9c];  /* 0x8a0-0x8db: Frame pool data */
    int frame_buf_size;             /* 0x8dc: Frame buffer size (from GetSrcFrameCntAndSize) */

    /* Pixel map buffer pool */
    uint8_t pixmap_pool[0x3c];      /* 0x8e0-0x91b: PixMap buffer pool */
    int frame_count;                /* 0x91c: Frame count */
    int src_fourcc;                 /* 0x918: Source FourCC */
    int metadata_type;              /* 0x920: Metadata type */

    /* Extended fields (not part of binary structure) */
    int hw_encoder_fd;              /* Hardware encoder file descriptor */
    HWEncoderParams hw_params;      /* Hardware encoder parameters */
    int use_hardware;               /* Flag: 1=hardware, 0=software */
} AL_CodecEncode;

/* Global codec state */
static void *g_pCodec = NULL;
static pthread_mutex_t g_codec_mutex = PTHREAD_MUTEX_INITIALIZER;
static AL_CodecEncode *g_codec_instances[6] = {NULL};

/**
 * AL_Codec_Encode_SetDefaultParam - based on decompilation at 0x790b8
 * Sets default encoding parameters
 */
int AL_Codec_Encode_SetDefaultParam(void *param) {
    if (param == NULL) {
        LOG_CODEC("SetDefaultParam: NULL param");
        return -1;
    }
    
    /* Clear entire structure */
    memset(param, 0, 0x794);
    
    /* Set default values from decompilation */
    uint8_t *p = (uint8_t*)param;
    
    /* Basic settings */
    *(int32_t*)(p + 0x00) = 0;          /* codec type */
    *(int32_t*)(p + 0x04) = 0;          /* reserved */
    *(int32_t*)(p + 0x14) = 0x188;      /* width default */
    *(int32_t*)(p + 0x1c) = 8;          /* bit depth */
    *(int32_t*)(p + 0x20) = 0x1000001;  /* H264 codec */
    *(int32_t*)(p + 0x24) = 0x32;       /* profile (50 = high) */
    *(int32_t*)(p + 0x34) = 0x1c;       /* level */
    *(int32_t*)(p + 0x30) = 0x40000;    /* bitrate */
    
    /* QP settings */
    *(uint8_t*)(p + 0x38) = 0xff;       /* initial QP */
    *(uint8_t*)(p + 0x39) = 0xff;       /* min QP */
    *(uint8_t*)(p + 0x3f) = 1;          /* enable QP */
    *(uint8_t*)(p + 0x44) = 1;          /* enable rate control */
    
    /* GOP settings */
    *(uint16_t*)(p + 0x4e) = 0xffff;
    *(uint16_t*)(p + 0x50) = 0xffff;
    *(uint16_t*)(p + 0x4a) = 0xffff;
    *(uint16_t*)(p + 0x4c) = 0xffff;
    
    /* Rate control */
    *(uint8_t*)(p + 0x53) = 3;          /* RC mode */
    *(uint16_t*)(p + 0x8a) = 0xffff;
    *(uint16_t*)(p + 0x8c) = 0xffff;
    *(uint8_t*)(p + 0x55) = 2;
    *(int32_t*)(p + 0x90) = 2;
    *(uint8_t*)(p + 0x6a) = 0xf;
    *(uint16_t*)(p + 0x92) = 0xa;
    *(uint16_t*)(p + 0x94) = 0x11;
    
    /* Timing */
    *(int32_t*)(p + 0x7c) = 0xaae60;    /* framerate num */
    *(int32_t*)(p + 0x80) = 0xaae60;    /* framerate den */
    *(int32_t*)(p + 0x9c) = 0x1068;
    
    /* Slice settings */
    *(uint8_t*)(p + 0x52) = 5;
    *(uint8_t*)(p + 0x54) = 5;
    *(int32_t*)(p + 0x74) = 0x41eb0;
    *(uint16_t*)(p + 0x78) = 0x19;
    *(uint16_t*)(p + 0x7a) = 0x3e8;
    *(uint16_t*)(p + 0x84) = 0x19;
    *(uint16_t*)(p + 0x88) = 0x33;
    
    /* Enable flags */
    *(uint8_t*)(p + 0x56) = 1;
    *(uint8_t*)(p + 0x57) = 1;
    *(uint8_t*)(p + 0x58) = 1;
    *(uint8_t*)(p + 0x6c) = 1;
    
    /* GOP parameters */
    *(int32_t*)(p + 0xac) = 2;
    *(int32_t*)(p + 0xb4) = 0x7fffffff;
    *(int32_t*)(p + 0xcc) = 3;          /* buffer count */
    *(int32_t*)(p + 0x100) = 4;
    *(int32_t*)(p + 0xb0) = 0x19;       /* GOP length */
    *(int32_t*)(p + 0xe8) = 5;
    *(int32_t*)(p + 0x104) = 5;
    *(uint8_t*)(p + 0x108) = 1;
    *(uint8_t*)(p + 0x10c) = 1;
    *(uint8_t*)(p + 0x110) = 1;
    *(uint8_t*)(p + 0x116) = 1;
    *(uint8_t*)(p + 0x11c) = 1;
    *(uint8_t*)(p + 0x124) = 1;
    *(uint8_t*)(p + 0x128) = 1;
    
    /* Pixel format */
    strncpy((char*)(p + 0x764), "NV12", 4);
    *(uint8_t*)(p + 0x758) = 1;
    *(uint8_t*)(p + 0x760) = 1;
    *(uint8_t*)(p + 0x768) = 1;
    *(uint8_t*)(p + 0x76c) = 0x10;      /* alignment */
    
    LOG_CODEC("SetDefaultParam: initialized");
    return 0;
}

/**
 * AL_Codec_Encode_GetSrcFrameCntAndSize - based on decompilation at 0x7a694
 * Returns frame buffer count and size
 */
int AL_Codec_Encode_GetSrcFrameCntAndSize(void *codec, int *cnt, int *size) {
    if (codec == NULL || cnt == NULL || size == NULL) {
        return -1;
    }
    
    AL_CodecEncode *enc = (AL_CodecEncode*)codec;
    
    /* From decompilation: offsets 0x840 and 0x8dc */
    *cnt = enc->frame_buf_count;
    *size = enc->frame_buf_size;
    
    return 0;
}

/**
 * AL_Codec_Encode_GetSrcStreamCntAndSize - based on decompilation at 0x7a6ac
 * Returns stream buffer count and size
 */
int AL_Codec_Encode_GetSrcStreamCntAndSize(void *codec, int *cnt, int *size) {
    if (codec == NULL || cnt == NULL || size == NULL) {
        return -1;
    }
    
    AL_CodecEncode *enc = (AL_CodecEncode*)codec;
    
    /* From decompilation: offsets 0x7ac and 0x7b0 */
    *cnt = enc->stream_buf_count;
    *size = enc->stream_buf_size;
    
    return 0;
}

/**
 * AL_Codec_Encode_Create - based on decompilation at 0x7950c
 * Creates a codec encoder instance
 */
int AL_Codec_Encode_Create(void **codec, void *params) {
    if (codec == NULL || params == NULL) {
        LOG_CODEC("Create: NULL parameters");
        return -1;
    }
    
    /* Allocate codec structure using real size */
    AL_CodecEncode *enc = (AL_CodecEncode*)malloc(sizeof(AL_CodecEncode));
    if (enc == NULL) {
        LOG_CODEC("Create: malloc failed");
        return -1;
    }

    memset(enc, 0, sizeof(AL_CodecEncode));

    /* Initialize from parameters */
    enc->g_pCodec = g_pCodec;
    memcpy(enc->codec_param, params, 0x794);
    
    /* Set default buffer counts and sizes */
    enc->frame_buf_count = 4;           /* Default frame buffer count */
    enc->frame_buf_size = 0x100000;     /* 1MB per frame */
    enc->stream_buf_count = 7;          /* Default stream buffer count */
    enc->stream_buf_size = 0x38;        /* Stream buffer size */
    
    /* Allocate and initialize FIFO control structures safely */
    int fifo_size = Fifo_SizeOf();
    enc->fifo_frames = malloc(fifo_size);
    enc->fifo_streams = malloc(fifo_size);
    if (enc->fifo_frames == NULL || enc->fifo_streams == NULL) {
        LOG_CODEC("Create: FIFO alloc failed");
        if (enc->fifo_frames) free(enc->fifo_frames);
        if (enc->fifo_streams) free(enc->fifo_streams);
        free(enc);
        return -1;
    }
    Fifo_Init(enc->fifo_frames, enc->frame_buf_count);
    Fifo_Init(enc->fifo_streams, enc->stream_buf_count);

    /* Set source FourCC to NV12 */
    enc->src_fourcc = 0x3231564e;  /* 'NV12' */
    enc->metadata_type = -1;

    /* Initialize hardware encoder */
    enc->hw_encoder_fd = -1;
    enc->use_hardware = 0;

    /* Extract encoder parameters from codec_param */
    uint8_t *p = enc->codec_param;
    enc->hw_params.codec_type = *(uint32_t*)(p + 0x1f);  /* Codec type at offset 0x1f */
    enc->hw_params.width = *(uint32_t*)(p + 0x14);       /* Width */
    enc->hw_params.height = *(uint32_t*)(p + 0x18);      /* Height */
    enc->hw_params.fps_num = 30;                         /* Default FPS */
    enc->hw_params.fps_den = 1;
    enc->hw_params.gop_length = 30;                      /* Default GOP */
    enc->hw_params.rc_mode = HW_RC_MODE_CBR;             /* Default RC mode */
    enc->hw_params.bitrate = *(uint32_t*)(p + 0x30);     /* Bitrate */
    enc->hw_params.profile = HW_PROFILE_MAIN;            /* Default profile */
    enc->hw_params.qp = 25;                              /* Default QP */
    enc->hw_params.max_qp = 51;
    enc->hw_params.min_qp = 0;

    /* Try to initialize hardware encoder */
    if (HW_Encoder_Init(&enc->hw_encoder_fd, &enc->hw_params) == 0) {
        enc->use_hardware = 1;
        LOG_CODEC("Create: using hardware encoder");
    } else {
        enc->use_hardware = 0;
        LOG_CODEC("Create: using software fallback");
    }

    /* Register in global instances */
    pthread_mutex_lock(&g_codec_mutex);
    for (int i = 0; i < 6; i++) {
        if (g_codec_instances[i] == NULL) {
            g_codec_instances[i] = enc;
            enc->channel_id = i + 1;
            pthread_mutex_unlock(&g_codec_mutex);

            *codec = enc;
            LOG_CODEC("Create: codec=%p, channel=%d", enc, i);
            return 0;
        }
    }
    pthread_mutex_unlock(&g_codec_mutex);
    
    /* No free slots */
    Fifo_Deinit(enc->fifo_frames);
    Fifo_Deinit(enc->fifo_streams);
    free(enc->fifo_frames);
    free(enc->fifo_streams);
    free(enc);
    LOG_CODEC("Create: no free slots");
    return -1;
}

/**
 * AL_Codec_Encode_Destroy - based on decompilation at 0x7a180
 * Destroys a codec encoder instance
 */
int AL_Codec_Encode_Destroy(void *codec) {
    if (codec == NULL) {
        return -1;
    }
    
    AL_CodecEncode *enc = (AL_CodecEncode*)codec;
    
    LOG_CODEC("Destroy: codec=%p, channel=%d", codec, enc->channel_id - 1);

    /* Deinitialize hardware encoder */
    if (enc->hw_encoder_fd >= 0) {
        HW_Encoder_Deinit(enc->hw_encoder_fd);
        enc->hw_encoder_fd = -1;
    }

    /* Unregister from global instances */
    pthread_mutex_lock(&g_codec_mutex);
    for (int i = 0; i < 6; i++) {
        if (g_codec_instances[i] == enc) {
            g_codec_instances[i] = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&g_codec_mutex);

    /* Deinitialize FIFOs */
    Fifo_Deinit(enc->fifo_frames);
    Fifo_Deinit(enc->fifo_streams);

    /* Free FIFO control blocks */
    free(enc->fifo_frames);
    free(enc->fifo_streams);

    /* Free codec structure */
    free(enc);

    return 0;
}

/**
 * AL_Codec_Encode_Process - based on decompilation at 0x7a334
 * Process a frame for encoding
 */
int AL_Codec_Encode_Process(void *codec, void *frame, void *user_data) {
    if (codec == NULL) {
        return -1;
    }

    AL_CodecEncode *enc = (AL_CodecEncode*)codec;

    (void)user_data;

    if (frame == NULL) {
        /* NULL frame means flush */
        LOG_CODEC("Process: flush requested");
        return 0;
    }

    /* Encode frame using hardware or software */
    HWStreamBuffer *hw_stream = (HWStreamBuffer*)malloc(sizeof(HWStreamBuffer));
    if (hw_stream == NULL) {
        LOG_CODEC("Process: failed to allocate stream buffer");
        return -1;
    }

    /* Extract frame data from VBM frame structure */
    /* VBMFrame structure layout (0x428 bytes):
     * 0x00: index
     * 0x04: chn
     * 0x08: width
     * 0x0c: height
     * 0x10: pixfmt
     * 0x14: size
     * 0x18: phys_addr
     * 0x1c: virt_addr
     * 0x20-0x427: data
     */
    uint8_t *frame_bytes = (uint8_t*)frame;
    uint32_t width, height, pixfmt, size, phys_addr, virt_addr;

    memcpy(&width, frame_bytes + 0x08, sizeof(uint32_t));
    memcpy(&height, frame_bytes + 0x0c, sizeof(uint32_t));
    memcpy(&pixfmt, frame_bytes + 0x10, sizeof(uint32_t));
    memcpy(&size, frame_bytes + 0x14, sizeof(uint32_t));
    memcpy(&phys_addr, frame_bytes + 0x18, sizeof(uint32_t));
    memcpy(&virt_addr, frame_bytes + 0x1c, sizeof(uint32_t));

    /* Get current timestamp */
    uint64_t timestamp = IMP_System_GetTimeStamp();

    if (enc->use_hardware && enc->hw_encoder_fd >= 0) {
        /* Use hardware encoder */
        HWFrameBuffer hw_frame;
        memset(&hw_frame, 0, sizeof(HWFrameBuffer));

        /* Use actual frame data from VBM */
        hw_frame.phys_addr = phys_addr;
        hw_frame.virt_addr = virt_addr;
        hw_frame.size = size;
        hw_frame.width = width;
        hw_frame.height = height;
        hw_frame.pixfmt = pixfmt;
        hw_frame.timestamp = timestamp;

        LOG_CODEC("Process: HW encode frame %ux%u, phys=0x%x, virt=0x%x, size=%u",
                  width, height, phys_addr, virt_addr, size);

        /* Submit frame for encoding */
        if (HW_Encoder_Encode(enc->hw_encoder_fd, &hw_frame) < 0) {
            LOG_CODEC("Process: hardware encoding failed");
            free(hw_stream);
            return -1;
        }

        /* Get encoded stream */
        if (HW_Encoder_GetStream(enc->hw_encoder_fd, hw_stream, 100) < 0) {
            LOG_CODEC("Process: failed to get stream from hardware");
            free(hw_stream);
            return -1;
        }
    } else {
        /* Use software fallback */
        HWFrameBuffer hw_frame;
        memset(&hw_frame, 0, sizeof(HWFrameBuffer));
        hw_frame.phys_addr = phys_addr;
        hw_frame.virt_addr = virt_addr;
        hw_frame.width = width;
        hw_frame.height = height;
        hw_frame.timestamp = timestamp;

        LOG_CODEC("Process: SW encode frame %ux%u", width, height);

        if (HW_Encoder_Encode_Software(&hw_frame, hw_stream) < 0) {
            LOG_CODEC("Process: software encoding failed");
            free(hw_stream);
            return -1;
        }
    }

    /* Queue encoded stream to FIFO */
    if (Fifo_Queue(enc->fifo_streams, hw_stream, -1) == 0) {
        LOG_CODEC("Process: failed to queue stream");
        free(hw_stream);
        return -1;
    }

    LOG_CODEC("Process: encoded and queued stream, length=%u", hw_stream->length);
    return 0;
}

/**
 * AL_Codec_Encode_GetStream - based on decompilation at 0x7a548
 * Get an encoded stream
 */
int AL_Codec_Encode_GetStream(void *codec, void **stream) {
    if (codec == NULL || stream == NULL) {
        return -1;
    }

    AL_CodecEncode *enc = (AL_CodecEncode*)codec;

    /* Dequeue stream from FIFO (wait indefinitely) */
    void *s = Fifo_Dequeue(enc->fifo_streams, -1);
    if (s == NULL) {
        return -1;
    }

    *stream = s;
    LOG_CODEC("GetStream: got stream %p", s);
    return 0;
}

/**
 * AL_Codec_Encode_ReleaseStream - based on decompilation at 0x7a624
 * Release an encoded stream
 */
int AL_Codec_Encode_ReleaseStream(void *codec, void *stream, void *user_data) {
    if (codec == NULL || stream == NULL) {
        return -1;
    }

    AL_CodecEncode *enc = (AL_CodecEncode*)codec;

    /* Based on decompilation at 0x7a624:
     * AL_Buffer_Unref(arg3)
     * AL_Encoder_PutStreamBuffer(*(arg1 + 0x798), arg2)
     * AL_Buffer_Unref(arg2)
     */

    /* Unref user_data buffer if provided */
    if (user_data != NULL) {
        /* AL_Buffer_Unref(user_data) - would decrement reference count */
        (void)user_data;
    }

    /* Return stream buffer to pool via FIFO */
    /* AL_Encoder_PutStreamBuffer at offset 0x798 */
    if (enc->fifo_streams != NULL) {
        Fifo_Queue(enc->fifo_streams, stream, 0);
    }

    /* Unref stream buffer */
    /* AL_Buffer_Unref(stream) - would decrement reference count and free if 0 */

    LOG_CODEC("ReleaseStream: returned stream %p to pool", stream);

    return 0;
}

