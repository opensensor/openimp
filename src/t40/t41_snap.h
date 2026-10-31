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

/*
 * A buffer the driver completed long ago and nobody dequeued.  With the
 * stream kept running (fs_keepalive) and no consumer, every capture buffer
 * fills once and stays "done": the first DQBUF after the idle period hands
 * out the oldest of them, a picture minutes old (day colours in a night
 * scene).  age_us = now - the buffer's completion time; interval_us = the
 * channel's frame interval.  Such a buffer is requeued and the next one
 * taken; a negative age (clock skew) is never stale.
 */
static inline int t41_frame_is_stale(int64_t age_us, uint32_t interval_us)
{
    int64_t limit = (int64_t)interval_us * 6;

    if (limit < 300000)
        limit = 300000;
    return age_us > limit;
}

/*
 * T40: the frame-channel DQBUF has no completion time (wire words 5/6 are 0),
 * so the age is bounded from the QBUF time instead.  The driver fills the
 * queued buffers in order, one per frame: a buffer is done at the latest
 * buffer_count frame intervals after its QBUF.  A buffer queued longer ago
 * than that plus the t41_frame_is_stale limit completed more than the limit
 * ago.  queued_us 0 = unknown (never stale).
 */
static inline int t40_buffer_is_stale(int64_t now_us, int64_t queued_us,
                                      uint32_t buffer_count,
                                      uint32_t interval_us)
{
    if (!queued_us)
        return 0;
    return t41_frame_is_stale(now_us - queued_us -
                              (int64_t)buffer_count * interval_us,
                              interval_us);
}

/*
 * T40 measured frame interval (#148).  The stale bounds above assume one
 * frame per interval; the configured fps is only a lower bound for that
 * interval (a sensor in a long-exposure night mode may run slower).  est_us
 * follows the time between successive DQBUFs of the channel (1/8 EWMA);
 * deltas below half the configured interval (a backlog drained back to
 * back) or above 1 s (a pause, not a frame interval) are ignored.  The
 * interval the bounds use is never shorter than the configured one.
 */
static inline void t40_interval_note(uint32_t *est_us, int64_t delta_us,
                                     uint32_t config_us)
{
    if (delta_us < (int64_t)(config_us / 2u) || delta_us > 1000000)
        return;
    if (!*est_us)
        *est_us = (uint32_t)delta_us;
    else
        *est_us = (uint32_t)(((int64_t)*est_us * 7 + delta_us) / 8);
}

static inline uint32_t t40_interval_bound(uint32_t config_us, uint32_t est_us)
{
    return est_us > config_us ? est_us : config_us;
}

/*
 * T40 consumer-gap flush mark (#168, #148).  A consumer that was away
 * longer than buffer_count intervals plus the stale limit gets only frames
 * from buffers queued after the gap was seen: buffers queued before the
 * returned mark are dropped.  The mark is latched: it stays the time the
 * gap was first seen until a frame is returned (the caller then clears it),
 * so a consumer that probes in short slices (the encoder's 20 ms WaitFrame)
 * gets the buffers requeued after the mark once the sensor fills them.
 * Re-arming it at every call dropped each buffer exactly when it was filled
 * (~2 intervals after its QBUF: 100 ms at 20 fps with 3 buffers) and never
 * returned a frame.  A mark older than the stale bound itself (channel
 * disabled meanwhile, sensor stopped) is renewed.  Returns the mark to use
 * (0 = no gap).
 */
static inline int64_t t40_gap_flush_mark(int64_t now_us, int64_t last_frame_us,
                                         int64_t mark_us,
                                         uint32_t buffer_count,
                                         uint32_t interval_us)
{
    int64_t ahead = (int64_t)buffer_count * interval_us;

    if (mark_us && !t41_frame_is_stale(now_us - mark_us - ahead, interval_us))
        return mark_us;
    if (last_frame_us &&
        t41_frame_is_stale(now_us - last_frame_us - ahead, interval_us))
        return now_us;
    return 0;
}

/*
 * T40 3A feed: the driver's frame 3A (AE/AWB/day-night) samples completed
 * frames, and the frame channels complete a frame only into a buffer queued
 * to the driver.  A stream kept open without a consumer (timps
 * fs_keepalive=1, no client) fills every buffer, the MSCA stalls and the AE
 * freezes.  The vendor capture thread keeps dequeueing (drop-oldest).  The
 * feeder therefore recycles the frames of ONE idle channel - the 3A channel,
 * the lowest enabled one (the driver meters channel 0 and falls back to
 * channel 1 only while channel 0 is idle) - at the channel's frame rate.
 * enabled_mask: bit n = channel n enabled.  Returns 1 if channel is it.
 */
static inline int t40_3a_feed_channel(int channel, uint32_t enabled_mask)
{
    int c;

    if (channel < 0 || channel >= 32 || !(enabled_mask & (1u << channel)))
        return 0;
    for (c = 0; c < channel; c++)
        if (enabled_mask & (1u << c))
            return 0;
    return 1;
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
