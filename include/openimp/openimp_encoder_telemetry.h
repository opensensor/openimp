/* OpenIMP extension (not in any vendor SDK): cumulative per-channel encoder
 * telemetry.  Counted when GetStream hands a stream out; reset by
 * IMP_Encoder_CreateChn.  Link it weakly if the streamer must also run on the
 * vendor libimp. */
#ifndef OPENIMP_ENCODER_TELEMETRY_H
#define OPENIMP_ENCODER_TELEMETRY_H

#include <stdint.h>

typedef struct {
    uint64_t frames;            /* streams handed out by GetStream */
    uint64_t bytes;             /* their total length */
    uint64_t idr_frames;
    uint32_t last_frame_bytes;
    uint32_t encode_us_last;    /* source frame -> codec -> stream back */
    uint32_t encode_us_avg;
    uint32_t encode_us_max;
    uint32_t fps_x100;          /* delivered frame rate x100 (first to last stream) */
    uint32_t avg_kbps;          /* bit rate over the same window */
    uint32_t encoding;          /* 1 while a picture is in the codec */
} OpenIMPEncoderTelemetry;

int OpenIMP_Encoder_GetTelemetry(int encChn, OpenIMPEncoderTelemetry *t);

#endif
