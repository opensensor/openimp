#ifndef OPENIMP_P2_HEVC_POLICY_H
#define OPENIMP_P2_HEVC_POLICY_H

/*
 * H.265 channel policy of IMP_Encoder_CreateChn.
 *
 * T10/T20/T21/T23 have no HEVC hardware (the Helix encoder is H.264/JPEG
 * only; Radix exists only on T30).  The vendor libimp returns 0 and creates
 * an empty channel that never delivers a frame; OpenIMP fails CreateChn with
 * -1 instead, so a streamer falls back to H.264 at once (documented
 * beyond-vendor deviation).  T10 shares the T20 libimp, hence PLATFORM_T20.
 * Other SoCs keep their existing handling (only the T31 AVPU encodes HEVC).
 */
#if defined(PLATFORM_T20) || defined(PLATFORM_T21) || defined(PLATFORM_T23)
#define P2_HEVC_NO_HARDWARE 1
#define P2_HEVC_NO_HW_MSG \
    "H.265 not supported by the hardware on this SoC " \
    "(Helix encoder is H.264/JPEG only); use H.264"
#else
#define P2_HEVC_NO_HARDWARE 0
#endif

#endif
