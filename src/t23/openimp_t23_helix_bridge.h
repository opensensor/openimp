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
int OpenIMP_T23_HelixRequestIDR(T23HelixBridge *bridge);
void OpenIMP_T23_HelixExit(T23HelixBridge *bridge);

#endif
