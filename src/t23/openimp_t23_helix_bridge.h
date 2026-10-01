#ifndef OPENIMP_T23_HELIX_BRIDGE_H
#define OPENIMP_T23_HELIX_BRIDGE_H

#include <stdint.h>
#include <sys/types.h>

#include <imp/imp_common.h>
#include <imp/imp_encoder.h>

#include "hw_encoder.h"
#include "openimp_t23_helix_ipc.h"

/* Encoder parameters set through the IMP API, kept per session and
 * replayed whenever a worker (re)starts, since the worker is created on the
 * first frame and restarted after a failure. */
#define T23_HELIX_MAX_PARAMS 24

typedef struct {
    uint32_t id;
    uint32_t key;               /* e.g. the ROI index: one entry per key */
    uint32_t size;
    uint8_t data[T23_HELIX_PARAM_MAX];
} T23HelixParam;

typedef struct {
    T23HelixParam params[T23_HELIX_MAX_PARAMS];
    uint32_t count;
    T23HelixFrameCtl frame_ctl;
    int frame_ctl_set;
} T23HelixParamCache;

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
    T23HelixParamCache *cache; /* lazily allocated, survives restarts */
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
/* i264e parameter `id` (T23_I264E_*): remembered for worker restarts and
 * applied now when the worker runs.  Returns 0, or -1 when the encoder
 * refused it. */
int OpenIMP_T23_HelixSetParam(T23HelixBridge *bridge, uint32_t id,
                              uint32_t key, const void *data, uint32_t size);
/* Read parameter `id` from the running encoder (data is also its input,
 * e.g. the ROI index); before the worker runs, the remembered value.
 * Returns 0, 1 when nothing is known yet, -1 on error. */
int OpenIMP_T23_HelixGetParam(T23HelixBridge *bridge, uint32_t id,
                              uint32_t key, void *data, uint32_t size);
int OpenIMP_T23_HelixSetFrameCtl(T23HelixBridge *bridge,
                                 const T23HelixFrameCtl *ctl);
int OpenIMP_T23_HelixGetFrameCtl(T23HelixBridge *bridge,
                                 T23HelixFrameCtl *ctl);
void OpenIMP_T23_HelixExit(T23HelixBridge *bridge);

#endif
