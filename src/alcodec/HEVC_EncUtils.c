#include <stdint.h>

#include "alcodec/al_buffer.h"
#include "alcodec/al_fourcc.h"
#include "alcodec/al_nal.h"
#include "alcodec/al_rtos.h"

extern char _gp;
extern uint8_t PicStructToFieldNumber[];
extern int32_t __assert(const char *expression, const char *file, int32_t line, const char *function, ...);

#define READ_U8(base, off) (*(uint8_t *)((uint8_t *)(base) + (off)))
#define READ_S8(base, off) (*(int8_t *)((uint8_t *)(base) + (off)))
#define READ_U16(base, off) (*(uint16_t *)((uint8_t *)(base) + (off)))
#define READ_S16(base, off) (*(int16_t *)((uint8_t *)(base) + (off)))
#define READ_U32(base, off) (*(uint32_t *)((uint8_t *)(base) + (off)))
#define READ_S32(base, off) (*(int32_t *)((uint8_t *)(base) + (off)))
#define READ_PTR(base, off) (*(void **)((uint8_t *)(base) + (off)))
#define WRITE_U8(base, off, val) (*(uint8_t *)((uint8_t *)(base) + (off)) = (uint8_t)(val))
#define WRITE_U16(base, off, val) (*(uint16_t *)((uint8_t *)(base) + (off)) = (uint16_t)(val))
#define WRITE_U32(base, off, val) (*(uint32_t *)((uint8_t *)(base) + (off)) = (uint32_t)(val))

static const uint8_t AL_HEVC_DefaultScalingLists8x8[0x80] = {
    0x10, 0x10, 0x10, 0x10, 0x11, 0x12, 0x15, 0x18, 0x10, 0x10, 0x10, 0x10, 0x11, 0x13, 0x16, 0x19,
    0x10, 0x10, 0x11, 0x12, 0x14, 0x16, 0x19, 0x1d, 0x10, 0x10, 0x12, 0x15, 0x18, 0x1b, 0x1f, 0x24,
    0x11, 0x11, 0x14, 0x18, 0x1e, 0x23, 0x29, 0x2f, 0x12, 0x13, 0x16, 0x1b, 0x23, 0x2c, 0x36, 0x41,
    0x15, 0x16, 0x19, 0x1f, 0x29, 0x36, 0x46, 0x58, 0x18, 0x19, 0x1d, 0x24, 0x2f, 0x41, 0x58, 0x73,
    0x10, 0x10, 0x10, 0x10, 0x11, 0x12, 0x14, 0x18, 0x10, 0x10, 0x10, 0x11, 0x12, 0x14, 0x18, 0x19,
    0x10, 0x10, 0x11, 0x12, 0x14, 0x18, 0x19, 0x1c, 0x10, 0x11, 0x12, 0x14, 0x18, 0x19, 0x1c, 0x21,
    0x11, 0x12, 0x14, 0x18, 0x19, 0x1c, 0x21, 0x29, 0x12, 0x14, 0x18, 0x19, 0x1c, 0x21, 0x29, 0x36,
    0x14, 0x18, 0x19, 0x1c, 0x21, 0x29, 0x36, 0x47, 0x18, 0x19, 0x1c, 0x21, 0x29, 0x36, 0x47, 0x5b,
};

static const uint8_t AL_HEVC_DefaultScalingLists4x4[0x20] = {
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
};

void AL_HEVC_GenerateHwScalingList(void *arg1, void *arg2); /* forward decl, ported by T19 */
int32_t AL_HEVC_WriteEncHwScalingList(void *arg1, void *arg2, uint32_t *arg3); /* forward decl, ported by T19 */
void *AL_GetHevcRbspWriter(void); /* forward decl, ported by T36 */
uintptr_t *AL_ExtractNalsData(uintptr_t *dst, void *pEnc, int32_t flag); /* T45 */
/* GenerateSections actual prototype in T45 Sections.c is 14-arg with
 * a CreateNutHeaderFunc typedef. Use an opaque matching prototype here. */
int32_t GenerateSections(void *a1, void *a2, int32_t a3, int32_t a4, uint32_t a5, int32_t a6, int32_t a7,
                         void *a8, void *a9, void *a10, void *a11, int32_t a12, char a13, char a14); /* T45 */
int32_t AL_GetSrcBufferFromStatus(int32_t status, void *pEnc); /* forward decl, ported by T44 later */
uint32_t AL_PixMapBuffer_GetFourCC(AL_TBuffer *arg1); /* forward decl, ported by T<N> later */
int16_t *AL_PixMapBuffer_GetDimension(int16_t *arg1, AL_TBuffer *arg2); /* forward decl, ported by T<N> later */
int32_t AL_IsGdrEnabled(void *pEncSettings); /* forward decl, ported by T44 later */
int32_t AL_Common_Encoder_SetHlsParam(void *pEnc, int32_t flag, int32_t value); /* T44 */
void *AL_Common_Encoder_SetME(int32_t a1, int32_t a2, int32_t a3, int32_t a4, int8_t *chanParam); /* T44 */
int32_t AL_Common_Encoder_ComputeRCParam(int32_t a1, int32_t a2, int32_t a3, int32_t a4, int16_t *rcParam); /* T44 */
int32_t AL_UpdateAspectRatio(uint8_t *arg1, int32_t arg2, int32_t arg3, int32_t arg4); /* forward decl */
int32_t AL_H273_ColourDescToColourPrimaries(int32_t arg1); /* forward decl, ported by T14 later */
int32_t AL_Reduction(int32_t *arg1, int32_t *arg2); /* forward decl, ported by T14 later */
uint64_t __udivdi3(uint64_t num, uint64_t den); /* libgcc 64-bit unsigned div */

static int16_t AL_sUpdateProfileTierLevel_constprop_3(uint8_t *arg1, uint8_t *arg2);
static int32_t shouldReleaseSource(void);
static int32_t updateHlsAndWriteSections(uint8_t *arg1, uint8_t *arg2, int32_t arg3, int32_t arg4, int32_t arg5);
static int32_t preprocessEp1(uint8_t *arg1, int32_t *arg2);
static int32_t generateNals(uint8_t *arg1, int32_t arg2, char arg3);
static int32_t ConfigureChannel(uint8_t *arg1, uint8_t *arg2, uint8_t *arg3);
static char *GetNalHeaderHevc(char *arg1, int32_t arg2, int32_t arg3, char arg4, char arg5);
static uint32_t *CreateHevcNuts(uint32_t *arg1);
int32_t AL_CreateHevcEncoder(int32_t (**arg1)());
int32_t HEVC_GenerateSections(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4, char arg5);

uint8_t AL_HEVC_GenerateFilterParam(uint8_t *arg1, int32_t arg2, int32_t arg3, int32_t arg4, char arg5, char arg6)
{
    int32_t v1 = (int32_t)(int8_t)arg5;
    int32_t t0 = (int32_t)(int8_t)arg6;
    int32_t v0 = arg4 & 0x1000;
    uint8_t result;

    if ((arg3 & 4) == 0 || v0 != 0) {
        int32_t a2 = v1 | t0;

        WRITE_U8(arg1, 0x752, (uint8_t)(a2 > 0));
        if (arg2 != 0)
            goto label_59a9c;
        result = (uint8_t)((uint32_t)v0 >> 12);
        if (a2 != 0)
            goto label_59aa4;
        return result;
    }

    WRITE_U8(arg1, 0x752, 1);
    if (arg2 == 0)
        goto label_59aa0;

label_59a9c:
    WRITE_U8(arg1, 0x752, 1);

label_59aa0:
    result = (uint8_t)((uint32_t)v0 >> 12);

label_59aa4:
    {
        int32_t a3 = ((uint32_t)arg4 >> 13) & 1;

        WRITE_U8(arg1, 0x753, result);
        WRITE_U8(arg1, 0x754, (uint8_t)a3);
        if (a3 == 0)
            return result;
        WRITE_U8(arg1, 0x755, (uint8_t)v1);
        WRITE_U8(arg1, 0x756, (uint8_t)t0);
    }

    return result;
}

static int16_t AL_sUpdateProfileTierLevel_constprop_3(uint8_t *arg1, uint8_t *arg2)
{
    uint8_t v0;
    uint32_t v1;
    uint32_t v1_1;
    uint8_t a0_2;

    (void)_gp;
    Rtos_Memset(arg1, 0, 0x1cc);
    v0 = READ_U8(arg2, 0x1c);
    v1 = (uint32_t)v0;
    WRITE_U8(arg1, 2, (uint8_t)v1);
    WRITE_U8(arg1, 1, READ_U8(arg2, 0x21));
    if (READ_S32(arg2, 0xc) == 0) {
        WRITE_U8(arg1, 0x23, 1);
        WRITE_U8(arg1, 0x26, 1);
        v1_1 = (uint32_t)READ_U8(arg2, 0x20);
        a0_2 = (uint8_t)(v1_1 << 1);
        if (v1_1 != 0xff)
            goto label_59b44;
        goto label_59c64;
    }

    if (v1 == 4) {
        int32_t v1_3 = READ_S32(arg2, 0x1c) & 0xffff00;

        WRITE_U8(arg1, 0x2d, (uint8_t)((uint32_t)v1_3 >> 20) & 1);
        WRITE_U8(arg1, 0x2e, (uint8_t)((uint32_t)v1_3 >> 19) & 1);
        WRITE_U8(arg1, 0x2f, (uint8_t)((uint32_t)v1_3 >> 18) & 1);
        WRITE_U8(arg1, 0x2a, (uint8_t)((uint32_t)v1_3 >> 23));
        WRITE_U8(arg1, 0x2b, (uint8_t)((uint32_t)v1_3 >> 22) & 1);
        WRITE_U8(arg1, 0x2c, (uint8_t)((uint32_t)v1_3 >> 21) & 1);
    }

    {
        int32_t a2_2 = READ_S32(arg2, 0x1c);
        uint8_t a0_6 = (uint8_t)((uint32_t)a2_2 >> 11) & 1;
        int32_t v1_8;
        uint8_t v1_11;

        if ((uint32_t)a2_2 >> 24 != 0)
            a0_6 = 0;
        WRITE_U8(arg1, 0x30,
                 (uint8_t)(a0_6 | (uint8_t)(((uint32_t)((a2_2 & 0xff0200ff) - 0x1020004)) < 1U)));
        v1_8 = READ_S32(arg2, 0x1c);
        if ((v1_8 & 0xff0100ff) == 0x1010004)
            v1_11 = 1;
        else
            v1_11 = (uint8_t)(((uint32_t)((v1_8 & 0xff0000ff) - 0x1000003)) < 1U);
        WRITE_U8(arg1, 0x31, v1_11);
        WRITE_U8(arg1, 0x32,
                 (uint8_t)(((uint32_t)((READ_S32(arg2, 0x1c) & 0xff0080ff) - 0x1008004)) < 1U));
        v1_1 = (uint32_t)READ_U8(arg2, 0x20);
        a0_2 = (uint8_t)(v1_1 << 1);
        if (v1_1 != 0xff)
            goto label_59b44;
    }

label_59c64:
    WRITE_U8(arg1, 0x33, 0);
    WRITE_U8(arg1, (uint32_t)v0 + 3, 1);
    {
        int16_t v0_8 = (int16_t)(((uint32_t)READ_U32(arg2, 0x1c) >> 8) & 0xffff);

        WRITE_U16(arg1, 0x28, (uint16_t)v0_8);
        return v0_8;
    }

label_59b44:
    WRITE_U8(arg1, 0x33, (uint8_t)(v1_1 + a0_2));
    WRITE_U8(arg1, (uint32_t)v0 + 3, 1);
    {
        int16_t v0_4 = (int16_t)(((uint32_t)READ_U32(arg2, 0x1c) >> 8) & 0xffff);

        WRITE_U16(arg1, 0x28, (uint16_t)v0_4);
        return v0_4;
    }
}

int32_t AL_HEVC_PreprocessScalingList(int32_t arg1, int32_t *arg2)
{
    uint8_t var_5dd0[0x5dd0];

    (void)_gp;
    AL_HEVC_GenerateHwScalingList((void *)(intptr_t)arg1, var_5dd0);
    AL_HEVC_WriteEncHwScalingList((void *)(intptr_t)arg1, var_5dd0, (uint32_t *)(intptr_t)(*arg2 + 0x100));
    arg2[5] |= 0x10;
    return arg2[5];
}

char *AL_HEVC_GenerateVPS(char *arg1, void *arg2, char arg3)
{
    int32_t a0;
    char v1_3;
    int32_t s2;
    char s3;
    char s4;
    char a2_2;
    char *i;

    (void)_gp;
    arg1[1] = 1;
    arg1[2] = 1;
    arg1[0] = 0;
    arg1[3] = 0;
    a0 = READ_S32(arg2, 0xa8);
    if ((a0 & 4) == 0) {
        s3 = 0;
        s4 = 0;
        v1_3 = 0;
        s2 = 1;
    } else {
        uint32_t v0_1 = (uint32_t)READ_U8(arg2, 0xae);
        int32_t a2 = (int32_t)(v0_1 ^ 0xf);
        int32_t v0_2;
        int32_t v1_1 = 2;

        if (v0_1 == 7) {
            s2 = 3;
            v0_2 = 4;
        } else {
            if (a2 == 0)
                v1_1 = 4;
            s2 = v1_1;
            v0_2 = 3;
            if (a2 == 0)
                v0_2 = 5;
        }

        if ((a0 & 1) != 0) {
            v1_3 = (char)(s2 - 1);
            s3 = (char)(s2 - 1);
            s4 = 1;
        } else {
            s3 = (char)s2;
            v1_3 = (char)(v0_2 - 1);
            s2 = v0_2;
            s4 = 1;
        }
    }

    arg1[4] = v1_3;
    arg1[5] = 1;
    AL_sUpdateProfileTierLevel_constprop_3((uint8_t *)&arg1[8], arg2);
    arg1[6] = s4;
    a2_2 = (char)(arg3 - s3);
    i = arg1;
    do {
        i[0x1d4] = a2_2;
        i[0x1dc] = a2_2;
        i = &i[1];
        a2_2 = (char)(a2_2 + 1);
    } while (&arg1[s2] != i);
    arg1[0x204] = 0;
    arg1[0x288] = 0;
    arg1[0xbf4] = 0;
    WRITE_U16(arg1, 0x1e4, 0);
    WRITE_U16(arg1, 0x206, 0);
    return i;
}

uint32_t AL_HEVC_GenerateSPS_Format(uint8_t *arg1, int32_t arg2, char arg3, char arg4, int16_t arg5, int16_t arg6,
                                    char arg7)
{
    uint32_t v0_5 = (uint32_t)(uint8_t)arg7;

    if (v0_5 != 0) {
        WRITE_U8(arg1, 0x1d1, 0);
        return v0_5;
    }

    {
        uint32_t t1 = (uint32_t)(uint16_t)arg5;
        uint32_t t2 = (uint32_t)(uint16_t)arg6;
        int16_t v1_2 = (int16_t)(((t1 + 7) >> 3) << 3);
        int16_t v0_2 = (int16_t)(((t2 + 7) >> 3) << 3);
        uint32_t t0 = (uint32_t)(uint16_t)v1_2;
        uint32_t t3 = (uint32_t)(uint16_t)v0_2;
        int32_t t0_1;

        WRITE_U8(arg1, 0x1d3, (uint8_t)arg2);
        WRITE_U8(arg1, 0x1d4, 0);
        WRITE_U16(arg1, 0x1d6, (uint16_t)t0);
        WRITE_U16(arg1, 0x1d8, (uint16_t)t3);
        if (t1 == t0) {
            t0_1 = arg2 - 1;
            if (t2 == t3) {
                WRITE_U8(arg1, 0x1da, 0);
                goto label_after_conformance;
            }
        } else {
            t0_1 = arg2 - 1;
        }

        WRITE_U8(arg1, 0x1da, 1);

label_after_conformance:
        {
            int32_t a1;
            int32_t t3_1;

            if ((uint32_t)t0_1 < 2U) {
                int32_t t0_4 = 1;

                t3_1 = 2;
                if (arg2 == 1)
                    t0_4 = 2;
                a1 = t0_4;
            } else {
                t3_1 = 1;
                a1 = 1;
            }

            if (t3_1 == 0)
                __builtin_trap();
            WRITE_U16(arg1, 0x1dc, 0);
            WRITE_U16(arg1, 0x1e0, 0);
            WRITE_U16(arg1, 0x1de, (uint16_t)(((uint32_t)(uint16_t)v1_2 - t1) / (uint32_t)t3_1));
            if (a1 == 0)
                __builtin_trap();
            v0_2 = (int16_t)(((uint32_t)(uint16_t)v0_2 - t2) / (uint32_t)a1);
            WRITE_U16(arg1, 0x1e2, (uint16_t)v0_2);
            WRITE_U8(arg1, 0x1e4, (uint8_t)(arg3 - 8));
            WRITE_U8(arg1, 0x1e5, (uint8_t)(arg4 - 8));
            return (uint32_t)(uint16_t)v0_2;
        }
    }
}

int32_t AL_HEVC_MultiLayerExtSpsFlag(uint8_t *arg1, int32_t arg2)
{
    if (arg2 == 0)
        return 0;
    return (((uint32_t)READ_U8(arg1, 2) ^ 7U) < 1U) ? 1 : 0;
}

uint32_t AL_HEVC_GenerateSPS(uint8_t *arg1, uint8_t *arg2, uint8_t *arg3, char arg4, int32_t arg5)
{
    int32_t v0;
    int32_t s2 = 1;
    int32_t s3;

    (void)_gp;
    WRITE_U8(arg1, 0x873, 0);
    WRITE_U8(arg1, 0x874, 0);
    WRITE_U8(arg1, 0x2b6b, 1);
    arg1[0] = 0;
    v0 = READ_S32(arg3, 0xa8);
    if ((v0 & 4) != 0) {
        uint32_t a0_1 = (uint32_t)READ_U8(arg3, 0xae);
        int32_t a1 = 3;

        if (a0_1 == 7)
            s2 = 4 - (v0 & 1);
        else {
            if (a0_1 == 0xf)
                a1 = 5;
            s2 = a1 - (v0 & 1);
        }
    }

    if (arg5 != 0) {
        WRITE_U8(arg1, 2, 7);
        s3 = AL_HEVC_MultiLayerExtSpsFlag(arg1, arg5);
        if (s3 != 0)
            WRITE_U8(arg1, 3, 1);
        AL_sUpdateProfileTierLevel_constprop_3(&arg1[4], arg3);
    } else {
        WRITE_U8(arg1, 1, (uint8_t)(s2 - 1));
        s3 = AL_HEVC_MultiLayerExtSpsFlag(arg1, arg5);
        if (s3 != 0)
            WRITE_U8(arg1, 3, 1);
        AL_sUpdateProfileTierLevel_constprop_3(&arg1[4], arg3);
    }

    WRITE_U8(arg1, 0x1d0, (uint8_t)arg5);
    {
        int32_t a1_2 = READ_S32(arg3, 0x10);

        AL_HEVC_GenerateSPS_Format(arg1, ((uint32_t)a1_2 >> 8) & 0xf, (char)(a1_2 & 0xf),
                                   (char)(((uint32_t)a1_2 >> 4) & 0xf), READ_U16(arg3, 4), READ_U16(arg3, 6),
                                   (char)s3);
    }

    if (s3 == 0)
        WRITE_U8(arg1, 0x1e6, (READ_U8(arg3, 0x24) & 0xf) - 3);
    WRITE_U8(arg1, 0x1e7, 1);
    {
        char a3_3 = (char)(arg4 - ((uint8_t)s2 - 1));
        uint8_t *i = arg1;

        do {
            WRITE_U8(i, 0x1e8, (uint8_t)a3_3);
            WRITE_U8(i, 0x1f0, 0);
            i = &i[1];
            a3_3 = (char)(a3_3 + 1);
        } while (i != &arg1[s2]);
    }

    WRITE_U16(arg1, 0x1f8, 0);
    WRITE_U8(arg1, 0x218, READ_U8(arg3, 0x4f) - 3);
    WRITE_U8(arg1, 0x219, READ_U8(arg3, 0x4e) - READ_U8(arg3, 0x4f));
    WRITE_U8(arg1, 0x21a, READ_U8(arg3, 0x51) - 2);
    WRITE_U8(arg1, 0x21b, READ_U8(arg3, 0x50) - READ_U8(arg3, 0x51));
    WRITE_U8(arg1, 0x21d, READ_U8(arg3, 0x53));
    WRITE_U8(arg1, 0x21c, READ_U8(arg3, 0x52));
    {
        int32_t v0_17 = READ_S32(arg2, 0x10c);

        WRITE_U8(arg1, 0x21e, (uint8_t)(v0_17 > 0));
        if (s3 != 0) {
            int32_t a0_22 = (((uint32_t)v0_17 ^ 2U) < 1U) ? 1 : 0;

            WRITE_U8(arg1, 0x21f, (uint8_t)a0_22);
            if (a0_22 == 0)
                goto label_5a11c;
            WRITE_U8(arg1, 0x220, 0);
            WRITE_U8(arg1, 0x872, 0);
        } else {
            if (READ_U8(arg1, 0x21f) == 0)
                goto label_5a11c;
            WRITE_U8(arg1, 0x220, 0);
            WRITE_U8(arg1, 0x872, 0);
        }

        if (v0_17 == 1) {
            uint8_t *i_2 = &arg1[0x6de];
            const uint8_t *s2_2 = AL_HEVC_DefaultScalingLists8x8;
            const uint8_t *s5_1 = AL_HEVC_DefaultScalingLists4x4;
            uint8_t *s3_2 = &arg1[0x258];

            do {
                const uint8_t *a1_21;

                Rtos_Memcpy(i_2, s2_2, 0x40);
                s3_2 += 3;
                Rtos_Memcpy(i_2 - 0x180, s2_2, 0x40);
                Rtos_Memcpy(i_2 - 0x140, s2_2, 0x40);
                Rtos_Memcpy(i_2 - 0x100, s2_2, 0x40);
                Rtos_Memcpy(i_2 - 0x300, s2_2, 0x40);
                Rtos_Memcpy(i_2 - 0x2c0, s2_2, 0x40);
                a1_21 = s2_2;
                s2_2 += 0x40;
                Rtos_Memcpy(i_2 - 0x280, a1_21, 0x40);
                Rtos_Memcpy(i_2 - 0x480, s5_1, 0x10);
                Rtos_Memcpy(i_2 - 0x440, s5_1, 0x10);
                {
                    uint8_t *a0_46 = i_2 - 0x400;

                    i_2 += 0xc0;
                    Rtos_Memcpy(a0_46, s5_1, 0x10);
                }
                s5_1 += 0x10;
                *(s3_2 - 3) = 0x10;
                *(s3_2 - 9) = 0x10;
                *(s3_2 - 8) = 0x10;
                *(s3_2 - 7) = 0x10;
            } while (i_2 != &arg1[0x85e]);
            WRITE_U8(arg1, 0x872, 0);
        } else {
            int32_t var_40_1 = -12;
            int32_t var_34_1 = -2;
            int32_t i_1 = 0;

            while (i_1 != 4) {
                int32_t v0_48 = 0x40;
                int32_t s6_1 = i_1 * 6;
                int32_t j = 0;
                uint8_t *s2_1;
                uint8_t *s1_3;
                uint8_t *v0_49;

                if (i_1 == 0)
                    v0_48 = 0x10;
                s2_1 = &arg1[s6_1];
                s1_3 = &arg1[var_34_1 * 6];
                v0_49 = s2_1;
                do {
                    WRITE_U8(v0_49, 0x222, 0);
                    WRITE_U8(v0_49, 0x23a, 0);
                    if (i_1 >= 2) {
                        uint8_t *v0_52;

                        if (i_1 == 3 && j == i_1)
                            v0_52 = arg2 + 7;
                        else
                            v0_52 = arg2 + var_40_1 + j;
                        if (READ_U8(v0_52, 0x748) == 0) {
                            WRITE_U8(s1_3, j + 0x252, 0x10);
                            Rtos_Memcpy(&arg1[((s6_1 + j) << 6) + 0x25e],
                                        &AL_HEVC_DefaultScalingLists8x8[(j / 3) << 6], 0x40);
                        } else {
                            int32_t a0_29 = (s6_1 + j) << 6;

                            WRITE_U8(s1_3, j + 0x252, READ_U8(v0_52, 0x740));
                            WRITE_U8(s2_1, j + 0x222, 1);
                            Rtos_Memcpy(&arg1[a0_29 + 0x25e], arg2 + a0_29 + 0x128, (size_t)v0_48);
                        }
                    } else if (READ_U8(arg2, s6_1 + j + 0x728) == 0) {
                        if (i_1 != 0) {
                            if (i_1 != 1)
                                __builtin_trap();
                            WRITE_U8(s1_3, j + 0x252, 0x10);
                            Rtos_Memcpy(&arg1[((s6_1 + j) << 6) + 0x25e],
                                        &AL_HEVC_DefaultScalingLists8x8[(j / 3) << 6], 0x40);
                        } else {
                            Rtos_Memcpy(&arg1[(j << 6) + 0x25e], &AL_HEVC_DefaultScalingLists4x4[(j / 3) << 4], 0x10);
                            WRITE_U8(s2_1, j + 0x222, 1);
                        }
                    } else {
                        int32_t a0_29 = (s6_1 + j) << 6;

                        WRITE_U8(s1_3, j + 0x252, READ_U8(arg2, s6_1 + j + 0x740));
                        WRITE_U8(s2_1, j + 0x222, 1);
                        Rtos_Memcpy(&arg1[a0_29 + 0x25e], arg2 + a0_29 + 0x128, (size_t)v0_48);
                    }

                    {
                        int32_t v0_58 = 1;

                        if (i_1 == 3)
                            v0_58 = 3;
                        j += v0_58;
                        v0_49 = s2_1 + j;
                    }
                } while (j < 6);
                i_1 += 1;
                var_34_1 += 1;
                var_40_1 += 6;
            }
            WRITE_U8(arg1, 0x872, 0);
        }

label_5a11c:
        WRITE_U8(arg1, 0x221, (uint8_t)((((uint32_t)v0_17 ^ 2U) < 1U) ? 1 : 0));
        if (v0_17 == 2) {
            WRITE_U8(arg1, 0x220, 0);
            WRITE_U8(arg1, 0x872, 0);
        }
    }

    {
        int32_t v0_19 = READ_S32(arg3, 0x30) & 0x800;

        WRITE_U8(arg1, 0x874, (uint8_t)(v0_19 > 0));
        if (v0_19 != 0) {
            WRITE_U8(arg1, 0x875, (READ_U8(arg3, 0x10) & 0xf) - 1);
            WRITE_U8(arg1, 0x876, ((READ_U8(arg3, 0x10) >> 4) & 0xf) - 1);
            WRITE_U8(arg1, 0x878, 0);
            WRITE_U8(arg1, 0x877, READ_U8(arg3, 0x4e) - 3);
            WRITE_U8(arg1, 0x879, (uint8_t)((((uint32_t)READ_U32(arg3, 0x30) ^ 4U) >> 2) & 1));
            WRITE_U8(arg1, 0x87a, 0);
        }
    }

    {
        int32_t v0_31 = READ_S32(arg3, 0x24);
        uint32_t v0_34;
        uint32_t t0;
        int32_t rate_num;

        WRITE_U8(arg1, 0x2b05, 0);
        WRITE_U8(arg1, 0x2b04, (uint8_t)((((uint32_t)v0_31 >> 14) & 0x3f) > 0));
        WRITE_U8(arg1, 0x2b69, 1);
        v0_34 = (uint32_t)READ_U8(arg3, 0x4e);
        WRITE_U8(arg1, 0x2b7b, 0);
        WRITE_U8(arg1, 0x2b7c, 0);
        WRITE_U8(arg1, 0x2b6a, (uint8_t)(((v0_34 < 5U) ? 1 : 0) ^ 1));
        WRITE_U8(arg1, 0x2b7d, 0);
        WRITE_U8(arg1, 0x2b7e, 0);
        if (READ_S32(arg3, 0xc) == 0) {
            WRITE_U8(arg1, 0x2b7f, 0);
            WRITE_U8(arg1, 0x2b80, 0);
            WRITE_U8(arg1, 0x2b81, 0);
        }
        AL_UpdateAspectRatio(&arg1[0x2b6c], READ_U16(arg3, 4), READ_U16(arg3, 6), READ_S32(arg2, 0xfc));
        WRITE_U8(arg1, 0x2b74, 1);
        WRITE_U8(arg1, 0x2b75, 5);
        WRITE_U8(arg1, 0x2b77, 1);
        WRITE_U8(arg1, 0x2b72, 0);
        WRITE_U8(arg1, 0x2b76, 0);
        WRITE_U8(arg1, 0x2b78, (uint8_t)AL_H273_ColourDescToColourPrimaries(READ_S32(arg2, 0x100)));
        WRITE_U8(arg1, 0x2b79, READ_U8(arg2, 0x104));
        WRITE_U8(arg1, 0x2b94, (uint8_t)(arg5 < 1));
        WRITE_U8(arg1, 0x2b7a, READ_U8(arg2, 0x108));
        t0 = (uint32_t)READ_U16(arg3, 0x76);
        rate_num = READ_U16(arg3, 0x74) * 0x3e8;
        WRITE_U32(arg1, 0x2b9c, (uint32_t)rate_num);
        WRITE_U32(arg1, 0x2b98, t0);
        AL_Reduction((int32_t *)&arg1[0x2b9c], (int32_t *)&arg1[0x2b98]);
    }

    WRITE_U8(arg1, 0x2ba0, 0);
    WRITE_U8(arg1, 0x2bae, 0);
    WRITE_U8(arg1, 0x2bac, 0);
    WRITE_U8(arg1, 0x2bad, 0);
    WRITE_U8(arg1, 0x2ba8, 0);
    WRITE_U8(arg1, 0x3060, 0);
    WRITE_U8(arg1, 0x3061, 0);
    WRITE_U8(arg1, 0x3062, 1);
    WRITE_U8(arg1, 0x3063, 1);
    WRITE_U8(arg1, 0x3064, 0);
    WRITE_U8(arg1, 0x3065, 0);
    WRITE_U8(arg1, 0x3066, 0);
    WRITE_U8(arg1, 0x3067, 0xf);
    WRITE_U8(arg1, 0x3068, 0xf);
    WRITE_U8(arg1, 0x306c, (uint8_t)(arg5 > 0));
    if (arg5 == 0) {
        uint32_t result = (uint32_t)READ_U8(arg1, 0x306e);

        if (result == 0)
            return 0xf;
        return result;
    }

    WRITE_U8(arg1, 0x306d, 0);
    WRITE_U8(arg1, 0x306e, 1);
    WRITE_U8(arg1, 0x306f, 0);
    WRITE_U8(arg1, 0x3070, 0);
    WRITE_U8(arg1, 0x3071, 0);
    WRITE_U8(arg1, 0x3073, 0);
    return 0xf;
}

uint32_t AL_HEVC_GeneratePPS(char *arg1, void *arg2, void *arg3, char arg4, int32_t arg5)
{
    char v0 = (char)(arg5 & 0xff);
    uint8_t v1 = 1;

    (void)_gp;
    arg1[0] = v0;
    arg1[1] = v0;
    arg1[0x11] = 0;
    arg1[0xdaa] = 0;
    arg1[2] = (char)READ_U8(arg2, 0x110);
    arg1[3] = 0;
    arg1[4] = (char)((READ_U32(arg3, 0x28) >> 1) & 1);
    arg1[5] = (char)(arg4 - 1);
    arg1[6] = (char)(arg4 - 1);
    arg1[7] = 0;
    arg1[8] = (char)((READ_U32(arg3, 0x30) >> 6) & 1);
    arg1[9] = (char)((READ_U32(arg3, 0x30) >> 7) & 1);
    if (READ_S32(arg2, 0x118) == 0 && READ_S32(arg2, 0x11c) == 0) {
        int32_t tmp = READ_S32(arg3, 0x68);

        if (tmp != 3 && READ_S32(arg3, 0xa4) == 0 && READ_S32(arg3, 0xa0) == 0 && READ_S32(arg3, 0x9c) == 0 &&
            READ_U8(arg3, 0x3e) == 0) {
            v1 = 0;
            if (READ_S32(arg3, 0xe8) != 0)
                v1 = (uint8_t)(READ_U32(arg3, 0xec) > 0);
        }
    }
    arg1[0xa] = (char)v1;
    arg1[0xb] = (char)READ_U8(arg3, 0x3a);
    arg1[0xc] = (char)READ_U8(arg3, 0x38);
    arg1[0xd] = (char)READ_U8(arg3, 0x39);
    arg1[0xf] = 0;
    arg1[0x10] = 0;
    arg1[0xe] = (char)((READ_U32(arg3, 0x34) & 0xffff0000U) > 0);
    arg1[0x12] = 0;
    arg1[0x13] = (char)((READ_U32(arg3, 0x30) >> 1) & 1);
    arg1[0x1a] = 1;
    arg1[0x14] = (char)(READ_U8(arg3, 0x30) & 1);
    arg1[0x750] = (char)((READ_U32(arg3, 0x30) >> 4) & 1);
    arg1[0x751] = (char)((READ_U32(arg3, 0x30) >> 3) & 1);
    AL_HEVC_GenerateFilterParam((uint8_t *)arg1, AL_IsGdrEnabled(arg2), READ_S32(arg3, 0x30),
                                READ_S32(arg3, 0x28), (char)READ_U8(arg3, 0x34), (char)READ_U8(arg3, 0x35));
    arg1[0x757] = 0;
    arg1[0xda9] = 0;
    arg1[0xda8] = (char)(READ_U8(arg3, 0x28) & 1);
    arg1[0xdab] = 0;
    arg1[0xdac] = (char)(arg5 > 0);
    if (arg5 == 0) {
        uint32_t result = (uint32_t)(uint8_t)arg1[0xdae];

        if (result == 0)
            return 0;
        return result;
    }

    arg1[0xdad] = 0;
    arg1[0xdae] = 1;
    arg1[0xdaf] = 0;
    arg1[0xdb0] = 0;
    arg1[0xdb1] = 0;
    arg1[0xdc6] = 1;
    arg1[0xdc7] = 0;
    arg1[0xdd0] = 0;
    WRITE_U32(arg1, 0xdcc, 0);
    return 1;
}

int32_t AL_HEVC_UpdateSPS(void *arg1, int32_t arg2, char *arg3, int32_t arg4)
{
    uint32_t v0 = (uint32_t)(uint8_t)arg3[0];

    if (v0 == 0)
        return (int32_t)v0;

    {
        AL_TBuffer *v0_1;
        uint32_t v0_2;
        int32_t v0_3;
        int32_t v0_4;
        int16_t var_20;
        int16_t var_1c;
        char v0_6;

        (void)_gp;
        v0_1 = (AL_TBuffer *)(intptr_t)AL_GetSrcBufferFromStatus((int32_t)(intptr_t)arg3, (void *)(intptr_t)arg2);
        v0_2 = AL_PixMapBuffer_GetFourCC(v0_1);
        AL_PixMapBuffer_GetDimension(&var_20, v0_1);
        v0_3 = (int32_t)AL_GetBitDepth(v0_2);
        v0_4 = AL_HEVC_MultiLayerExtSpsFlag(arg1, arg4);
        AL_HEVC_GenerateSPS_Format(arg1, AL_GetChromaMode(v0_2), (char)v0_3, (char)v0_3, var_20, var_1c,
                                   (char)v0_4);
        v0_6 = arg3[1];
        WRITE_U8(arg1, 0x1d0, (uint8_t)v0_6);
        return (int32_t)(uint8_t)v0_6;
    }
}

int32_t AL_HEVC_UpdatePPS(char *arg1, void *arg2, void *arg3, char *arg4, int32_t arg5)
{
    uint32_t v0_2;
    uint32_t t1;
    uint32_t v1_1;
    uint8_t v0_3;
    int32_t result = 0;

    (void)_gp;
    arg1[7] = (char)(READ_U8(arg3, 0xac) - 0x1a);
    v0_2 = (uint32_t)READ_U16(arg3, 0x2c);
    t1 = (uint32_t)READ_U16(arg3, 0x2e);
    v1_1 = (uint32_t)(uint16_t)(v0_2 - 1);
    WRITE_U16(arg1, 0x16, (uint16_t)v1_1);
    WRITE_U16(arg1, 0x18, (uint16_t)(t1 - 1));
    if (v1_1 != 0) {
        arg1[0x13] = 1;
        if ((int32_t)v0_2 >= 2) {
            int32_t *i = (int32_t *)((uint8_t *)arg3 + 0x40);
            uint8_t *v1_6 = (uint8_t *)&arg1[0x1c];

            do {
                int16_t t0_1 = (int16_t)(*i);

                v1_6 += 2;
                i = &i[1];
                *(int16_t *)(v1_6 - 2) = t0_1;
            } while ((uint8_t *)i != (uint8_t *)arg3 + ((v0_2 + 0xf) << 2));
        }
        if ((int32_t)t1 >= 2) {
            int32_t *i_1 = (int32_t *)((uint8_t *)arg3 + 0x44);
            uint8_t *v1_7 = (uint8_t *)&arg1[0x44];

            do {
                int16_t t0_2 = (int16_t)(*i_1);

                v1_7 += 2;
                i_1 = &i_1[1];
                *(int16_t *)(v1_7 - 2) = t0_2;
            } while ((uint8_t *)i_1 != (uint8_t *)arg3 + ((t1 + 0x10) << 2));
        }
        v0_3 = READ_U8(arg3, 0xb5);
    } else {
        arg1[0x13] = 0;
        v0_3 = READ_U8(arg3, 0xb5);
    }
    arg1[0xb] = (char)v0_3;
    if ((uint8_t)arg4[0] != 0) {
        result = 1;
        arg1[0] = arg4[1];
        arg1[1] = arg4[1];
    }
    if ((uint8_t)arg4[2] != 0) {
        uint8_t *a1_2 = (uint8_t *)arg2 + arg5 * 0xf0;

        AL_HEVC_GenerateFilterParam((uint8_t *)arg1, AL_IsGdrEnabled(arg2), READ_S32(a1_2, 0x30), READ_S32(a1_2, 0x28),
                                    arg4[3], arg4[4]);
    }
    return result;
}

static int32_t shouldReleaseSource(void)
{
    return 1;
}

static int32_t updateHlsAndWriteSections(uint8_t *arg1, uint8_t *arg2, int32_t arg3, int32_t arg4, int32_t arg5)
{
    int32_t s0_4 = arg5 * 0xe0c4;
    int32_t a1_2 = 0;
    int32_t result;

    (void)_gp;
    AL_HEVC_UpdateSPS(arg1 + s0_4 + 0x1c, (int32_t)(intptr_t)arg2, (char *)(intptr_t)arg3, arg5);
    HEVC_GenerateSections(arg1, arg4, (int32_t)(intptr_t)arg2, arg5,
                          (char)AL_HEVC_UpdatePPS((char *)(arg1 + s0_4 + 0x5a28), READ_PTR(arg1, 0x14), arg2,
                                                  (char *)(intptr_t)arg3, arg5));
    if (READ_S32(arg2, 0xa0) != 2)
        a1_2 = READ_S32(arg1, 0xed58);
    result = (int32_t)PicStructToFieldNumber[READ_U8(READ_PTR(arg2, 0xa4), 0)] + a1_2;
    WRITE_U32(arg1, 0xed58, (uint32_t)result);
    return result;
}

static int32_t preprocessEp1(uint8_t *arg1, int32_t *arg2)
{
    int32_t result = READ_S32(READ_PTR(arg1, 0x14), 0x10c);

    if (result != 0)
        return AL_HEVC_PreprocessScalingList((int32_t)(intptr_t)(arg1 + 0x23e), arg2);
    return result;
}

static int32_t generateNals(uint8_t *arg1, int32_t arg2, char arg3)
{
    uint8_t *s6 = READ_PTR(arg1, 0x14);
    uint8_t *s1_2 = s6 + arg2 * 0xf0;
    int32_t v1 = READ_S32(s1_2, 0x7c);
    uint32_t v0_2;
    int32_t s0_4 = arg2 * 0xe0c4;
    int32_t result;

    /* Decomp: $hi:$lo = __udivdi3((hi<<32)|lo, divisor). In this call,
     * num = (0 << 32) | (READ_U32 * v1), den = (0 << 32) | 0x15f90.
     * Codex mis-lifted the Binja 5-arg form; collapse to 2 args. */
    v0_2 = (uint32_t)__udivdi3((uint64_t)READ_U32(s1_2, 0x70) * (uint64_t)(uint32_t)v1,
                               (uint64_t)0x15f90);
    AL_HEVC_GenerateSPS(arg1 + s0_4 + 0x1c, s6, s1_2, (char)READ_U8(arg1, 0x18), (int32_t)v0_2);
    result = (int32_t)AL_HEVC_GeneratePPS((char *)(arg1 + s0_4 + 0x5a28), READ_PTR(arg1, 0x14), s1_2, READ_U8(arg1, 0x18),
                                          arg2);
    if ((uint8_t)arg3 != 0)
        return (int32_t)(intptr_t)AL_HEVC_GenerateVPS((char *)(arg1 + 0x8), READ_PTR(arg1, 0x14), READ_U8(arg1, 0x18));
    return result;
}

static int32_t ConfigureChannel(uint8_t *arg1, uint8_t *arg2, uint8_t *arg3)
{
    int32_t a0 = 0xa;
    int32_t v1_2;
    int32_t v0_6;
    int32_t result;

    (void)_gp;
    if ((READ_S32(arg2, 0x90) & 8) != 0)
        a0 = 0x10;
    WRITE_U32(arg2, 0x24, 0x100000);
    if (arg2 == (uint8_t *)(intptr_t)0xffffffdc) {
        return AL_CreateHevcEncoder((int32_t (**)(void))(intptr_t)__assert(
            "pHlsParam", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/include/lib_common_enc/EncChanParam.h",
            0x9e, "AL_SET_SPS_LOG2_MAX_POC"));
    }
    v1_2 = READ_S32(arg2, 0x28) | 1;
    WRITE_U32(arg2, 0x24, (uint32_t)((a0 - 1) | 0x100000));
    WRITE_U32(arg2, 0x28, (uint32_t)v1_2);
    AL_Common_Encoder_SetHlsParam(arg2, 0x100000, 0x10);
    if (READ_U8(READ_PTR(arg1, 0x14), 0x110) != 0) {
        v0_6 = READ_S32(arg2, 0x28) | 8;
        WRITE_U32(arg2, 0x28, (uint32_t)v0_6);
    } else {
        v0_6 = READ_S32(arg2, 0x28);
    }
    if (READ_U32(arg2, 0xb4) != 0)
        WRITE_U32(arg2, 0x24, READ_U32(arg2, 0x24) | 0xfc000);
    if ((READ_S32(arg2, 0x30) & 4) == 0)
        WRITE_U32(arg2, 0x28, (uint32_t)(v0_6 | 0x2000));
    else
        WRITE_U32(arg2, 0x28, (uint32_t)(v0_6 | 0x1000));
    AL_Common_Encoder_SetME(0x20, 0x20, 0x10, 0x10, (int8_t *)arg2);
    AL_Common_Encoder_ComputeRCParam((int32_t)READ_S8(arg2, 0x38) + (int32_t)READ_S8(arg2, 0x36),
                                     (int32_t)READ_S8(arg2, 0x39) + (int32_t)READ_S8(arg2, 0x37), 1, 0xa,
                                     (int16_t *)arg2);
    result = READ_S32(arg3, 0x10c);
    if (result != 0) {
        result = READ_S32(arg2, 0x30) | 0x20;
        WRITE_U32(arg2, 0x30, (uint32_t)result);
    }
    return result;
}

int32_t AL_CreateHevcEncoder(int32_t (**arg1)())
{
    *arg1 = (int32_t (*)())shouldReleaseSource;
    arg1[1] = (int32_t (*)())preprocessEp1;
    arg1[2] = (int32_t (*)())ConfigureChannel;
    arg1[3] = (int32_t (*)())generateNals;
    arg1[4] = (int32_t (*)())updateHlsAndWriteSections;
    return (int32_t)(intptr_t)updateHlsAndWriteSections;
}

static char *GetNalHeaderHevc(char *arg1, int32_t arg2, int32_t arg3, char arg4, char arg5)
{
    uint32_t a3 = (uint32_t)(uint8_t)arg4;

    (void)arg3;
    WRITE_U32(arg1, 4, 2);
    arg1[0] = (char)(((a3 >> 5) & 1) | ((arg2 & 0x3f) << 1));
    arg1[1] = (char)((arg5 + 1) | (char)(a3 << 3));
    return arg1;
}

static uint32_t *CreateHevcNuts(uint32_t *arg1)
{
    arg1[0] = (uint32_t)(uintptr_t)GetNalHeaderHevc;
    arg1[1] = 0x21;
    arg1[2] = 0x22;
    arg1[3] = 0x23;
    arg1[4] = 0x26;
    arg1[5] = 0x27;
    arg1[6] = 0x28;
    return arg1;
}

int32_t HEVC_GenerateSections(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4, char arg5)
{
    uint32_t var_40[7];
    uint8_t var_60[0x20];
    uint8_t *s2_1;

    CreateHevcNuts(var_40);
    AL_ExtractNalsData((uintptr_t *)var_60, arg1, arg4);
    s2_1 = READ_PTR(arg1, 0x14);
    return GenerateSections(AL_GetHevcRbspWriter(), (void *)(uintptr_t)var_40[0],
                            (int32_t)var_40[1], (int32_t)var_40[2],
                            (int32_t)var_40[3], (int32_t)var_40[4], (int32_t)var_40[5],
                            (void *)(uintptr_t)var_40[6], var_60,
                            (void *)(intptr_t)arg2, (void *)(intptr_t)arg3, arg4, (char)READ_U8(s2_1, 0x40),
                            (char)READ_U8(s2_1, 0xc4));
}
