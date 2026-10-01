#ifndef OPENIMP_T23_HELIX_BRIDGE_H
#define OPENIMP_T23_HELIX_BRIDGE_H

#include <stdint.h>
#include <sys/types.h>

#include <imp/imp_common.h>
#include <imp/imp_encoder.h>

#include "hw_encoder.h"
#include "openimp_t23_helix_ipc.h"

typedef struct {
    T23EncoderYuvIn input;
    void *shared_buffer;
    uint32_t shared_size;
    uint32_t input_capacity;
    uint32_t input_size;
    uint32_t output_capacity;
    uint32_t width;
    uint32_t height;
    uint32_t frames;
    uint32_t recoveries;
    int socket_fd;
    int shared_fd;
    pid_t worker_pid;
    int failed;
} T23HelixBridge;

int OpenIMP_T23_HelixInit(T23HelixBridge *bridge,
                          const HWEncoderParams *params);
int OpenIMP_T23_HelixEncode(T23HelixBridge *bridge,
                            const IMPFrameInfo *frame,
                            HWStreamBuffer **stream);
/* Unbound (IMP_Encoder_Yuv*) session: caller-supplied rate control and a
 * packed NV12 input of width * height * 3 / 2 bytes. */
int OpenIMP_T23_HelixInitYuv(T23HelixBridge *bridge, uint32_t width,
                             uint32_t height, const T23EncoderYuvIn *input);
/* Encode into a caller buffer; *length is its capacity on entry and the
 * access unit length on success. */
int OpenIMP_T23_HelixEncodeInto(T23HelixBridge *bridge,
                                const IMPFrameInfo *frame, void *output,
                                uint32_t *length);
int OpenIMP_T23_HelixRequestIDR(T23HelixBridge *bridge);
void OpenIMP_T23_HelixExit(T23HelixBridge *bridge);

#endif
