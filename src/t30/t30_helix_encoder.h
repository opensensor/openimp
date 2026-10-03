#ifndef OPENIMP_T30_HELIX_ENCODER_H
#define OPENIMP_T30_HELIX_ENCODER_H

#include <stdint.h>

#include <imp/imp_common.h>

#include "hw_encoder.h"

typedef struct T30HelixEncoder T30HelixEncoder;

int OpenIMP_T30_HelixCreate(T30HelixEncoder **encoder,
                            const HWEncoderParams *params);
int OpenIMP_T30_HelixEncode(T30HelixEncoder *encoder,
                            const IMPFrameInfo *frame,
                            HWStreamBuffer **stream);
int OpenIMP_T30_HelixRequestIDR(T30HelixEncoder *encoder);
/* Adopt the codec's current rate-control fields (fps, GOP, RC mode,
 * bitrate, QP and its bounds).  Call on the encoding thread before a
 * picture; zero or out-of-range fields keep their current value. */
int OpenIMP_T30_HelixUpdateParams(T30HelixEncoder *encoder,
                                  const HWEncoderParams *params);
void OpenIMP_T30_HelixDestroy(T30HelixEncoder *encoder);

#if defined(PLATFORM_T23)
/* Apply changed rate-control, frame-rate, GOP and QP-bound settings
 * between pictures (call from the encoding thread).  Unchanged values are
 * a no-op; zero fields keep the current value. */
int OpenIMP_T30_HelixReconfigure(T30HelixEncoder *encoder,
                                 const HWEncoderParams *params);
#endif
/* Consecutive failed pictures since the last good one. */
uint32_t OpenIMP_T30_HelixFailures(const T30HelixEncoder *encoder);

#endif
