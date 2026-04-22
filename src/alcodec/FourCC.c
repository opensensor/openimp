#include "alcodec/al_fourcc.h"

#include <stddef.h>
#include <stdint.h>

extern void __assert(const char *expr, const char *file, int line, const char *func)
    __attribute__((noreturn));

typedef struct FourCCMapping {
    uint32_t tFourCC;
    int32_t eChromaMode;
    int32_t uBitDepth;
    int32_t eStorageMode;
    int32_t eChromaOrder;
    uint8_t bCompressed;
    uint8_t b10BitPacked;
    uint8_t reserved0;
    uint8_t reserved1;
} FourCCMapping;

static const FourCCMapping FourCCMappings[] = {
    { UINT32_C(0x30323449), 1, 8, 0, 1, 0, 0, 0, 0 },
    { UINT32_C(0x56555949), 1, 8, 0, 1, 0, 0, 0, 0 },
    { UINT32_C(0x32315659), 1, 8, 0, 2, 0, 0, 0, 0 },
    { UINT32_C(0x32323449), 2, 8, 0, 1, 0, 0, 0, 0 },
    { UINT32_C(0x36315659), 2, 8, 0, 1, 0, 0, 0, 0 },
    { UINT32_C(0x4c413049), 1, 10, 0, 1, 0, 0, 0, 0 },
    { UINT32_C(0x4c413249), 2, 10, 0, 1, 0, 0, 0, 0 },
    { UINT32_C(0x3231564e), 1, 8, 0, 3, 0, 0, 0, 0 },
    { UINT32_C(0x3631564e), 2, 8, 0, 3, 0, 0, 0, 0 },
    { UINT32_C(0x30313050), 1, 10, 0, 3, 0, 0, 0, 0 },
    { UINT32_C(0x30313250), 2, 10, 0, 3, 0, 0, 0, 0 },
    { UINT32_C(0x30303859), 0, 8, 0, 0, 0, 0, 0, 0 },
    { UINT32_C(0x30313059), 0, 10, 0, 0, 0, 0, 0, 0 },
    { UINT32_C(0x38303654), 1, 8, 3, 3, 0, 0, 0, 0 },
    { UINT32_C(0x38323654), 2, 8, 3, 3, 0, 0, 0, 0 },
    { UINT32_C(0x386d3654), 0, 8, 3, 0, 0, 0, 0, 0 },
    { UINT32_C(0x41303654), 1, 10, 3, 3, 0, 0, 0, 0 },
    { UINT32_C(0x41323654), 2, 10, 3, 3, 0, 0, 0, 0 },
    { UINT32_C(0x416d3654), 0, 10, 3, 0, 0, 0, 0, 0 },
    { UINT32_C(0x38303554), 1, 8, 2, 3, 0, 0, 0, 0 },
    { UINT32_C(0x38323554), 2, 8, 2, 3, 0, 0, 0, 0 },
    { UINT32_C(0x386d3554), 0, 8, 2, 0, 0, 0, 0, 0 },
    { UINT32_C(0x41303554), 1, 10, 2, 3, 0, 0, 0, 0 },
    { UINT32_C(0x41323554), 2, 10, 2, 3, 0, 0, 0, 0 },
    { UINT32_C(0x416d3554), 0, 10, 2, 0, 0, 0, 0, 0 },
    { UINT32_C(0x38303643), 1, 8, 3, 3, 1, 0, 0, 0 },
    { UINT32_C(0x38323643), 2, 8, 3, 3, 1, 0, 0, 0 },
    { UINT32_C(0x386d3643), 0, 8, 3, 0, 1, 0, 0, 0 },
    { UINT32_C(0x41303643), 1, 10, 3, 3, 1, 0, 0, 0 },
    { UINT32_C(0x41323643), 2, 10, 3, 3, 1, 0, 0, 0 },
    { UINT32_C(0x416d3643), 0, 10, 3, 0, 1, 0, 0, 0 },
    { UINT32_C(0x38303543), 1, 8, 2, 3, 1, 0, 0, 0 },
    { UINT32_C(0x38323543), 2, 8, 2, 3, 1, 0, 0, 0 },
    { UINT32_C(0x386d3543), 0, 8, 2, 0, 1, 0, 0, 0 },
    { UINT32_C(0x41303543), 1, 10, 2, 3, 1, 0, 0, 0 },
    { UINT32_C(0x41323543), 2, 10, 2, 3, 1, 0, 0, 0 },
    { UINT32_C(0x416d3543), 0, 10, 2, 0, 1, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};

_Static_assert(sizeof(FourCCMapping) == 24, "FourCCMapping size");

int32_t AL_GetPicFormat(uint32_t tFourCC, AL_TPicFormat *pPicFormat)
{
    const FourCCMapping *pMapping = &FourCCMappings[0];

    if (UINT32_C(0x30323449) != tFourCC) {
        const FourCCMapping *pCurrent = &FourCCMappings[1];

        do {
            if (pCurrent->tFourCC == 0) {
                __assert("0 && \"Unknown fourCC\"",
                         "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common/FourCC.c",
                         0x5c, "AL_GetPicFormat");
            }
            pCurrent += 1;
        } while ((pCurrent - 1)->tFourCC != tFourCC);

        pMapping = pCurrent - 1;
    }

    pPicFormat->eChromaMode = pMapping->eChromaMode;
    pPicFormat->uBitDepth = pMapping->uBitDepth;
    pPicFormat->eStorageMode = pMapping->eStorageMode;
    pPicFormat->eChromaOrder = pMapping->eChromaOrder;
    pPicFormat->uFlags.fields.bCompressed = pMapping->bCompressed;
    pPicFormat->uFlags.fields.b10BitPacked = pMapping->b10BitPacked;
    pPicFormat->uFlags.fields.reserved0 = pMapping->reserved0;
    pPicFormat->uFlags.fields.reserved1 = pMapping->reserved1;
    return 1;
}

uint32_t AL_GetFourCC(AL_TPicFormat tPicFormat)
{
    const FourCCMapping *pMapping = &FourCCMappings[0];

    for (;;) {
        if (pMapping->tFourCC == 0) {
            __assert("0 && \"Unknown picture format\"",
                     "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common/FourCC.c",
                     0x72, "AL_GetFourCC");
        }

        if (pMapping->eStorageMode == tPicFormat.eStorageMode) {
            if (pMapping->eChromaOrder == tPicFormat.eChromaOrder) {
                if (pMapping->eChromaMode == tPicFormat.eChromaMode) {
                    if ((uint8_t)pMapping->uBitDepth == (uint8_t)tPicFormat.uBitDepth) {
                        if (pMapping->bCompressed == tPicFormat.uFlags.fields.bCompressed) {
                            if (pMapping->b10BitPacked == tPicFormat.uFlags.fields.b10BitPacked) {
                                return pMapping->tFourCC;
                            }
                        }
                    }
                }
            }
        }

        pMapping += 1;
    }
}

int32_t AL_GetChromaMode(uint32_t tFourCC)
{
    AL_TPicFormat tPicFormat;

    if (AL_GetPicFormat(tFourCC, &tPicFormat) != 0) {
        return tPicFormat.eChromaMode;
    }

    return -1;
}

uint32_t AL_GetBitDepth(uint32_t tFourCC)
{
    AL_TPicFormat tPicFormat;

    if (AL_GetPicFormat(tFourCC, &tPicFormat) != 0) {
        return (uint8_t)tPicFormat.uBitDepth;
    }

    return 0xff;
}

int32_t AL_GetPixelSize(uint32_t tFourCC)
{
    if (AL_GetBitDepth(tFourCC) >= 9U) {
        return 2;
    }

    return 1;
}

int32_t AL_GetSubsampling(uint32_t tFourCC, int32_t *pHScale, int32_t *pVScale)
{
    int32_t result = AL_GetChromaMode(tFourCC);

    if (result == 1) {
        *pHScale = 2;
        *pVScale = 2;
        return 2;
    }

    if (result == 2) {
        *pHScale = result;
        *pVScale = 1;
        return result;
    }

    *pHScale = 1;
    *pVScale = 1;
    return result;
}

uint32_t AL_Is10bitPacked(uint32_t tFourCC)
{
    AL_TPicFormat tPicFormat;

    if (AL_GetPicFormat(tFourCC, &tPicFormat) == 0) {
        return 0;
    }

    return tPicFormat.uFlags.fields.b10BitPacked;
}

int32_t AL_IsMonochrome(uint32_t tFourCC)
{
    return (uint32_t)AL_GetChromaMode(tFourCC) < 1U ? 1 : 0;
}

int32_t AL_IsSemiPlanar(uint32_t tFourCC)
{
    int32_t result;
    AL_TPicFormat tPicFormat;

    result = AL_GetPicFormat(tFourCC, &tPicFormat);
    if (result == 0) {
        return result;
    }

    return (uint32_t)(tPicFormat.eChromaOrder ^ 3) < 1U ? 1 : 0;
}

uint32_t AL_IsCompressed(uint32_t tFourCC)
{
    AL_TPicFormat tPicFormat;

    if (AL_GetPicFormat(tFourCC, &tPicFormat) == 0) {
        return 0;
    }

    return tPicFormat.uFlags.fields.bCompressed;
}

int32_t AL_IsTiled(uint32_t tFourCC)
{
    int32_t result;
    AL_TPicFormat tPicFormat;

    result = AL_GetPicFormat(tFourCC, &tPicFormat);
    if (result == 0) {
        return result;
    }

    return 0 < tPicFormat.eStorageMode ? 1 : 0;
}

int32_t AL_GetStorageMode(uint32_t tFourCC)
{
    AL_TPicFormat tPicFormat;

    if (AL_GetPicFormat(tFourCC, &tPicFormat) == 0) {
        return 0;
    }

    return tPicFormat.eStorageMode;
}
