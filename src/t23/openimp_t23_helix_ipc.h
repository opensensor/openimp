#ifndef OPENIMP_T23_HELIX_IPC_H
#define OPENIMP_T23_HELIX_IPC_H

#include <stdint.h>

#include <imp/imp_encoder.h>

#define T23_HELIX_IPC_MAGIC 0x4f483233u /* "OH23" */
/* 3: one protocol for both extensions of version 1 that were developed
 *    side by side as "version 2":
 *    - SET_PARAM/GET_PARAM/SET_FRAME_CTL and the DEC_* commands with the
 *      param_id/param_size/param/frame_ctl request fields and the response
 *      param fields;
 *    - zero-copy input: input_physical/flags in the request and the
 *      T23_HELIX_INIT_ZERO_COPY flag of INIT.
 * The worker and libimp are built and installed together; a version
 * mismatch fails INIT. */
#define T23_HELIX_IPC_VERSION 3u
#define T23_HELIX_PARAM_MAX 64u

/* INIT flags */
#define T23_HELIX_INIT_ZERO_COPY 0x1u /* allocate the input copy lazily */

/* The OEM IMP_Encoder_Yuv* in/out structures (imp/imp_encoder.h). */
typedef IMPEncoderYuvIn T23EncoderYuvIn;
typedef IMPEncoderYuvOut T23EncoderYuvOut;

enum {
    T23_HELIX_COMMAND_INIT = 1,
    T23_HELIX_COMMAND_ENCODE = 2,
    T23_HELIX_COMMAND_REQUEST_IDR = 3,
    T23_HELIX_COMMAND_EXIT = 4,
    /* i264e_set_param/i264e_get_param on the worker's encoder, the calls
     * behind the OEM IMP_Encoder_Set/Get* channel functions */
    T23_HELIX_COMMAND_SET_PARAM = 5,
    T23_HELIX_COMMAND_GET_PARAM = 6,
    /* per-frame picture controls (initial QP, GDR) applied before every
     * following encode */
    T23_HELIX_COMMAND_SET_FRAME_CTL = 7,
    /* OEM IMP_Decoder (hardware JPEG) session:
     * DEC_INIT param = IMPDecoderCHNAttr (28 bytes); DEC_DECODE decodes the
     * JPEG in the shared input window and answers with the OEM frame
     * (IMPFrameInfo, 56 bytes, in param) and its handle (output_offset),
     * which stays the worker's until DEC_RELEASE (param_id = handle);
     * DEC_COPY copies a held frame to the shared output window. */
    T23_HELIX_COMMAND_DEC_INIT = 8,
    T23_HELIX_COMMAND_DEC_DECODE = 9,
    T23_HELIX_COMMAND_DEC_RELEASE = 10,
    T23_HELIX_COMMAND_DEC_COPY = 11,
};

/* OEM i264e parameter ids (IMP_Encoder_* -> i264e_set_param) */
enum {
    T23_I264E_COLOR2GREY = 0,
    T23_I264E_CROP = 1,
    T23_I264E_ROI = 2,
    T23_I264E_RC = 3,
    T23_I264E_RC_TRIG = 4,
    T23_I264E_FPS = 5,
    T23_I264E_GOP = 7,
    T23_I264E_DENOISE = 8,
    T23_I264E_HSKIP = 9,
    T23_I264E_BLACK_ENHANCE = 10,
    T23_I264E_MBRC = 11,
    T23_I264E_CHANGE_REF = 12,
    T23_I264E_SUPER_FRAME = 13,
    T23_I264E_H264_TRANS = 14,
    T23_I264E_QPG_MODE = 15,
};

/* Picture fields the OEM channel thread sets before i264e_encode; in the
 * OEM YUV session they stay zero. */
typedef struct {
    int32_t init_qp;        /* 0: rate control picks the QP */
    int32_t gdr_enable;
    int32_t gdr_cycle;
    int32_t gdr_frames;
    int32_t gdr_request;    /* one-shot */
} T23HelixFrameCtl;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t command;
    uint32_t width;
    uint32_t height;
    uint32_t input_size;
    uint32_t input_capacity;
    uint32_t output_capacity;
    uint32_t pixel_format;
    int64_t timestamp;
    T23EncoderYuvIn encoder_input;
    uint32_t param_id;
    uint32_t param_size;
    uint8_t param[T23_HELIX_PARAM_MAX];
    T23HelixFrameCtl frame_ctl;
    /* ENCODE: physical address of the frame in the reserved memory both
     * processes map (OpenIMP's VBM pool), or 0 for a frame copied into the
     * shared window. The caller keeps the frame until the response. */
    uint32_t input_physical;
    uint32_t flags;
} T23HelixIpcRequest;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t command;
    int32_t status;
    uint32_t output_offset;
    uint32_t output_length;
    uint32_t param_size;
    uint8_t param[T23_HELIX_PARAM_MAX];
} T23HelixIpcResponse;

#endif
