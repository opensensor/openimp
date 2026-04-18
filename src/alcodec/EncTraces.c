#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern void __assert(const char *expr, const char *file, int32_t line, const char *func);

char *getBasePathName(char *arg1, const char *arg2, int32_t arg3, char *arg4, int32_t arg5, int32_t arg6);
int32_t TraceCmdList(const char *arg1, const char *arg2, int32_t *arg3, int32_t arg4);
int32_t TraceJpegCmd(void *arg1, int32_t arg2, void *arg3, int32_t arg4);
int32_t AL_TraceBuffer(const char *arg1, const char *arg2, const int32_t *arg3, int32_t arg4);
FILE *safe_fopen(const char *arg1, const char *arg2);
size_t AL_Write32(FILE *arg1, uint32_t arg2);
int32_t CmdRegsEnc1ToSliceParam(void *arg1, void *arg2, int32_t arg3); /* forward decl, ported by T<N> later */
int32_t CtrlRegsToJpegParam(void *arg1, void *arg2); /* forward decl, ported by T<N> later */
int32_t TraceBufSrc_8bits_constprop_57(const char *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6, void *arg7); /* forward decl, ported by T<N> later */

typedef struct EncTraceCtx {
    char *prefix;
    char base[0x200];
    char scratch[0x200];
} EncTraceCtx;

int32_t AL_EncTrace_TraceStatus(char *arg1, char *arg2, int32_t arg3, char arg4, char arg5, char arg6, int32_t **arg7)
{
    EncTraceCtx var_570;
    int32_t **s1 = arg7;
    uint32_t s3 = (uint32_t)(uint8_t)arg6;
    const char *var_3c;
    void *var_164;
    int32_t result;

    if (arg3 != 0) {
        var_3c = "ab";
    } else {
        var_3c = "wb";
    }

    result = CmdRegsEnc1ToSliceParam(s1[(uint32_t)(uint8_t)arg5], &var_164, 0);
    if (s3 != 0) {
        int32_t s0_1 = 0;
        int32_t var_160;

        do {
            char var_564[0x200];
            char str[0x200];

            getBasePathName(arg1, arg2, -1, var_564, (uint32_t)(uint8_t)arg4 + s0_1, var_160);
            s0_1 += 1;
            strcpy(str, var_564);
            __builtin_strncpy(&str[strlen(str)], ".stat.hex", 10);
            TraceCmdList(str, var_3c, *s1, 0);
            result = (uint32_t)((uint8_t)s3 - 1) + 1;
            if (arg3 == 0) {
                int32_t *a2_1 = *s1;

                s1 = &s1[1];
                TraceCmdList("/tmp/trace_debug.txt", "wt", a2_1, 0);
                result = (uint32_t)((uint8_t)s3 - 1) + 1;
            } else {
                break;
            }
        } while (s0_1 != result);
    }

    return result;
}

int32_t AL_EncTrace_TraceJpeg(char *arg1, char *arg2, void *arg3, int32_t *arg4)
{
    EncTraceCtx var_458;
    int32_t (*var_30)(void *arg1, int32_t arg2, void *arg3, int32_t arg4) = TraceJpegCmd;
    void *var_4c;
    char var_44c[0x200];
    char str[0x200];
    int32_t var_44;
    int16_t var_40;
    int16_t var_3e;
    int32_t *s6;
    FILE *stream;
    uint32_t i;
    int32_t s6_1;
    int32_t v1;
    int32_t s1_1;
    int32_t *v0_7;
    int32_t a2_2;

    var_458.prefix = arg1;
    CtrlRegsToJpegParam(arg3, &var_4c);
    getBasePathName(var_458.prefix, arg2, 0, var_44c, 0, 4);
    var_30(&var_458, (int32_t)(intptr_t)"wt", arg3, 0);
    TraceBufSrc_8bits_constprop_57(var_44c, (uint32_t)(uint16_t)var_3e, (uint32_t)(uint16_t)var_40, 8, var_44, 0, (void *)(intptr_t)*arg4);

    strcpy(str, var_44c);
    s6 = (int32_t *)(intptr_t)arg4[0x2e];
    __builtin_strncpy(&str[strlen(str)], ".tab.hex", 9);
    stream = safe_fopen(str, "wt");
    i = (uint32_t)s6[2] >> 2;
    s6_1 = *s6;
    while (i != 0) {
        s6_1 += 4;
        i -= 1;
        AL_Write32(stream, *(uint32_t *)(intptr_t)(s6_1 - 4));
    }
    fclose(stream);

    v1 = arg4[0x3d];
    s1_1 = *(int32_t *)((char *)arg3 + 0x34);
    if (v1 < s1_1) {
        s1_1 = v1;
    }

    strcpy(str, var_44c);
    a2_2 = arg4[0x3b];
    __builtin_strncpy(&str[strlen(str)], ".bit.hex", 9);
    AL_TraceBuffer(str, "wt", (int32_t *)(intptr_t)a2_2, s1_1);
    return var_30(&var_458, (int32_t)(intptr_t)"wt", arg3, 1);
}

int32_t AL_EncTrace_TraceJpegStatus(char *arg1, char *arg2, int32_t arg3, void *arg4)
{
    EncTraceCtx var_438;
    const char *s0;
    void *var_2c;
    char var_42c[0x400];

    var_438.prefix = arg1;
    if (arg3 == 0) {
        s0 = "wb";
    } else {
        s0 = "ab";
    }
    CtrlRegsToJpegParam(arg4, &var_2c);
    getBasePathName(var_438.prefix, arg2, 0, var_42c, 0, 4);
    return TraceJpegCmd(&var_438, (int32_t)(intptr_t)s0, arg4, 1);
}
