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
    /* A request went unanswered (timeout, I/O error) or got a reply that
     * does not belong to it: the worker may still be busy with it and its
     * next reply cannot be matched to a request. */
    int stream_lost;
    T23HelixParamCache *cache; /* lazily allocated, survives restarts */
    int zero_copy;      /* frames are passed by physical address */
    uint32_t rmem_phys; /* the worker's slice of the rmem arena, 0: none */
    uint32_t rmem_size;
    /* OPENIMP_T23_PACE_STATS window */
    uint64_t stats_start_us;
    uint32_t stats_frames;
    uint64_t stats_flush_us;
    uint64_t stats_exchange_us;
    uint64_t stats_exchange_max_us;
    uint64_t stats_oem_us;
    uint64_t stats_oem_max_us;
    uint64_t stats_bytes;
    uint32_t idr_requested;     /* frame number of a pending RequestIDR + 1 */
    uint32_t idr_requests;
} T23HelixBridge;

/* Path of the OEM helper (OPENIMP_T23_HELIX_HELPER or the default) and
 * whether it is executable; images without it run native-only. */
/* OPENIMP_T23_HELIX_BRIDGE (build-t23.sh, default 1): the OEM Helix worker
 * openimp-t23-helixd (fallback H.264 backend, IMP_Decoder_*). */
#ifndef OPENIMP_T23_HELIX_BRIDGE
#define OPENIMP_T23_HELIX_BRIDGE 1
#endif
#if OPENIMP_T23_HELIX_BRIDGE
const char *OpenIMP_T23_HelixHelperPath(void);
int OpenIMP_T23_HelixHelperAvailable(void);
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
/* OEM hardware JPEG decoder (IMP_Decoder channel 0) in a worker.  Decoded
 * frames stay in the worker's VBM until released; `handle` names one. */
int OpenIMP_T23_HelixDecoderOpen(T23HelixBridge *bridge, const void *attr,
                                 uint32_t attr_size, uint32_t max_width,
                                 uint32_t max_height);
int OpenIMP_T23_HelixDecode(T23HelixBridge *bridge, const void *data,
                            uint32_t length, int64_t timestamp,
                            uint32_t timeout_ms, IMPFrameInfo *frame,
                            uint32_t *handle);
int OpenIMP_T23_HelixDecoderRelease(T23HelixBridge *bridge, uint32_t handle);
/* copy a held frame (size bytes at most) for callers that cannot map it */
int OpenIMP_T23_HelixDecoderCopy(T23HelixBridge *bridge, uint32_t handle,
                                 void *out, uint32_t size);
void OpenIMP_T23_HelixExit(T23HelixBridge *bridge);
#else
/* OPENIMP_T23_HELIX_BRIDGE=0 (build-t23.sh): no OEM worker.  The stubs make
 * the worker unavailable: the native Helix encoder is the only H.264
 * backend, encoder parameters stay local (as OPENIMP_T23_ENC_PARAMS=0) and
 * IMP_Decoder_CreateChn fails. */
static inline const char *OpenIMP_T23_HelixHelperPath(void)
{
    return "(OEM Helix worker not built)";
}
static inline int OpenIMP_T23_HelixHelperAvailable(void) { return 0; }
static inline int OpenIMP_T23_HelixInit(T23HelixBridge *bridge,
                                        const HWEncoderParams *params)
{
    (void)bridge; (void)params;
    return -1;
}
static inline int OpenIMP_T23_HelixEncode(T23HelixBridge *bridge,
                                          const IMPFrameInfo *frame,
                                          HWStreamBuffer **stream)
{
    (void)bridge; (void)frame; (void)stream;
    return -1;
}
static inline int OpenIMP_T23_HelixInitYuv(T23HelixBridge *bridge,
                                           uint32_t width, uint32_t height,
                                           const T23EncoderYuvIn *input)
{
    (void)bridge; (void)width; (void)height; (void)input;
    return -1;
}
static inline int OpenIMP_T23_HelixEncodeInto(T23HelixBridge *bridge,
                                              const IMPFrameInfo *frame,
                                              void *output, uint32_t *length)
{
    (void)bridge; (void)frame; (void)output; (void)length;
    return -1;
}
static inline int OpenIMP_T23_HelixRequestIDR(T23HelixBridge *bridge)
{
    (void)bridge;
    return -1;
}
static inline int OpenIMP_T23_HelixSetParam(T23HelixBridge *bridge,
                                            uint32_t id, uint32_t key,
                                            const void *data, uint32_t size)
{
    (void)bridge; (void)id; (void)key; (void)data; (void)size;
    return -1;
}
static inline int OpenIMP_T23_HelixGetParam(T23HelixBridge *bridge,
                                            uint32_t id, uint32_t key,
                                            void *data, uint32_t size)
{
    (void)bridge; (void)id; (void)key; (void)data; (void)size;
    return -1;
}
static inline int OpenIMP_T23_HelixSetFrameCtl(T23HelixBridge *bridge,
                                               const T23HelixFrameCtl *ctl)
{
    (void)bridge; (void)ctl;
    return -1;
}
static inline int OpenIMP_T23_HelixGetFrameCtl(T23HelixBridge *bridge,
                                               T23HelixFrameCtl *ctl)
{
    (void)bridge; (void)ctl;
    return -1;
}
static inline int OpenIMP_T23_HelixDecoderOpen(T23HelixBridge *bridge,
                                               const void *attr,
                                               uint32_t attr_size,
                                               uint32_t max_width,
                                               uint32_t max_height)
{
    (void)bridge; (void)attr; (void)attr_size; (void)max_width;
    (void)max_height;
    return -1;
}
static inline int OpenIMP_T23_HelixDecode(T23HelixBridge *bridge,
                                          const void *data, uint32_t length,
                                          int64_t timestamp,
                                          uint32_t timeout_ms,
                                          IMPFrameInfo *frame,
                                          uint32_t *handle)
{
    (void)bridge; (void)data; (void)length; (void)timestamp;
    (void)timeout_ms; (void)frame; (void)handle;
    return -1;
}
static inline int OpenIMP_T23_HelixDecoderRelease(T23HelixBridge *bridge,
                                                  uint32_t handle)
{
    (void)bridge; (void)handle;
    return -1;
}
static inline int OpenIMP_T23_HelixDecoderCopy(T23HelixBridge *bridge,
                                               uint32_t handle, void *out,
                                               uint32_t size)
{
    (void)bridge; (void)handle; (void)out; (void)size;
    return -1;
}
static inline void OpenIMP_T23_HelixExit(T23HelixBridge *bridge)
{
    (void)bridge;
}
#endif

#endif
