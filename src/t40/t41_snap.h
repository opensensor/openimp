/* T41 IMP_FrameSource_SnapFrame helpers: the frame lend state machine and
 * the NV12 copy.  Kept free of driver state for the host test. */
#ifndef OPENIMP_T41_SNAP_H
#define OPENIMP_T41_SNAP_H

#include <stdint.h>
#include <string.h>

/*
 * T41 has no capture thread: a frame leaves the driver only when a consumer
 * (encoder, IVS feeder, application) dequeues it.  A snapshot must not take
 * a frame away from the encoder, so it borrows the next frame some consumer
 * dequeues: the dequeue marks that buffer held, the consumer's release is
 * deferred while the snapshot copies, and the snapshot finish requeues it.
 */
struct t41_snap_state {
    uint32_t waiting;          /* a SnapFrame caller wants the next frame */
    uint32_t ready;            /* held buffer below is valid */
    uint32_t index;            /* held buffer */
    uint32_t deferred;         /* the consumer released it while held */
};

/* Dequeue of buffer index: 1 when the snapshot takes (holds) it. */
static inline int t41_snap_on_dequeue(struct t41_snap_state *s, uint32_t index)
{
    if (!s->waiting || s->ready)
        return 0;
    s->ready = 1;
    s->index = index;
    s->deferred = 0;
    return 1;
}

/* Consumer release of buffer index: 1 = deferred (do not QBUF now),
 * -1 = second release of a held buffer, 0 = normal release. */
static inline int t41_snap_on_release(struct t41_snap_state *s, uint32_t index)
{
    if (!s->ready || s->index != index)
        return 0;
    if (s->deferred)
        return -1;
    s->deferred = 1;
    return 1;
}

/* Snapshot done: 1 when the held buffer must be requeued now (its consumer
 * already released it), 0 when the consumer still owns it. */
static inline int t41_snap_finish(struct t41_snap_state *s)
{
    int requeue = s->ready && s->deferred;

    s->waiting = 0;
    s->ready = 0;
    s->deferred = 0;
    return requeue;
}

static inline uint32_t t41_snap_align16(uint32_t v)
{
    return (v + 15u) & ~15u;
}

/* Bytes of the packed NV12 picture SnapFrame returns (vendor: the channel
 * resolution, NV12). */
static inline uint32_t t41_snap_nv12_bytes(uint32_t width, uint32_t height)
{
    return width * height + width * (height / 2u);
}

/*
 * The T41 capture buffer is NV12 with an ALIGN16(width) pitch and
 * ALIGN16(height) luma lines (codec-t40.c avpu_get_nv12_src_uv_offset);
 * the snapshot is packed (pitch = width, UV right after width * height).
 * Returns 0, or -1 when the source buffer is too small for that layout.
 */
static inline int t41_snap_copy_nv12_pitch(void *dst, const void *src,
                                           uint32_t src_size, uint32_t width,
                                           uint32_t height, uint32_t src_pitch)
{
    const uint8_t *in = src;
    uint8_t *out = dst;
    uint32_t pitch = src_pitch ? src_pitch : t41_snap_align16(width);
    uint32_t lines = t41_snap_align16(height);
    uint32_t uv = pitch * lines;
    uint32_t y;

    if (!dst || !src || !width || !height || (width & 1u) || (height & 1u) ||
        (src_pitch && src_pitch < width) ||
        width > 8192u || height > 8192u ||
        src_size < uv + pitch * (height / 2u))
        return -1;
    for (y = 0; y < height; ++y)
        memcpy(out + y * width, in + y * pitch, width);
    out += width * height;
    for (y = 0; y < height / 2u; ++y)
        memcpy(out + y * width, in + uv + y * pitch, width);
    return 0;
}

/* src_pitch 0: the capture layout (ALIGN16(width)); an I2D output passes
 * its own pitch (p1_i2d.h). */
static inline int t41_snap_copy_nv12(void *dst, const void *src,
                                     uint32_t src_size, uint32_t width,
                                     uint32_t height)
{
    return t41_snap_copy_nv12_pitch(dst, src, src_size, width, height, 0u);
}

#endif
