/* T40 IMP_Encoder_Query status block (host-tested: tests/t40/query_test.c).
 *
 * Vendor T40 1.3.1 IMP_Encoder_Query (0x6a2b8) copies a 24-byte status
 * block of the channel record: `registered` (RegisterChn), `leftPics`
 * (frames waiting for the encoder, refreshed after every encoded picture),
 * `leftStreamFrames` (streams still queued, sampled in GetStream),
 * `work_done` (1 when idle, 0 while a picture is encoded); the vendor never
 * writes `leftStreamBytes` or `curPacks` (always 0).  OpenIMP encodes on
 * demand in PollingStream/GetStream (no input queue, at most one finished
 * stream), so: leftPics 0, leftStreamFrames 1 while a finished stream waits
 * for GetStream (live instead of the vendor's GetStream-time sample),
 * work_done 0 while a PollingStream is encoding.  The old answer reported
 * leftPics/curPacks 1 for a pending stream and leftStreamFrames 0. */
#ifndef OPENIMP_T40_P2_QUERY_H
#define OPENIMP_T40_P2_QUERY_H

#include <string.h>

/* `encoding` = the codec is working on a picture right now (1 from the
 * hand-over of the source frame to the codec until its stream is back); a
 * PollingStream that only waits for the next frame is idle (work_done 1,
 * the same as T41; the vendor's work_done is 0 only while a picture is
 * encoded).  leftPics follows it: the one picture in the codec. */
static inline void openimp_t40_query_fill(IMPEncoderCHNStat *stat,
                                          int registered, int stream_ready,
                                          int stream_out, int encoding)
{
    memset(stat, 0, sizeof(*stat));
    stat->registered = registered != 0;
    stat->leftPics = encoding ? 1u : 0u;
    stat->leftStreamFrames = (stream_ready && !stream_out) ? 1u : 0u;
    stat->work_done = encoding ? 0u : 1u;
}

#endif
