#ifndef OPENIMP_ALCODEC_AL_FOURCC_H
#define OPENIMP_ALCODEC_AL_FOURCC_H

#include <stdint.h>

#include "al_types.h"

enum {
    AL_CHROMA_MONO = 0,
    AL_CHROMA_4_2_0 = 1,
    AL_CHROMA_4_2_2 = 2,
    AL_CHROMA_4_4_4 = 3,
};

enum {
    AL_C_ORDER_NO_CHROMA = 0,
    AL_C_ORDER_U_V = 1,
    AL_C_ORDER_V_U = 2,
    AL_C_ORDER_SEMIPLANAR = 3,
};

enum {
    AL_FB_RASTER = 0,
    AL_FB_TILE_32x4 = 2,
    AL_FB_TILE_64x4 = 3,
};

typedef union AL_TPicFormatFlags {
    struct {
        uint8_t bCompressed;
        uint8_t b10BitPacked;
        uint8_t reserved0;
        uint8_t reserved1;
    } fields;
    uint32_t uValue;
} AL_TPicFormatFlags;

typedef struct AL_TPicFormat {
    int32_t eChromaMode;
    int32_t uBitDepth;
    int32_t eStorageMode;
    int32_t eChromaOrder;
    AL_TPicFormatFlags uFlags;
} AL_TPicFormat;

_Static_assert(sizeof(AL_TPicFormat) == 20, "AL_TPicFormat size");

int32_t AL_GetPicFormat(uint32_t tFourCC, AL_TPicFormat *pPicFormat);
uint32_t AL_GetFourCC(AL_TPicFormat tPicFormat);
int32_t AL_GetChromaMode(uint32_t tFourCC);
uint32_t AL_GetBitDepth(uint32_t tFourCC);
int32_t AL_GetPixelSize(uint32_t tFourCC);
int32_t AL_GetSubsampling(uint32_t tFourCC, int32_t *pHScale, int32_t *pVScale);
uint32_t AL_Is10bitPacked(uint32_t tFourCC);
int32_t AL_IsMonochrome(uint32_t tFourCC);
int32_t AL_IsSemiPlanar(uint32_t tFourCC);
uint32_t AL_IsCompressed(uint32_t tFourCC);
int32_t AL_IsTiled(uint32_t tFourCC);
int32_t AL_GetStorageMode(uint32_t tFourCC);

uint32_t AL_EncGetSrcFourCC(AL_TPicFormat picFmt);
AL_TPicFormat *AL_EncGetSrcPicFormat(AL_TPicFormat *pPicFormat, int32_t eChromaMode,
                                     uint8_t uBitDepth, int32_t eStorageMode,
                                     uint8_t bCompressed);
uint32_t AL_GetRecFourCC(AL_TPicFormat picFmt);
AL_TPicFormat *AL_EncGetRecPicFormat(AL_TPicFormat *pPicFormat, int32_t eChromaMode,
                                     uint8_t uBitDepth, uint8_t bCompressed);

#endif
