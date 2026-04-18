#include <stdint.h>
#include <stdio.h>

#include "alcodec/BitStreamLite.h"
#include "alcodec/al_buffer.h"
#include "alcodec/al_nal.h"
#include "alcodec/al_rtos.h"

extern char _gp;
extern int32_t __assert(const char *expression, const char *file, int32_t line, const char *function, void *caller);

void *AL_GetAvcRbspWriter(void); /* forward decl, ported by T15 already */
void *AL_GetHevcRbspWriter(void); /* forward decl, ported by T15 already */
void *CreateAvcNuts(void *arg1); /* forward decl, ported by T<N> later */
void *CreateHevcNuts(void *arg1); /* forward decl, ported by T<N> later */
int32_t AL_RbspEncoding_BeginSEI2(AL_BitStreamLite *arg1, int32_t arg2, int32_t arg3); /* forward decl, ported by T15 already */
int32_t AL_RbspEncoding_CloseSEI(AL_BitStreamLite *arg1); /* forward decl, ported by T15 already */

uint32_t AL_StreamMetaData_AddSection(void *metadata, int32_t offset, int32_t length, int32_t handle, int32_t user_data,
                                      int32_t flags); /* forward decl, ported by T18 already */
int32_t AL_StreamMetaData_AddSeiSection(void *arg1, char arg2, int32_t arg3, int32_t arg4,
                                        int32_t arg5); /* forward decl, ported by T18 already */
void *AL_StreamMetaData_SetSectionFlags(void *metadata, int32_t section_id,
                                        int32_t flags); /* forward decl, ported by T18 already */
int32_t AL_StreamMetaData_GetUnusedStreamPart(void *arg1); /* forward decl, ported by T18 already */
int32_t AL_StreamMetaData_GetLastSectionOfFlag(void *arg1, int32_t arg2); /* forward decl, ported by T18 already */

typedef void *(*CreateNutHeaderFunc)(void *arg1, ...);
typedef int32_t (*GetCodecFunc)(void);
typedef int32_t (*NalWriteFunc)(void *arg1, AL_BitStreamLite *arg2, void *arg3, int32_t arg4);
typedef int32_t (*AudWriterFunc)(AL_BitStreamLite *arg1, int32_t arg2);
typedef int32_t (*SpsWriterFunc)(AL_BitStreamLite *arg1, void *arg2, int32_t arg3);
typedef int32_t (*PpsWriterFunc)(AL_BitStreamLite *arg1, void *arg2);
typedef int32_t (*VpsWriterFunc)(AL_BitStreamLite *arg1, void *arg2);
typedef int32_t (*SeiApsWriterFunc)(AL_BitStreamLite *arg1, void *arg2, char *arg3);
typedef int32_t (*SeiBufferingWriterFunc)(AL_BitStreamLite *arg1, void *arg2, int32_t arg3, int32_t arg4,
                                          int32_t arg5);
typedef int32_t (*SeiRecoveryWriterFunc)(AL_BitStreamLite *arg1, int32_t arg2);
typedef int32_t (*SeiPictureTimingWriterFunc)(AL_BitStreamLite *arg1, void *arg2, int32_t arg3, int32_t arg4,
                                              int32_t arg5);
typedef int32_t (*SeiMdcvWriterFunc)(AL_BitStreamLite *arg1, int16_t *arg2);
typedef int32_t (*SeiCllWriterFunc)(AL_BitStreamLite *arg1, int16_t *arg2);
typedef int32_t (*SeiUduWriterFunc)(AL_BitStreamLite *arg1, char *arg2, int8_t arg3);

int32_t writeStartCode(AL_BitStreamLite *arg1, int32_t arg2, int32_t arg3);
int32_t FlushNAL(AL_BitStreamLite *arg1, int32_t arg2, char arg3, char *arg4, char *arg5, int32_t arg6);
int32_t WriteNal(void *arg1, AL_BitStreamLite *arg2, int32_t arg3, uintptr_t *arg4);
int32_t GenerateSections(void *arg1, CreateNutHeaderFunc arg2, int32_t arg3, int32_t arg4, uint32_t arg5,
                         int32_t arg6, int32_t arg7, uintptr_t *arg8, uintptr_t *arg9, AL_TBuffer *arg10,
                         void *arg11, int32_t arg12, char arg13, char arg14);
int32_t getUserSeiPrefixOffset(void *arg1);
int32_t AL_WriteSeiSection(int32_t arg1, CreateNutHeaderFunc arg2, int32_t arg3, int32_t arg4, AL_TBuffer *arg5,
                           char arg6, int32_t arg7, int32_t arg8, int32_t arg9, int32_t arg10);
int32_t audWrite(void *arg1, AL_BitStreamLite *arg2, void *arg3, int32_t arg4);
int32_t spsWrite(void *arg1, AL_BitStreamLite *arg2, void *arg3, int32_t arg4);
int32_t ppsWrite(void *arg1, AL_BitStreamLite *arg2, void *arg3, int32_t arg4);
int32_t vpsWrite(void *arg1, AL_BitStreamLite *arg2, void *arg3, int32_t arg4);
int32_t seiPrefixAPSWrite(void *arg1, AL_BitStreamLite *arg2, uintptr_t *arg3, int32_t arg4);
int32_t seiPrefixUDUWrite(void *arg1, AL_BitStreamLite *arg2, void *arg3, int32_t arg4);
int32_t seiPrefixWrite(void *arg1, AL_BitStreamLite *arg2, uintptr_t *arg3, int32_t arg4);
int32_t seiExternalWrite(int32_t arg1, AL_BitStreamLite *arg2, int32_t *arg3, int32_t arg4);
uintptr_t *AL_CreateNalUnit(uintptr_t *arg1, void *arg2, void *arg3, int32_t arg4, int32_t arg5, int32_t arg6,
                            int32_t arg7);
uintptr_t *AL_CreateAud(uintptr_t *arg1, int32_t arg2, int32_t arg3, int32_t arg4);
uintptr_t *AL_CreateSps(uintptr_t *arg1, int32_t arg2, void *arg3, int32_t arg4, int32_t arg5);
uintptr_t *AL_CreatePps(uintptr_t *arg1, int32_t arg2, void *arg3, int32_t arg4, int32_t arg5);
uintptr_t *AL_CreateVps(uintptr_t *arg1, int32_t arg2, int32_t arg3);
uintptr_t *AL_CreateSeiPrefixAPS(uintptr_t *arg1, void *arg2, void *arg3, int32_t arg4, int32_t arg5);
uintptr_t *AL_CreateSeiPrefix(uintptr_t *arg1, void *arg2, void *arg3, int32_t arg4, int32_t arg5);
uintptr_t *AL_CreateSeiPrefixUDU(uintptr_t *arg1, void *arg2, void *arg3, int32_t arg4, int32_t arg5, int32_t arg6);
uintptr_t *AL_CreateExternalSei(uintptr_t *arg1, void *arg2, int32_t arg3, int32_t arg4);
int32_t WriteFillerData(AL_BitStreamLite *arg1, int32_t arg2, char arg3, char *arg4, int32_t arg5, char arg6);
uint32_t AddFlagsToAllSections(void *arg1, int32_t arg2);

uintptr_t *AL_ExtractNalsData(uintptr_t *arg1, void *arg2, int32_t arg3)
{
    void *a3;
    uint8_t a2;
    void *t1_1;
    int32_t a2_1;
    int32_t a0;
    void *a2_4;

    a3 = *(void **)((uint8_t *)arg2 + 0x14);
    a2 = ((uint32_t)arg3 < 1U) ? 1U : 0U;

    if (*(uint8_t *)((uint8_t *)a3 + 0xf0) == 0) {
        a2 = 0;
    }

    t1_1 = (uint8_t *)arg2 + ((uint32_t)arg3 * 0xe0c4U);
    a2_1 = 1;

    if (*(uint8_t *)((uint8_t *)a3 + 0xc4) == 0) {
        a2_1 = *(int32_t *)((uint8_t *)a3 + 0xf4);
    }

    a0 = *(int32_t *)((uint8_t *)a3 + 0xf8);

    if (*(uint8_t *)((uint8_t *)arg2 + 0xed5c) == 0) {
        a0 &= 0xfffffff7;
    }

    if (*(uint8_t *)((uint8_t *)arg2 + 0xed78) == 0) {
        a0 &= 0xffffffef;
    }

    a2_4 = NULL;
    if (a0 != 0) {
        a2_4 = (uint8_t *)arg2 + 0xed54;
    }

    arg1[0] = (uintptr_t)((uint8_t *)arg2 + 0xe154);
    arg1[1] = (uintptr_t)((uint8_t *)t1_1 + 0x1c);
    arg1[2] = (uintptr_t)((uint8_t *)t1_1 + 0x5a28);
    arg1[3] = (uintptr_t)a2;
    arg1[4] = (uintptr_t)a2_1;
    arg1[5] = 0;
    arg1[6] = (uintptr_t)a2_4;
    arg1[7] = (uintptr_t)a0;
    return arg1;
}

int32_t CreateNuts(uintptr_t *arg1, uint32_t arg2)
{
    uint32_t a1;
    uintptr_t var_28;
    uintptr_t var_24;
    uintptr_t var_20;
    uintptr_t var_1c;
    uintptr_t var_18;
    uintptr_t var_14;
    uintptr_t var_10;

    a1 = arg2 >> 0x18;

    if (a1 != 0) {
        if (a1 != 1) {
            return 0;
        }

        CreateHevcNuts(&var_28);
        arg1[0] = var_28;
        arg1[1] = var_24;
        arg1[2] = var_20;
        arg1[3] = var_1c;
        arg1[4] = var_18;
        arg1[5] = var_14;
        arg1[6] = var_10;
        return 1;
    }

    CreateAvcNuts(&var_28);
    arg1[0] = var_28;
    arg1[1] = var_24;
    arg1[2] = var_20;
    arg1[3] = var_1c;
    arg1[4] = var_18;
    arg1[5] = var_14;
    arg1[6] = var_10;
    return 1;
}

int32_t writeStartCode(AL_BitStreamLite *arg1, int32_t arg2, int32_t arg3)
{
    int32_t a2_1;

    if (arg2 == 0) {
        a2_1 = ((uint32_t)(arg3 - 6) < 0xaU) ? 1 : 0;
    } else {
        a2_1 = 0;

        if (arg2 == 1) {
            a2_1 = ((uint32_t)(arg3 - 0x20) < 9U) ? 1 : 0;
        }
    }

    if (a2_1 != 0) {
        AL_BitStreamLite_PutBits(arg1, 8, 0);
    }

    AL_BitStreamLite_PutBits(arg1, 8, 0);
    AL_BitStreamLite_PutBits(arg1, 8, 0);
    return AL_BitStreamLite_PutBits(arg1, 8, 1);
}

int32_t FlushNAL(AL_BitStreamLite *arg1, int32_t arg2, char arg3, char *arg4, char *arg5, int32_t arg6)
{
    char *s0;
    int32_t result;
    uint32_t s2_2;

    s0 = arg5;
    writeStartCode(arg1, arg2, (uint8_t)arg3);
    result = *(int32_t *)(arg4 + 4);

    if (result > 0) {
        AL_BitStreamLite_PutBits(arg1, 8, (uint8_t)arg4[0]);
        result = (*(int32_t *)(arg4 + 4) < 2) ? 1 : 0;

        if (result == 0) {
            result = AL_BitStreamLite_PutBits(arg1, 8, (uint8_t)arg4[1]);
        }
    }

    s2_2 = ((uint32_t)arg6 + 7U) >> 3;

    if (s0 != NULL) {
        if (s2_2 != 0) {
            uint32_t v0_1;
            int32_t s1;

            result = (s2_2 < 3U) ? 1 : 0;
            v0_1 = (uint8_t)*s0;
            s1 = 2;

            if (result == 0) {
                while (1) {
                    AL_BitStreamLite_PutBits(arg1, 8, v0_1);

                    if ((uint8_t)*s0 != 0) {
                        v0_1 = (uint8_t)s0[1];
                        s0 = &s0[1];
                    } else {
                        v0_1 = (uint8_t)s0[1];

                        if (v0_1 == 0) {
                            int32_t a3_1 = (int32_t)((uint8_t)s0[2] & 0xfffffffcU);

                            if (a3_1 == 0) {
                                AL_BitStreamLite_PutBits(arg1, 8, 0);
                                AL_BitStreamLite_PutBits(arg1, 8, 3);
                                s1 += 2;
                                v0_1 = (uint8_t)s0[2];
                                s0 = &s0[2];

                                if (s1 >= (int32_t)s2_2) {
                                    break;
                                }
                                continue;
                            }
                        }

                        s0 = &s0[1];
                    }

                    s1 += 1;
                    if (s1 >= (int32_t)s2_2) {
                        break;
                    }
                }
            }

            if ((int32_t)s2_2 >= s1) {
                AL_BitStreamLite_PutBits(arg1, 8, v0_1);
                v0_1 = (uint8_t)s0[1];
            }

            return AL_BitStreamLite_PutBits(arg1, 8, v0_1);
        }
    }

    return result;
}

int32_t WriteNal(void *arg1, AL_BitStreamLite *arg2, int32_t arg3, uintptr_t *arg4)
{
    uint8_t var_230[0x200];
    AL_BitStreamLite var_30;
    int32_t v0_1;
    int32_t v0_2;
    int32_t a0_4;
    int32_t v0_4;

    (void)arg3;

    AL_BitStreamLite_Init(&var_30, var_230, 0x200);
    ((NalWriteFunc)(uintptr_t)arg4[0])(arg1, &var_30, (void *)(uintptr_t)arg4[1], (int32_t)arg4[4]);

    if (var_30.reset_flag == 0) {
        v0_1 = AL_BitStreamLite_GetBitsCount(&var_30);

        if (v0_1 >= 0) {
            v0_2 = AL_BitStreamLite_GetBitsCount(arg2);
            a0_4 = v0_2 + 7;

            if (v0_2 < 0) {
                v0_2 = a0_4;
            }

            FlushNAL(arg2, ((GetCodecFunc)*(void **)arg1)(), (char)(uint8_t)arg4[2], (char *)&arg4[6],
                     (char *)var_230, v0_1);
            v0_4 = AL_BitStreamLite_GetBitsCount(arg2);

            if (v0_4 < 0) {
                v0_4 += 7;
            }

            if (arg2->reset_flag == 0) {
                return (v0_4 >> 3) - (v0_2 >> 3);
            }
        }
    }

    return -1;
}

int32_t GenerateSections(void *arg1, CreateNutHeaderFunc arg2, int32_t arg3, int32_t arg4, uint32_t arg5,
                         int32_t arg6, int32_t arg7, uintptr_t *arg8, uintptr_t *arg9, AL_TBuffer *arg10,
                         void *arg11, int32_t arg12, char arg13, char arg14)
{
    uint32_t v0;
    int32_t arg_4;
    uint32_t var_10;
    uintptr_t *s2;
    void *v0_2;
    uint32_t var_238;
    int32_t var_234;
    uintptr_t var_228[72];
    int32_t var_108[9];
    AL_BitStreamLite var_a4;
    uintptr_t var_70[8];
    char const *a1_21;
    int32_t s7_1;
    uint32_t v0_30;
    uintptr_t *v0_12;
    void *v0_19;
    int32_t s4_5;
    int32_t *s4_9;
    int32_t i;
    int32_t s5_3;

    v0 = (uint8_t)arg14;
    arg_4 = (int32_t)arg2;
    var_10 = arg5;
    s2 = arg8;
    v0_2 = AL_Buffer_GetMetaData(arg10, 1);
    var_234 = 0;
    (void)arg_4;
    (void)var_10;

    if (*(uint8_t *)((uint8_t *)arg11 + 0xa9) != 0) {
        arg5 = 0;

        if ((uint8_t)arg9[3] != 0) {
            arg5 = 1;
            AL_CreateAud(var_70, arg6, *(int32_t *)((uint8_t *)arg11 + 0xa0), *(uint8_t *)((uint8_t *)arg11 + 0xb4));
            var_228[0] = var_70[0];
            var_228[1] = var_70[1];
            var_228[2] = var_70[2];
            var_228[3] = var_70[3];
            var_228[4] = var_70[4];
            var_228[5] = var_70[5];
            var_228[6] = var_70[6];
            var_228[7] = var_70[7];
            var_108[0] = 0x10000000;
        }
    }

    if (*(uint8_t *)((uint8_t *)arg11 + 0xa8) == 0) {
        s7_1 = *(int32_t *)((uint8_t *)arg11 + 0xb0);
    } else {
        if (arg12 == 0 && ((void **)arg1)[2] != NULL) {
            AL_CreateVps(var_70, (int32_t)arg9[0], *(uint8_t *)((uint8_t *)arg11 + 0xb4));
            {
                uintptr_t *s4_13 = &var_228[arg5 * 8U];

                s4_13[0] = var_70[0];
                s4_13[1] = var_70[1];
                s4_13[2] = var_70[2];
                s4_13[3] = var_70[3];
                s4_13[4] = var_70[4];
                s4_13[5] = var_70[5];
                s4_13[6] = var_70[6];
                s4_13[7] = var_70[7];
                var_108[arg5] = 0x10000000;
                arg5 += 1;
            }
        }

        AL_CreateSps(var_70, arg3, (void *)(uintptr_t)arg9[1], arg12, *(uint8_t *)((uint8_t *)arg11 + 0xb4));
        {
            uintptr_t *s4_2 = &var_228[arg5 * 8U];

            s4_2[0] = var_70[0];
            s4_2[1] = var_70[1];
            s4_2[2] = var_70[2];
            s4_2[3] = var_70[3];
            s4_2[4] = var_70[4];
            s4_2[5] = var_70[5];
            s4_2[6] = var_70[6];
            s4_2[7] = var_70[7];
            var_108[arg5] = 0x10000000;
            arg5 += 1;
        }

        s7_1 = 1;
        v0_12 = arg9;
        goto label_46578;
    }

    v0_30 = v0;
    if (*(uint8_t *)((uint8_t *)arg11 + 0xa8) != 0 || s7_1 != 0) {
        if (arg12 == 0 && ((void **)arg1)[2] != NULL) {
            AL_CreateVps(var_70, (int32_t)arg9[0], *(uint8_t *)((uint8_t *)arg11 + 0xb4));
            {
                uintptr_t *s4_13 = &var_228[arg5 * 8U];

                s4_13[0] = var_70[0];
                s4_13[1] = var_70[1];
                s4_13[2] = var_70[2];
                s4_13[3] = var_70[3];
                s4_13[4] = var_70[4];
                s4_13[5] = var_70[5];
                s4_13[6] = var_70[6];
                s4_13[7] = var_70[7];
                var_108[arg5] = 0x10000000;
                arg5 += 1;
            }
        }

        AL_CreateSps(var_70, arg3, (void *)(uintptr_t)arg9[1], arg12, *(uint8_t *)((uint8_t *)arg11 + 0xb4));
        {
            uintptr_t *s4_2 = &var_228[arg5 * 8U];

            s4_2[0] = var_70[0];
            s4_2[1] = var_70[1];
            s4_2[2] = var_70[2];
            s4_2[3] = var_70[3];
            s4_2[4] = var_70[4];
            s4_2[5] = var_70[5];
            s4_2[6] = var_70[6];
            s4_2[7] = var_70[7];
            var_108[arg5] = 0x10000000;
            arg5 += 1;
        }

        s7_1 = 1;
        v0_12 = arg9;

label_46578:
        var_238 = *(uint8_t *)((uint8_t *)arg11 + 0xb4);
        AL_CreatePps(var_70, arg4, (void *)(uintptr_t)v0_12[2], arg12, (int32_t)var_238);
        {
            int32_t a0_4 = (int32_t)arg9[7];
            uintptr_t *s4_4 = &var_228[arg5 * 8U];

            s4_4[0] = var_70[0];
            s4_4[1] = var_70[1];
            s4_4[2] = var_70[2];
            s4_4[3] = var_70[3];
            s4_4[4] = var_70[4];
            s4_4[5] = var_70[5];
            s4_4[6] = var_70[6];
            s4_4[7] = var_70[7];
            var_108[arg5] = 0x10000000;
            arg5 += 1;

            if ((a0_4 & 0xffff) != 0) {
                int32_t v0_17 = 2;

                if (*(int32_t *)((uint8_t *)arg11 + 0xa0) == 2) {
                    v0_17 = 3;

                    if (*(uint8_t *)((uint8_t *)arg11 + 0xa8) == 0) {
                        v0_17 = 7;
                    }

                    if (s7_1 != 0) {
                        v0_17 |= 0x18;
                    }
                } else {
                    if (*(int32_t *)((uint8_t *)arg11 + 0xb0) != 0) {
                        v0_17 = 6;
                    }

                    if (s7_1 != 0) {
                        v0_17 |= 0x18;
                    }
                }

                s4_5 = v0_17 & a0_4;
                v0_19 = arg1;

                if ((s4_5 & 3) == 0) {
                    goto label_46b78;
                }
                goto label_46620;
            }
        }
    } else {
        v0_12 = arg9;

        if ((uint8_t)arg9[5] != 0) {
            goto label_46578;
        }

        {
            int32_t a0_30 = (int32_t)v0_12[7];

            v0_30 = v0;
            if ((a0_30 & 0xffff) != 0) {
                int32_t v0_66 = 2;

                if (*(int32_t *)((uint8_t *)arg11 + 0xa0) == 2) {
                    v0_66 = 7;
                }

                s4_5 = v0_66 & a0_30;
                v0_19 = arg1;

                if ((s4_5 & 3) != 0) {
label_46620:
                    if (((void **)v0_19)[5] != NULL) {
                        uintptr_t var_7c;
                        uintptr_t var_78_1;
                        uintptr_t *s7_3;

                        var_78_1 = arg9[0];
                        var_7c = arg9[1];
                        AL_CreateSeiPrefixAPS(var_70, &var_7c, s2, *(uint8_t *)((uint8_t *)arg11 + 0xb4),
                                              (int32_t)var_238);
                        s7_3 = &var_228[arg5 * 8U];
                        s7_3[0] = var_70[0];
                        s7_3[1] = var_70[1];
                        s7_3[2] = var_70[2];
                        s7_3[3] = var_70[3];
                        s7_3[4] = var_70[4];
                        s7_3[5] = var_70[5];
                        s7_3[6] = var_70[6];
                        s7_3[7] = var_70[7];
                        var_108[arg5] = 0x80000000;
                        arg5 += 1;
                    }

label_466c8:
                    {
                        uintptr_t *v0_26 = (uintptr_t *)(uintptr_t)arg9[6];
                        uintptr_t var_e4;
                        uintptr_t var_e0_1;
                        uintptr_t var_dc_1;
                        uintptr_t var_d8_1;
                        uintptr_t var_d0_1;
                        uintptr_t var_d4_1;
                        uintptr_t *s4_7;

                        var_e4 = arg9[1];
                        var_e0_1 = v0_26[0];
                        var_dc_1 = v0_26[1];
                        var_d8_1 = (uintptr_t)s4_5;
                        var_d0_1 = (uintptr_t)&v0_26[2];
                        var_d4_1 = (uintptr_t)arg11;
                        AL_CreateSeiPrefix(var_70, &var_e4, s2, *(uint8_t *)((uint8_t *)arg11 + 0xb4),
                                           (int32_t)var_238);
                        s4_7 = &var_228[arg5 * 8U];
                        s4_7[0] = var_70[0];
                        s4_7[1] = var_70[1];
                        s4_7[2] = var_70[2];
                        s4_7[3] = var_70[3];
                        s4_7[4] = var_70[4];
                        s4_7[5] = var_70[5];
                        s4_7[6] = var_70[6];
                        s4_7[7] = var_70[7];
                        var_108[arg5] = 0x80000000;
                        arg5 += 1;
                    }
                } else {
label_46b78:
                    v0_30 = v0;

                    if (s4_5 != 0) {
                        goto label_466c8;
                    }
                }
            }
        }
    }

    a1_21 = "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common/FbcMapSize.c";

    if (v0_30 != 0) {
        goto label_46be0;
    }

    if (arg5 != 0) {
        goto label_46780;
    }

    AL_BitStreamLite_Init(&var_a4, AL_Buffer_GetData(arg10), 0x100);
    AL_Buffer_GetMetaData(arg10, 1);

    do {
        uintptr_t s2_0;
        int32_t *s0_6;

        s2_0 = 0;
        s0_6 = (int32_t *)((uint8_t *)AL_Buffer_GetData(arg10) + *(int32_t *)((uint8_t *)arg11 + 0x34));

        if (*(int32_t *)((uint8_t *)arg11 + 0x38) <= 0) {
label_46910:
            {
                uint32_t v0_42 = *(uint32_t *)((uint8_t *)v0_2 + 0x14);
                int32_t s2_1;
                int32_t s4_11;
                AL_BitStreamLite var_94;

                s2_1 = 0;
                if (v0_42 == 0) {
                    s4_11 = 0;
                } else {
                    int32_t *v0_46 = (int32_t *)((uint8_t *)*(void **)((uint8_t *)v0_2 + 0x10) + (v0_42 * 0x14U) - 0x14U);

                    s4_11 = v0_46[0] + v0_46[1];
                    s2_1 = s4_11 << 3;
                }

                AL_BitStreamLite_Init(&var_94, AL_Buffer_GetData(arg10), (int32_t)AL_Buffer_GetSize(arg10));
                AL_BitStreamLite_SkipBits(&var_94, s2_1);
                s2_0 = arg9[4];

                if (s2_0 == 0) {
                    uint32_t v0_52 = v0;

label_46b24:
                    if (v0_52 != 0) {
                        if (*(uint8_t *)((uint8_t *)arg11 + 0xaa) != 0) {
                            goto label_469bc;
                        }
                    }

label_46aa4:
                    {
                        int32_t result = *(uint8_t *)((uint8_t *)arg11 + 0xa8);

                        if (result == 0) {
                            return result;
                        }

                        return (int32_t)AddFlagsToAllSections(v0_2, 0x40000000);
                    }
                } else {
                    uint32_t v0_52 = v0;

                    if (*(int32_t *)((uint8_t *)arg11 + 0x28) == 0) {
                        goto label_46b24;
                    }

label_469bc:
                    arg5 = (uint8_t)arg7;

                    {
                        int32_t v0_53 = AL_BitStreamLite_GetBitsCount(&var_94);
                        uintptr_t var_84[8];
                        int32_t s0_7;
                        int32_t a1_19;
                        int32_t s0_8;
                        int32_t v0_59;

                        arg2(var_84, (int32_t)arg5, 0, 0, *(uint8_t *)((uint8_t *)arg11 + 0xb4), var_234);
                        WriteFillerData(&var_94, ((GetCodecFunc)*(void **)arg1)(), (char)arg5, (char *)var_84,
                                        *(int32_t *)((uint8_t *)arg11 + 0x28), (((uint32_t)s2_0 ^ 2U) < 1U) ? 1 : 0);
                        s0_7 = AL_BitStreamLite_GetBitsCount(&var_94) - v0_53;
                        a1_19 = *(int32_t *)((uint8_t *)arg11 + 0x28);

                        if (s0_7 < 0) {
                            s0_7 += 7;
                        }

                        s0_8 = s0_7 >> 3;

                        if (s0_8 < a1_19) {
                            printf("[WARNING] Filler data (%i) doesn't fit in the current buffer. Clip it to %i !\n",
                                   a1_19, s0_8);
                        }

                        s2_0 ^= 2U;
                        v0_59 = 0x4000000;
                        if (s2_0 != 0) {
                            v0_59 = 0x8000000;
                        }

                        var_234 = v0_59;
                        var_238 = 0xffffffff;

                        if ((int32_t)AL_StreamMetaData_AddSection(v0_2, s4_11, s0_8, arg7, -1, var_234) >= 0) {
                            goto label_46a9c;
                        }
                    }
                }
            }
        } else {
            while (1) {
                var_234 = 0;
                var_238 = (uint32_t)s0_6[3];

                if ((int32_t)AL_StreamMetaData_AddSection(v0_2, s0_6[0], s0_6[1], s0_6[2], s0_6[3], 0) < 0) {
                    break;
                }

                s2_0 += 1;
                s0_6 = &s0_6[4];
                if ((int32_t)s2_0 >= *(int32_t *)((uint8_t *)arg11 + 0x38)) {
                    goto label_46910;
                }
            }
        }

label_46a9c:
        if (*(uint8_t *)((uint8_t *)arg11 + 0xaa) == 0) {
            goto label_46aa4;
        }

        var_234 = 0x20000000;
        var_238 = 0xffffffff;
        if ((int32_t)AL_StreamMetaData_AddSection(v0_2, 0, 0, -1, -1, 0x20000000) >= 0) {
            goto label_46aa4;
        }
    } while (0);

label_46be0:
    {
        int32_t var_b8[4];
        char var_a8;
        uintptr_t *s2_2;

        Rtos_Memcpy(var_b8, (const uint8_t *)a1_21 + 0x2b1c, 0x10);
        Rtos_Memcpy(&var_a8, &arg13, 1);
        s2_2 = &var_228[arg5 * 8U];
        {
            uintptr_t var_cc_1;
            uintptr_t var_c8_1;
            uintptr_t var_c4_1;
            uintptr_t var_c0_1;
            uintptr_t var_bc_1;

            var_cc_1 = (uintptr_t)var_b8[0];
            var_c8_1 = (uintptr_t)var_b8[1];
            var_c4_1 = (uintptr_t)var_b8[2];
            var_c0_1 = (uintptr_t)var_b8[3];
            var_bc_1 = (uintptr_t)(uint8_t)var_a8;
            AL_CreateSeiPrefixUDU(var_70, var_b8, s2, *(uint8_t *)((uint8_t *)arg11 + 0xb4), (int32_t)var_238,
                                  var_234);
            s2_2[0] = var_70[0];
            s2_2[1] = var_70[1];
            s2_2[2] = var_70[2];
            s2_2[3] = var_70[3];
            s2_2[4] = var_70[4];
            s2_2[5] = var_70[5];
            s2_2[6] = var_70[6];
            s2_2[7] = var_70[7];
            var_108[arg5] = 0x80000000;
            arg5 += 1;
        }
    }

label_46780:
    s2 = var_228;
    {
        uintptr_t *s4_8 = var_228;
        int32_t s7_4 = 0;

label_467a4:
        var_238 = (uint8_t)s4_8[5];
        {
            int32_t var_50[2];
            uintptr_t *s5_2;

            arg2(var_50, (uint8_t)s4_8[2], (uint8_t)s4_8[3], (uint8_t)s4_8[4], (int32_t)var_238, var_234);
            s5_2 = &var_228[s7_4 * 8U];
            s5_2[6] = (uintptr_t)var_50[0];
            s5_2[7] = (uintptr_t)var_50[1];

            if (s7_4 + 1 != (int32_t)arg5) {
                s7_4 += 1;
                s4_8 = &s4_8[8];
                goto label_467a4;
            }
        }

        s4_9 = var_108;
        s5_3 = 0;
        AL_BitStreamLite_Init(&var_a4, AL_Buffer_GetData(arg10), 0x100);
        v0_2 = AL_Buffer_GetMetaData(arg10, 1);

label_4683c:
        {
            int32_t fp_1 = *s4_9;
            int32_t v0_35 = AL_BitStreamLite_GetBitsCount(&var_a4);
            int32_t s0_3 = v0_35 + 7;
            int32_t v0_36;

            if (v0_35 >= 0) {
                s0_3 = v0_35;
            }

            v0_36 = WriteNal(arg1, &var_a4, 0x100, s2);

            if (v0_36 < 0) {
                return getUserSeiPrefixOffset((void *)(intptr_t)__assert(
                    "size >= 0", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_encode/Sections.c", 0x3e,
                    "GenerateNal", &_gp));
            }

            var_234 = fp_1;
            var_238 = 0xffffffff;
            i = (int32_t)AL_StreamMetaData_AddSection(v0_2, s0_3 >> 3, v0_36, (int32_t)s2[2], -1, var_234);
        }

        if (i < 0) {
            do {
            } while (0);
        }

        s5_3 += 1;
        s4_9 = &s4_9[1];
        s2 = &s2[8];
        if (s5_3 != (int32_t)arg5) {
            goto label_4683c;
        }
    }

    return i;
}

int32_t getUserSeiPrefixOffset(void *arg1)
{
    int32_t v0;

    v0 = AL_StreamMetaData_GetLastSectionOfFlag(arg1, 0x80000000);
    if (v0 == -1) {
        return 0x100;
    }

    {
        int32_t *v1_3 = (int32_t *)((uint8_t *)*(void **)((uint8_t *)arg1 + 0x10) + ((uint32_t)v0 * 0x14U));

        return v1_3[0] + v1_3[1];
    }
}

int32_t AL_WriteSeiSection(int32_t arg1, CreateNutHeaderFunc arg2, int32_t arg3, int32_t arg4, AL_TBuffer *arg5,
                           char arg6, int32_t arg7, int32_t arg8, int32_t arg9, int32_t arg10)
{
    uint32_t s1;
    int32_t fp;
    void *v0;

    s1 = (uint8_t)arg6;
    fp = arg3;
    v0 = AL_Buffer_GetMetaData(arg5, 1);

    if (v0 != NULL) {
        if (arg7 >= 0) {
            int32_t var_50;
            int32_t a2_1;
            int32_t s5;
            uintptr_t var_80[8];
            int32_t var_40[2];
            int32_t v0_7;
            int32_t v1;
            AL_BitStreamLite var_60;
            void *a0_7;
            int32_t v0_9;

            if (s1 != 0) {
                s5 = getUserSeiPrefixOffset(v0);
                var_50 = arg8;
                a2_1 = fp;
            } else {
                s5 = AL_StreamMetaData_GetUnusedStreamPart(v0);
                var_50 = arg8;
                a2_1 = arg4;
            }

            AL_CreateExternalSei(var_80, &var_50, a2_1, arg10);
            arg2(var_40, (uint8_t)var_80[2], (uint8_t)var_80[3], (uint8_t)var_80[4], (uint8_t)var_80[5]);
            var_80[6] = (uintptr_t)var_40[0];
            var_80[7] = (uintptr_t)var_40[1];
            v0_7 = (int32_t)AL_Buffer_GetSize(arg5);
            v1 = 0x100;

            if (v0_7 < 0x101) {
                v1 = v0_7;
            }

            AL_BitStreamLite_Init(&var_60, (uint8_t *)AL_Buffer_GetData(arg5) + s5, v1);

            if (arg1 == 0) {
                a0_7 = AL_GetAvcRbspWriter();
            } else {
                a0_7 = NULL;

                if (arg1 == 1) {
                    a0_7 = AL_GetHevcRbspWriter();
                }
            }

            v0_9 = WriteNal(a0_7, &var_60, v1, var_80);
            if (v0_9 >= 0) {
                if (s1 == 0) {
                    fp = arg4;
                }

                {
                    int32_t result =
                        AL_StreamMetaData_AddSeiSection(v0, (char)(s1 != 0), s5, v0_9, fp);

                    if (result >= 0) {
                        return result;
                    }
                }
            }
        } else {
            __assert("iPayloadType >= 0", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_encode/Sections.c",
                     0x12a, "AL_WriteSeiSection", &_gp);
        }
    } else {
        __assert("pMetaData", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_encode/Sections.c", 0x129,
                 "AL_WriteSeiSection", &_gp);
    }

    return -1;
}

int32_t audWrite(void *arg1, AL_BitStreamLite *arg2, void *arg3, int32_t arg4)
{
    (void)arg4;
    return ((AudWriterFunc)*(void **)((uint8_t *)arg1 + 4))(arg2, (int32_t)(intptr_t)arg3);
}

int32_t spsWrite(void *arg1, AL_BitStreamLite *arg2, void *arg3, int32_t arg4)
{
    return ((SpsWriterFunc)*(void **)((uint8_t *)arg1 + 0xc))(arg2, arg3, arg4);
}

int32_t ppsWrite(void *arg1, AL_BitStreamLite *arg2, void *arg3, int32_t arg4)
{
    (void)arg4;
    return ((PpsWriterFunc)*(void **)((uint8_t *)arg1 + 0x10))(arg2, arg3);
}

int32_t vpsWrite(void *arg1, AL_BitStreamLite *arg2, void *arg3, int32_t arg4)
{
    (void)arg4;
    return ((VpsWriterFunc)*(void **)((uint8_t *)arg1 + 8))(arg2, arg3);
}

int32_t seiPrefixAPSWrite(void *arg1, AL_BitStreamLite *arg2, uintptr_t *arg3, int32_t arg4)
{
    (void)arg4;
    return ((SeiApsWriterFunc)*(void **)((uint8_t *)arg1 + 0x14))(arg2, (void *)(uintptr_t)arg3[0],
                                                                    (char *)(uintptr_t)arg3[1]);
}

int32_t seiPrefixUDUWrite(void *arg1, AL_BitStreamLite *arg2, void *arg3, int32_t arg4)
{
    (void)arg4;
    return ((SeiUduWriterFunc)*(void **)((uint8_t *)arg1 + 0x2c))(arg2, (char *)arg3,
                                                                   *(int8_t *)((uint8_t *)arg3 + 0x10));
}

int32_t seiPrefixWrite(void *arg1, AL_BitStreamLite *arg2, uintptr_t *arg3, int32_t arg4)
{
    int32_t s0;
    int32_t result;

    (void)arg4;

    s0 = (int32_t)arg3[3];
    result = s0 & 1;
    if (s0 == 0) {
        return result;
    }

    if (result != 0) {
        int32_t var_38;

        do {
            var_38 = *(int32_t *)((uint8_t *)(uintptr_t)arg3[4] + 0xa4);
            ((SeiBufferingWriterFunc)*(void **)((uint8_t *)arg1 + 0x18))(arg2, (void *)(uintptr_t)arg3[0],
                                                                          (int32_t)arg3[1], 0, var_38);
            s0 &= 0xfffffffe;
            if (s0 == 0) {
                goto label_572d0;
            }
        } while ((s0 & 1) != 0);
    }

    while (1) {
        int32_t v0;

        v0 = s0 & 4;
        if (v0 != 0) {
            ((SeiRecoveryWriterFunc)*(void **)((uint8_t *)arg1 + 0x1c))(arg2,
                                                                         *(int32_t *)((uint8_t *)(uintptr_t)arg3[4] + 0xb0));
            s0 &= 0xfffffffb;
            break;
        }

        if ((s0 & 2) != 0) {
            void *v0_5 = (void *)(uintptr_t)arg3[4];
            int32_t var_38 = *(int32_t *)((uint8_t *)v0_5 + 0xa4);

            ((SeiPictureTimingWriterFunc)*(void **)((uint8_t *)arg1 + 0x20))(arg2, (void *)(uintptr_t)arg3[0],
                                                                              (int32_t)arg3[2],
                                                                              *(int32_t *)((uint8_t *)v0_5 + 0x18),
                                                                              var_38);
            s0 &= 0xfffffffd;
            break;
        }

        if ((s0 & 8) != 0) {
            ((SeiMdcvWriterFunc)*(void **)((uint8_t *)arg1 + 0x24))(arg2,
                                                                    (int16_t *)((uint8_t *)(uintptr_t)arg3[5] + 4));
            s0 &= 0xfffffff7;
            break;
        }

        if ((s0 & 0x10) != 0) {
            ((SeiCllWriterFunc)*(void **)((uint8_t *)arg1 + 0x28))(arg2,
                                                                   (int16_t *)((uint8_t *)(uintptr_t)arg3[5] + 0x1e));
            s0 &= 0xffffffef;
            break;
        }

        if (s0 != 0) {
            continue;
        }
        goto label_572d0;
    }

    if (s0 != 0) {
        goto label_47278_dummy;
    }

label_572d0:
    return AL_RbspEncoding_CloseSEI(arg2);

label_47278_dummy:
    while (1) {
        int32_t v0;

        v0 = s0 & 4;
        if (v0 != 0) {
            ((SeiRecoveryWriterFunc)*(void **)((uint8_t *)arg1 + 0x1c))(arg2,
                                                                         *(int32_t *)((uint8_t *)(uintptr_t)arg3[4] + 0xb0));
            s0 &= 0xfffffffb;
            break;
        }

        if ((s0 & 2) != 0) {
            void *v0_5 = (void *)(uintptr_t)arg3[4];
            int32_t var_38 = *(int32_t *)((uint8_t *)v0_5 + 0xa4);

            ((SeiPictureTimingWriterFunc)*(void **)((uint8_t *)arg1 + 0x20))(arg2, (void *)(uintptr_t)arg3[0],
                                                                              (int32_t)arg3[2],
                                                                              *(int32_t *)((uint8_t *)v0_5 + 0x18),
                                                                              var_38);
            s0 &= 0xfffffffd;
            break;
        }

        if ((s0 & 8) != 0) {
            ((SeiMdcvWriterFunc)*(void **)((uint8_t *)arg1 + 0x24))(arg2,
                                                                    (int16_t *)((uint8_t *)(uintptr_t)arg3[5] + 4));
            s0 &= 0xfffffff7;
            break;
        }

        if ((s0 & 0x10) != 0) {
            ((SeiCllWriterFunc)*(void **)((uint8_t *)arg1 + 0x28))(arg2,
                                                                   (int16_t *)((uint8_t *)(uintptr_t)arg3[5] + 0x1e));
            s0 &= 0xffffffef;
            break;
        }

        if (s0 != 0) {
            continue;
        }
        break;
    }

    if (s0 == 0) {
        return AL_RbspEncoding_CloseSEI(arg2);
    }

    goto label_572d0;
}

int32_t seiExternalWrite(int32_t arg1, AL_BitStreamLite *arg2, int32_t *arg3, int32_t arg4)
{
    int32_t s1;
    int32_t s3;
    uint8_t *v0_1;

    (void)arg1;
    (void)arg4;

    s1 = arg3[2];
    s3 = arg3[0];
    AL_RbspEncoding_BeginSEI2(arg2, arg3[1], s1);
    v0_1 = AL_BitStreamLite_GetCurData(arg2);
    AL_BitStreamLite_SkipBits(arg2, s1 << 3);

    if (arg2->reset_flag == 0) {
        Rtos_Memcpy(v0_1, (void *)(intptr_t)s3, (size_t)s1);
    }

    return AL_RbspEncoding_CloseSEI(arg2);
}

uintptr_t *AL_CreateNalUnit(uintptr_t *arg1, void *arg2, void *arg3, int32_t arg4, int32_t arg5, int32_t arg6,
                            int32_t arg7)
{
    arg1[6] = 0;
    arg1[3] = (uintptr_t)arg5;
    arg1[7] = 0;
    arg1[0] = (uintptr_t)arg2;
    arg1[4] = (uintptr_t)arg6;
    arg1[1] = (uintptr_t)arg3;
    arg1[2] = (uintptr_t)arg4;
    arg1[5] = (uintptr_t)arg7;
    return arg1;
}

uintptr_t *AL_CreateAud(uintptr_t *arg1, int32_t arg2, int32_t arg3, int32_t arg4)
{
    AL_CreateNalUnit(arg1, audWrite, (void *)(intptr_t)arg3, arg2, 0, 0, arg4);
    return arg1;
}

uintptr_t *AL_CreateSps(uintptr_t *arg1, int32_t arg2, void *arg3, int32_t arg4, int32_t arg5)
{
    AL_CreateNalUnit(arg1, spsWrite, arg3, arg2, 1, arg4, arg5);
    return arg1;
}

uintptr_t *AL_CreatePps(uintptr_t *arg1, int32_t arg2, void *arg3, int32_t arg4, int32_t arg5)
{
    AL_CreateNalUnit(arg1, ppsWrite, arg3, arg2, 1, arg4, arg5);
    return arg1;
}

uintptr_t *AL_CreateVps(uintptr_t *arg1, int32_t arg2, int32_t arg3)
{
    AL_CreateNalUnit(arg1, vpsWrite, (void *)(intptr_t)arg2, 0x20, 0, 0, arg3);
    return arg1;
}

uintptr_t *AL_CreateSeiPrefixAPS(uintptr_t *arg1, void *arg2, void *arg3, int32_t arg4, int32_t arg5)
{
    AL_CreateNalUnit(arg1, seiPrefixAPSWrite, arg2, (int32_t)(intptr_t)arg3, 0, 0, arg4);
    (void)arg5;
    return arg1;
}

uintptr_t *AL_CreateSeiPrefix(uintptr_t *arg1, void *arg2, void *arg3, int32_t arg4, int32_t arg5)
{
    AL_CreateNalUnit(arg1, seiPrefixWrite, arg2, (int32_t)(intptr_t)arg3, 0, 0, arg4);
    (void)arg5;
    return arg1;
}

uintptr_t *AL_CreateSeiPrefixUDU(uintptr_t *arg1, void *arg2, void *arg3, int32_t arg4, int32_t arg5, int32_t arg6)
{
    AL_CreateNalUnit(arg1, seiPrefixUDUWrite, arg2, (int32_t)(intptr_t)arg3, 0, 0, arg4);
    (void)arg5;
    (void)arg6;
    return arg1;
}

uintptr_t *AL_CreateExternalSei(uintptr_t *arg1, void *arg2, int32_t arg3, int32_t arg4)
{
    AL_CreateNalUnit(arg1, seiExternalWrite, arg2, arg3, 0, 0, arg4);
    return arg1;
}

int32_t WriteFillerData(AL_BitStreamLite *arg1, int32_t arg2, char arg3, char *arg4, int32_t arg5, char arg6)
{
    int32_t v0;
    int32_t v0_4;
    int32_t a0_4;
    int32_t v0_5;
    int32_t s1_1;
    int32_t s0_3;
    int32_t v0_7;
    int32_t v0_9;

    v0 = AL_BitStreamLite_GetBitsCount(arg1);
    writeStartCode(arg1, arg2, (uint8_t)arg3);

    if (*(int32_t *)(arg4 + 4) > 0) {
        AL_BitStreamLite_PutBits(arg1, 8, (uint8_t)arg4[0]);

        if (*(int32_t *)(arg4 + 4) >= 2) {
            AL_BitStreamLite_PutBits(arg1, 8, (uint8_t)arg4[1]);
        }
    }

    v0_4 = AL_BitStreamLite_GetBitsCount(arg1);
    a0_4 = arg1->size_bits;
    v0_5 = v0_4 - v0;

    if (v0_5 < 0) {
        v0_5 += 7;
    }

    s1_1 = a0_4 + 7;
    if (a0_4 >= 0) {
        s1_1 = a0_4;
    }

    s0_3 = arg5 - (v0_5 >> 3);
    v0_7 = AL_BitStreamLite_GetBitsCount(arg1);
    if (v0_7 < 0) {
        v0_7 += 7;
    }

    v0_9 = (s1_1 >> 3) - (v0_7 >> 3);
    if (s0_3 >= v0_9) {
        s0_3 = v0_9;
    }

    if (s0_3 - 1 > 0) {
        if ((uint8_t)arg6 != 0) {
            *AL_BitStreamLite_GetCurData(arg1) = 0xff;
        } else {
            Rtos_Memset(AL_BitStreamLite_GetCurData(arg1), 0xff, (size_t)(s0_3 - 1));
        }

        AL_BitStreamLite_SkipBits(arg1, (s0_3 - 1) << 3);
    }

    return AL_BitStreamLite_PutBits(arg1, 8, 0x80);
}

uint32_t AddFlagsToAllSections(void *arg1, int32_t arg2)
{
    uint32_t i;

    i = *(uint32_t *)((uint8_t *)arg1 + 0x14);
    if (i != 0) {
        int32_t s2_1;
        int32_t s0_1;

        s2_1 = 0;
        s0_1 = 0;

        do {
            int32_t a1;

            a1 = s0_1;
            s0_1 += 1;
            AL_StreamMetaData_SetSectionFlags(
                arg1, a1, arg2 | *(int32_t *)((uint8_t *)*(void **)((uint8_t *)arg1 + 0x10) + s2_1 + 8));
            i = (s0_1 < *(int32_t *)((uint8_t *)arg1 + 0x14)) ? 1U : 0U;
            s2_1 += 0x14;
        } while (i != 0);
    }

    return i;
}

int32_t AL_Encoder_AddSei(int32_t *arg1, int32_t arg2, char arg3, int32_t arg4, int32_t arg5, int32_t arg6,
                          int32_t arg7)
{
    int32_t s0;
    uintptr_t var_38[7];

    s0 = *(int32_t *)((uint8_t *)*(void **)((uint8_t *)*(void **)arg1 + 0x14) + 0x1c);
    if (CreateNuts(var_38, (uint32_t)s0) == 0) {
        return -1;
    }

    return AL_WriteSeiSection((uint32_t)s0 >> 0x18, (CreateNutHeaderFunc)(uintptr_t)var_38[0], (int32_t)var_38[1],
                              (int32_t)var_38[2], (AL_TBuffer *)(intptr_t)arg4, arg3, arg5, arg2, arg6, arg7);
}
