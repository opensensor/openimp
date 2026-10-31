/* T41 SnapFrame: frame lend state machine and the NV12 de-pitch copy. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "t40/t41_snap.h"

static void lend(void)
{
    struct t41_snap_state s = {0, 0, 0, 0};

    /* Nobody waiting: dequeues and releases are untouched. */
    assert(!t41_snap_on_dequeue(&s, 1));
    assert(!t41_snap_on_release(&s, 1));
    /* Encoder dequeues buffer 2 while a snapshot waits: lent, and the
     * encoder's release is deferred, not refused and not queued. */
    s.waiting = 1;
    assert(t41_snap_on_dequeue(&s, 2) == 1);
    assert(!t41_snap_on_dequeue(&s, 3));          /* one frame only */
    assert(!t41_snap_on_release(&s, 3));          /* other buffers normal */
    assert(t41_snap_on_release(&s, 2) == 1);
    assert(t41_snap_on_release(&s, 2) == -1);     /* double release */
    assert(t41_snap_finish(&s) == 1);             /* snapshot requeues it */
    assert(!s.waiting && !s.ready && !s.deferred);
    /* Copy done before the encoder releases: the encoder requeues. */
    s.waiting = 1;
    assert(t41_snap_on_dequeue(&s, 0) == 1);
    assert(t41_snap_finish(&s) == 0);
    assert(!t41_snap_on_release(&s, 0));
    /* Timed out without a frame. */
    s.waiting = 1;
    assert(t41_snap_finish(&s) == 0);
}

static void copy(void)
{
    const uint32_t w = 20, h = 10, pitch = 32, lines = 16;
    uint32_t size = pitch * lines + pitch * (h / 2), x, y;
    uint8_t *src = malloc(size), *dst = malloc(w * h * 3 / 2 + 1);

    assert(src && dst);
    memset(src, 0xee, size);
    for (y = 0; y < h; ++y)
        for (x = 0; x < w; ++x)
            src[y * pitch + x] = (uint8_t)(y * 16 + x);
    for (y = 0; y < h / 2; ++y)
        for (x = 0; x < w; ++x)
            src[pitch * lines + y * pitch + x] = (uint8_t)(0x80 + y * 16 + x);
    dst[w * h * 3 / 2] = 0x5a;
    assert(t41_snap_nv12_bytes(w, h) == w * h * 3 / 2);
    assert(!t41_snap_copy_nv12(dst, src, size, w, h));
    for (y = 0; y < h; ++y)
        for (x = 0; x < w; ++x)
            assert(dst[y * w + x] == (uint8_t)(y * 16 + x));
    for (y = 0; y < h / 2; ++y)
        for (x = 0; x < w; ++x)
            assert(dst[w * h + y * w + x] == (uint8_t)(0x80 + y * 16 + x));
    assert(dst[w * h * 3 / 2] == 0x5a);           /* no overrun */
    /* A buffer too small for the pitched layout, odd sizes: refused. */
    assert(t41_snap_copy_nv12(dst, src, size - 1, w, h) == -1);
    assert(t41_snap_copy_nv12(dst, src, size, w, h - 1) == -1);
    assert(t41_snap_copy_nv12(dst, src, size, 0, h) == -1);
    assert(t41_snap_copy_nv12(NULL, src, size, w, h) == -1);
    /* 1920x1080: 1088 luma lines, UV at 1920 * 1088. */
    assert(t41_snap_align16(1080) == 1088 && t41_snap_align16(1920) == 1920);
    free(src);
    free(dst);
}

static void stale(void)
{
    assert(!t41_frame_is_stale(0, 33333));
    assert(!t41_frame_is_stale(-5000000, 33333));
    assert(!t41_frame_is_stale(299999, 33333));
    assert(t41_frame_is_stale(300001, 33333));
    assert(t41_frame_is_stale(120000000, 33333));   /* minutes old */
    /* slow channel (5 fps): six intervals */
    assert(!t41_frame_is_stale(1100000, 200000));
    assert(t41_frame_is_stale(1300000, 200000));
    /* T40: queued-age bound = buffer_count intervals + the limit */
    assert(!t40_buffer_is_stale(10000000, 0, 4, 50000));           /* unknown */
    assert(!t40_buffer_is_stale(10000000, 9900000, 4, 50000));     /* 0.1 s */
    assert(!t40_buffer_is_stale(10000000, 9500001, 4, 50000));     /* 0.5 s: 4*50 ms + 300 ms */
    assert(t40_buffer_is_stale(10000000, 9499999, 4, 50000));
    assert(t40_buffer_is_stale(100000000, 5000000, 4, 50000));     /* minutes */
    assert(!t40_buffer_is_stale(1000, 2000, 4, 50000));            /* clock skew */
    /* #148 (.184 dark scene): 20 fps, 3 buffers, a buffer handed back
     * 100 ms after its QBUF is a fresh frame, not a stale one */
    assert(!t40_buffer_is_stale(10000000, 9900000, 3, 50000));
    assert(!t40_buffer_is_stale(10000000, 9550001, 3, 50000));     /* 3*50 + 300 ms */
    assert(t40_buffer_is_stale(10000000, 9549999, 3, 50000));
}

/* #148: the consumer-gap mark is latched until a frame is returned */
static void gapmark(void)
{
    const uint32_t iv = 50000, bc = 3;        /* 20 fps, 3 buffers */
    int64_t t = 100000000, last = t - 2000000, mark, m2, queued;
    uint32_t est = 0;

    /* no gap: frames 50 ms apart */
    assert(!t40_gap_flush_mark(t, t - 50000, 0, bc, iv));
    assert(!t40_gap_flush_mark(t, t - 450000, 0, bc, iv));    /* 150 + 300 ms */
    assert(!t40_gap_flush_mark(t, 0, 0, bc, iv));             /* no frame yet */
    /* a 2 s gap arms the mark at the first probe */
    mark = t40_gap_flush_mark(t, last, 0, bc, iv);
    assert(mark == t);
    /* the buffer dropped there is requeued at t + 1 ms; the encoder probes
     * again every 20 ms: the mark stays t, so that buffer, filled 100 ms
     * later, is returned (re-arming dropped it at "queued 100 ms ago") */
    queued = t + 1000;
    for (m2 = t + 20000; m2 <= t + 140000; m2 += 20000)
        assert(t40_gap_flush_mark(m2, last, mark, bc, iv) == mark);
    assert(!(queued < t40_gap_flush_mark(t + 101000, last, mark, bc, iv)));
    assert(!t40_buffer_is_stale(t + 101000, queued, bc, iv));
    /* a mark older than the bound (channel disabled, sensor stopped) is
     * renewed: buffers queued before the new probe are dropped (#168) */
    assert(t40_gap_flush_mark(t + 450001, last, mark, bc, iv) == t + 450001);
    assert(t40_gap_flush_mark(t + 450000, last, mark, bc, iv) == mark);
    /* the caller clears the mark once a frame was returned: no gap then */
    assert(!t40_gap_flush_mark(t + 200000, t + 101000, 0, bc, iv));

    /* measured interval: never below the configured one, follows a slower
     * sensor, ignores a drained backlog and pauses */
    t40_interval_note(&est, 1000, iv);                /* backlog: ignored */
    assert(est == 0 && t40_interval_bound(iv, est) == iv);
    t40_interval_note(&est, 5000000, iv);             /* pause: ignored */
    assert(est == 0);
    t40_interval_note(&est, 50000, iv);
    assert(est == 50000 && t40_interval_bound(iv, est) == iv);
    {
        int i;

        for (i = 0; i < 64; i++)
            t40_interval_note(&est, 200000, iv);       /* sensor at 5 fps */
    }
    assert(est > 190000 && est <= 200000);
    assert(t40_interval_bound(iv, est) == est);
    /* 5 fps real, 20 fps configured: a buffer waits up to 3 * 200 ms; the
     * bound from the measured interval keeps it (the configured one,
     * 150 + 300 ms, dropped it) */
    assert(t40_buffer_is_stale(10000000, 10000000 - 600000, bc, iv));
    assert(!t40_buffer_is_stale(10000000, 10000000 - 600000, bc,
                                t40_interval_bound(iv, est)));
}

static void feed3a(void)
{
    assert(!t40_3a_feed_channel(0, 0));             /* nothing enabled */
    assert(t40_3a_feed_channel(0, 0x7));            /* ch0 meters */
    assert(!t40_3a_feed_channel(1, 0x7));
    assert(!t40_3a_feed_channel(2, 0x7));
    assert(t40_3a_feed_channel(1, 0x6));            /* ch0 off: ch1 */
    assert(!t40_3a_feed_channel(1, 0x5));           /* ch1 off */
    assert(!t40_3a_feed_channel(-1, 0xff));
    assert(!t40_3a_feed_channel(32, 0xffffffffu));
}

int main(void)
{
    feed3a();
    stale();
    gapmark();
    lend();
    copy();
    puts("T41 SnapFrame tests passed");
    return 0;
}
