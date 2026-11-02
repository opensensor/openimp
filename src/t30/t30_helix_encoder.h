#ifndef OPENIMP_T30_HELIX_ENCODER_H
#define OPENIMP_T30_HELIX_ENCODER_H

#include <stdint.h>

#include <imp/imp_common.h>

#include "hw_encoder.h"

typedef struct T30HelixEncoder T30HelixEncoder;

/* IMP_Encoder_InsertUserData queue: up to 2 payloads of up to 1024 bytes */
#define T30_USER_DATA_MAX_CNT  2u
#define T30_USER_DATA_MAX_SIZE 1024u
extern const uint8_t t30_user_data_uuid[16];
/* Replace the pending user data (SEI payloads for the next picture). */
int OpenIMP_T30_HelixSetUserData(T30HelixEncoder *encoder, uint32_t count,
                                 const uint32_t *lengths,
                                 const uint8_t (*data)[T30_USER_DATA_MAX_SIZE]);

int OpenIMP_T30_HelixCreate(T30HelixEncoder **encoder,
                            const HWEncoderParams *params);
int OpenIMP_T30_HelixEncode(T30HelixEncoder *encoder,
                            const IMPFrameInfo *frame,
                            HWStreamBuffer **stream);
int OpenIMP_T30_HelixRequestIDR(T30HelixEncoder *encoder);
/* SPS frame cropping (visible rectangle); next picture is an IDR. */
int OpenIMP_T30_HelixSetCrop(T30HelixEncoder *encoder, int enable,
                             uint32_t x, uint32_t y, uint32_t w, uint32_t h);
int OpenIMP_T30_HelixGetCrop(const T30HelixEncoder *encoder, int *enable,
                             uint32_t *x, uint32_t *y, uint32_t *w,
                             uint32_t *h);
/* Adopt the codec's current rate-control fields (fps, GOP, RC mode,
 * bitrate, QP and its bounds).  Call on the encoding thread before a
 * picture; zero or out-of-range fields keep their current value. */
int OpenIMP_T30_HelixUpdateParams(T30HelixEncoder *encoder,
                                  const HWEncoderParams *params);
void OpenIMP_T30_HelixDestroy(T30HelixEncoder *encoder);

/* IMP_Encoder_SetChnROI: the eight i264e ROI table entries (helix_roi.h)
 * for the next pictures.  Call on the encoding thread before a picture. */
int OpenIMP_T30_HelixSetRoi(T30HelixEncoder *encoder,
                            const uint8_t roi[8][7]);

#if defined(PLATFORM_T23)
/* Apply changed rate-control, frame-rate, GOP and QP-bound settings
 * between pictures (call from the encoding thread).  Unchanged values are
 * a no-op; zero fields keep the current value. */
int OpenIMP_T30_HelixReconfigure(T30HelixEncoder *encoder,
                                 const HWEncoderParams *params);
#endif
/* Consecutive failed pictures since the last good one. */
uint32_t OpenIMP_T30_HelixFailures(const T30HelixEncoder *encoder);
#if defined(PLATFORM_T21) && !defined(PLATFORM_T20)
/* the chroma QP index offset the encoder codes with now (IMP_Encoder_GetH264TransCfg reads the applied value;
 * a new one is applied with the next IDR picture) */
int OpenIMP_T30_HelixChromaQpOffset(const T30HelixEncoder *encoder);
#endif
#if defined(PLATFORM_T20)
/* the QP of the last picture as the OEM i264e_encode reports it (first try,
 * 0 for a BlackEnhance picture); 0 before the first picture, as the OEM record */
int OpenIMP_T30_HelixPictureQp(const T30HelixEncoder *encoder);
#endif

#endif
