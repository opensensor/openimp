#ifndef OPENIMP_VIDEO_ENCODER_CHANNEL_LAYOUT_H
#define OPENIMP_VIDEO_ENCODER_CHANNEL_LAYOUT_H

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
#include <semaphore.h>

/*
 * Canonical encoder-channel ABI view for the ported T31 encoder path.
 *
 * The goal is to stop writing ad-hoc raw offsets in multiple files. Unknown
 * regions remain explicit padding, but every named field below is checked
 * against the OEM offset at compile time.
 *
 * pthread/semaphore objects are represented as fixed-size opaque blobs so the
 * struct layout stays target-stable even when building on a host toolchain
 * with different libc object sizes.
 */

typedef struct {
    uint8_t bytes[0x10];
} EncSemLayout;

typedef struct {
    uint8_t bytes[0x18];
} EncMutexLayout;

typedef struct {
    uint8_t bytes[0x30];
} EncCondLayout;

static inline sem_t *enc_sem_ptr(EncSemLayout *sem)
{
    return (sem_t *)(void *)sem;
}

static inline pthread_mutex_t *enc_mutex_ptr(EncMutexLayout *mutex)
{
    return (pthread_mutex_t *)(void *)mutex;
}

static inline pthread_cond_t *enc_cond_ptr(EncCondLayout *cond)
{
    return (pthread_cond_t *)(void *)cond;
}

static inline void *enc_u32_as_ptr(uint32_t raw)
{
    return (void *)(uintptr_t)raw;
}

static inline uint32_t enc_ptr_as_u32(const void *ptr)
{
    return (uint32_t)(uintptr_t)ptr;
}

typedef struct EncoderRcAttrLayout {
    uint32_t rc_mode;               /* 0x2c in attr blob */
    union {
        struct {
            int16_t qp;
        } fixqp;
        struct {
            uint32_t target_bitrate;
            int16_t initial_qp;
            int16_t min_qp;
            int16_t max_qp;
            int16_t ip_delta;
            int16_t pb_delta;
            uint16_t _pad_0e;
            uint32_t rc_options;
            uint32_t max_picture_size;
        } cbr;
        struct {
            uint32_t target_bitrate;
            uint32_t max_bitrate;
            int16_t initial_qp;
            int16_t min_qp;
            int16_t max_qp;
            int16_t ip_delta;
            int16_t pb_delta;
            uint16_t _pad_10;
            uint32_t rc_options;
            uint32_t max_picture_size;
            uint16_t max_psnr;
        } vbr;
    } u;
} EncoderRcAttrLayout;

typedef struct EncoderAttrLayout {
    uint32_t profile;               /* 0x00 */
    uint8_t level;                  /* 0x04 */
    uint8_t tier;                   /* 0x05 */
    uint16_t width;                 /* 0x06 */
    uint16_t height;                /* 0x08 */
    uint16_t _pad_0a;
    uint32_t pic_format;            /* 0x0c */
    uint32_t enc_options;           /* 0x10 */
    uint32_t enc_tools;             /* 0x14 */
    uint8_t _pad_18[0x14];
    EncoderRcAttrLayout rc;         /* 0x2c */
    uint32_t out_fps_num;           /* 0x50 */
    uint32_t out_fps_den;           /* 0x54 */
    uint32_t gop_ctrl_mode;         /* 0x58 */
    uint16_t gop_length;            /* 0x5c */
    uint8_t notify_user_lt_inter;   /* 0x5e */
    uint8_t _pad_5f;
    uint32_t max_same_scene_cnt;    /* 0x60 */
    uint8_t enable_lt;              /* 0x64 */
    uint8_t _pad_65[0x03];
    uint32_t freq_lt;               /* 0x68 */
    uint8_t ltrc;                   /* 0x6c */
    uint8_t _pad_6d[0x03];
} EncoderAttrLayout;

typedef struct EncoderChannelLayout {
    int32_t chn_id;                       /* 0x000 */
    uint32_t group_ptr;                   /* 0x004 */
    uint32_t codec_handle;                /* 0x008 */
    int32_t src_frame_cnt;                /* 0x00c */
    int32_t src_frame_size;               /* 0x010 */
    uint32_t src_frame_array;             /* 0x014 */
    uint8_t public_stream_fifo[0x080];    /* 0x018 */
    uint8_t attr[0x070];                  /* 0x098 */
    uint8_t registered;                   /* 0x108 */
    uint8_t _pad_109[0x067];
    uint32_t enabled;                     /* 0x170 */
    uint32_t started;                     /* 0x174 */
    EncSemLayout frame_slot_sem;          /* 0x178 */
    EncSemLayout stream_sem_ready;        /* 0x188 */
    EncSemLayout stream_sem_avail;        /* 0x198 */
    EncMutexLayout queue_mutex;           /* 0x1a8 */
    EncMutexLayout stream_mutex;          /* 0x1c0 */
    EncMutexLayout signal_mutex;          /* 0x1d8 */
    EncCondLayout stream_cond;            /* 0x1f0 */
    int32_t eventfd;                      /* 0x220 */
    uint8_t _pad_224[0x004];
    uint32_t stream_ready_lo;             /* 0x228 */
    uint32_t stream_ready_hi;             /* 0x22c */
    int32_t stream_ring_count;            /* 0x230 */
    int32_t stream_capacity_bytes;        /* 0x234 */
    int32_t fisheye;                      /* 0x238 */
    uint8_t _pad_23c[0x008];
    uint32_t stream_ring_base;            /* 0x244 */
    uint32_t stream_ring_index;           /* 0x248 */
    uint8_t _pad_24c[0x004];
    uint32_t resize_frame_slot;           /* 0x250 */
    uint32_t resize_tmp_flag;             /* 0x254 */
    uint32_t resize_tmp_slot;             /* 0x258 */
    uint32_t resize_user_slot;            /* 0x25c */
    uint32_t resize_fn_slot;              /* 0x260 */
    uint8_t _pad_264[0x008];
    uint32_t resize_mode;                 /* 0x26c */
    uint8_t _pad_270[0x038];
    uint32_t bufshare_chn;                /* 0x2a8 */
    uint8_t _pad_2ac[0x01c];
    uint32_t bufshare_p1;                 /* 0x2c8 */
    uint32_t bufshare_p2;                 /* 0x2cc */
    uint32_t bufshare_p3;                 /* 0x2d0 */
    uint8_t _pad_2d4[0x034];
} EncoderChannelLayout;

typedef struct EncoderCompatRuntime {
    EncSemLayout release_sem;
    EncMutexLayout release_mutex;
    EncMutexLayout submit_mutex;
    EncCondLayout submit_cond;
    void *release_frame;
    void *pending_frame;
    int32_t release_num;
    int32_t release_den;
    pthread_t release_thread;
    pthread_t submit_thread;
    pthread_t update_thread;
    int release_valid;
    int submit_valid;
    int update_valid;
} EncoderCompatRuntime;

#define ENC_LAYOUT_ASSERT(field, off) \
    _Static_assert(offsetof(EncoderChannelLayout, field) == (off), \
                   "EncoderChannelLayout offset mismatch: " #field)

_Static_assert(sizeof(EncSemLayout) == 0x10, "EncSemLayout size mismatch");
_Static_assert(sizeof(EncMutexLayout) == 0x18, "EncMutexLayout size mismatch");
_Static_assert(sizeof(EncCondLayout) == 0x30, "EncCondLayout size mismatch");
ENC_LAYOUT_ASSERT(chn_id, 0x000);
ENC_LAYOUT_ASSERT(group_ptr, 0x004);
ENC_LAYOUT_ASSERT(codec_handle, 0x008);
ENC_LAYOUT_ASSERT(src_frame_cnt, 0x00c);
ENC_LAYOUT_ASSERT(src_frame_size, 0x010);
ENC_LAYOUT_ASSERT(src_frame_array, 0x014);
ENC_LAYOUT_ASSERT(attr, 0x098);
ENC_LAYOUT_ASSERT(registered, 0x108);
ENC_LAYOUT_ASSERT(enabled, 0x170);
ENC_LAYOUT_ASSERT(started, 0x174);
ENC_LAYOUT_ASSERT(frame_slot_sem, 0x178);
ENC_LAYOUT_ASSERT(stream_sem_ready, 0x188);
ENC_LAYOUT_ASSERT(stream_sem_avail, 0x198);
ENC_LAYOUT_ASSERT(queue_mutex, 0x1a8);
ENC_LAYOUT_ASSERT(stream_mutex, 0x1c0);
ENC_LAYOUT_ASSERT(signal_mutex, 0x1d8);
ENC_LAYOUT_ASSERT(stream_cond, 0x1f0);
ENC_LAYOUT_ASSERT(eventfd, 0x220);
ENC_LAYOUT_ASSERT(stream_ready_lo, 0x228);
ENC_LAYOUT_ASSERT(stream_ready_hi, 0x22c);
ENC_LAYOUT_ASSERT(stream_ring_count, 0x230);
ENC_LAYOUT_ASSERT(stream_capacity_bytes, 0x234);
ENC_LAYOUT_ASSERT(resize_frame_slot, 0x250);
ENC_LAYOUT_ASSERT(resize_tmp_slot, 0x258);
ENC_LAYOUT_ASSERT(resize_user_slot, 0x25c);
ENC_LAYOUT_ASSERT(resize_fn_slot, 0x260);
ENC_LAYOUT_ASSERT(resize_mode, 0x26c);

_Static_assert(sizeof(EncoderAttrLayout) == 0x70, "EncoderAttrLayout size mismatch");
_Static_assert(offsetof(EncoderAttrLayout, profile) == 0x00, "EncoderAttrLayout profile offset mismatch");
_Static_assert(offsetof(EncoderAttrLayout, level) == 0x04, "EncoderAttrLayout level offset mismatch");
_Static_assert(offsetof(EncoderAttrLayout, tier) == 0x05, "EncoderAttrLayout tier offset mismatch");
_Static_assert(offsetof(EncoderAttrLayout, width) == 0x06, "EncoderAttrLayout width offset mismatch");
_Static_assert(offsetof(EncoderAttrLayout, height) == 0x08, "EncoderAttrLayout height offset mismatch");
_Static_assert(offsetof(EncoderAttrLayout, pic_format) == 0x0c, "EncoderAttrLayout pic_format offset mismatch");
_Static_assert(offsetof(EncoderAttrLayout, enc_options) == 0x10, "EncoderAttrLayout enc_options offset mismatch");
_Static_assert(offsetof(EncoderAttrLayout, enc_tools) == 0x14, "EncoderAttrLayout enc_tools offset mismatch");
_Static_assert(offsetof(EncoderAttrLayout, rc) == 0x2c, "EncoderAttrLayout rc offset mismatch");
_Static_assert(offsetof(EncoderAttrLayout, out_fps_num) == 0x50, "EncoderAttrLayout out_fps_num offset mismatch");
_Static_assert(offsetof(EncoderAttrLayout, out_fps_den) == 0x54, "EncoderAttrLayout out_fps_den offset mismatch");
_Static_assert(offsetof(EncoderAttrLayout, gop_ctrl_mode) == 0x58, "EncoderAttrLayout gop_ctrl_mode offset mismatch");
_Static_assert(offsetof(EncoderAttrLayout, gop_length) == 0x5c, "EncoderAttrLayout gop_length offset mismatch");
_Static_assert(offsetof(EncoderAttrLayout, notify_user_lt_inter) == 0x5e, "EncoderAttrLayout notify_user_lt_inter offset mismatch");
_Static_assert(offsetof(EncoderAttrLayout, max_same_scene_cnt) == 0x60, "EncoderAttrLayout max_same_scene_cnt offset mismatch");
_Static_assert(offsetof(EncoderAttrLayout, enable_lt) == 0x64, "EncoderAttrLayout enable_lt offset mismatch");
_Static_assert(offsetof(EncoderAttrLayout, freq_lt) == 0x68, "EncoderAttrLayout freq_lt offset mismatch");
_Static_assert(offsetof(EncoderAttrLayout, ltrc) == 0x6c, "EncoderAttrLayout ltrc offset mismatch");

static inline EncoderAttrLayout *enc_channel_attr(EncoderChannelLayout *chn)
{
    return (EncoderAttrLayout *)(void *)chn->attr;
}

static inline const EncoderAttrLayout *enc_channel_attr_const(const EncoderChannelLayout *chn)
{
    return (const EncoderAttrLayout *)(const void *)chn->attr;
}

static inline void *enc_channel_codec(const EncoderChannelLayout *chn)
{
    return enc_u32_as_ptr(chn->codec_handle);
}

static inline uint8_t *enc_channel_active_flag(EncoderChannelLayout *chn)
{
    return (uint8_t *)((uint8_t *)chn + 0x108);
}

static inline int32_t *enc_channel_stream_cookie(EncoderChannelLayout *chn)
{
    return (int32_t *)((uint8_t *)chn + 0x70);
}

static inline uint32_t *enc_channel_enabled_word(EncoderChannelLayout *chn)
{
    return &chn->enabled;
}

static inline uint32_t *enc_channel_started_word(EncoderChannelLayout *chn)
{
    return &chn->started;
}

static inline void **enc_channel_resize_frame_slot(EncoderChannelLayout *chn)
{
    return (void **)((uint8_t *)chn + 0x250);
}

static inline void **enc_channel_resize_tmp_slot(EncoderChannelLayout *chn)
{
    return (void **)((uint8_t *)chn + 0x258);
}

static inline void **enc_channel_resize_user_slot(EncoderChannelLayout *chn)
{
    return (void **)((uint8_t *)chn + 0x25c);
}

static inline void (**enc_channel_resize_fn_slot(EncoderChannelLayout *chn))(void)
{
    return (void (**)(void))((uint8_t *)chn + 0x260);
}

static inline uint8_t *enc_channel_resize_mode(EncoderChannelLayout *chn)
{
    return (uint8_t *)((uint8_t *)chn + 0x26c);
}

static inline sem_t *enc_channel_frame_slot_sem(EncoderChannelLayout *chn)
{
    return (sem_t *)(void *)((uint8_t *)chn + 0x178);
}

static inline uint32_t *enc_channel_stream_ready_lo(EncoderChannelLayout *chn)
{
    return (uint32_t *)((uint8_t *)chn + 0x228);
}

static inline uint32_t *enc_channel_stream_ready_hi(EncoderChannelLayout *chn)
{
    return (uint32_t *)((uint8_t *)chn + 0x22c);
}

static inline uint8_t *enc_channel_recv_pic_enabled(EncoderChannelLayout *chn)
{
    return (uint8_t *)enc_channel_enabled_word(chn);
}

static inline uint8_t *enc_channel_recv_pic_started(EncoderChannelLayout *chn)
{
    return (uint8_t *)enc_channel_started_word(chn);
}

static inline int32_t *enc_channel_stream_capacity_bytes(EncoderChannelLayout *chn)
{
    return (int32_t *)((uint8_t *)chn + 0x234);
}

static inline int32_t *enc_channel_stream_ring_base(EncoderChannelLayout *chn)
{
    return (int32_t *)((uint8_t *)chn + 0x244);
}

static inline int32_t *enc_channel_stream_ring_index(EncoderChannelLayout *chn)
{
    return (int32_t *)((uint8_t *)chn + 0x248);
}

static inline int32_t *enc_channel_stream_ring_count(EncoderChannelLayout *chn)
{
    return (int32_t *)((uint8_t *)chn + 0x230);
}

static inline int32_t *enc_channel_ltr_frame_index(EncoderChannelLayout *chn)
{
    return (int32_t *)((uint8_t *)chn + 0x2d8);
}

static inline int32_t *enc_channel_ltr_gop_index(EncoderChannelLayout *chn)
{
    return (int32_t *)((uint8_t *)chn + 0x2dc);
}

#endif
