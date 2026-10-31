/* IMP_Decoder on T10/T20/T21: exported, answers ENOTSUP (see below). */
#include <stdint.h>

#include "imp_log_int.h"

/* ---- IMP_Decoder ------------------------------------------------------- */
/*
 * IMP_Decoder_* of the T10/T20 3.12.0 and T21 1.0.33 libimp (imp_decoder.h:
 * a JPEG decoder on the Helix VPU).  OpenIMP has no JPEG decoder on these
 * SoCs (T23 runs the OEM one in its Helix helper): every call fails with -1
 * and errno ENOTSUP, so an application sees the missing feature at CreateChn
 * instead of failing to link.  No streamer calls the decoder.
 */
#include <errno.h>

static int t2x_decoder_unsupported(const char *function)
{
    IMP_LOG_LIMITED(LOG_ERR, "Decoder", "%s: no JPEG decoder in OpenIMP on "
                    "T10/T20/T21", function);
    errno = ENOTSUP;
    return -1;
}

int IMP_Decoder_CreateChn(int decChn, const void *attr);
int IMP_Decoder_CreateChn(int decChn, const void *attr)
{
    (void)decChn;
    (void)attr;
    return t2x_decoder_unsupported(__func__);
}

int IMP_Decoder_DestroyChn(int decChn);
int IMP_Decoder_DestroyChn(int decChn)
{
    (void)decChn;
    return t2x_decoder_unsupported(__func__);
}

int IMP_Decoder_StartRecvPic(int decChn);
int IMP_Decoder_StartRecvPic(int decChn)
{
    (void)decChn;
    return t2x_decoder_unsupported(__func__);
}

int IMP_Decoder_StopRecvPic(int decChn);
int IMP_Decoder_StopRecvPic(int decChn)
{
    (void)decChn;
    return t2x_decoder_unsupported(__func__);
}

int IMP_Decoder_SendStreamTimeout(int decChn, void *stream,
                                  uint32_t timeoutMsec);
int IMP_Decoder_SendStreamTimeout(int decChn, void *stream,
                                  uint32_t timeoutMsec)
{
    (void)decChn;
    (void)stream;
    (void)timeoutMsec;
    return t2x_decoder_unsupported(__func__);
}

int IMP_Decoder_PollingFrame(int decChn, uint32_t timeoutMsec);
int IMP_Decoder_PollingFrame(int decChn, uint32_t timeoutMsec)
{
    (void)decChn;
    (void)timeoutMsec;
    return t2x_decoder_unsupported(__func__);
}

int IMP_Decoder_GetFrame(int decChn, void **frame);
int IMP_Decoder_GetFrame(int decChn, void **frame)
{
    (void)decChn;
    if (frame)
        *frame = NULL;
    return t2x_decoder_unsupported(__func__);
}

int IMP_Decoder_ReleaseFrame(int decChn, void *frame);
int IMP_Decoder_ReleaseFrame(int decChn, void *frame)
{
    (void)decChn;
    (void)frame;
    return t2x_decoder_unsupported(__func__);
}
