#ifndef OPENIMP_T23_HELIX_IPC_H
#define OPENIMP_T23_HELIX_IPC_H

#include <stdint.h>

#include <imp/imp_encoder.h>

#define T23_HELIX_IPC_MAGIC 0x4f483233u /* "OH23" */
#define T23_HELIX_IPC_VERSION 1u

typedef struct {
    IMPPayloadType type;
    IMPEncoderAttrRcMode mode;
    IMPEncoderFrmRate outFrmRate;
    uint32_t maxGop;
} T23EncoderYuvIn;

typedef struct {
    void *outAddr;
    uint32_t outLen;
} T23EncoderYuvOut;

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
