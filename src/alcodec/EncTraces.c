#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern void __assert(const char *expr, const char *file, int32_t line, const char *func);

#define READ_U8(base, off) (*(uint8_t *)((uint8_t *)(base) + (off)))
#define READ_U16(base, off) (*(uint16_t *)((uint8_t *)(base) + (off)))
#define READ_U32(base, off) (*(uint32_t *)((uint8_t *)(base) + (off)))
#define READ_S32(base, off) (*(int32_t *)((uint8_t *)(base) + (off)))

char *getBasePathName(char *arg1, const char *arg2, int32_t arg3, char *arg4, int32_t arg5, int32_t arg6);
int32_t TraceCmdList(const char *arg1, const char *arg2, int32_t *arg3, int32_t arg4);
int32_t TraceJpegCmd(void *arg1, int32_t arg2, void *arg3, int32_t arg4);
int32_t AL_TraceBuffer(const char *arg1, const char *arg2, const int32_t *arg3, int32_t arg4);
FILE *safe_fopen(const char *arg1, const char *arg2);
size_t AL_Write32(FILE *arg1, uint32_t arg2);
int32_t CmdRegsEnc2ToSliceParam(void *arg1, void *arg2); /* forward decl, ported by T40 */
int32_t EncodingStatusRegsToSliceStatus(void *arg1, void *arg2); /* forward decl, ported by T40 */
int32_t EntropyStatusRegsToSliceStatus(void *arg1, char *arg2, int32_t arg3); /* forward decl, ported by T40 */
uint32_t GetSliceEnc2CmdOffset(char arg1, char arg2, char arg3); /* forward decl, ported by T40 */
int32_t CmdRegsEnc1ToSliceParam(void *arg1, void *arg2, int32_t arg3); /* forward decl, ported by T<N> later */
int32_t CtrlRegsToJpegParam(void *arg1, void *arg2); /* forward decl, ported by T<N> later */
int32_t AL_GetAllocSize_Src(int32_t arg1, int32_t arg2, char arg3, int32_t arg4, int32_t arg5); /* forward decl, ported by T20 */
int32_t AL_GetAllocSizeSrc_Y(int32_t arg1, int32_t arg2, int32_t arg3); /* forward decl, ported by T20 */
uint32_t AL_GetAllocSizeSrc_UV(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4); /* forward decl, ported by T20 */
int32_t AL_GetAllocSizeSrc_MapY(int32_t arg1, int32_t arg2, int32_t arg3); /* forward decl, ported by T20 */
int32_t AL_GetAllocSizeSrc_MapUV(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4); /* forward decl, ported by T20 */
int32_t GetMaxLCU(int32_t arg1, int32_t arg2, char arg3); /* forward decl, ported by T20 */
int32_t *AL_PixMapBuffer_GetDimension(int32_t *arg1, void *arg2); /* forward decl, ported by T17 */
int32_t AL_PixMapBuffer_GetPlanePitch(void *arg1, int32_t arg2); /* forward decl, ported by T17 */
int32_t AL_PixMapBuffer_GetPlaneAddress(void *arg1, int32_t arg2); /* forward decl, ported by T17 */
int32_t TraceBufMV_constprop_51(const char *arg1, int32_t *arg2, int32_t arg3); /* T12 helper */
int32_t TraceHwRc_constprop_54(const char *arg1, int32_t arg2, int32_t *arg3); /* T12 helper */
int32_t TraceBufSrc_8bits_constprop_57(const char *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6, void *arg7); /* forward decl, ported by T<N> later */
int32_t GetH26xBufferName_constprop_59(char *arg1, const char *arg2, const char *arg3); /* T12 helper */
uint32_t TraceBufRec_isra_49_constprop_64(void *arg1, char *arg2, char *arg3, char *arg4, char *arg5, int32_t *arg6, int32_t *arg7); /* T12 helper */

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

static void *DumpBuffer_part_41(FILE *stream, int32_t *offset, int32_t **cursor, int32_t size, int32_t *stash)
{
    int32_t *src;
    int32_t remaining;

    (void)stash;
    if (size <= 0) {
        return cursor;
    }

    src = *cursor;
    remaining = size;
    while (remaining >= 4) {
        AL_Write32(stream, (uint32_t)*src);
        src = &src[1];
        *offset += 4;
        remaining -= 4;
    }

    if (remaining != 0) {
        uint32_t tail = *(uint32_t *)src;
        AL_Write32(stream, tail);
        src = &src[1];
        *offset += remaining;
    }

    *cursor = src;
    return cursor;
}

static int32_t DumpZero_part_46(FILE *stream, int32_t *offset, int32_t *stash, int32_t size, int32_t *tail_word)
{
    (void)stash;
    (void)tail_word;
    while (size > 0) {
        AL_Write32(stream, 0);
        *offset += size >= 4 ? 4 : size;
        size -= 4;
    }
    return *offset;
}

static int32_t TraceRecMapLog_local(char *prefix, const char *name, int32_t append, void *status)
{
    char path[0x200];
    FILE *stream;
    const char *mode = append != 0 ? "at" : "wt";
    uint8_t *map_y = *(uint8_t **)((char *)status + 0x54);
    int32_t map_y_size = READ_S32(status, 0x58);
    uint8_t *map_c = *(uint8_t **)((char *)status + 0x5c);
    int32_t map_c_size = READ_S32(status, 0x60);
    int32_t total_tiles = 0;
    int32_t used_tiles = 0;
    int32_t i;

    if (map_y == NULL) {
        __assert("pBufYuv[AL_PLANE_MAP_Y].pBuf", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_trace/EncTraces.c", 0x274, "TraceRecMapLog");
        return 0;
    }

    getBasePathName(prefix, name, 0, path, 0, 0);
    strcat(path, ".map.log");
    stream = safe_fopen(path, mode);

    for (i = 0; i < map_y_size; ++i) {
        uint32_t entry = map_y[i];
        if ((entry & 0xfU) != 0) {
            total_tiles += 1;
            used_tiles += (int32_t)(entry & 0xfU);
        }
        if ((entry & 0xf0U) != 0) {
            total_tiles += 1;
            used_tiles += (int32_t)(entry >> 4);
        }
    }

    if (map_c != NULL && map_c_size != 0) {
        for (i = 0; i < map_c_size; ++i) {
            uint32_t entry = map_c[i];
            if ((entry & 0xfU) != 0) {
                total_tiles += 1;
                used_tiles += (int32_t)(entry & 0xfU);
            }
            if ((entry & 0xf0U) != 0) {
                total_tiles += 1;
                used_tiles += (int32_t)(entry >> 4);
            }
        }
    }

    if (total_tiles != 0) {
        fprintf(stream, "%d%%\n", (used_tiles * 100 + total_tiles - 1) / total_tiles);
    } else {
        fprintf(stream, "0%%\n");
    }

    return fclose(stream);
}

int32_t AL_EncTrace_TraceInputsFifo(char *arg1, char *arg2, int32_t arg3, char arg4, const char *arg5,
                                    int32_t arg6, int32_t arg7, int32_t arg8, char arg9, char arg10,
                                    int16_t arg11, int32_t **arg12, int32_t *arg13, int32_t *arg14)
{
    EncTraceCtx ctx;
    char base[0x200];
    char path[0x200];
    char aux[0x200];
    char h26x_name[0x20];
    uint8_t fifo_idx = (uint8_t)arg9;
    uint8_t base_idx = (uint8_t)arg4;
    uint32_t slot = (uint32_t)fifo_idx;
    uint32_t limit = (uint16_t)arg11;
    int32_t *cmd = arg12[slot];
    uint8_t storage_mode = 0;
    uint8_t chroma_mode = 0;
    uint8_t bit_depth = 8;
    uint8_t lcu_size = 0;
    uint8_t has_map_a = 0;
    uint8_t has_map_b = 0;
    uint8_t has_srd = 0;
    uint8_t has_two_cmds = 0;
    uint8_t hwr_count = (uint8_t)arg10;
    int32_t width = 0;
    int32_t height = 0;
    int32_t rec_count_a;
    int32_t rec_count_b;
    int32_t i;
    char slice_param[0x194];

    (void)arg5;
    (void)arg6;
    (void)arg7;
    (void)arg8;

    if (arg14 == NULL) {
        __assert("pBuffers", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_trace/EncTraces.c", 0x411, "AL_EncTrace_TraceInputsFifo");
        return 0;
    }

    memset(&ctx, 0, sizeof(ctx));
    memset(slice_param, 0, sizeof(slice_param));
    ctx.prefix = arg1;

    CmdRegsEnc1ToSliceParam(cmd, slice_param, 0);
    if (READ_S32(cmd, 0xc4) != 0) {
        CmdRegsEnc2ToSliceParam(cmd, slice_param);
    }

    storage_mode = READ_U8(slice_param, 0xf7);
    chroma_mode = READ_U8(slice_param, 0x12);
    bit_depth = READ_U8(slice_param, 0x7e) == 2 ? 10 : 8;
    lcu_size = READ_U8(slice_param, 0x19);
    width = READ_U16(slice_param, 0x74);
    height = READ_U16(slice_param, 0x78);
    has_map_a = READ_U8(slice_param, 0xee);
    has_map_b = READ_U8(slice_param, 0xef);
    has_srd = READ_U8(slice_param, 0xf6);
    has_two_cmds = READ_U8(slice_param, 0xf7);

    getBasePathName(arg1, arg2, arg3, base, (int32_t)(base_idx + fifo_idx), 0);

    strcpy(path, base);
    strcat(path, ".cmd.hex");
    TraceCmdList(path, "wt", cmd, 1);
    if (fifo_idx < limit) {
        strcpy(aux, base);
        strcat(aux, ".cmd2.hex");
        TraceCmdList(aux, "wt", cmd, 1);
    }

    strcpy(path, base);
    strcat(path, ".sram_map.hex");
    {
        FILE *stream = safe_fopen(path, "wt");
        int32_t cmd2_addr = arg13 != NULL ? arg13[slot] : 0;

        fprintf(stream, "(IN )  %08X => %s%d\n", cmd2_addr, "cmd", base_idx + fifo_idx);
        fprintf(stream, "(OUT)  %08X => %s%d\n", cmd2_addr, "stat", base_idx + fifo_idx);
        fprintf(stream, "(IN )  %08X => %s%d\n", READ_U32(cmd, 0x80), "srcY", base_idx + fifo_idx);
        fprintf(stream, "(IN )  %08X => %s%d\n", READ_U32(cmd, 0x84), "srcC", base_idx + fifo_idx);
        if ((READ_U32(cmd, 0x88) & 0xfffffffdU) == 5U) {
            fprintf(stream, "(IN )  %08X => %s%d\n", READ_U32(cmd, 0x190), "srcYmap", base_idx + fifo_idx);
            fprintf(stream, "(IN )  %08X => %s%d\n", READ_U32(cmd, 0x194), "srcCmap", base_idx + fifo_idx);
        }
        fprintf(stream, "(IN )  %08X => %s%d\n", READ_U32(cmd, 0x8c), "recMap", base_idx + fifo_idx);
        fprintf(stream, "(OUT)  %08X => %s%d\n", READ_U32(cmd, 0x90), "recY", base_idx + fifo_idx);
        fprintf(stream, "(OUT)  %08X => %s%d\n", READ_U32(cmd, 0x94), "recC", base_idx + fifo_idx);
        fprintf(stream, "(IN )  %08X => %s%d\n", READ_U32(cmd, 0x9c), "refMap", base_idx + fifo_idx);
        rec_count_a = arg14[1];
        rec_count_b = arg14[9];
        if (rec_count_a != 0) {
            fprintf(stream, "(IN )  %08X => %s%d\n", READ_U32(cmd, 0xa0), "refAY", base_idx + fifo_idx);
            fprintf(stream, "(IN )  %08X => %s%d\n", READ_U32(cmd, 0xa4), "refAC", base_idx + fifo_idx);
        }
        if (rec_count_b != 0) {
            fprintf(stream, "(IN )  %08X => %s%d\n", READ_U32(cmd, 0xa8), "refBY", base_idx + fifo_idx);
            fprintf(stream, "(IN )  %08X => %s%d\n", READ_U32(cmd, 0xac), "refBC", base_idx + fifo_idx);
        }
        fprintf(stream, "(IN )  %08X => %s%d\n", READ_U32(cmd, 0xb0), "mvcol", base_idx + fifo_idx);
        fprintf(stream, "(OUT)  %08X => %s%d\n", READ_U32(cmd, 0xb8), "mv", base_idx + fifo_idx);
        fprintf(stream, "(OUT)  %08X => %s%d\n", READ_U32(cmd, 0xbc), "wpp", base_idx + fifo_idx);
        fprintf(stream, "(IN )  %08X => %s%d\n", READ_U32(cmd, 0xb4), "hwrc.in", base_idx + fifo_idx);
        fprintf(stream, "(OUT)  %08X => %s%d\n", READ_U32(cmd, 0xb4), "hwrc.out", base_idx + fifo_idx);
        if (READ_S32(cmd, 0xc4) != 0) {
            if (has_srd != 0) {
                fprintf(stream, "(IN )  %08X => %s%d\n", READ_U32(cmd, 0x1bc), "srd.in", base_idx + fifo_idx);
                fprintf(stream, "(OUT)  %08X => %s%d\n", READ_U32(cmd, 0x1bc), "srd.out", base_idx + fifo_idx);
            }
            if (arg13 != NULL) {
                fprintf(stream, "(IN )  %08X => %s%d\n", arg13[slot], "cmd2", base_idx + fifo_idx);
            }
            if (READ_U32(cmd, 0xe8) != 0) {
                fprintf(stream, "(IN )  %08X => %s%d\n", READ_U32(cmd, 0xe8), "cpr.map", base_idx + fifo_idx);
                fprintf(stream, "(IN )  %08X => %s%d\n", READ_U32(cmd, 0xec), "cpr.dat", base_idx + fifo_idx);
                GetH26xBufferName_constprop_59(h26x_name, "h", ".hex");
                fprintf(stream, "(OUT)  %08X => %s%d\n", READ_U32(cmd, 0xf0), h26x_name, base_idx + fifo_idx);
            }
            if (READ_U32(cmd, 0xc0) != 0) {
                GetH26xBufferName_constprop_59(h26x_name, "h", ".hex");
                fprintf(stream, "(OUT)  %08X => %s%d\n", READ_U32(cmd, 0xc0), h26x_name, base_idx + fifo_idx);
            }
            if (has_map_a != 0) {
                fprintf(stream, "(IN )  %08X => %s%d\n", READ_U32(cmd, 0xdc), "mapY", base_idx + fifo_idx);
                if (arg8 != 0) {
                    fprintf(stream, "(IN )  %08X => %s%d\n", READ_U32(cmd, 0xe0), "mapAY", base_idx + fifo_idx);
                    if (has_map_b != 0) {
                        fprintf(stream, "(IN )  %08X => %s%d\n", READ_U32(cmd, 0xe4), "mapBY", base_idx + fifo_idx);
                    }
                    fprintf(stream, "(IN )  %08X => %s%d\n", READ_U32(cmd, 0x19c), "mapC", base_idx + fifo_idx);
                    fprintf(stream, "(IN )  %08X => %s%d\n", READ_U32(cmd, 0x1a0), "mapAC", base_idx + fifo_idx);
                    if (has_map_b != 0) {
                        fprintf(stream, "(IN )  %08X => %s%d\n", READ_U32(cmd, 0x1a4), "mapBC", base_idx + fifo_idx);
                    }
                } else {
                    if (has_map_b != 0) {
                        fprintf(stream, "(IN )  %08X => %s%d\n", READ_U32(cmd, 0xe4), "mapBY", base_idx + fifo_idx);
                    }
                    fprintf(stream, "(OUT)  %08X => %s%d\n", READ_U32(cmd, 0x19c), "mapC", base_idx + fifo_idx);
                    fprintf(stream, "(IN )  %08X => %s%d\n", READ_U32(cmd, 0x1a0), "mapAC", base_idx + fifo_idx);
                    if (has_map_b != 0) {
                        fprintf(stream, "(IN )  %08X => %s%d\n", READ_U32(cmd, 0x1a4), "mapBC", base_idx + fifo_idx);
                    }
                }
            } else if (has_two_cmds != 0) {
                fprintf(stream, "(IN )  %08X => %s%d\n", READ_U32(cmd, 0xd0), "mapY", base_idx + fifo_idx);
                fprintf(stream, "(IN )  %08X => %s%d\n", READ_U32(cmd, 0xd4), "cpr.dat", base_idx + fifo_idx);
            }
        }
        fclose(stream);
    }

    if (*(void **)arg14 != NULL) {
        if (bit_depth == 8) {
            TraceBufSrc_8bits_constprop_57(base, width, height, 8, chroma_mode, storage_mode, *(void **)arg14);
        } else {
            void *pixmap = *(void **)arg14;
            int32_t dims[2];
            int32_t y_lines;
            int32_t plane;

            AL_PixMapBuffer_GetDimension(dims, pixmap);
            y_lines = ((dims[1] + 7) >> 3) << 3;
            for (plane = 0; plane <= (chroma_mode > 0 ? 1 : 0); ++plane) {
                int32_t plane_pitch = AL_PixMapBuffer_GetPlanePitch(pixmap, plane);
                int32_t width_words = (plane_pitch + 1) >> 1;
                int32_t rows = y_lines;
                int32_t row;
                int32_t src_width = dims[0];

                strcpy(path, base);
                strcat(path, plane == 0 ? ".srcY.hex" : ".srcC.hex");
                {
                    FILE *stream = safe_fopen(path, "wt");
                    uint32_t *src = (uint32_t *)(intptr_t)AL_PixMapBuffer_GetPlaneAddress(pixmap, plane);

                    if (plane == 1 && chroma_mode == 1) {
                        rows >>= 1;
                    }
                    for (row = 0; row < rows; ++row) {
                        int32_t col_words = 0;
                        while (col_words < src_width) {
                            AL_Write32(stream, src[col_words >> 1]);
                            col_words += 2;
                        }
                        while (col_words < width_words) {
                            fwrite("00000000\n", 9, 1, stream);
                            col_words += 2;
                        }
                        src = (uint32_t *)((char *)src + plane_pitch);
                    }
                    fclose(stream);
                }
            }
        }
    } else {
        int32_t src_size = AL_GetAllocSize_Src(height, width, (char)bit_depth, chroma_mode, storage_mode);
        strcpy(path, base);
        strcat(path, ".srcY.hex");
        {
            FILE *stream = safe_fopen(path, "wt");
            for (i = 0; i < src_size; i += 4) {
                fwrite("00000000\n", 9, 1, stream);
            }
            fclose(stream);
        }
    }

    strcpy(path, base);
    strcat(path, ".qp.hex");
    {
        FILE *stream = safe_fopen(path, "wt");
        int32_t *buf = (int32_t *)(intptr_t)arg14[0x2f];
        if (buf != NULL && *buf != 0) {
            int32_t *src = (int32_t *)(intptr_t)*buf;
            int32_t size = buf[2];
            for (i = 0; i < size; i += 4) {
                AL_Write32(stream, *(uint32_t *)((char *)src + i));
            }
        } else {
            int32_t max_lcu = GetMaxLCU(height, width, (char)lcu_size) + 0x30;
            for (i = 0; i < max_lcu; i += 4) {
                fwrite("00000000\n", 9, 1, stream);
            }
        }
        fclose(stream);
    }

    strcpy(path, base);
    strcat(path, ".scl.hex");
    {
        FILE *stream = safe_fopen(path, "wt");
        int32_t *buf = (int32_t *)(intptr_t)arg14[0x2e];
        if (buf != NULL && *buf != 0 && (buf[5] & 0x11) != 0) {
            int32_t *src = (int32_t *)(intptr_t)*buf;
            for (i = 0; i < 0x6400; i += 4) {
                AL_Write32(stream, *(uint32_t *)((char *)src + i));
            }
        } else {
            for (i = 0; i < 0x6400; i += 4) {
                fwrite("00000000\n", 9, 1, stream);
            }
        }
        fclose(stream);
    }

    {
        int32_t map_type = storage_mode;
        TraceBufRec_isra_49_constprop_64(&ctx, ".refAY.hex", ".refAC.hex", ".mapA", (char *)&has_map_a, &map_type, &arg14[1]);
        TraceBufRec_isra_49_constprop_64(&ctx, ".refBY.hex", ".refBC.hex", ".mapB", (char *)&has_map_b, &map_type, &arg14[9]);
    }

    strcpy(path, base);
    strcat(path, ".mvcol.hex");
    TraceBufMV_constprop_51(path, &arg14[0x19], 0);

    if (hwr_count != 0) {
        int32_t *hwr = &arg14[0x30];
        for (i = 0; i < hwr_count; ++i) {
            getBasePathName(arg1, arg2, arg3, base, (int32_t)(base_idx + i), 0);
            strcpy(path, base);
            strcat(path, ".hwrc.in.hex");
            TraceHwRc_constprop_54(path, 0, hwr);
            hwr = &hwr[6];
        }
    }

    if (has_srd != 0) {
        strcpy(path, base);
        strcat(path, ".srd.in.hex");
        return AL_TraceBuffer(path, "wt", (int32_t *)(intptr_t)arg14[0x36], arg14[0x38]);
    }

    return has_srd;
}

uint32_t AL_EncTrace_TraceOutputsFifo(char *arg1, char *arg2, int32_t arg3, char arg4, char arg5, char arg6,
                                      char arg7, int16_t arg8, char arg9, char arg10, int32_t **arg11,
                                      void *arg12)
{
    EncTraceCtx ctx;
    char base[0x200];
    char path[0x200];
    char suffix[0x20];
    char cmd_sp[0x238];
    char enc_status[0x70];
    char entropy_status[0x10];
    uint8_t fifo_idx = (uint8_t)arg5;
    uint8_t base_idx = (uint8_t)arg4;
    uint8_t hwrc_count = (uint8_t)arg6;
    uint8_t h26x_dump = (uint8_t)arg10;
    uint8_t stat_count = (uint8_t)arg6;
    uint32_t status_limit = (uint16_t)arg8;
    uint32_t idx = fifo_idx;
    int32_t *cmd = arg11[idx];
    int32_t offset = 0;
    int32_t scratch = 0;
    int32_t *cursor;
    int32_t i;

    (void)arg7;
    (void)arg9;

    memset(&ctx, 0, sizeof(ctx));
    memset(cmd_sp, 0, sizeof(cmd_sp));
    ctx.prefix = arg1;

    CmdRegsEnc1ToSliceParam(cmd, cmd_sp, 0);
    if (READ_S32(cmd, 0xc4) != 0) {
        CmdRegsEnc2ToSliceParam(cmd, cmd_sp);
    }

    if (hwrc_count != 0) {
        for (i = 0; i < hwrc_count; ++i) {
            getBasePathName(arg1, arg2, arg3, base, (int32_t)(base_idx + i), 0);
            strcpy(path, base);
            strcat(path, ".hwrc.out.hex");
            TraceHwRc_constprop_54(path, 0, (int32_t *)((char *)arg12 + 0xc0 + i * 0x18));
        }
    } else {
        getBasePathName(arg1, arg2, arg3, base, (int32_t)(base_idx + fifo_idx), 0);
    }

    if (READ_U8(cmd_sp, 0xf6) != 0) {
        strcpy(path, base);
        strcat(path, ".srd.out.hex");
        AL_TraceBuffer(path, "wt", (int32_t *)(intptr_t)READ_S32(arg12, 0xd8), READ_S32(arg12, 0xe0));
    }

    if (h26x_dump != 0) {
        int32_t *md = *(int32_t **)((char *)arg12 + 0xb4);
        uint32_t offset_bytes = READ_U32(arg12, 0x100) + READ_U32(arg12, 0x104);

        if (READ_U32(arg12, 0xf4) < offset_bytes) {
            __assert("p26x->iOffset + p26x->iAvailSize <= (int)p26x->tMD.uSize", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_trace/EncTraces.c", 0x3ef, "TraceH26xFullWithoutHeaders");
        }

        GetH26xBufferName_constprop_59(suffix, "h", ".hex");
        strcpy(path, base);
        strcat(path, suffix);
        {
            FILE *stream = safe_fopen(path, "wt");
            if (md != NULL && *md != 0) {
                uint8_t *bitstream_base = (uint8_t *)(intptr_t)*md;
                uint32_t size = md[1];
                uint32_t dump_offset = READ_U32(arg12, 0x100);
                uint32_t dump_size = READ_U32(arg12, 0x104);

                if (dump_offset + dump_size > size) {
                    dump_size = size > dump_offset ? size - dump_offset : 0;
                }

                offset = 0;
                cursor = (int32_t *)(intptr_t)(bitstream_base + dump_offset);
                DumpZero_part_46(stream, &offset, &scratch, (int32_t)READ_U32(arg12, 0xec), &scratch);
                DumpBuffer_part_41(stream, &offset, &cursor, (int32_t)dump_size, &scratch);
                if ((offset & 3) != 0) {
                    AL_Write32(stream, (uint32_t)scratch);
                }
            }
            fclose(stream);
        }
    }

    if (READ_U8(cmd_sp, 0xee) != 0) {
        int32_t flag = 0;
        int32_t map_kind = 0;
        TraceBufRec_isra_49_constprop_64(&ctx, ".recY.hex", ".recC.hex", ".map", (char *)&flag, &map_kind, (int32_t *)((char *)arg12 + 0x44));
        if (flag != 0) {
            TraceRecMapLog_local(arg1, arg2, arg3, arg12);
        }
    }

    if (READ_U8(cmd_sp, 0xef) != 0) {
        strcpy(path, base);
        strcat(path, ".cpr.map.hex");
        {
            FILE *stream = safe_fopen(path, "wt");
            int32_t *src = (int32_t *)(intptr_t)READ_S32(arg12, 0xa0);
            int32_t size = READ_S32(arg12, 0xa8);
            if (src == NULL) {
                __assert("pCompMap && pCompMap->tMD.pVirtualAddr", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_trace/EncTraces.c", 0x24c, "TraceBufMap");
            }
            for (i = 0; src != NULL && i < size; i += 4) {
                AL_Write32(stream, *(uint32_t *)((char *)src + i));
            }
            fclose(stream);
        }

        strcpy(path, base);
        strcat(path, ".cpr.dat.hex");
        {
            FILE *stream = safe_fopen(path, "wt");
            int32_t *src = (int32_t *)(intptr_t)READ_S32(arg12, 0x8c);
            int32_t size = READ_S32(arg12, 0x94);
            if (src == NULL) {
                __assert("pCompData && pCompData->tMD.pVirtualAddr", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_trace/EncTraces.c", 0x25c, "TraceBufComp");
            }
            for (i = 0; src != NULL && i < size; i += 4) {
                AL_Write32(stream, *(uint32_t *)((char *)src + i));
            }
            fclose(stream);
        }
    }

    strcpy(path, base);
    strcat(path, ".mv.hex");
    TraceBufMV_constprop_51(path, (int32_t *)((char *)arg12 + 0x78), READ_U8(cmd_sp, 0xf7));

    strcpy(path, base);
    strcat(path, ".wpp.hex");
    {
        FILE *stream = safe_fopen(path, "wt");
        int32_t *buf = *(int32_t **)((char *)arg12 + 0xb4);
        if (buf != NULL && *buf != 0) {
            uint32_t words = (uint32_t)buf[2] >> 2;
            int32_t *src = (int32_t *)(intptr_t)*buf;
            for (i = 0; (uint32_t)i < words; ++i) {
                AL_Write32(stream, (uint32_t)src[i]);
            }
        }
        fclose(stream);
    }

    if (stat_count != 0) {
        uint32_t end = (uint32_t)(base_idx + fifo_idx + stat_count);
        uint32_t cur;

        for (cur = base_idx; cur < end; ++cur) {
            getBasePathName(arg1, arg2, arg3, base, (int32_t)cur, 0);
            strcpy(path, base);
            strcat(path, ".stat.hex");
            TraceCmdList(path, "wt", *arg11, 1);
            arg11 = &arg11[1];
        }
    }

    EncodingStatusRegsToSliceStatus(cmd, enc_status);
    EntropyStatusRegsToSliceStatus(cmd, entropy_status, READ_S32(arg12, 0xec));
    if (READ_S32(cmd, 0xc4) != 0 && status_limit != 0) {
        uint32_t slice;
        uint32_t core_count = (uint8_t)arg6 != 0 ? (uint8_t)arg6 : 1;
        uint32_t slice_count = status_limit < core_count ? status_limit : core_count;

        for (slice = 0; slice < slice_count; ++slice) {
            uint32_t enc2_off = GetSliceEnc2CmdOffset((char)core_count, (char)status_limit, (char)slice);
            void *slice_cmd = (uint8_t *)arg11[slice % core_count] + (enc2_off << 9);
            CmdRegsEnc1ToSliceParam(slice_cmd, cmd_sp, 0);
            EncodingStatusRegsToSliceStatus(slice_cmd, enc_status);
            EntropyStatusRegsToSliceStatus(slice_cmd, entropy_status, READ_S32(arg12, 0xec));
        }
    }

    return (uint32_t)hwrc_count;
}
