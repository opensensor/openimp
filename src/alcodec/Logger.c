#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "alcodec/al_settings.h"

static uint8_t read_u8(const void *base, size_t offset)
{
    uint8_t value;

    memcpy(&value, (const uint8_t *)base + offset, sizeof(value));
    return value;
}

static int8_t read_s8(const void *base, size_t offset)
{
    int8_t value;

    memcpy(&value, (const uint8_t *)base + offset, sizeof(value));
    return value;
}

static uint16_t read_u16(const void *base, size_t offset)
{
    uint16_t value;

    memcpy(&value, (const uint8_t *)base + offset, sizeof(value));
    return value;
}

static int16_t read_s16(const void *base, size_t offset)
{
    int16_t value;

    memcpy(&value, (const uint8_t *)base + offset, sizeof(value));
    return value;
}

static uint32_t read_u32(const void *base, size_t offset)
{
    uint32_t value;

    memcpy(&value, (const uint8_t *)base + offset, sizeof(value));
    return value;
}

static int32_t read_s32(const void *base, size_t offset)
{
    int32_t value;

    memcpy(&value, (const uint8_t *)base + offset, sizeof(value));
    return value;
}

int32_t AL_Dump_TRCParam(AL_TRCParam *arg1)
{
    printf("eRCMode=0x%x\n", read_u32(arg1, 0x00));
    printf("uInitialRemDelay=%u\n", read_u32(arg1, 0x04));
    printf("uCPBSize=%u\n", read_u32(arg1, 0x08));
    printf("uFrameRate=%u\n", (uint32_t)read_u16(arg1, 0x0c));
    printf("uClkRatio=%u\n", (uint32_t)read_u16(arg1, 0x0e));
    printf("uTargetBitRate=%u\n", read_u32(arg1, 0x10));
    printf("uMaxBitRate=%u\n", read_u32(arg1, 0x14));
    printf("iInitialQP=%d\n", (int32_t)read_s16(arg1, 0x18));
    printf("iMinQP=%d\n", (int32_t)read_s16(arg1, 0x1a));
    printf("iMaxQP=%d\n", (int32_t)read_s16(arg1, 0x1c));
    printf("uIPDelta=%d\n", (int32_t)read_s16(arg1, 0x1e));
    printf("uPBDelta=%d\n", (int32_t)read_s16(arg1, 0x20));
    printf("bUseGoldenRef=%d\n", (uint32_t)read_u16(arg1, 0x22));
    printf("uPGoldenDelta=%d\n", (int32_t)read_s16(arg1, 0x24));
    printf("uGoldenRefFrequency=%d\n", (int32_t)read_s16(arg1, 0x26));
    printf("eOptions=0x%x\n", read_u32(arg1, 0x28));
    printf("uNumPel=%u\n", read_u32(arg1, 0x2c));
    printf("uMaxPSNR=%u\n", (uint32_t)read_u16(arg1, 0x30));
    printf("uMaxPelVal=%u\n", (uint32_t)read_u16(arg1, 0x32));
    return printf("pMaxPictureSize=[%u, %u, %u]\n",
                  read_u32(arg1, 0x34), read_u32(arg1, 0x38), read_u32(arg1, 0x3c));
}

int32_t AL_Dump_TGopParam(AL_TGopParam *arg1)
{
    printf("eMode=0x%x\n", read_u32(arg1, 0x00));
    printf("uGopLength=%u\n", (uint32_t)read_u16(arg1, 0x04));
    printf("uNumB=%u\n", (uint32_t)read_u8(arg1, 0x06));
    printf("uFreqGoldenRef=%u\n", (uint32_t)read_u8(arg1, 0x07));
    printf("uFreqIDR=%u\n", read_u32(arg1, 0x08));
    printf("bEnableLT=%u\n", (uint32_t)read_u8(arg1, 0x0c));
    printf("bLTRC=%u\n", (uint32_t)read_u8(arg1, 0x0d));
    printf("uFreqLT=%u\n", read_u32(arg1, 0x10));
    printf("eGdrMode=0x%x\n", read_u32(arg1, 0x14));
    return printf("tempDQP[4]={%d,%d,%d,%d}\n",
                  (int32_t)read_s8(arg1, 0x18), (int32_t)read_s8(arg1, 0x19),
                  (int32_t)read_s8(arg1, 0x1a), (int32_t)read_s8(arg1, 0x1b));
}

int32_t AL_Dump_TEncChanParam(AL_TEncChanParam *arg1)
{
    printf("iLayerID=%d\n", read_s32(arg1, 0x00));
    printf("uEncWidth=%u\n", (uint32_t)read_u16(arg1, 0x04));
    printf("uEncHeight=%u\n", (uint32_t)read_u16(arg1, 0x06));
    printf("uSrcWidth=%u\n", (uint32_t)read_u16(arg1, 0x08));
    printf("uSrcHeight=%u\n", (uint32_t)read_u16(arg1, 0x0a));
    printf("eVideoMode=0x%x\n", read_u32(arg1, 0x0c));
    printf("ePicFormat=0x%x\n", read_u32(arg1, 0x10));
    printf("eSrcMode=0x%x\n", read_u32(arg1, 0x14));
    printf("uSrcBitDepth=%u\n", (uint32_t)read_u8(arg1, 0x18));
    printf("eProfile=0x%x\n", read_u32(arg1, 0x1c));
    printf("uLevel=%u\n", (uint32_t)read_u8(arg1, 0x20));
    printf("uTier=%u\n", (uint32_t)read_u8(arg1, 0x21));
    printf("uSpsParam=0x%x\n", read_u32(arg1, 0x24));
    printf("uPpsParam=0x%x\n", read_u32(arg1, 0x28));
    printf("eEncOptions=0x%x\n", read_u32(arg1, 0x2c));
    printf("eEncTools=0x%x\n", read_u32(arg1, 0x30));
    printf("iBetaOffset=%d\n", (int32_t)read_s8(arg1, 0x34));
    printf("iTcOffset=%d\n", (int32_t)read_s8(arg1, 0x35));
    printf("iCbSliceQpOffset=%d\n", (int32_t)read_s8(arg1, 0x36));
    printf("iCrSliceQpOffset=%d\n", (int32_t)read_s8(arg1, 0x37));
    printf("iCbPicQpOffset=%d\n", (int32_t)read_s8(arg1, 0x38));
    printf("iCrPicQpOffset=%d\n", (int32_t)read_s8(arg1, 0x39));
    printf("uCuQPDeltaDepth=%u\n", (uint32_t)read_u8(arg1, 0x3a));
    printf("uCabacInitIdc=%u\n", (uint32_t)read_u8(arg1, 0x3b));
    printf("uNumCore=%u\n", (uint32_t)read_u8(arg1, 0x3c));
    printf("uSliceSize=%u\n", (uint32_t)read_u16(arg1, 0x3e));
    printf("uNumSlices=%u\n", (uint32_t)read_u16(arg1, 0x40));
    printf("uClipHrzRange=%u\n", (uint32_t)read_u16(arg1, 0x42));
    printf("uClipVrtRange=%u\n", (uint32_t)read_u16(arg1, 0x44));
    printf("pMeRange[2][2]={{%d,%d},{%d,%d}}\n",
           (int32_t)read_s16(arg1, 0x46), (int32_t)read_s16(arg1, 0x48),
           (int32_t)read_s16(arg1, 0x4a), (int32_t)read_s16(arg1, 0x4c));
    printf("uMaxCuSize=%u\n", (uint32_t)read_u8(arg1, 0x4e));
    printf("uMinCuSize=%u\n", (uint32_t)read_u8(arg1, 0x4f));
    printf("uMaxTuSize=%u\n", (uint32_t)read_u8(arg1, 0x50));
    printf("uMinTuSize=%u\n", (uint32_t)read_u8(arg1, 0x51));
    printf("uMaxTransfoDepthIntra=%u\n", (uint32_t)read_u8(arg1, 0x52));
    printf("uMaxTransfoDepthInter=%u\n", (uint32_t)read_u8(arg1, 0x53));
    printf("eEntropyMode=0x%x\n", read_u32(arg1, 0x54));
    printf("eWPMode=0x%x\n", read_u32(arg1, 0x58));
    printf("bUseGMV=%d\n", (uint32_t)read_u8(arg1, 0x5c));
    printf("LossLess=%d\n", (uint32_t)read_u8(arg1, 0x5d));
    printf("RestartInterval=%u\n", (uint32_t)read_u8(arg1, 0x5e));
    printf("AspectRatioUnit=%u\n", (uint32_t)read_u8(arg1, 0x60));
    printf("X_Density=%u\n", (uint32_t)read_u16(arg1, 0x62));
    printf("Y_Density=%u\n", (uint32_t)read_u16(arg1, 0x64));
    printf("uSRDThreshold=%u\n", (uint32_t)read_u16(arg1, 0x66));
    puts("--------AL_Dump_TRCParam(&param->tRCParam) start-------");
    AL_Dump_TRCParam((AL_TRCParam *)((uint8_t *)arg1 + 0x68));
    puts("----------AL_Dump_TRCParam(&param->tRCParam) end------");
    puts("---------AL_Dump_TGopParam(&param->tGopParam) start------");
    AL_Dump_TGopParam((AL_TGopParam *)((uint8_t *)arg1 + 0xa8));
    puts("----------AL_Dump_TGopParam(&param->tGopParam) end---------");
    printf("bSubframeLatency=%d\n", (uint32_t)read_u8(arg1, 0xc4));
    printf("eLdaCtrlMode=0x%x\n", read_u32(arg1, 0xc8));
    printf("LdaFactors[6]={%d, %d, %d, %d, %d, %d}\n",
           read_s32(arg1, 0xcc), read_s32(arg1, 0xd0), read_s32(arg1, 0xd4),
           read_s32(arg1, 0xd8), read_s32(arg1, 0xdc), read_s32(arg1, 0xe0));
    return printf("MaxNumMergeCand=%d\n", (int32_t)read_s8(arg1, 0xe4));
}

int32_t AL_Dump_TEncSettings(AL_TEncSettings *arg1)
{
    const uint8_t *base;
    const uint8_t *i;
    const uint8_t *i_1;

    base = (const uint8_t *)arg1;
    printf("##############################%s(%d) %dx%d profile=0x%08x start##########################\n",
           "AL_Dump_TEncSettings", 0x86, (uint32_t)read_u16(base, 0x04),
           (uint32_t)read_u16(base, 0x06), read_u32(base, 0x1c));
    i = base + 0x2a8;
    printf("------------debug TEncChanParam[%d] start--------------\n", 0);
    AL_Dump_TEncChanParam((AL_TEncChanParam *)arg1);
    printf("------------debug TEncChanParam[%d] end----------------\n", 0);
    printf("bEnableAUD=%d\n", (uint32_t)read_u8(base, 0xf0));
    printf("eEnableFillerData=%d\n", read_s32(base, 0xf4));
    printf("uEnableSEI=%d\n", read_s32(base, 0xf8));
    printf("eAspectRatio=%d\n", read_s32(base, 0xfc));
    printf("eColourDescription=%d\n", read_s32(base, 0x100));
    printf("eTransferCharacteristics=%d\n", read_s32(base, 0x104));
    printf("eColourMatrixCoeffs=%d\n", read_s32(base, 0x108));
    printf("eScalingList=%d\n", read_s32(base, 0x10c));
    printf("bDependentSlice=%d\n", (uint32_t)read_u8(base, 0x110));
    printf("bDisIntra=%d\n", (uint32_t)read_u8(base, 0x111));
    printf("bForceLoad=%d\n", (uint32_t)read_u8(base, 0x112));
    printf("uClipHrzRange=%d\n", (uint32_t)read_u16(base, 0x114));
    printf("uClipVrtRange=%d\n", (uint32_t)read_u16(base, 0x116));
    printf("eQpCtrlMode=%d\n", read_s32(base, 0x118));
    printf("eQpTableMode=%d\n", read_s32(base, 0x11c));
    printf("NumView=%d\n", read_s32(base, 0x120));
    printf("NumLayer=%d\n", read_s32(base, 0x124));
    puts("uint8_t ScalingList[4][6][64] =");
    puts("{");
    do {
        const uint8_t *s2_1;

        s2_1 = i - 0x180;
        puts("  {");
        do {
            const uint8_t *s5_1;
            int32_t fp_1;
            int32_t s7_1;

            s5_1 = s2_1;
            puts("    {");
            fp_1 = 1;
            s7_1 = 0;
            while (1) {
                if (s7_1 == 0) {
                    printf("0x%02x,", (uint32_t)*s5_1);
                } else if ((s7_1 & 0xf) != 0) {
                    printf("0x%02x,", (uint32_t)*s5_1);
                    if (fp_1 == 0x40) {
                        break;
                    }
                } else {
                    putchar(0x0a);
                    printf("0x%02x,", (uint32_t)*s5_1);
                    if (fp_1 == 0x40) {
                        break;
                    }
                }
                s7_1 += 1;
                fp_1 += 1;
                s5_1 = &s5_1[1];
            }
            s2_1 = &s2_1[0x40];
            puts("\n    },");
        } while (i != s2_1);
        i += 0x180;
        puts("  },");
    } while (i != base + 0x8a8);
    i_1 = base + 0x728;
    puts("};");
    puts("uint8_t SclFlag[4][6] =");
    puts("{");
    do {
        uint32_t v0_4;
        uint32_t a3_1;
        uint32_t a2_1;
        uint32_t v0_5;
        uint32_t a1_19;
        uint32_t v0_6;

        v0_4 = (uint32_t)i_1[5];
        a3_1 = (uint32_t)i_1[2];
        a2_1 = (uint32_t)i_1[1];
        v0_5 = (uint32_t)i_1[4];
        a1_19 = (uint32_t)i_1[0];
        v0_6 = (uint32_t)i_1[3];
        i_1 = &i_1[6];
        printf("  {0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x},\n",
               a1_19, a2_1, a3_1, v0_6, v0_5, v0_4);
    } while (i_1 != base + 0x740);
    puts("};");
    printf("DcCoeff[8] = {0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x}\n",
           (uint32_t)base[0x740], (uint32_t)base[0x741], (uint32_t)base[0x742],
           (uint32_t)base[0x743], (uint32_t)base[0x744], (uint32_t)base[0x745],
           (uint32_t)base[0x746], (uint32_t)base[0x747]);
    printf("DcCoeffFlag[8] = {0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x}\n",
           (uint32_t)base[0x748], (uint32_t)base[0x749], (uint32_t)base[0x74a],
           (uint32_t)base[0x74b], (uint32_t)base[0x74c], (uint32_t)base[0x74d],
           (uint32_t)base[0x74e], (uint32_t)base[0x74f]);
    printf("bEnableWatchdog=%d\n", (uint32_t)base[0x750]);
    return printf("##############################%s(%d) %dx%d profile=0x%08x end##########################\n",
                  "AL_Dump_TEncSettings", 0xc3, (uint32_t)read_u16(base, 0x04),
                  (uint32_t)read_u16(base, 0x06), read_u32(base, 0x1c));
}

const char *AL_Encoder_ErrorToString(int32_t arg1)
{
    switch (arg1) {
    case 0:
        return "Success";
    case 2:
        return "Warning some LCU exceed the maximum allowed bits";
    case 3:
        return "Warning num slices have been adjusted";
    case 0x87:
        return "Memory shortage detected (DMA, embedded memory or virtual memory)";
    case 0x88:
        return "Stream Error: Stream overflow";
    case 0x89:
        return "Stream Error: Too many slices";
    case 0x8d:
        return "Channel creation failed, no channel available";
    case 0x8e:
        return "Channel creation failed, processing power of the available cores insufficient";
    case 0x8f:
        return "Channel creation failed, couldn't spread the load on enough cores";
    case 0x90:
        return "Channel creation failed, request was malformed";
    case 0x93:
        return "Intermediate buffer overflow";
    default:
        return "Unknown error";
    }
}
