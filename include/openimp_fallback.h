/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * OpenIMP fallback frame (extension beyond the vendor IMP API, T40 and T41
 * FrameSource).
 *
 * When no frame arrives on a FrameSource channel for a while (sensor cable,
 * sensor or ISP stall) while a consumer (the encoder) is waiting for frames,
 * OpenIMP can generate NV12 frames itself at the channel frame rate: the
 * test pattern with a black band reading "NO SENSOR SIGNAL", a 16 bit frame
 * counter (top left) and a blinking square (top right).  The encoder, JPEG
 * and the streamers keep running on a live stream; the first real frame
 * switches back automatically.  The change is logged:
 *
 *   FrameSource channel N: no sensor signal for 2000 ms, delivering fallback frames
 *   FrameSource channel N: sensor signal is back, fallback frames stopped (K generated)
 *
 * Default ON for T40 and T41 since 26.10. (the vendor library has no such
 * function).  OPENIMP_FALLBACK_FRAME=0 switches it off for all channels (read
 * at start; the dwell time is OPENIMP_FALLBACK_TIMEOUT_MS, 200..60000,
 * default 2000); per channel it can be changed at any time with
 * OpenIMP_FrameSource_SetFallback().  A build that has the API exports the
 * constant OpenIMP_Cap_FallbackFrame (1).
 *
 * Limits: the picture is generated in a buffer of its own taken from video
 * memory when the fallback starts (about 1.5 x width x height bytes) and
 * freed again when the channel is disabled; a channel with the I2D
 * rotation/flip pass is not covered (no frames, as before).  IVS and
 * SnapFrame see no synthetic frames.
 *
 * ABI: OpenIMPFallbackState starts with a size field set by the caller;
 * new fields are only appended, Get fills at most the caller's size.
 */
#ifndef OPENIMP_FALLBACK_PUBLIC_H
#define OPENIMP_FALLBACK_PUBLIC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OPENIMP_FALLBACK_ABI 1

/* OpenIMPFallbackState.error while synthetic frames are delivered (errno ENOLINK) */
#define OPENIMP_FALLBACK_ERR_NO_SIGNAL 67

typedef struct OpenIMPFallbackState {
    uint32_t size;          /* sizeof(OpenIMPFallbackState) of the caller */
    uint32_t enabled;       /* fallback armed for this channel */
    uint32_t active;        /* 1: the consumer currently gets synthetic frames */
    int32_t error;          /* OPENIMP_FALLBACK_ERR_NO_SIGNAL while active, else 0 */
    uint32_t timeout_ms;    /* dwell time without a real frame */
    uint32_t generated;     /* synthetic frames since the channel was enabled */
    uint32_t activations;   /* switches into fallback since then */
    uint32_t last_real_age_ms; /* time since the last real frame (0 when not streaming);
                                * the clock is set at EnableChn and by real frames
                                * dequeued through GetFrame (a consumer that was away
                                * for a while restarts it).  A channel that only feeds
                                * the bound encoder is never read through GetFrame, so
                                * there it is just the time since EnableChn: not
                                * meaningful for such a channel. */
} OpenIMPFallbackState;

/* on != 0 arms the fallback of FrameSource channel chn, 0 disarms it (an active
 * fallback ends at once, the next GetFrame sees only real frames).  May be called
 * at any time.  Returns 0, or -1 (errno EINVAL: bad channel). */
int OpenIMP_FrameSource_SetFallback(int chn, int on);

/* state->size must be set.  Returns 0, or -1 (errno EINVAL). */
int OpenIMP_FrameSource_GetFallback(int chn, OpenIMPFallbackState *state);

extern const int OpenIMP_Cap_FallbackFrame;     /* 1 */

#ifdef __cplusplus
}
#endif

#endif
