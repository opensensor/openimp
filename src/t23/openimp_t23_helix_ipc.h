#ifndef OPENIMP_T23_HELIX_IPC_H
#define OPENIMP_T23_HELIX_IPC_H

#include <stdint.h>

#include <imp/imp_encoder.h>

#define T23_HELIX_IPC_MAGIC 0x4f483233u /* "OH23" */
/* 2: input_physical/flags (zero-copy input). The worker and libimp are
 * built and installed together; a version mismatch fails INIT. */
#define T23_HELIX_IPC_VERSION 2u

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
};

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
} T23HelixIpcResponse;

#endif
