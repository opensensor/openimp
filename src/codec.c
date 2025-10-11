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
#include "fifo.h"

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
    
    /* FIFOs - offsets from decompilation at 0x79ab0 */
    uint8_t fifo_frames[64];        /* 0x7f8: Frame FIFO */
    uint8_t fifo_streams[64];       /* 0x81c: Stream FIFO (0x7f8 + 0x24 = 0x81c) */
    
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
    
    /* Allocate codec structure (0x924 bytes from decompilation) */
    AL_CodecEncode *enc = (AL_CodecEncode*)malloc(0x924);
    if (enc == NULL) {
        LOG_CODEC("Create: malloc failed");
        return -1;
    }
    
    memset(enc, 0, 0x924);
    
    /* Initialize from parameters */
    enc->g_pCodec = g_pCodec;
    memcpy(enc->codec_param, params, 0x794);
    
    /* Set default buffer counts and sizes */
    enc->frame_buf_count = 4;           /* Default frame buffer count */
    enc->frame_buf_size = 0x100000;     /* 1MB per frame */
    enc->stream_buf_count = 7;          /* Default stream buffer count */
    enc->stream_buf_size = 0x38;        /* Stream buffer size */
    
    /* Initialize FIFOs (from decompilation at 0x79ab0) */
    Fifo_Init(enc->fifo_frames, enc->frame_buf_count);
    Fifo_Init(enc->fifo_streams, enc->stream_buf_count);
    
    /* Set source FourCC to NV12 */
    enc->src_fourcc = 0x3231564e;  /* 'NV12' */
    enc->metadata_type = -1;
    
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

    /* Queue frame to FIFO for processing */
    if (Fifo_Queue(enc->fifo_frames, frame, -1) == 0) {
        LOG_CODEC("Process: failed to queue frame");
        return -1;
    }

    LOG_CODEC("Process: queued frame %p", frame);
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

    (void)user_data;

    LOG_CODEC("ReleaseStream: released stream %p", stream);

    /* Stream is released back to pool */
    /* In real implementation, this would return buffer to pool */

    return 0;
}

