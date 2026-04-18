#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "alcodec/al_logger.h"
#include "alcodec/al_rtos.h"

extern void __assert(const char *expr, const char *file, int32_t line, const char *func);

int32_t AL_GetAllocSize_Src(int32_t arg1, int32_t arg2, char arg3, int32_t arg4, int32_t arg5); /* forward decl, ported by T<N> later */
int32_t AL_PixMapBuffer_GetPlaneAddress(void *arg1, int32_t arg2); /* forward decl, ported by T<N> later */
int32_t AL_PixMapBuffer_GetPlanePitch(void *arg1, int32_t arg2); /* forward decl, ported by T<N> later */
int32_t *AL_PixMapBuffer_GetDimension(int32_t *arg1, void *arg2); /* forward decl, ported by T<N> later */

typedef uint32_t (*WriteZoneFn)(FILE *stream, void *base, const void *zone);

typedef struct ZoneDesc {
    uint32_t unused0;
    uint8_t reg_offset;
    uint8_t unused5;
    uint8_t reg_count;
    uint8_t unused7;
} ZoneDesc;

static const char g_trace_mode[] = "wt";
static const char g_zero_word[] = "00000000\n";
static const char g_empty_string[] = "";
static const char g_encoder_name[] = "Encoder";
static const char g_jpeg_name[] = "Jpeg";
static const char g_common_traces_path[] = "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_trace/CommonTraces.c";
static const char g_enc_traces_path[] = "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_trace/EncTraces.c";
static const char g_src_y[] = "srcY";
static const char g_src_c[] = "srcC";
static const char g_tab[] = "tab";
static const char g_bit[] = "bit";

static const uint64_t AL_ENCJPEG_STATUS = 0x3000c00000030ULL;
static const uint64_t AL_ENCJPEG_CMD = 0x0b000000000000ULL;
static const uint64_t AL_ENC2_STATUS = 0x02007900000178ULL;
static const uint64_t AL_ENC1_STATUS = 0x1d004100000104ULL;

uint64_t getCpuTime(void);
int32_t getTime(void *arg1);
size_t AL_Write32(FILE *arg1, uint32_t arg2);

static void *HwTimerVtable = (void *)getTime;
static void *CpuTimerVtable = (void *)getCpuTime;

static void *DumpBuffer_part_41(FILE *arg1, int32_t *arg2, int32_t **arg3, int32_t arg4, int32_t *arg5);
static int32_t DumpZero_part_46(FILE *arg1, int32_t *arg2, int32_t *arg3, int32_t arg4, int32_t *arg5);
static int32_t TraceBufMV_constprop_51(const char *arg1, int32_t *arg2, int32_t arg3);
static int32_t TraceHwRc_constprop_54(const char *arg1, int32_t arg2, int32_t *arg3);
static int32_t TraceBufSrc_8bits_constprop_57(const char *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6, void *arg7);
static int32_t GetH26xBufferName_constprop_59(char *arg1, const char *arg2, const char *arg3);
static uint32_t AL_WriteZoneArray__constprop_60(FILE *arg1, void *arg2, const void *arg3, WriteZoneFn arg4);
static int32_t TraceBuf_constprop_65(const char *arg1, const int32_t *arg2, int32_t arg3);
static void TraceRefBufMap_constprop_66(const char *arg1, const int32_t *arg2, int32_t arg3, const int32_t *arg4, int32_t arg5);
static uint32_t TraceBufRec_isra_49_constprop_64(void *arg1, char *arg2, char *arg3, char *arg4, char *arg5, int32_t *arg6, int32_t *arg7);

int32_t AL_LoggerInit(AL_TLogger *logger, int32_t writer, int32_t entries, int32_t capacity)
{
    logger->writer = (void *)(intptr_t)writer;
    logger->count = 0;
    logger->entries = (void *)(intptr_t)entries;
    logger->capacity = capacity;
    logger->mutex = Rtos_CreateMutex();
    if (logger->mutex == NULL) {
        __assert("logger->mutex", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_perfs/Logger.c", 0x11, "AL_LoggerInit");
        return AL_LoggerDeinit(logger);
    }
    return (int32_t)(intptr_t)logger->mutex;
}

int32_t AL_LoggerDeinit(AL_TLogger *logger)
{
    Rtos_DeleteMutex(logger->mutex);
    return 0;
}

int32_t AL_Log(AL_TLogger *logger, char *message)
{
    uint32_t hi;
    uint32_t lo;
    int32_t count;
    int32_t now_hi;
    int32_t now_lo;
    char *entry;
    int32_t carry;

    Rtos_GetMutex(logger->mutex);
    if (logger->count >= logger->capacity) {
        return Rtos_ReleaseMutex(logger->mutex);
    }

    carry = 0;
    now_lo = ((int32_t (*)(void))(*(void **)logger->writer))();
    count = logger->count;
    now_hi = now_lo;
    if (count > 0) {
        entry = (char *)logger->entries + (count << 3) + (count << 5);
        hi = *(uint32_t *)(entry - 0x24);
        lo = *(uint32_t *)(entry - 0x28);
        if (hi != 0 || (uint32_t)now_hi < lo) {
            carry = (now_hi - 1U < (uint32_t)now_hi) ? 1 : 0;
            now_hi -= 1;
            while (1) {
                int32_t next_carry = (now_hi - 1U < (uint32_t)now_hi) ? 1 : 0;
                if ((uint32_t)carry >= hi) {
                    if (hi != (uint32_t)carry) {
                        break;
                    }
                    if ((uint32_t)now_hi >= lo) {
                        break;
                    }
                }
                now_hi -= 1;
                carry += next_carry;
            }
        }
    } else {
        entry = (char *)logger->entries + (count << 3) + (count << 5);
    }

    strcpy(entry + 8, message);
    count = logger->count;
    *(int32_t *)((char *)logger->entries + count * 0x28) = now_hi;
    *(int32_t *)((char *)logger->entries + count * 0x28 + 4) = carry;
    logger->count = count + 1;
    return Rtos_ReleaseMutex(logger->mutex);
}

char *AL_SignalToString(char *arg1, int32_t arg2, int32_t arg3)
{
    snprintf(arg1, 0x20, "Signal_%d %d", arg2, arg3);
    return arg1;
}

uint64_t getCpuTime(void)
{
    struct timespec tp;

    clock_gettime(0, &tp);
    return (uint64_t)tp.tv_sec * 0x3b9aca00ULL + (uint64_t)tp.tv_nsec;
}

int32_t getTime(void *arg1)
{
    void *s0 = *(void **)((char *)arg1 + 4);
    int32_t result;

    while (1) {
        result = (*(int32_t (**)(void *, int32_t))(*(intptr_t *)s0 + 4))(s0, 0x8030);
        if (result != 0) {
            break;
        }
        (*(int32_t (**)(void *, int32_t, int32_t))(*(intptr_t *)s0 + 8))(s0, 0x8028, ((*(int32_t (**)(void *, int32_t))(*(intptr_t *)s0 + 4))(s0, 0x8028) & 0xfffffff3) | 4);
    }
    return result;
}

void *AL_HwTimerInit(void **arg1, void *arg2)
{
    arg1[1] = arg2;
    arg1[0] = &HwTimerVtable;
    return arg1;
}

void *AL_CpuTimerInit(void **arg1)
{
    *arg1 = &CpuTimerVtable;
    return arg1;
}

FILE *safe_fopen(const char *arg1, const char *arg2)
{
    FILE *result = fopen(arg1, arg2);

    if (result != NULL) {
        return result;
    }

    printf("\nCouldn't open %s, you might need to create the directory containing the file\n", arg1);
    __assert("pFile", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_trace/CommonTraces.c", 0xb, "safe_fopen");
    return (FILE *)(intptr_t)AL_Write32(NULL, 0);
}

size_t AL_Write32(FILE *arg1, uint32_t arg2)
{
    char var_18[9];
    char *t0 = var_18;
    int32_t i = 0x1c;
    char var_10 = '\n';

    while (i != -4) {
        uint32_t v1 = (arg2 >> (i & 0x1f)) & 0xf;
        char a3 = '0';
        if ((int32_t)v1 >= 0xa) {
            a3 = '7';
        }
        i -= 4;
        *t0 = (char)(a3 + (char)v1);
        t0 = &t0[1];
    }

    var_18[8] = var_10;
    return fwrite(var_18, 9, 1, arg1);
}

int32_t AL_TraceBuffer(const char *arg1, const char *arg2, const int32_t *arg3, int32_t arg4)
{
    FILE *stream = safe_fopen(arg1, arg2);
    const int32_t *s0 = arg3;

    if (arg4 != 0) {
        do {
            int32_t a1 = *s0;
            s0 = &s0[1];
            AL_Write32(stream, (uint32_t)a1);
        } while ((uintptr_t)((const char *)s0 - (const char *)arg3) < (uint32_t)arg4);
    }

    return fclose(stream);
}

uint32_t WriteZoneRegisters(FILE *arg1, void *arg2, const void *arg3)
{
    uint32_t i = ((const ZoneDesc *)arg3)->reg_count;
    int32_t *s0_2 = (int32_t *)((char *)arg2 + (((const ZoneDesc *)arg3)->reg_offset << 2));

    if (i != 0) {
        int32_t s1_1 = 0;
        do {
            s1_1 += 1;
            AL_Write32(arg1, (uint32_t)*s0_2);
            i = (uint32_t)s1_1 < ((const ZoneDesc *)arg3)->reg_count ? 1U : 0U;
            s0_2 = &s0_2[1];
        } while (i != 0);
    }

    return i;
}

uint32_t WriteZoneAsZeros(FILE *arg1, void *arg2, const void *arg3)
{
    uint32_t i = ((const ZoneDesc *)arg3)->reg_count;
    (void)arg2;

    if (i != 0) {
        int32_t s0_1 = 0;
        do {
            AL_Write32(arg1, 0);
            s0_1 += 1;
            i = (uint32_t)s0_1 < ((const ZoneDesc *)arg3)->reg_count ? 1U : 0U;
        } while (i != 0);
    }

    return i;
}

char *getBasePathName(char *arg1, const char *arg2, int32_t arg3, char *arg4, int32_t arg5, int32_t arg6)
{
    int32_t v1 = arg5;
    const char *v0;
    const char *a3;
    int32_t var_228 = '/';
    char var_210[0x200];

    if (arg6 != 4) {
        v0 = g_encoder_name;
        if (arg1 == NULL) {
            goto label_5c014;
        }
        goto label_5bf64;
    }

    v1 += 1;
    v0 = g_jpeg_name;
    if (arg1 != NULL) {
label_5bf64:
        a3 = arg1;
        if (arg2 == NULL) {
            arg2 = g_empty_string;
        }
        goto label_5bf80;
    }

label_5c014:
    a3 = g_empty_string;
    if (arg2 == NULL) {
        arg2 = g_empty_string;
    }

label_5bf80:
    if (snprintf(arg4, 0x200, "%s%c%s%s%01d", a3, '/', arg2, v0, v1) >= 0x200) {
        __assert("iNumWrittenChar < 512", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_trace/EncTraces.c", 0x2e, "getBasePathName");
    }

    if (arg3 < 0) {
        return arg4;
    }

    var_228 = arg3;
    if (snprintf(var_210, 0x200, "%s_%04d", arg4, var_228) < 0x200) {
        return strcpy(arg4, var_210);
    }

    __assert("iNumWrittenChar < 512", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_trace/EncTraces.c", 0x34, "getBasePathName");
    return arg4;
}

static void *DumpBuffer_part_41(FILE *arg1, int32_t *arg2, int32_t **arg3, int32_t arg4, int32_t *arg5)
{
    int32_t i = *arg2 & 3;
    int32_t i_2 = arg4;
    int32_t *v0_1;

    if (i != 0) {
        v0_1 = *arg3;
        do {
            if (i_2 == 0) {
                *arg5 = 0;
                *arg2 = *arg2;
                *arg3 = v0_1;
                return v0_1;
            }
            v0_1 += 1;
            *arg3 = v0_1;
            *arg5 |= (uint32_t)(uint8_t)*(v0_1 - 1) << ((i << 3) & 0x1f);
            i_2 -= 1;
            *arg2 = *arg2 + 1;
            i = *arg2 & 3;
        } while (i != 0);

        AL_Write32(arg1, (uint32_t)*arg5);
    }

    {
        int32_t i_1 = i_2;
        int32_t *v0_3;
        int32_t i_3;

        if (i_2 < 4) {
            v0_3 = *arg3;
            i_3 = i_2;
        } else {
            v0_3 = *arg3;
            do {
                i_1 -= 4;
                AL_Write32(arg1, (uint32_t)*v0_3);
                v0_3 = *arg3 + 4;
                *arg2 += 4;
                *arg3 = v0_3;
            } while (i_1 >= 4);
            i_2 &= 3;
            i_3 = i_2;
            if (i_2 == 0) {
                *arg5 = 0;
                v0_3 = (int32_t *)((char *)v0_3 + i_3);
                *arg2 += i_2;
                *arg3 = v0_3;
                return v0_3;
            }
        }

        *arg5 = ((1 << ((i_2 << 3) & 0x1f)) - 1) & *v0_3;
        v0_3 = (int32_t *)((char *)v0_3 + i_3);
        *arg2 += i_2;
        *arg3 = v0_3;
        return v0_3;
    }
}

static int32_t DumpZero_part_46(FILE *arg1, int32_t *arg2, int32_t *arg3, int32_t arg4, int32_t *arg5)
{
    int32_t v0 = *arg2;
    int32_t i_1 = arg4;

    *arg3 += arg4;
    if ((v0 & 3) != 0) {
        int32_t v0_1 = v0 + 1;
        if (arg4 != 0) {
            int32_t v1_2 = v0_1 & 3;

            while (1) {
                int32_t a0 = v0_1;
                if (v1_2 == 0) {
                    *arg2 = v0_1;
                    AL_Write32(arg1, (uint32_t)*arg5);
                    goto label_5c2d4;
                }

                i_1 -= 1;
                if (i_1 == 0) {
                    *arg2 = a0;
                    break;
                }

                v0_1 += 1;
                v1_2 = v0_1 & 3;
            }
        }

        *arg5 = 0;
        {
            int32_t v0_6 = *arg2;
            *arg2 = v0_6;
            return v0_6;
        }
    }

label_5c2d4:
    if (i_1 >= 4) {
        int32_t i = i_1;

        do {
            fwrite(g_zero_word, 9, 1, arg1);
            i -= 4;
            *arg2 += 4;
        } while (i >= 4);

        i_1 &= 3;
    }

    *arg5 = 0;
    {
        int32_t v0_5 = *arg2;
        *arg2 = v0_5 + i_1;
        return v0_5;
    }
}

static int32_t TraceBufMV_constprop_51(const char *arg1, int32_t *arg2, int32_t arg3)
{
    FILE *stream = safe_fopen(arg1, g_trace_mode);

    if (arg3 == 0 && arg2 != NULL) {
        int32_t v0 = *arg2;

        if (v0 != 0) {
            uint32_t s1_3 = (uint32_t)(arg2[2] - 0x100) >> 2;
            int32_t *i = (int32_t *)((char *)(intptr_t)v0 + 0x100);

            if (s1_3 != 0) {
                do {
                    i = (int32_t *)((char *)i + 4);
                    AL_Write32(stream, (uint32_t)*(i - 1));
                } while (i != (int32_t *)((char *)(intptr_t)v0 + ((s1_3 + 0x40) << 2)));
            }
        }
    }

    return fclose(stream);
}

static int32_t TraceHwRc_constprop_54(const char *arg1, int32_t arg2, int32_t *arg3)
{
    FILE *stream = safe_fopen(arg1, g_trace_mode);
    int32_t i_2 = *arg3;
    (void)arg2;

    if (i_2 != 0) {
        int32_t v0 = arg3[5];

        if ((v0 & 1) != 0) {
            int32_t *i = (int32_t *)(intptr_t)i_2;

            do {
                i = (int32_t *)((char *)i + 4);
                AL_Write32(stream, (uint32_t)*(i - 1));
            } while (i != (int32_t *)(intptr_t)(i_2 + 0x200));

            v0 = arg3[5];
        }

        {
            int32_t i_1 = i_2 + 0x400;

            if ((v0 & 2) != 0) {
                int32_t *s2_2 = (int32_t *)(intptr_t)(i_2 + 0x200);

                do {
                    s2_2 = (int32_t *)((char *)s2_2 + 4);
                    AL_Write32(stream, (uint32_t)*(s2_2 - 1));
                } while (s2_2 != (int32_t *)(intptr_t)i_1);
            }

            do {
                i_1 += 4;
                AL_Write32(stream, (uint32_t)*(int32_t *)(intptr_t)(i_1 - 4));
            } while (i_1 != i_2 + 0x1420);
        }
    }

    return fclose(stream);
}

static int32_t TraceBufSrc_8bits_constprop_57(const char *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6, void *arg7)
{
    char str[0x12c];
    char var_17c[0x12c];
    int32_t i_1;
    int32_t result;

    strcpy(str, arg1);
    strcat(str, ".srcY.hex");
    strcpy(var_17c, arg1);
    strcat(var_17c, ".srcC.hex");

    if (arg7 == NULL) {
        int32_t v0_22 = AL_GetAllocSize_Src(arg3, arg2, (char)arg4, arg5, arg6);
        FILE *stream_1 = safe_fopen(str, g_trace_mode);

        if (v0_22 != 0) {
            int32_t s1_3 = 0;
            do {
                s1_3 += 4;
                fwrite(g_zero_word, 9, 1, stream_1);
            } while ((uint32_t)s1_3 < (uint32_t)v0_22);
        }

        return fclose(stream_1);
    }

    AL_PixMapBuffer_GetDimension(&i_1, arg7);
    {
        int32_t i = i_1;
        int32_t var_4c;
        int32_t s7_3 = ((var_4c + 7) >> 3) << 3;
        char (*var_3c_1)[0x12c] = &str;

        result = 0;
        while (1) {
            FILE *stream = safe_fopen(*var_3c_1, g_trace_mode);
            int32_t s1_2 = AL_PixMapBuffer_GetPlaneAddress(arg7, result);
            int32_t v0_12 = AL_PixMapBuffer_GetPlanePitch(arg7, result);

            if (s7_3 > 0) {
                int32_t s5_1 = 0;

                do {
                    int32_t s6_4 = s1_2;

                    if (i > 0) {
                        do {
                            s6_4 += 4;
                            AL_Write32(stream, *(uint32_t *)(intptr_t)(s6_4 - 4));
                        } while (s6_4 - s1_2 < i);
                    }

                    s1_2 += ((((uint32_t)(i - 1) >> 2) + 1) << 2);
                    if (i < v0_12) {
                        int32_t i_2 = i;

                        do {
                            i_2 += 4;
                            fwrite(g_zero_word, 9, 1, stream);
                        } while (i_2 < v0_12);
                    }

                    s1_2 += ((((uint32_t)(~i + v0_12) >> 2) + 1) << 2);
                    s5_1 += 1;
                } while (s5_1 != s7_3);
            }

            fclose(stream);
            if (result == ((0U < (uint32_t)arg5) ? 1 : 0)) {
                break;
            }
            if (result != -1 && arg5 == 1) {
                s7_3 = ((uint32_t)s7_3 >> 31) + s7_3;
                s7_3 >>= 1;
            }
            result += 1;
            var_3c_1 = &var_3c_1[1];
        }
    }

    return result;
}

static int32_t GetH26xBufferName_constprop_59(char *arg1, const char *arg2, const char *arg3)
{
    return sprintf(arg1, "%s26x.out%s", arg2, arg3);
}

static uint32_t AL_WriteZoneArray__constprop_60(FILE *arg1, void *arg2, const void *arg3, WriteZoneFn arg4)
{
    return arg4(arg1, arg2, arg3);
}

int32_t TraceJpegCmd(void *arg1, int32_t arg2, void *arg3, int32_t arg4)
{
    char *base = (char *)arg1;

    if (arg4 != 0) {
        char *str = strcpy(base + 0x204, base + 4);
        FILE *stream_2;

        strcat(str, ".stat.hex");
        stream_2 = safe_fopen(str, (const char *)(intptr_t)arg2);
        AL_WriteZoneArray__constprop_60(stream_2, arg3, &AL_ENCJPEG_STATUS, WriteZoneRegisters);
        return fclose(stream_2);
    }

    strcpy(base + 0x204, base + 4);
    strcat(base + 0x204, ".cmd.hex");
    {
        FILE *stream = safe_fopen(base + 0x204, (const char *)(intptr_t)arg2);

        AL_WriteZoneArray__constprop_60(stream, arg3, &AL_ENCJPEG_CMD, WriteZoneRegisters);
        AL_WriteZoneArray__constprop_60(stream, arg3, &AL_ENCJPEG_STATUS, WriteZoneAsZeros);
        fclose(stream);
    }

    {
        char *v0_4 = base + 0x204;
        FILE *stream_1;

        strcpy(v0_4, base + 4);
        strcat(v0_4, ".sram_map.hex");
        stream_1 = safe_fopen(base + 0x204, g_trace_mode);
        fprintf(stream_1, "(IN )  %08X => %s%d\n", *(uint32_t *)((char *)arg3 + 0x10), g_src_y, 1);
        fprintf(stream_1, "(IN )  %08X => %s%d\n", *(uint32_t *)((char *)arg3 + 0x14), g_src_c, 1);
        fprintf(stream_1, "(IN )  %08X => %s%d\n", *(uint32_t *)((char *)arg3 + 0x18), g_tab, 1);
        fprintf(stream_1, "(OUT)  %08X => %s%d\n", *(uint32_t *)((char *)arg3 + 0x1c), g_bit, 1);
        return fclose(stream_1);
    }
}

int32_t TraceCmdList(const char *arg1, const char *arg2, int32_t *arg3, int32_t arg4)
{
    int32_t *s0 = arg3;
    FILE *stream = safe_fopen(arg1, arg2);

    while (1) {
        int32_t *fp_1 = &s0[0x80];

        if (arg4 == 0) {
            do {
                int32_t a1 = *s0;
                s0 = &s0[1];
                AL_Write32(stream, (uint32_t)a1);
            } while (fp_1 != s0);

            s0 = fp_1;
            if (*(fp_1 - 0x200) < 0) {
                break;
            }
        } else {
            fwrite(g_zero_word, 9, 1, stream);
            AL_WriteZoneArray__constprop_60(stream, s0, &AL_ENC1_STATUS, WriteZoneRegisters);
            {
                int32_t i = 3;
                while (i != 0) {
                    i -= 1;
                    fwrite(g_zero_word, 9, 1, stream);
                }
            }
            AL_WriteZoneArray__constprop_60(stream, s0, &AL_ENC2_STATUS, WriteZoneRegisters);
            {
                int32_t i_1 = 5;
                while (i_1 != 0) {
                    i_1 -= 1;
                    fwrite(g_zero_word, 9, 1, stream);
                }
            }

            if (*s0 < 0) {
                break;
            }
            s0 = &s0[0x80];
        }
    }

    return fclose(stream);
}

static int32_t TraceBuf_constprop_65(const char *arg1, const int32_t *arg2, int32_t arg3)
{
    FILE *stream;
    const int32_t *s0;

    if (arg2 == NULL) {
        return 0;
    }

    stream = safe_fopen(arg1, g_trace_mode);
    s0 = arg2;
    if (arg3 != 0) {
        do {
            s0 = (const int32_t *)((const char *)s0 + 4);
            AL_Write32(stream, *(const uint32_t *)((const char *)s0 - 4));
        } while ((uintptr_t)((const char *)s0 - (const char *)arg2) < (uint32_t)arg3);
    }

    return fclose(stream);
}

static void TraceRefBufMap_constprop_66(const char *arg1, const int32_t *arg2, int32_t arg3, const int32_t *arg4, int32_t arg5)
{
    char var_14c[0x130];
    char var_278[0x12c];

    if (arg2 == NULL) {
        return;
    }

    strcpy(var_14c, arg1);
    __builtin_strncpy(&var_14c[strlen(var_14c)], "Y.hex", 6);
    strcpy(var_278, arg1);
    __builtin_strncpy(&var_278[strlen(var_278)], "C.hex", 6);
    AL_TraceBuffer(var_14c, g_trace_mode, arg2, arg3);
    if (arg4 != NULL) {
        AL_TraceBuffer(var_278, g_trace_mode, arg4, arg5);
    }
}

static uint32_t TraceBufRec_isra_49_constprop_64(void *arg1, char *arg2, char *arg3, char *arg4, char *arg5, int32_t *arg6, int32_t *arg7)
{
    char *base = (char *)arg1;
    uint32_t result;

    strcpy(base + 0x204, base + 4);
    TraceBuf_constprop_65(strcat(base + 0x204, arg2), (int32_t *)(intptr_t)*arg7, arg7[1]);
    if (*arg6 != 0) {
        strcpy(base + 0x204, base + 4);
        TraceBuf_constprop_65(strcat(base + 0x204, arg3), (int32_t *)(intptr_t)arg7[2], arg7[3]);
    }

    result = (uint32_t)*arg5;
    if (result == 0) {
        return result;
    }

    strcpy(base + 0x204, base + 4);
    TraceRefBufMap_constprop_66(strcat(base + 0x204, arg4), (int32_t *)(intptr_t)arg7[4], arg7[5], (int32_t *)(intptr_t)arg7[6], arg7[7]);
    return result;
}
