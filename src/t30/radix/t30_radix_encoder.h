/*
 * T30 H.265 encoder on the Radix block (docs/T30_HEVC.md).
 *
 * Follows the vendor libimp 1.0.5 i265e picture path: the rate controller
 * (EPRC30_*, codec 1) fills the 0x1204-byte parameter block and the CU QP
 * map, codec_enc / hwicodec_pf_h265e_t30_enc add the per-picture fields and
 * addresses, the command list goes to /dev/soc_vpu (Radix, codec direction
 * 0x10), and the slice data the hardware writes is wrapped into a NAL unit
 * behind the slice header (I265e::sliceEncap), with VPS, SPS and PPS before
 * every IDR picture.
 */
#ifndef OPENIMP_T30_RADIX_ENCODER_H
#define OPENIMP_T30_RADIX_ENCODER_H

#include <stdint.h>

#include <imp/imp_common.h>

#include "hw_encoder.h"

typedef struct T30RadixEncoder T30RadixEncoder;

/* params: size, frame rate, GOP, RC mode/bit rate/QPs as for the Helix
 * encoder (HWEncoderParams; rc_mode HW_RC_MODE_*, bit rate in bit/s). */
int OpenIMP_T30_RadixCreate(T30RadixEncoder **encoder,
                            const HWEncoderParams *params);
/* One access unit (VPS/SPS/PPS on an IDR, then the slice NAL) in a heap
 * buffer that the stream owns, as OpenIMP_T30_HelixEncode. */
int OpenIMP_T30_RadixEncode(T30RadixEncoder *encoder,
                            const IMPFrameInfo *frame,
                            HWStreamBuffer **stream);
int OpenIMP_T30_RadixRequestIDR(T30RadixEncoder *encoder);
/* Run-time change of frame rate, GOP, RC mode, bit rate and QPs
 * (IMP_Encoder_SetChnAttrRcMode / SetChnFrmRate / SetGOPSize): latched and
 * applied at the next IDR as the vendor i265e (I265eReconfig, I265e::reset);
 * a new frame rate or GOP requests that IDR.  Call between pictures. */
int OpenIMP_T30_RadixUpdateParams(T30RadixEncoder *encoder,
                                  const HWEncoderParams *params);
/* Consecutive failed pictures since the last good one. */
uint32_t OpenIMP_T30_RadixFailures(const T30RadixEncoder *encoder);
void OpenIMP_T30_RadixDestroy(T30RadixEncoder *encoder);

#endif
