#ifndef OPENIMP_ALCODEC_BITSTREAMLITE_H
#define OPENIMP_ALCODEC_BITSTREAMLITE_H

#include <stdint.h>

typedef struct AL_BitStreamLite {
    uint8_t *buffer;
    int32_t bit_count;
    int32_t size_bits;
    uint8_t reset_flag;
    uint8_t reserved_0d;
    uint8_t reserved_0e;
    uint8_t reserved_0f;
} AL_BitStreamLite;

int32_t AL_BitStreamLite_Init(AL_BitStreamLite *arg1, uint8_t *arg2, int32_t arg3);
int32_t AL_BitStreamLite_Deinit(AL_BitStreamLite *arg1);
int32_t AL_BitStreamLite_Reset(void *arg1);
uint8_t *AL_BitStreamLite_GetData(AL_BitStreamLite *arg1);
uint8_t *AL_BitStreamLite_GetCurData(AL_BitStreamLite *arg1);
int32_t AL_BitStreamLite_GetBitsCount(void *arg1);
int32_t AL_BitStreamLite_PutBits(AL_BitStreamLite *arg1, uint8_t arg2, uint32_t arg3);
int32_t AL_BitStreamLite_PutBit(AL_BitStreamLite *arg1, uint8_t arg2);
int32_t AL_BitStreamLite_AlignWithBits(void *arg1, uint8_t arg2);
uint32_t AL_BitStreamLite_EndOfSEIPayload(void *arg1);
int32_t AL_BitStreamLite_SkipBits(void *arg1, int32_t arg2);
int32_t AL_BitStreamLite_PutU(AL_BitStreamLite *arg1, uint8_t arg2, uint32_t arg3);
int32_t AL_BitStreamLite_PutUE(AL_BitStreamLite *arg1, int32_t arg2);
int32_t AL_BitStreamLite_PutSE(AL_BitStreamLite *arg1, int32_t arg2);
int32_t PutUV(AL_BitStreamLite *arg1, int32_t arg2);

#endif
