#include "alcodec/al_fourcc.h"

#include <stdint.h>

extern void __assert(const char *expr, const char *file, int line, const char *func)
    __attribute__((noreturn));

uint32_t AL_EncGetSrcFourCC(AL_TPicFormat picFmt)
{
    if (picFmt.eStorageMode != 0 || picFmt.eChromaMode == 0 || picFmt.eChromaOrder == 3) {
        return AL_GetFourCC(picFmt);
    }

    __assert("picFmt.eChromaMode == AL_CHROMA_MONO || picFmt.eChromaOrder == AL_C_ORDER_SEMIPLANAR",
             "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common_enc/IpEncFourCC.c",
             0xb, "AL_EncGetSrcFourCC");
}

AL_TPicFormat *AL_EncGetSrcPicFormat(AL_TPicFormat *pPicFormat, int32_t eChromaMode,
                                     uint8_t uBitDepth, int32_t eStorageMode,
                                     uint8_t bCompressed)
{
    int32_t eChromaOrder = 3;

    if (eChromaMode == 0) {
        eChromaOrder = 0;
    }

    pPicFormat->eChromaMode = eChromaMode;
    pPicFormat->uBitDepth = uBitDepth;
    pPicFormat->eStorageMode = eStorageMode;
    pPicFormat->eChromaOrder = eChromaOrder;
    pPicFormat->uFlags.fields.bCompressed = bCompressed;
    pPicFormat->uFlags.fields.b10BitPacked = 0;
    pPicFormat->uFlags.fields.reserved0 = 0;
    pPicFormat->uFlags.fields.reserved1 = 0;
    return pPicFormat;
}

uint32_t AL_GetRecFourCC(AL_TPicFormat picFmt)
{
    if (picFmt.eStorageMode == 3) {
        return AL_GetFourCC(picFmt);
    }

    __assert("picFmt.eStorageMode == AL_FB_TILE_64x4",
             "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common_enc/IpEncFourCC.c",
             0x1d, "AL_GetRecFourCC");
}

AL_TPicFormat *AL_EncGetRecPicFormat(AL_TPicFormat *pPicFormat, int32_t eChromaMode,
                                     uint8_t uBitDepth, uint8_t bCompressed)
{
    int32_t eChromaOrder = 3;

    if (eChromaMode == 0) {
        eChromaOrder = 0;
    }

    pPicFormat->eChromaMode = eChromaMode;
    pPicFormat->uBitDepth = uBitDepth;
    pPicFormat->eStorageMode = 3;
    pPicFormat->eChromaOrder = eChromaOrder;
    pPicFormat->uFlags.fields.bCompressed = bCompressed;
    pPicFormat->uFlags.fields.b10BitPacked = 0;
    pPicFormat->uFlags.fields.reserved0 = 0;
    pPicFormat->uFlags.fields.reserved1 = 0;
    return pPicFormat;
}
