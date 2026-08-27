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
int OpenIMP_T30_HelixSetBitrate(T30HelixEncoder *encoder,
                                uint32_t bitrate);
void OpenIMP_T30_HelixDestroy(T30HelixEncoder *encoder);

#endif
