#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>

extern char _gp;
extern int32_t __assert(const char *expression, const char *file, int32_t line,
                        const char *function, void *caller);

int32_t CmdRegsEnc2ToSliceParam(void *arg1, void *arg2); /* forward decl, ported in this unit */
int32_t SliceParamToCmdRegsEnc2(void *arg1, void *arg2); /* forward decl, ported in this unit */
int32_t CtrlRegsToJpegParam(int32_t *arg1, char *arg2); /* forward decl, ported in this unit */

#define READ_U8(base, off) (*(uint8_t *)((uint8_t *)(base) + (off)))
#define READ_S8(base, off) (*(int8_t *)((uint8_t *)(base) + (off)))
#define READ_U16(base, off) (*(uint16_t *)((uint8_t *)(base) + (off)))
#define READ_S16(base, off) (*(int16_t *)((uint8_t *)(base) + (off)))
#define READ_U32(base, off) (*(uint32_t *)((uint8_t *)(base) + (off)))
#define READ_S32(base, off) (*(int32_t *)((uint8_t *)(base) + (off)))
#define WRITE_U8(base, off, val) (*(uint8_t *)((uint8_t *)(base) + (off)) = (uint8_t)(val))
#define WRITE_U16(base, off, val) (*(uint16_t *)((uint8_t *)(base) + (off)) = (uint16_t)(val))
#define WRITE_U32(base, off, val) (*(uint32_t *)((uint8_t *)(base) + (off)) = (uint32_t)(val))
#define WRITE_S32(base, off, val) (*(int32_t *)((uint8_t *)(base) + (off)) = (int32_t)(val))

static void slp_kmsg(const char *fmt, ...)
{
    int fd;
    char buf[256];
    va_list ap;
    int len;

    fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
    if (fd < 0)
        return;

    va_start(ap, fmt);
    len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (len > 0) {
        if (len >= (int)sizeof(buf))
            len = (int)sizeof(buf) - 1;
        write(fd, buf, (size_t)len);
    }
    close(fd);
}

static char g_dummy_elf_header;

typedef int32_t (*SetStepsFn)(void *arg1, void *arg2);

static int32_t setSteps_noentropy(void *arg1, void *arg2);
static int32_t setSteps_syncentropy(void *arg1, void *arg2);
static int32_t setSteps_synccropentropy(void *arg1, void *arg2);
static int32_t setSteps_asyncentropy(void *arg1, void *arg2);

uint32_t AddOneModuleByCore(int32_t arg1, void *arg2, int32_t arg3);

int32_t *SetCommandListBuffer(int32_t *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5)
{
    int32_t *j = (int32_t *)(intptr_t)(arg2 - 1);
    int32_t v1 = (int32_t)(intptr_t)j * arg3;
    int32_t *i = arg1;
    int32_t t4 = 0;

    arg1[0x260] = arg2;
    arg1[0x261] = 0;
    if (arg2 > 0) {
        do {
            int32_t v1_2 = arg4 + (t4 << 9);
            int32_t *cur = i;

            do {
                *cur = v1_2;
                cur[0x130] = arg5 - arg4 + v1_2;
                cur = &cur[1];
                v1_2 += arg3 << 9;
            } while (cur != &i[arg2]);

            j = cur;
            t4 += v1 + arg3;
            i = &i[0x10];
        } while (&arg1[0x130] != i);
    }
    return j;
}

int32_t PopCommandListAddresses(void *arg1, void *arg2)
{
    int32_t a3_2 = READ_S32(arg1, 0x980);
    int32_t t0 = READ_S32(arg1, 0x984);

    if (a3_2 > 0) {
        int32_t *v0_3 = (int32_t *)((uint8_t *)arg1 + ((t0 + 0x13) << 6));
        int32_t *out = (int32_t *)arg2;
        int32_t *a3_1 = (int32_t *)((uint8_t *)arg2 + (a3_2 << 2));

        do {
            int32_t v1_1 = *v0_3;

            out = &out[1];
            *(out - 1) = *(v0_3 - 0x130);
            *(out + 0xf) = v1_1;
            v0_3 = &v0_3[1];
        } while (out != a3_1);
    }

    WRITE_S32(arg1, 0x984, (t0 + 1) % 0x13);
    return ((t0 + 1) / 0x13) * 0x13;
}

int32_t EncodingStatusRegsToSliceStatus(void *arg1, void *arg2)
{
    int32_t v0_2;
    int32_t v1_2;
    int32_t v1_3;
    int32_t a2_1;
    int32_t a3_1;
    int32_t v0_7;
    int32_t v1_4;
    int32_t v0_17;

    WRITE_U32(arg2, 0x34, READ_U32(arg1, 0x130));
    WRITE_U32(arg2, 0x38, READ_U32(arg1, 0x134) & 0x0fffffffU);
    v0_2 = READ_S32(arg1, 0x138);
    WRITE_U32(arg2, 0x44, READ_U32(arg1, 0x13c));
    WRITE_U32(arg2, 0x48, READ_U32(arg1, 0x140));
    v1_2 = READ_S32(arg1, 0x144);
    WRITE_U16(arg2, 0x3e, (uint16_t)(v0_2 & 0xff));
    WRITE_U32(arg2, 0x4c, v1_2);
    v1_3 = READ_S32(arg1, 0x148);
    WRITE_U16(arg2, 0x42, (uint16_t)((uint32_t)v0_2 >> 16));
    WRITE_U16(arg2, 0x40, (uint16_t)(((uint32_t)v0_2 >> 8) & 0xff));
    WRITE_U32(arg2, 0x50, v1_3);
    WRITE_U32(arg2, 0x54, READ_U32(arg1, 0x14c));
    WRITE_U32(arg2, 0x58, READ_U32(arg1, 0x150));
    WRITE_U32(arg2, 0x5c, READ_U32(arg1, 0x154));
    a2_1 = READ_S32(arg1, 0x158);
    a3_1 = READ_S32(arg1, 0x15c);
    v0_7 = READ_S32(arg1, 0x160);
    v1_4 = READ_S32(arg1, 0x164);
    WRITE_U32(arg2, 0x14, READ_U32(arg1, 0x10c));
    WRITE_U32(arg2, 0x60, a3_1);
    WRITE_U32(arg2, 0x64, a2_1);
    WRITE_U32(arg2, 0x68, v1_4);
    WRITE_U32(arg2, 0x6c, v0_7);
    WRITE_U32(arg2, 0x18, READ_U32(arg1, 0x110));
    WRITE_U32(arg2, 0x1c, READ_U32(arg1, 0x114));
    WRITE_U32(arg2, 0x20, READ_U32(arg1, 0x11c));
    WRITE_U32(arg2, 0x24, READ_U32(arg1, 0x120));
    WRITE_U32(arg2, 0x28, READ_U32(arg1, 0x124));
    WRITE_U32(arg2, 0x2c, READ_U32(arg1, 0x128));
    WRITE_U32(arg2, 0x30, READ_U16(arg1, 0x12c));
    WRITE_U32(arg2, 0x10, READ_U16(arg1, 0x12e));
    v0_17 = (READ_U32(arg1, 8) >> 0xb) & 1;
    if (v0_17 != 0) {
        WRITE_U8(arg2, 2, 0);
        return v0_17;
    }

    {
        uint8_t v0_23 =
            (uint8_t)(((((READ_U32(arg1, 0x104) & 0x3fffffffU) + 0x1fU) >> 5 << 5) <
                       (((READ_U32(arg1, 0xcc) >> 5) - 1U) << 5))
                          ? 1
                          : 0) ^
                     1;
        WRITE_U8(arg2, 2, v0_23);
        return v0_23;
    }
}

int32_t EntropyStatusRegsToSliceStatus(void *arg1, char *arg2, int32_t arg3)
{
    if (((READ_U32(arg1, 8) >> 0xb) & 1U) != 0) {
        int32_t v0_2 = READ_S32(arg1, 0x104) & 0x3fffffff;
        uint8_t v0_7;

        WRITE_U32(arg2, 8, v0_2);
        WRITE_U32(arg2, 0xc, READ_U32(arg1, 0x108));
        v0_7 = (uint8_t)(((((uint32_t)v0_2 + 0x1fU) >> 5 << 5) < ((((uint32_t)arg3 >> 5) - 1U) << 5)) ? 1 : 0) ^ 1;
        WRITE_U8(arg2, 0, READ_U32(arg1, 0x104) >> 0x1f);
        WRITE_U8(arg2, 1, v0_7);
        return v0_7;
    }

    {
        uint8_t a3 = READ_U8(arg2, 2);
        int32_t v0_9 = READ_S32(arg1, 0x1e4) & 0x3fffffff;
        int32_t a0 = READ_S32(arg1, 0x1e4);
        uint8_t v0_14;

        WRITE_U32(arg2, 8, v0_9);
        WRITE_U32(arg2, 0xc, READ_U32(arg1, 0x1e8));
        v0_14 = (uint8_t)(((((uint32_t)v0_9 + 0x1fU) >> 5 << 5) < ((((uint32_t)arg3 >> 5) - 1U) << 5)) ? 1 : 0) ^ 1;
        WRITE_U8(arg2, 0, (uint32_t)a0 >> 0x1f);
        WRITE_U8(arg2, 1, v0_14);
        WRITE_U8(arg2, 2, ((((uint32_t)a0 >> 0x1e) & 1U) | a3) & 1U);
        return v0_14;
    }
}

int32_t InitSliceStatus(char *arg1)
{
    WRITE_U16(arg1, 0x3e, 0x7fff);
    __builtin_memset(&arg1[4], 0, 0x38);
    __builtin_memset(&arg1[0x44], 0, 0x2c);
    WRITE_U16(arg1, 0x42, 0);
    WRITE_U16(arg1, 0x40, 0);
    arg1[1] = 0;
    arg1[0] = 0;
    arg1[2] = 0;
    return 0;
}

int32_t MergeEncodingStatus(void *arg1, void *arg2)
{
    int32_t t7 = (int16_t)READ_U16(arg2, 0x3e);
    int32_t t8 = (int16_t)READ_U16(arg1, 0x3e);
    int32_t t1_1;
    int32_t t0_1;
    int32_t t5_1;
    int32_t a3_1;
    int32_t v0;
    int32_t t9;
    int32_t t4_1;
    int32_t a2_1;
    int32_t t3_1;
    int32_t v1_1;
    int32_t v0_1;
    int32_t a2_2;
    int32_t a2_3;
    int32_t t5_2;
    int32_t v0_4;
    int32_t v1_5;
    int32_t t6_3;
    int32_t s1_4;
    int32_t s0_1;
    int32_t t9_1;
    int32_t t8_1;
    int32_t t7_6;
    int32_t s2_1;
    int32_t t4_2;
    int32_t t3_3;
    int32_t t2_3;
    int32_t t1_3;
    int32_t t0_3;
    int32_t a3_4;
    int32_t t4_3;
    int32_t t3_4;
    int32_t a2_4;
    int32_t v1_6;
    uint8_t v0_7;
    int32_t t2_4;
    int32_t t1_4;
    uint8_t result;

    if (t7 >= t8) {
        t7 = t8;
    }
    t1_1 = READ_S32(arg1, 0x48) + READ_S32(arg2, 0x48);
    t0_1 = READ_S32(arg1, 0x4c) + READ_S32(arg2, 0x4c);
    t5_1 = READ_S32(arg1, 0x34) + READ_S32(arg2, 0x34);
    a3_1 = READ_S32(arg1, 0x50) + READ_S32(arg2, 0x50);
    v0 = (int16_t)READ_U16(arg2, 0x40);
    t9 = (int16_t)READ_U16(arg1, 0x40);
    t4_1 = READ_S32(arg1, 0x38) + READ_S32(arg2, 0x38);
    a2_1 = READ_S32(arg1, 0x54) + READ_S32(arg2, 0x54);
    if (t9 >= v0) {
        v0 = t9;
    }
    t3_1 = READ_S32(arg1, 0x44) + READ_S32(arg2, 0x44);
    v1_1 = READ_S32(arg1, 0x58) + READ_S32(arg2, 0x58);
    WRITE_U16(arg1, 0x42, READ_U16(arg1, 0x42) + READ_U16(arg2, 0x42));
    WRITE_S32(arg1, 0x34, t5_1);
    WRITE_S32(arg1, 0x38, t4_1);
    WRITE_S32(arg1, 0x44, t3_1);
    WRITE_U16(arg1, 0x40, (uint16_t)v0);
    WRITE_U16(arg1, 0x3e, (uint16_t)t7);
    WRITE_S32(arg1, 0x48, t1_1);
    v0_1 = READ_S32(arg1, 0x60);
    WRITE_S32(arg1, 0x54, a2_1);
    a2_2 = READ_S32(arg2, 0x60);
    WRITE_S32(arg1, 0x50, a3_1);
    WRITE_S32(arg1, 0x58, v1_1);
    a2_3 = v0_1 + a2_2;
    t5_2 = READ_S32(arg2, 0x5c);
    WRITE_S32(arg1, 0x64, ((uint32_t)a2_3 < (uint32_t)v0_1 ? 1 : 0) + READ_S32(arg1, 0x64) + READ_S32(arg2, 0x64));
    v0_4 = READ_S32(arg1, 0x68);
    v1_5 = v0_4 + READ_S32(arg2, 0x68);
    t6_3 = READ_S32(arg1, 0x6c) + READ_S32(arg2, 0x6c);
    s1_4 = READ_S32(arg2, 0x14);
    s0_1 = READ_S32(arg2, 0x30);
    t9_1 = READ_S32(arg2, 0x2c);
    t8_1 = READ_S32(arg2, 0x28);
    t7_6 = READ_S32(arg2, 0x24);
    s2_1 = READ_S32(arg1, 0x5c);
    t4_2 = READ_S32(arg1, 0x18);
    WRITE_S32(arg1, 0x4c, t0_1);
    t3_3 = READ_S32(arg1, 0x14) + s1_4;
    t2_3 = READ_S32(arg1, 0x30) + s0_1;
    t1_3 = READ_S32(arg1, 0x2c) + t9_1;
    t0_3 = READ_S32(arg1, 0x28) + t8_1;
    a3_4 = READ_S32(arg1, 0x24) + t7_6;
    t4_3 = t4_2 + READ_S32(arg2, 0x18);
    WRITE_S32(arg1, 0x60, a2_3);
    WRITE_S32(arg1, 0x68, v1_5);
    WRITE_S32(arg1, 0x6c, ((uint32_t)v1_5 < (uint32_t)v0_4 ? 1 : 0) + t6_3);
    WRITE_S32(arg1, 0x5c, s2_1 + t5_2);
    WRITE_S32(arg1, 0x18, t4_3);
    WRITE_S32(arg1, 0x14, t3_3);
    t3_4 = READ_S32(arg2, 0x1c);
    a2_4 = READ_S32(arg1, 0x20);
    v1_6 = READ_S32(arg1, 0x10);
    v0_7 = READ_U8(arg1, 2);
    WRITE_S32(arg1, 0x30, t2_3);
    WRITE_S32(arg1, 0x2c, t1_3);
    t2_4 = READ_S32(arg2, 0x20);
    t1_4 = READ_S32(arg2, 0x10);
    WRITE_S32(arg1, 0x28, t0_3);
    WRITE_S32(arg1, 0x24, a3_4);
    result = v0_7 | READ_U8(arg2, 2);
    WRITE_S32(arg1, 0x1c, READ_S32(arg1, 0x1c) + t3_4);
    WRITE_S32(arg1, 0x20, a2_4 + t2_4);
    WRITE_S32(arg1, 0x10, v1_6 + t1_4);
    WRITE_U8(arg1, 2, result);
    return result;
}

int32_t MergeEntropyStatus(char *arg1, char *arg2)
{
    int32_t t0_1 = READ_S32(arg1, 0xc) + READ_S32(arg2, 0xc);
    int32_t a3_1 = READ_S32(arg1, 8) + READ_S32(arg2, 8);
    uint8_t a1 = READ_U8(arg1, 1) | READ_U8(arg2, 1);
    uint8_t v1_1 = READ_U8(arg1, 0) | READ_U8(arg2, 0);
    uint8_t result = READ_U8(arg1, 2) | READ_U8(arg2, 2);

    WRITE_S32(arg1, 4, READ_S32(arg1, 4) + READ_S32(arg2, 4));
    WRITE_S32(arg1, 0xc, t0_1);
    WRITE_S32(arg1, 8, a3_1);
    WRITE_U8(arg1, 1, a1);
    WRITE_U8(arg1, 0, v1_1);
    WRITE_U8(arg1, 2, result);
    return result;
}

int32_t JpegStatusToStatusRegs(void *arg1, void *arg2)
{
    uint32_t v0 = READ_U8(arg1, 1);
    int32_t v1 = READ_S32(arg2, 0x38);
    int32_t result;

    WRITE_U32(arg2, 0x30, READ_U32(arg1, 0x70));
    result = (v1 & 0xfffffffd) | (int32_t)(v0 << 1);
    WRITE_U32(arg2, 0x34, READ_U32(arg1, 8));
    WRITE_U32(arg2, 0x38, result);
    return result;
}

int32_t SliceParamToCmdRegsEnc1(char *arg1, int32_t *arg2, void *arg3, ...)
{
    int32_t v1_1;
    int32_t v1_4;
    int32_t v1_6;
    int32_t a3_2;
    int32_t t9_1;
    int32_t t1_5;
    int32_t a3_5;
    int32_t t1_7;
    int32_t a3_6;
    int32_t v0_25;
    int32_t v0_27;
    int32_t v0_29;
    int32_t v0_32;
    int32_t v0_34;
    int32_t v0_38;
    int32_t v0_40;
    uint32_t t0_24;
    int32_t v1_41;
    int32_t v0_55;
    int32_t v0_46;
    uint32_t s0_1;
    int32_t a3_21;
    uint32_t s5_1;
    uint32_t s4_1;
    int32_t s6_1;
    uint32_t s3_1;
    uint32_t s2_1;
    int32_t t0_31;
    int32_t t6_6;
    int32_t t6_8;
    int32_t a3_29;
    int32_t s2_2;
    int32_t v0_61;
    uint32_t a3_38;
    int32_t v1_54;
    uint32_t a3_40;
    int32_t v1_56;
    uint32_t t0_32;
    int32_t v1_58;
    int32_t s2_3;
    int32_t v0_76;
    int32_t v1_65;
    uint32_t s0_2;
    uint32_t s2_5;
    uint32_t s1_2;
    uint32_t s5_3;
    uint32_t s0_3;
    uint32_t s7_5;
    uint32_t s2_7;
    uint32_t s1_4;
    uint32_t s0_4;
    uint32_t s5_5;
    uint32_t s4_8;
    uint32_t s3_3;
    int32_t v0_86;
    uint32_t v1_71;
    int32_t t4_3;
    int32_t a3_49;
    int32_t v1_73;
    int32_t t4_11;
    int32_t a3_58;
    uint32_t t0_46;
    uint32_t t1_24;
    int32_t a3_61;
    uint32_t v0_96;
    int32_t t4_12;
    int32_t t1_30;
    int32_t v1_88;
    int32_t a3_73;
    int32_t v0_106;
    int32_t v1_92;
    int32_t a3_76;
    int32_t t9_3;
    int32_t v1_95;
    int32_t t7_11;
    int32_t t6_14;
    int32_t t0_23;
    int32_t t1_31;
    int32_t v0_128;
    int32_t a3_81;
    int32_t t1_33;
    uint32_t v1_103;
    uint32_t v1_104;
    uint32_t t0_63;
    int32_t a3_84;
    int32_t result;

    if (arg1 == 0 || arg2 == 0) {
        __assert("pSP && pCmdRegs",
                 "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/EncSliceParam.c",
                 0x15, "SliceParamToCmdRegsEnc1", &_gp);
    }

    if (READ_U16(arg1, 0x108) <= 1U || READ_U16(arg1, 0x10a) <= 1U) {
        uint32_t pic_w8 = READ_U16(arg1, 0x0a);
        uint32_t pic_h8 = READ_U16(arg1, 0x0c);
        uint32_t lcu_shift = READ_U8(arg1, 3) & 0x1fU;
        uint32_t lcu_span;

        if (lcu_shift == 0U || lcu_shift > 8U)
            lcu_shift = 4U;

        lcu_span = 1U << lcu_shift;
        if (READ_U16(arg1, 0x108) <= 1U && pic_w8 != 0U)
            WRITE_U16(arg1, 0x108, (uint16_t)(((pic_w8 << 3) + lcu_span - 1U) >> lcu_shift));
        if (READ_U16(arg1, 0x10a) <= 1U && pic_h8 != 0U)
            WRITE_U16(arg1, 0x10a, (uint16_t)(((pic_h8 << 3) + lcu_span - 1U) >> lcu_shift));
        if (READ_U16(arg1, 0x7c) <= 1U && pic_h8 != 0U)
            WRITE_U16(arg1, 0x7c, (uint16_t)pic_h8);
    }

    fprintf(stderr,
            "libimp/SLP: enc1-pack entry sp=%p cmd=%p meta=%p type=%u dim=%ux%u lcu=%ux%u 7a=%u 7c=%u a8=%u aa=%u ac=%u\n",
            arg1, arg2, arg3,
            (unsigned)READ_U8(arg1, 0x0f),
            (unsigned)READ_U16(arg1, 0x0a), (unsigned)READ_U16(arg1, 0x0c),
            (unsigned)READ_U16(arg1, 0x108), (unsigned)READ_U16(arg1, 0x10a),
            (unsigned)READ_U16(arg1, 0x7a), (unsigned)READ_U16(arg1, 0x7c),
            (unsigned)READ_U16(arg1, 0xa8), (unsigned)READ_U16(arg1, 0xaa),
            (unsigned)READ_U8(arg1, 0xac));
    slp_kmsg("libimp/SLP: entry sp=%p cmd=%p meta=%p type=%u dim=%ux%u lcu=%ux%u 7a=%u 7c=%u a8=%u aa=%u ac=%u\n",
             arg1, arg2, arg3,
             (unsigned)READ_U8(arg1, 0x0f),
             (unsigned)READ_U16(arg1, 0x0a), (unsigned)READ_U16(arg1, 0x0c),
             (unsigned)READ_U16(arg1, 0x108), (unsigned)READ_U16(arg1, 0x10a),
             (unsigned)READ_U16(arg1, 0x7a), (unsigned)READ_U16(arg1, 0x7c),
             (unsigned)READ_U16(arg1, 0xa8), (unsigned)READ_U16(arg1, 0xaa),
             (unsigned)READ_U8(arg1, 0xac));

    v1_1 = *arg2;
    *arg2 = (v1_1 & 0xffffff88) | 0x11;
    v1_4 = ((((uint32_t)READ_U8(arg1, 0) - 2U) & 3U) << 8) | (*arg2 & 0xfffffc88) | 0x11;
    *arg2 = v1_4;
    v1_6 = ((((uint32_t)READ_U8(arg1, 1) - 2U) & 3U) << 0xa) | (*arg2 & 0xfffff3ff);
    *arg2 = v1_6;
    a3_2 = ((((uint32_t)READ_U8(arg1, 2) - 4U) & 7U) << 0x14) | (*arg2 & 0xff8fffff);
    *arg2 = a3_2;
    t9_1 = READ_S32(arg1, 4);
    t1_5 = (((((uint32_t)READ_U16(arg1, 0xa) - 1U) & 0x7ffU) | ((uint32_t)arg2[1] & 0xfffff800U)) & 0xff800fffU) |
           ((((uint32_t)READ_U16(arg1, 0xc) - 1U) & 0x7ffU) << 0xc);
    a3_5 = (((((((((uint32_t)READ_U8(arg1, 3) - 4U) & 7U) << 0x18) | (*arg2 & 0xf8ffffff)) & 0xcfffffffU) |
              ((uint32_t)t9_1 & 3U) << 0x1c) &
             0x7fffffffU) |
            ((uint32_t)READ_U8(arg1, 8) << 0x1f));
    arg2[1] = t1_5;
    *arg2 = a3_5;
    t1_7 = (((uint32_t)READ_U8(arg1, 0x19) & 0xfU) << 0x18) | (arg2[1] & 0xf0ffffff);
    arg2[1] = t1_7;
    a3_6 = arg2[2];
    arg2[1] = ((uint32_t)READ_U8(arg1, 0x1a) << 0x1c) | (t1_7 & 0x0fffffff);
    v0_25 = (((a3_6 & 0xfffffff8U) | ((uint32_t)READ_U8(arg1, 0xe) & 7U)) & 0xfffffff7U) |
            ((READ_U32(arg1, 0x14) & 1U) << 3);
    arg2[2] = v0_25;
    v0_27 = (((uint32_t)READ_U8(arg1, 0xf) & 7U) << 4) | (v0_25 & 0xffffff8f);
    arg2[2] = v0_27;
    v0_29 = (((uint32_t)READ_U8(arg1, 0x10) & 3U) << 8) | (v0_27 & 0xfffffcff);
    arg2[2] = v0_29;
    v0_32 = (((((((((uint32_t)READ_U8(arg1, 0x11) & 1U) << 0xa) | (v0_29 & 0xfffffbff)) & 0xfffff7ffU) |
               ((uint32_t)READ_U8(arg1, 0x12) << 0xb)) &
              0xffff9fffU) |
             ((uint32_t)READ_U8(arg1, 0x10e) << 0xe) | 0x2000) &
            0xffff7fffU) |
           ((uint32_t)READ_U8(arg1, 0x18) << 0xf);
    arg2[2] = v0_32;
    v0_34 = (((uint32_t)READ_U8(arg1, 0x19) & 3U) << 0x10) | (v0_32 & 0xfffcffff);
    arg2[2] = v0_34;
    v0_38 = ((((uint32_t)READ_U8(arg1, 0x1a) & 3U) << 0x12) | (v0_34 & 0xfff3ffff));
    v0_38 = (v0_38 & 0xffcfffff) | ((READ_U32(arg1, 0x1c) & 3U) << 0x14);
    arg2[2] = v0_38;
    if (t9_1 == 3) {
        v0_40 = ((uint32_t)READ_U8(arg1, 0x2a) << 0x17) | (v0_38 & 0xff7fffff);
    } else {
        v0_40 = ((uint32_t)READ_U8(arg1, 0x20) << 0x17) | (v0_38 & 0xff7fffff);
    }
    arg2[2] = ((((((((uint32_t)READ_U8(arg1, 0x21) << 0x1a) | (v0_40 & 0xfbffffff)) & 0xf7ffffffU) |
                ((uint32_t)READ_U8(arg1, 0x22) << 0x1b)) &
               0xbfffffffU) |
              ((uint32_t)READ_U8(arg1, 0x23) << 0x1e)) &
             0x7fffffffU) |
            ((uint32_t)READ_U8(arg1, 0x24) << 0x1f);
    v0_46 = (arg2[3] & 0xffffffe0) | ((uint32_t)READ_U8(arg1, 0x26) & 0x1fU);
    arg2[3] = v0_46;
    t0_23 = ((((((((((((((uint32_t)READ_U8(arg1, 0x27) & 0x1fU) << 8) | (v0_46 & 0xffffe0ff)) & 0xffff7fffU) |
                    ((uint32_t)READ_U8(arg1, 0x25) << 0xf)) &
                   0xff00ffffU) |
                  ((uint32_t)READ_U8(arg1, 0x28) << 0x10)) &
                 0xfeffffffU) |
                ((uint32_t)READ_U8(arg1, 0x2b) << 0x18)) &
               0xfbffffffU) |
              ((uint32_t)READ_U8(arg1, 0x2c) << 0x1a)) &
             0xf7ffffffU) |
            ((uint32_t)READ_U8(arg1, 0x2d) << 0x1b)) &
           0xcfffffffU;
    arg2[3] = (((((t0_23 | ((READ_U32(arg1, 0x30) & 3U) << 0x1c)) & 0xbfffffffU) |
                 ((uint32_t)READ_U8(arg1, 0x34) << 0x1e)) &
                0x7fffffffU) |
               ((uint32_t)READ_U8(arg1, 0x35) << 0x1f));
    t0_24 = READ_U8(arg1, 0x38);
    v1_41 = (((uint32_t)READ_U8(arg1, 0x37) & 0x3fU) << 8) | (arg2[4] & 0xffffc0ff);
    arg2[4] = v1_41;
    v0_55 = (((uint32_t)READ_U8(arg1, 0x36) & 0x1fU) | (v1_41 & 0xffffffe0));
    v0_55 = (v0_55 & 0xfffbffffU) | ((int32_t)t0_24 << 0x12);
    arg2[4] = v0_55;
    s0_1 = READ_U8(arg1, 0x108);
    arg2[4] = ((((uint32_t)READ_U8(arg1, 0x3a) & 3U) << 0x14) | (arg2[4] & 0xffcfffff));
    arg2[4] = (arg2[4] & 0xfff7ffffU) | ((uint32_t)READ_U8(arg1, 0x39) << 0x13);
    slp_kmsg("libimp/SLP: pre-lcu cmd=%p w0=%08x w1=%08x w2=%08x w3=%08x w4=%08x lcu=%u/%u start=%d end=%d\n",
             arg2,
             (unsigned)arg2[0], (unsigned)arg2[1], (unsigned)arg2[2],
             (unsigned)arg2[3], (unsigned)arg2[4],
             (unsigned)READ_U16(arg1, 0x108), (unsigned)READ_U16(arg1, 0x10a),
             READ_S32(arg1, 0x3c), READ_S32(arg1, 0x44));
    if (s0_1 != 0) {
        a3_21 = READ_S32(arg1, 0x3c);
        if (s0_1 == 0) {
            __builtin_trap();
        }
        s5_1 = (uint32_t)READ_U8(arg1, 0x8f) << 0xa;
        s4_1 = (uint32_t)READ_U8(arg1, 0x97) << 0xb;
        s6_1 = arg2[6] & 0xfffffc00;
        s3_1 = (uint32_t)READ_U8(arg1, 0x48) << 0x16;
        s2_1 = (uint32_t)READ_U8(arg1, 0x49) << 0x17;
        t0_31 = (((((uint32_t)READ_U16(arg1, 0x4a) - 1U) & 0x3ffU) | ((uint32_t)arg2[7] & 0xfffffc00U)) & 0xffc00fffU) |
                ((((uint32_t)READ_U16(arg1, 0x4c) - 1U) & 0x3ffU) << 0xc);
        t6_6 = (((((((arg2[5] & 0xfffffc00U) | ((uint32_t)a3_21 % s0_1 & 0x3ffU)) & 0xffc00fffU) |
                    ((((uint32_t)a3_21 / s0_1) & 0x3ffU) << 0xc)) &
                   0xffbfffffU) |
                  ((uint32_t)READ_U8(arg1, 0x8e) << 0x16)) &
                 0xff7fffffU) |
                ((uint32_t)READ_U8(arg1, 0x96) << 0x17);
        arg2[5] = t6_6;
        t6_8 = (((uint32_t)READ_U8(arg1, 0x41) & 0xfU) << 0x18) | (t6_6 & 0xf0ffffff);
        arg2[5] = t6_8;
        arg2[5] = ((uint32_t)READ_U8(arg1, 0x40) << 0x1c) | (t6_8 & 0x0fffffff);
        a3_29 = READ_S32(arg1, 0x44);
        if (s0_1 == 0) {
            __builtin_trap();
        }
        s2_2 = ((((((((((s6_1 | ((uint32_t)a3_29 % s0_1 & 0x3ffU)) & 0xfffffbffU) | s5_1) & 0xfffff7ffU) | s4_1) &
                     0xffc00fffU) |
                    ((((uint32_t)a3_29 / s0_1) & 0x3ffU) << 0xc)) &
                   0xffbfffffU) |
                  s3_1) &
                 0xff7fffffU) |
                s2_1;
        arg2[6] = s2_2;
        v0_61 = (((uint32_t)READ_U8(arg1, 0x8c) & 0xfU) << 0x18) | (s2_2 & 0xf0ffffff);
        arg2[6] = v0_61;
        a3_38 = READ_U8(arg1, 0x94);
        arg2[7] = t0_31;
        arg2[6] = (a3_38 << 0x1c) | (v0_61 & 0x0fffffff);
        v1_54 = ((uint32_t)READ_U8(arg1, 0x4e) & 1U) << 0x18 | (t0_31 & 0xfeffffff);
        arg2[7] = v1_54;
        a3_40 = READ_U8(arg1, 0x53);
        v1_56 = ((uint32_t)READ_U8(arg1, 0x4f) & 1U) << 0x19 | (v1_54 & 0xfdffffff);
        arg2[7] = v1_56;
        t0_32 = READ_U16(arg1, 0x58);
        v1_58 = ((uint32_t)READ_U8(arg1, 0x50) & 1U) << 0x1a | (v1_56 & 0xfbffffff);
        arg2[7] = v1_58;
        s2_3 = arg2[9];
        v0_76 = ((uint32_t)READ_U8(arg1, 0x51) & 1U) << 0x1b | (v1_58 & 0xf7ffffff);
        arg2[7] = v0_76;
        arg2[7] = ((((uint32_t)READ_U8(arg1, 0x52) & 1U) << 0x1c) | (v0_76 & 0xefffffff));
        arg2[7] = (arg2[7] & 0x7fffffffU) | (a3_40 << 0x1f);
        v1_65 = READ_S32(arg1, 0x54);
        if (s0_1 == 0) {
            __builtin_trap();
        }
        s0_2 = (uint32_t)READ_U8(arg1, 0x64) << 0x11;
        s2_5 = (uint32_t)READ_U8(arg1, 0x66) << 0x13;
        s1_2 = (uint32_t)READ_U8(arg1, 0x69) << 0x16;
        s5_3 = (uint32_t)READ_U8(arg1, 0x6a) << 0x17;
        s0_3 = (uint32_t)READ_U8(arg1, 0x6b) << 0x18;
        s7_5 = READ_U8(arg1, 0x63);
        s2_7 = READ_U8(arg1, 0x6f);
        s1_4 = READ_U8(arg1, 0x70);
        s0_4 = READ_U8(arg1, 0x71);
        s5_5 = (uint32_t)READ_U8(arg1, 0x6c) << 0x19;
        s4_8 = (uint32_t)READ_U8(arg1, 0x6d) << 0x1a;
        s3_3 = (uint32_t)READ_U8(arg1, 0x6e) << 0x1b;
        v0_86 = (((((arg2[8] & 0xfffffc00U) | ((uint32_t)v1_65 % s0_1 & 0x3ffU)) & 0xffc00fffU) |
                   ((((uint32_t)v1_65 / s0_1) & 0x3ffU) << 0xc)) &
                  0xf8ffffffU) |
                 (((((t0_32 >> 3) - 1U) & 7U) << 0x18));
        arg2[8] = (((v0_86 & 0xf7ffffffU) | ((uint32_t)READ_U8(arg1, 0x5c) << 0x1b)) & 0x8fffffffU) |
                  (((((uint32_t)READ_U16(arg1, 0x5a) >> 3) - 1U) & 7U) << 0x1c);
        v1_71 = READ_U8(arg1, 0x72);
        t4_3 = (s2_3 & 0xffffffe0) | ((uint32_t)READ_U8(arg1, 0x5d) & 0x1fU);
        arg2[9] = t4_3;
        a3_49 = (((((((uint32_t)READ_U8(arg1, 0x5e) & 0x1fU) << 5) | (t4_3 & 0xfffffc1f)) & 0xfffeffffU) |
                  (s7_5 << 0x10)) &
                 0xfffdffffU) |
                s0_2;
        a3_49 = (a3_49 & 0xfff7ffffU) | s2_5;
        arg2[9] = a3_49;
        v1_73 = (((((((((((((((((((((((uint32_t)READ_U8(arg1, 0x67) & 1U) << 0x14) | (a3_49 & 0xffefffff)) &
                                       0xffbfffffU) |
                                      s1_2) &
                                     0xff7fffffU) |
                                    s5_3) &
                                   0xfeffffffU) |
                                  s0_3) &
                                 0xfdffffffU) |
                                s5_5) &
                               0xfbffffffU) |
                              s4_8) &
                             0xf7ffffffU) |
                            s3_3) &
                           0xefffffffU) |
                          (s2_7 << 0x1c)) &
                         0xdfffffffU) |
                        (s1_4 << 0x1d)) &
                       0xbfffffffU) |
                      (s0_4 << 0x1e)) &
                     0x7fffffffU) |
                    (v1_71 << 0x1f);
        arg2[9] = v1_73;
        if (t9_1 == 0) {
            arg2[9] = ((((uint32_t)READ_U8(arg1, 0x60) << 0xf) | (v1_73 & 0xffff7fff)) & 0xfffbffffU) |
                      ((uint32_t)READ_U8(arg1, 0x61) << 0x12);
            arg2[9] = (arg2[9] & 0xfffff7ffU) | ((uint32_t)READ_U8(arg1, 0x62) << 0xb);
        } else if (t9_1 == 1) {
            arg2[9] = (((uint32_t)READ_U8(arg1, 0x5f) << 0xf) | (v1_73 & 0xffff7fff)) & 0xfffbffffU;
            arg2[9] |= (uint32_t)READ_U8(arg1, 0x65) << 0x12;
        }
        t4_11 = (((arg2[0xb] & 0xfffffc00U) | ((uint32_t)READ_U16(arg1, 0x7a) & 0x3ffU)) & 0xffc00fffU) |
                ((((uint32_t)READ_U16(arg1, 0x7c) - 1U) & 0x3ffU) << 0xc);
        a3_58 = READ_U16(arg1, 0x74) | (arg2[0xa] & 0xffff0000);
        arg2[0xb] = t4_11;
        arg2[0xa] = a3_58;
        arg2[0xb] = (((((((uint32_t)READ_U8(arg1, 0x7e) - 1U) & 3U) << 0x18) | (t4_11 & 0xfcffffff)) &
                     0xbfffffffU) |
                    ((uint32_t)READ_U8(arg1, 0x7f) << 0x1e));
        arg2[0xb] = (arg2[0xb] & 0x7fffffffU) | ((uint32_t)READ_U8(arg1, 0x80) << 0x1f);
        t0_46 = READ_U16(arg1, 0xa8);
        t1_24 = READ_U16(arg1, 0xaa);
        arg2[0xc] = READ_S32(arg1, 0x84);
        a3_61 = arg2[0x12];
        arg2[0xd] = READ_S32(arg1, 0x88);
        arg2[0xe] = READ_S32(arg1, 0x90);
        arg2[0xf] = READ_S32(arg1, 0x98);
        v0_96 = READ_U8(arg1, 0xac);
        arg2[0x10] = READ_S32(arg1, 0xa0);
        arg2[0x11] = READ_S32(arg1, 0xa4);
        arg2[0x12] = ((((((t0_46 >> 6) - 1U) & 0x3ffU) | ((uint32_t)a3_61 & 0xfffffc00U)) & 0xffc00fffU) |
                      (((((t1_24 >> 3) - 1U) & 0x3ffU) << 0xc))) &
                     0xbfffffffU;
        arg2[0x12] |= v0_96 << 0x1e;
        slp_kmsg("libimp/SLP: post-refpack cmd=%p w0c=%08x w0d=%08x w0e=%08x w0f=%08x w10=%08x w11=%08x w12=%08x s7_5=%u\n",
                 arg2,
                 (unsigned)arg2[0xc], (unsigned)arg2[0xd], (unsigned)arg2[0xe],
                 (unsigned)arg2[0xf], (unsigned)arg2[0x10], (unsigned)arg2[0x11],
                 (unsigned)arg2[0x12], (unsigned)s7_5);
        if (s7_5 != 0) {
            t4_12 = ((((((uint32_t)READ_U8(arg1, 0xc6) - 1U) & 0x3fU) | ((uint32_t)arg2[0x14] & 0xffffffc0U)) &
                       0xffff003fU) |
                      ((((uint32_t)READ_U16(arg1, 0xc4) - 1U) & 0x3ffU) << 6)) &
                     0xfbffffffU;
            t4_12 |= (uint32_t)READ_U8(arg1, 0xb5) << 0x1a;
            arg2[0x14] = (((((((((t4_12 & 0xf7ffffffU) | ((uint32_t)READ_U8(arg1, 0xb4) << 0x1b)) & 0xefffffffU) |
                               ((uint32_t)READ_U8(arg1, 0xb3) << 0x1c)) &
                              0xdfffffffU) |
                             ((uint32_t)READ_U8(arg1, 0xb2) << 0x1d)) &
                            0xbfffffffU) |
                           ((uint32_t)READ_U8(arg1, 0xb1) << 0x1e)) &
                          0x7fffffffU) |
                         ((uint32_t)READ_U8(arg1, 0xb0) << 0x1f);
            t1_30 = (arg2[0x15] & 0xff000000) | (READ_U32(arg1, 0xc0) & 0xffffffU);
            arg2[0x15] = t1_30;
            v1_88 = (((uint32_t)READ_U8(arg1, 0xe6) & 3U) << 0x1c) | (t1_30 & 0xcfffffff);
            arg2[0x15] = v1_88;
            a3_73 = arg2[0x16];
            v0_106 = (((uint32_t)READ_U8(arg1, 0xe4) & 1U) << 0x1e) | (v1_88 & 0xbfffffff);
            arg2[0x15] = v0_106;
            arg2[0x15] = ((uint32_t)READ_U8(arg1, 0xe5) << 0x1f) | (v0_106 & 0x7fffffff);
            v1_92 = arg2[0x17];
            a3_76 = (a3_73 & 0xff000000) | (READ_U32(arg1, 0xbc) & 0xffffffU);
            arg2[0x16] = a3_76;
            arg2[0x16] = (((READ_U32(arg1, 0xb8) >> 0xf) - 1U) << 0x18) | (a3_76 & 0x00ffffff);
            t9_3 = (v1_92 & 0xffffffc0) | ((uint32_t)READ_U8(arg1, 0xc8) & 0x3fU);
            arg2[0x17] = t9_3;
            v1_95 = (((uint32_t)READ_U8(arg1, 0xc9) & 0x3fU) << 8) | (t9_3 & 0xffffc0ff);
            arg2[0x17] = v1_95;
            t7_11 = (((uint32_t)READ_U8(arg1, 0xcb) & 0x3fU) << 0x10) | (v1_95 & 0xffc0ffff);
            arg2[0x17] = t7_11;
            arg2[0x17] = (((uint32_t)READ_U8(arg1, 0xca) & 0x3fU) << 0x18) | (t7_11 & 0xc0ffffff);
            t6_14 = (((((arg2[0x18] & 0x7fffffffU) | ((uint32_t)READ_U8(arg1, 0xcc) << 0x1f)) & 0xbfffffffU) |
                       ((uint32_t)READ_U8(arg1, 0xcd) << 0x1e)) &
                      0xf00fffffU) |
                     ((uint32_t)READ_U8(arg1, 0xd4) << 0x14);
            arg2[0x18] = t6_14;
            arg2[0x18] = ((READ_U32(arg1, 0xd0) >> 3) & 0xffffU) | (t6_14 & 0xffff0000);
            t1_31 = (arg2[0x19] & 0xffff0000) | READ_U16(arg1, 0xec);
            v0_128 = READ_S32(arg1, 0xf0);
            a3_81 = (arg2[0x1a] & 0xfffffc00U) | ((uint32_t)READ_U16(arg1, 0xf4) & 0x3ffU);
            arg2[0x19] = t1_31;
            t1_33 = ((uint32_t)READ_U8(arg1, 0xee) << 0x10) | (t1_31 & 0xff00ffff);
            v1_103 = READ_U16(arg1, 0x114) | ((uint32_t)READ_U16(arg1, 0x116) << 0x10);
            arg2[0x60] = READ_U16(arg1, 0x110) | ((uint32_t)READ_U16(arg1, 0x112) << 0x10);
            arg2[0x61] = (int32_t)v1_103;
            arg2[0x19] = t1_33;
            arg2[0x1a] = (a3_81 & 0xcfffffffU) | ((v0_128 & 3U) << 0x1c);
            v1_104 = READ_U8(arg1, 0x118);
            t0_63 = READ_U8(arg1, 0x11a);
            arg2[0x64] = READ_S32(arg3, 0x14);
            a3_84 = arg2[0x6e];
            arg2[0x65] = READ_S32(arg3, 0x18);
            arg2[0x67] = READ_S32(arg3, 0x54);
            arg2[0x68] = READ_S32(arg3, 0x34);
            arg2[0x69] = READ_S32(arg3, 0x44);
            arg2[0x6e] = (((a3_84 & 0xefffffffU) | (v1_104 << 0x1c)) & 0xffffff00U) | t0_63;
            result = READ_S32(arg3, 0x94);
            fprintf(stderr,
                    "libimp/SLP: enc1-pack exit-full cmd=%p w0=%08x w1=%08x w2=%08x w3=%08x w18=%08x w19=%08x w1a=%08x w6f=%08x\n",
                    arg2,
                    (unsigned)arg2[0], (unsigned)arg2[1], (unsigned)arg2[2], (unsigned)arg2[3],
                    (unsigned)arg2[0x18], (unsigned)arg2[0x19], (unsigned)arg2[0x1a], (unsigned)arg2[0x6f]);
            slp_kmsg("libimp/SLP: exit-full cmd=%p w0=%08x w1=%08x w2=%08x w3=%08x w18=%08x w19=%08x w1a=%08x w6f=%08x\n",
                     arg2,
                     (unsigned)arg2[0], (unsigned)arg2[1], (unsigned)arg2[2], (unsigned)arg2[3],
                     (unsigned)arg2[0x18], (unsigned)arg2[0x19], (unsigned)arg2[0x1a], (unsigned)arg2[0x6f]);
            arg2[0x6f] = result;
            return result;
        }
    }

    fprintf(stderr,
            "libimp/SLP: enc1-pack exit-basic cmd=%p w0=%08x w1=%08x w2=%08x w3=%08x w18=%08x w19=%08x w1a=%08x\n",
            arg2,
            (unsigned)arg2[0], (unsigned)arg2[1], (unsigned)arg2[2], (unsigned)arg2[3],
            (unsigned)arg2[0x18], (unsigned)arg2[0x19], (unsigned)arg2[0x1a]);
    slp_kmsg("libimp/SLP: exit-basic cmd=%p w0=%08x w1=%08x w2=%08x w3=%08x w18=%08x w19=%08x w1a=%08x\n",
             arg2,
             (unsigned)arg2[0], (unsigned)arg2[1], (unsigned)arg2[2], (unsigned)arg2[3],
             (unsigned)arg2[0x18], (unsigned)arg2[0x19], (unsigned)arg2[0x1a]);
    if (READ_U16(arg1, 0x108) == 0) {
        __assert("pSP->LcuWidth != 0",
                 "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/EncSliceParam.c",
                 0x57, "SliceParamToCmdRegsEnc1", &_gp);
        return SliceParamToCmdRegsEnc2(arg1, arg2);
    }
    return 0;
}

int32_t SliceParamToCmdRegsEnc2(void *arg1, void *arg2)
{
    int32_t a2_3;
    int32_t a2_5;
    int32_t t2;
    int32_t a2_10;
    int32_t a2_12;
    int32_t a2_14;
    int32_t a3_6;
    uint32_t a2_20;
    int32_t v0_5;
    int32_t v0_7;
    int32_t v0_12;
    int32_t result;

    if (arg1 == 0 || arg2 == 0) {
        __assert("pSP && pCmdRegs",
                 "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/EncSliceParam.c",
                 0xf3, "SliceParamToCmdRegsEnc2", &_gp);
        return CmdRegsEnc2ToSliceParam(arg1, arg2);
    }

    a2_3 = (((READ_S32(arg2, 0x6c) & 0xffffe000U) | (READ_S32(arg1, 0x74) & 0x1fffU)) & 0xfc00ffffU) |
           (((uint32_t)READ_U16(arg1, 0x78) & 0x3ffU) << 0x10);
    WRITE_S32(arg2, 0x6c, a2_3);
    a2_5 = (((uint32_t)READ_U8(arg1, 0x19) & 3U) << 0x1c) | (a2_3 & 0xcfffffff);
    WRITE_S32(arg2, 0x6c, a2_5);
    t2 = READ_S32(arg2, 0x70);
    WRITE_S32(arg2, 0x6c, ((uint32_t)READ_U8(arg1, 0x1a) << 0x1e) | (a2_5 & 0x3fffffff));
    a2_10 = (((((((uint32_t)READ_U8(arg1, 1) < 3U ? 1U : 0U) ^ 1U) << 1) | (t2 & 0xfffffffd)) & 0xfffffffbU) |
             ((uint32_t)READ_U8(arg1, 0xf6) << 2)) &
            0xfffffff7U;
    a2_10 |= (uint32_t)READ_U8(arg1, 0x66) << 3;
    WRITE_S32(arg2, 0x70, a2_10);
    a2_12 = ((((uint32_t)READ_U8(arg1, 0x7e) - 1U) & 3U) << 4) | (a2_10 & 0xffffffcf);
    WRITE_S32(arg2, 0x70, a2_12);
    a2_14 = (((uint32_t)READ_U8(arg1, 0x10) & 3U) << 8) | (a2_12 & 0xfffffcff);
    WRITE_S32(arg2, 0x70, a2_14);
    a3_6 = ((((((uint32_t)READ_U8(arg1, 0x11) & 1U) << 0xa) | (a2_14 & 0xfffffbff)) & 0xfffff7ffU) |
             ((uint32_t)READ_U8(arg1, 0x12) << 0xb)) &
            0xffc0ffffU;
    a3_6 |= ((uint32_t)READ_U8(arg1, 0x28) & 0x3fU) << 0x10;
    WRITE_S32(arg2, 0x70, (((a3_6 & 0xfcffffffU) | ((READ_U32(arg1, 0x1c) & 3U) << 0x18)) & 0xcfffffffU) |
                          ((READ_U32(arg1, 0x30) & 3U) << 0x1c));
    a2_20 = READ_U8(arg1, 0x108);
    if (a2_20 == 0) {
        __builtin_trap();
    }
    v0_5 = (((((uint32_t)READ_S32(arg1, 0x3c) / a2_20) & 0x3ffU) | ((uint32_t)READ_S32(arg2, 0x74) & 0xfffffc00U)) &
             0xffc00fffU) |
            ((((((uint32_t)READ_U16(arg1, 0xa) + 1U) >> 1) - 1U) & 0x3ffU) << 0xc);
    WRITE_S32(arg2, 0x74, v0_5);
    v0_7 = (((uint32_t)READ_U8(arg1, 0x41) & 0xfU) << 0x18) | (v0_5 & 0xf0ffffff);
    WRITE_S32(arg2, 0x74, v0_7);
    WRITE_S32(arg2, 0x74, ((uint32_t)READ_U8(arg1, 0x40) << 0x1c) | (v0_7 & 0x0fffffff));
    v0_12 = ((READ_S32(arg1, 0xfc) - 1) & 0x000fffff) | (READ_S32(arg2, 0x78) & 0xfff00000U);
    WRITE_S32(arg2, 0x78, v0_12);
    WRITE_S32(arg2, 0x78, (((uint32_t)READ_U8(arg1, 0xf8) & 0x1fU) << 0x18) | (v0_12 & 0xe0ffffff));
    result = READ_S32(arg1, 0x100);
    WRITE_S32(arg2, 0x7c, result);
    return result;
}

int32_t CmdRegsEnc2ToSliceParam(void *arg1, void *arg2)
{
    int32_t v1;
    int32_t v0_3;
    int32_t v1_6;
    int32_t v0_5;
    int32_t v1_7;
    int32_t v0_7;
    int32_t t0_1;
    uintptr_t v1_14;
    uintptr_t result;

    WRITE_U16(arg2, 0x74, READ_U32(arg1, 0x6c) & 0x1fffU);
    v1 = READ_S32(arg1, 0x6c);
    WRITE_U8(arg2, 0x19, (uint8_t)(((uint32_t)v1 >> 0x1c) & 3U) + 8);
    v0_3 = READ_S32(arg1, 0x70);
    WRITE_U16(arg2, 0x78, (uint16_t)(((uint32_t)v1 >> 0x10) & 0x3ffU));
    WRITE_U8(arg2, 0x7e, (uint8_t)(((uint32_t)v0_3 >> 4) & 3U) + 1);
    WRITE_U8(arg2, 0x10, (uint8_t)((READ_U32(arg1, 0x70) >> 8) & 3U));
    v1_6 = READ_S32(arg1, 0x70);
    WRITE_U8(arg2, 0xf7, (uint8_t)((v0_3 >> 1) & 1));
    WRITE_U8(arg2, 0x11, (uint8_t)((v1_6 >> 0xa) & 1));
    v0_5 = READ_S32(arg1, 0x74);
    v1_7 = READ_S32(arg1, 0x70);
    WRITE_U8(arg2, 0xf6, (uint8_t)((v0_3 >> 2) & 1));
    v0_7 = (v0_5 & 0x3ff) * ((((uint32_t)v0_5 >> 0xc) & 0x3ffU) + 1);
    t0_1 = (v1_7 >> 0xb) & 1;
    WRITE_U32(arg2, 0x30, ((uint32_t)v1_7 >> 0x1c) & 3U);
    WRITE_U8(arg2, 0x66, (uint8_t)((v0_3 >> 3) & 1));
    WRITE_U8(arg2, 0x12, (uint8_t)t0_1);
    WRITE_U32(arg2, 0x28, ((uint32_t)v1_7 >> 0x10) & 0x3fU);
    WRITE_U32(arg2, 0x1c, ((uint32_t)v1_7 >> 0x18) & 3U);
    WRITE_U16(arg2, 0x108, (uint16_t)((((uint32_t)v0_5 >> 0xc) & 0x3ffU) + 1));
    WRITE_S32(arg2, 0x3c, v0_7);
    WRITE_U8(arg2, 0x41, READ_U8(arg1, 0x77) & 0xfU);
    WRITE_U8(arg2, 0x40, (uint8_t)(READ_U32(arg1, 0x74) >> 0x1c));
    v1_14 = READ_U32(arg1, 0x78) & 0x000fffffU;
    WRITE_U32(arg2, 0xfc, (uint32_t)(v1_14 + 1));
    WRITE_U8(arg2, 0xf8, READ_U8(arg1, 0x7b) & 0x1fU);
    WRITE_S32(arg2, 0x100, READ_S32(arg1, 0x7c));
    if (t0_1 == 0) {
        WRITE_U8(arg2, 0xee, 0);
        WRITE_U16(arg2, 0xec, 0);
    }
    result = v1_14 + (uintptr_t)v0_7;
    WRITE_U32(arg2, 0x44, (uint32_t)result);
    return (int32_t)result;
}

int32_t *GetPicDimFromCmdRegsEnc1(int32_t *arg1, void *arg2)
{
    int32_t v1_2 = READ_S32(arg2, 4);

    arg1[0] = (v1_2 & 0x7ff) + 1;
    arg1[1] = (((uint32_t)v1_2 >> 0xc) & 0x7ffU) + 1;
    return arg1;
}

int32_t CmdRegsEnc1ToSliceParam(int32_t *arg1, char *arg2, int32_t arg3)
{
    int32_t var_40[2];
    int16_t t4;
    int32_t t5;
    uint32_t t6;
    int32_t t1;
    int32_t v1_12;
    int32_t a2;
    int32_t a3;
    int32_t t0_2;
    int16_t v0_15;
    int16_t v1_15;
    int32_t a1_3;
    int32_t a0_13;
    uint32_t v0_16;
    int32_t t4_1;
    int32_t t5_2;
    int32_t a2_4;
    int32_t a1_11;
    int32_t a0_14;
    int32_t t1_3;
    int32_t t3_3;
    int32_t a0_16;
    int32_t a0_18;
    int32_t a1_19;
    int32_t a0_24;
    int32_t t6_4;
    int32_t t4_6;
    int32_t a3_1;
    int32_t a1_26;
    int32_t a0_36;
    int32_t t7_2;
    int32_t t5_7;
    int32_t t5_9;
    int32_t a1_30;
    int32_t a1_31;
    int32_t a1_32;
    uint32_t v1_16;
    int32_t a1_34;
    int32_t t8_6;
    uint8_t t5_25;
    uint8_t a1_38;
    int32_t a1_40;
    int32_t a1_42;
    int32_t v1_18;
    int32_t v0_18;
    int32_t a1_43;
    int32_t a0_40;
    int16_t result;

    (void)arg3;

    WRITE_U8(arg2, 0, (uint8_t)((((uint32_t)*arg1 >> 8) & 3U) + 2));
    t0_2 = ((((uint32_t)*arg1 >> 0xa) & 3U) + 2);
    WRITE_U8(arg2, 1, (uint8_t)t0_2);
    WRITE_U8(arg2, 2, (uint8_t)(((int32_t)((uint32_t)*arg1 << 9) >> 0x1d) + 4));
    WRITE_U8(arg2, 3, (uint8_t)(((int32_t)((uint32_t)*arg1 << 5) >> 0x1d) + 4));
    a3 = ((uint32_t)*arg1 >> 0x1c) & 3U;
    WRITE_U32(arg2, 4, a3);
    WRITE_U8(arg2, 8, (uint8_t)((uint32_t)*arg1 >> 0x1f));
    if (a3 == 0) {
        WRITE_U8(arg2, 0xf7, (uint8_t)(((uint32_t)(t0_2 ^ 3) < 1U) ? 1 : 0));
    }

    GetPicDimFromCmdRegsEnc1(var_40, arg1);
    t4 = (int16_t)var_40[0];
    WRITE_U8(arg2, 0xe, READ_U8(&arg1[2], 0) & 7U);
    t5 = arg1[2];
    t6 = READ_U8(arg2, 3);
    t1 = READ_S32(arg2, 0x14);
    WRITE_U8(arg2, 0xf, (uint8_t)(((uint32_t)t5 >> 4) & 7U));
    WRITE_U8(arg2, 0x10, (uint8_t)(((uint32_t)arg1[2] >> 8) & 3U));
    v1_12 = 1 << (t6 & 0x1f);
    WRITE_U8(arg2, 0x11, (uint8_t)(((uint32_t)arg1[2] >> 0xa) & 1U));
    a2 = arg1[2];
    WRITE_U8(arg2, 0x19, (uint8_t)((((uint32_t)a2 >> 0x10) & 3U) + 8));
    a3 = READ_S32(arg2, 4);
    t0_2 = (int32_t)((((uint32_t)arg1[2] >> 0x12) & 3U) + 8);
    WRITE_U8(arg2, 0x1a, (uint8_t)t0_2);
    a1_3 = arg1[2];
    v0_15 = (int16_t)((((uint32_t)(uint16_t)t4 << 3) + v1_12 - 1) >> (t6 & 0x1f));
    v1_15 = (int16_t)((((uint32_t)(uint16_t)var_40[1] << 3) + v1_12 - 1) >> (t6 & 0x1f));
    WRITE_U32(arg2, 0x14, (t1 & 0xfffffffe) | (((uint32_t)t5 >> 3) & 1U));
    WRITE_U16(arg2, 0xa, (uint16_t)t4);
    WRITE_U16(arg2, 0xc, (uint16_t)var_40[1]);
    WRITE_U16(arg2, 0x108, (uint16_t)v0_15);
    WRITE_U16(arg2, 0x10a, (uint16_t)v1_15);
    WRITE_U8(arg2, 0x12, (uint8_t)(((uint32_t)a2 >> 0xb) & 1U));
    WRITE_U8(arg2, 0x10e, (uint8_t)(((uint32_t)a2 >> 0xe) & 1U));
    WRITE_U8(arg2, 0x18, (uint8_t)(((uint32_t)a2 >> 0xf) & 1U));
    WRITE_U32(arg2, 0x1c, ((uint32_t)a1_3 >> 0x14) & 3U);
    if (a3 == 3) {
        WRITE_U8(arg2, 0x2a, (uint8_t)(((uint32_t)a1_3 >> 0x17) & 1U));
    } else {
        WRITE_U8(arg2, 0x20, (uint8_t)(((uint32_t)a1_3 >> 0x17) & 1U));
    }
    WRITE_U8(arg2, 0x26, (uint8_t)(((int32_t)((uint32_t)arg1[3] << 0x1b)) >> 0x1b));
    WRITE_U8(arg2, 0x27, (uint8_t)(((int32_t)((uint32_t)arg1[3] << 0x13)) >> 0x1b));
    a0_13 = arg1[3];
    v0_16 = (uint16_t)v0_15;
    WRITE_U8(arg2, 0x37, (uint8_t)(((int32_t)((uint32_t)arg1[4] << 0x12)) >> 0x1a));
    WRITE_U8(arg2, 0x36, (uint8_t)(((int32_t)((uint32_t)arg1[4] << 0x1b)) >> 0x1b));
    t4_1 = arg1[5];
    t5_2 = arg1[4];
    a2_4 = ((((uint32_t)t4_1 >> 0xc) & 0x3ffU) * v0_16) + (t4_1 & 0x3ff);
    WRITE_U8(arg2, 0x41, (uint8_t)(((uint32_t)t4_1 >> 0x18) & 0xfU));
    a1_11 = arg1[5];
    WRITE_U32(arg2, 0x3c, a2_4);
    WRITE_U8(arg2, 0x23, (uint8_t)(((uint32_t)a1_3 >> 0x1e) & 1U));
    WRITE_U8(arg2, 0x40, (uint8_t)((uint32_t)a1_11 >> 0x1c));
    a0_14 = arg1[6];
    WRITE_U8(arg2, 0x24, (uint8_t)((uint32_t)a1_3 >> 0x1f));
    WRITE_U32(arg2, 0x28, ((uint32_t)a0_13 >> 0x10) & 0xffU);
    t1_3 = ((uint32_t)a0_14 >> 0xc) & 0x3ffU;
    WRITE_U8(arg2, 0x22, (uint8_t)(((uint32_t)a1_3 >> 0x1b) & 1U));
    WRITE_U8(arg2, 0x35, (uint8_t)((uint32_t)a0_13 >> 0x1f));
    WRITE_U8(arg2, 0x39, (uint8_t)(((uint32_t)t5_2 >> 0x13) & 1U));
    WRITE_U8(arg2, 0x8e, (uint8_t)(((uint32_t)t4_1 >> 0x16) & 1U));
    WRITE_U8(arg2, 0x96, (uint8_t)(((uint32_t)t4_1 >> 0x17) & 1U));
    WRITE_U8(arg2, 0x21, (uint8_t)(((uint32_t)a1_3 >> 0x1a) & 1U));
    WRITE_U8(arg2, 0x2b, (uint8_t)(((uint32_t)a0_13 >> 0x18) & 1U));
    WRITE_U8(arg2, 0x2c, (uint8_t)(((uint32_t)a0_13 >> 0x1a) & 1U));
    WRITE_U8(arg2, 0x2d, (uint8_t)(((uint32_t)a0_13 >> 0x1b) & 1U));
    WRITE_U32(arg2, 0x30, ((uint32_t)a0_13 >> 0x1c) & 3U);
    WRITE_U8(arg2, 0x34, (uint8_t)(((uint32_t)a0_13 >> 0x1e) & 1U));
    WRITE_U8(arg2, 0x38, (uint8_t)(((uint32_t)t5_2 >> 0x12) & 1U));
    t3_3 = t1_3 * (int32_t)v0_16 + (a0_14 & 0x3ff);
    WRITE_U8(arg2, 0x48, (uint8_t)(((uint32_t)a0_14 >> 0x16) & 1U));
    WRITE_U8(arg2, 0x49, (uint8_t)(((uint32_t)a0_14 >> 0x17) & 1U));
    WRITE_U8(arg2, 0x8c, (uint8_t)(((uint32_t)a0_14 >> 0x18) & 0xfU));
    a0_16 = arg1[6];
    WRITE_U32(arg2, 0x44, t3_3);
    WRITE_U8(arg2, 0x94, (uint8_t)((uint32_t)a0_16 >> 0x1c));
    a0_18 = arg1[7];
    WRITE_U8(arg2, 0x4e, (uint8_t)(((uint32_t)a0_18 >> 0x18) & 1U));
    WRITE_U8(arg2, 0x4f, (uint8_t)(((uint32_t)arg1[7] >> 0x19) & 1U));
    WRITE_U8(arg2, 0x50, (uint8_t)(((uint32_t)arg1[7] >> 0x1a) & 1U));
    a1_19 = arg1[7];
    WRITE_U16(arg2, 0x4c, (uint16_t)(((uint32_t)a0_18 >> 0xc) & 0x3ffU) + 1);
    WRITE_U16(arg2, 0x4a, (uint16_t)(a0_18 & 0x3ff) + 1);
    WRITE_U8(arg2, 0x51, (uint8_t)(((uint32_t)a1_19 >> 0x1b) & 1U));
    WRITE_U8(arg2, 0x52, (uint8_t)(((uint32_t)arg1[7] >> 0x1c) & 1U));
    a0_24 = arg1[8];
    t6_4 = ((uint32_t)a0_24 >> 0xc) & 0x3ffU;
    WRITE_U8(arg2, 0x53, (uint8_t)((uint32_t)arg1[7] >> 0x1f));
    WRITE_U32(arg2, 0x54, t6_4 * (int32_t)v0_16 + (a0_24 & 0x3ff));
    t4_6 = arg1[8];
    WRITE_U8(arg2, 0x5d, (uint8_t)(((int32_t)((uint32_t)arg1[9] << 0x1b)) >> 0x1b));
    WRITE_U8(arg2, 0x5e, (uint8_t)(((int32_t)((uint32_t)arg1[9] << 0x16)) >> 0x1b));
    WRITE_U16(arg2, 0x58, (uint16_t)(((((uint32_t)t4_6 >> 0x18) & 7U) + 1U) << 3));
    WRITE_U8(arg2, 0x5c, (uint8_t)(((uint32_t)t4_6 >> 0x1b) & 1U));
    WRITE_U16(arg2, 0x5a, (uint16_t)(((((uint32_t)t4_6 >> 0x1c) & 7U) + 1U) << 3));
    if (a3 == 0) {
        a3_1 = arg1[9];
        WRITE_U8(arg2, 0x60, (uint8_t)(((uint32_t)a3_1 >> 0xf) & 1U));
        WRITE_U8(arg2, 0x61, (uint8_t)(((uint32_t)a3_1 >> 0x12) & 1U));
        WRITE_U8(arg2, 0x62, (uint8_t)(((uint32_t)a3_1 >> 0xb) & 1U));
    } else if (a3 == 1) {
        a3_1 = arg1[9];
        WRITE_U8(arg2, 0x5f, (uint8_t)(((uint32_t)a3_1 >> 0xf) & 1U));
        WRITE_U8(arg2, 0x65, (uint8_t)(((uint32_t)a3_1 >> 0x12) & 1U));
    } else {
        a3_1 = arg1[9];
    }
    WRITE_U8(arg2, 0x67, (uint8_t)(((uint32_t)a3_1 >> 0x14) & 1U));
    a1_26 = arg1[9];
    WRITE_U16(arg2, 0x74, (uint16_t)arg1[0xa]);
    a0_36 = arg1[0xb];
    t7_2 = ((uint32_t)a3_1 >> 0x10) & 1U;
    WRITE_U8(arg2, 0x7e, (uint8_t)(((uint32_t)a0_36 >> 0x18) & 3U) + 1);
    t5_7 = arg1[0xb];
    WRITE_U32(arg2, 0x84, arg1[0xc]);
    WRITE_U32(arg2, 0x88, arg1[0xd]);
    WRITE_U32(arg2, 0x90, arg1[0xe]);
    t5_9 = arg1[0xf];
    WRITE_U8(arg2, 0x64, (uint8_t)(((uint32_t)a3_1 >> 0x11) & 1U));
    WRITE_U8(arg2, 0x71, (uint8_t)(((uint32_t)a1_26 >> 0x1e) & 1U));
    WRITE_U32(arg2, 0x98, t5_9);
    WRITE_U8(arg2, 0x63, (uint8_t)t7_2);
    WRITE_U8(arg2, 0x66, (uint8_t)(((uint32_t)a3_1 >> 0x13) & 1U));
    WRITE_U8(arg2, 0x69, (uint8_t)(((uint32_t)a1_26 >> 0x16) & 1U));
    WRITE_U8(arg2, 0x6a, (uint8_t)(((uint32_t)a1_26 >> 0x17) & 1U));
    WRITE_U8(arg2, 0x6b, (uint8_t)(((uint32_t)a1_26 >> 0x18) & 1U));
    WRITE_U8(arg2, 0x6c, (uint8_t)(((uint32_t)a1_26 >> 0x19) & 1U));
    WRITE_U8(arg2, 0x6d, (uint8_t)(((uint32_t)a1_26 >> 0x1a) & 1U));
    WRITE_U8(arg2, 0x6e, (uint8_t)(((uint32_t)a1_26 >> 0x1b) & 1U));
    WRITE_U8(arg2, 0x6f, (uint8_t)(((uint32_t)a1_26 >> 0x1c) & 1U));
    WRITE_U8(arg2, 0x70, (uint8_t)(((uint32_t)a1_26 >> 0x1d) & 1U));
    WRITE_U8(arg2, 0x72, (uint8_t)((uint32_t)a1_26 >> 0x1f));
    WRITE_U8(arg2, 0x7f, (uint8_t)(((uint32_t)t5_7 >> 0x1e) & 1U));
    WRITE_U8(arg2, 0x80, (uint8_t)((uint32_t)t5_7 >> 0x1f));
    a1_30 = arg1[0x10];
    a0_36 &= 0x3ff;
    WRITE_U16(arg2, 0x7a, (uint16_t)a0_36);
    WRITE_U32(arg2, 0xa0, a1_30);
    a1_31 = arg1[0x11];
    WRITE_U16(arg2, 0x7c, (uint16_t)((((uint32_t)arg1[0xb] >> 0xc) & 0x3ffU) + 1));
    WRITE_U32(arg2, 0xa4, a1_31);
    a1_32 = arg1[0x12];
    WRITE_U16(arg2, 0xa8, (uint16_t)(((a1_32 & 0x3ff) + 1) << 6));
    WRITE_U16(arg2, 0xaa, (uint16_t)(((((uint32_t)a1_32 >> 0xc) & 0x3ffU) + 1) << 3));
    WRITE_U8(arg2, 0xac, (uint8_t)(((uint32_t)a1_32 >> 0x1e) & 1U));
    v1_16 = (uint16_t)v1_15;
    if (t7_2 != 0) {
        a1_34 = arg1[0x14];
        t8_6 = arg1[0x15] & 0xffffff;
        WRITE_U32(arg2, 0xc0, t8_6);
        WRITE_U8(arg2, 0xe6, (uint8_t)(((uint32_t)arg1[0x15] >> 0x1c) & 3U));
        if ((((((uint32_t)a1_34 >> 6) & 0x3ffU) ^ 0xffffffffU)) == 0U) {
            __builtin_trap();
        }
        WRITE_U8(arg2, 0xe4, (uint8_t)(((uint32_t)arg1[0x15] >> 0x1e) & 1U));
        WRITE_U8(arg2, 0xe5, (uint8_t)((uint32_t)arg1[0x15] >> 0x1f));
        WRITE_U32(arg2, 0xb8, ((uint32_t)READ_U8(arg1, 0x5b) + 1U) << 0xf);
        WRITE_U32(arg2, 0xbc, arg1[0x16] & 0xffffff);
        WRITE_U8(arg2, 0xc8, READ_U8(&arg1[0x17 * 4], 0) & 0x3fU);
        v1_16 = (uint16_t)v1_15;
        WRITE_U8(arg2, 0xc9, (uint8_t)(((uint32_t)arg1[0x17] >> 8) & 0x3fU));
        t5_25 = READ_U8(arg1, 0x5e);
        WRITE_U8(arg2, 0xb0, (uint8_t)((uint32_t)a1_34 >> 0x1f));
        WRITE_U8(arg2, 0xb1, (uint8_t)(((uint32_t)a1_34 >> 0x1e) & 1U));
        WRITE_U8(arg2, 0xcb, t5_25 & 0x3fU);
        a1_38 = READ_U8(arg1, 0x5f);
        WRITE_U16(arg2, 0xd6, (uint16_t)(((uint32_t)a1_34 >> 0x10) & 0xffU));
        WRITE_U8(arg2, 0xb2, (uint8_t)(((uint32_t)a1_34 >> 0x1d) & 1U));
        WRITE_U8(arg2, 0xca, a1_38 & 0x3fU);
        a1_40 = arg1[0x18];
        WRITE_U16(arg2, 0xc6, (uint16_t)((a1_34 & 0x3f) + 1));
        WRITE_U16(arg2, 0xc4, (uint16_t)((((uint32_t)a1_34 >> 6) & 0x3ffU) + 1));
        WRITE_U8(arg2, 0xb3, (uint8_t)(((uint32_t)a1_34 >> 0x1c) & 1U));
        WRITE_U8(arg2, 0xb4, (uint8_t)(((uint32_t)a1_34 >> 0x1b) & 1U));
        WRITE_U8(arg2, 0xb5, (uint8_t)(((uint32_t)a1_34 >> 0x1a) & 1U));
        WRITE_U8(arg2, 0xcc, (uint8_t)((uint32_t)a1_40 >> 0x1f));
        WRITE_U8(arg2, 0xcd, (uint8_t)(((uint32_t)a1_40 >> 0x1e) & 1U));
        WRITE_U16(arg2, 0xd4, (uint16_t)(((uint32_t)a1_40 >> 0x14) & 0xffU));
        WRITE_U32(arg2, 0xd0, (a1_40 & 0xffff) << 3);
        WRITE_U16(arg2, 0xda, (uint16_t)(((a1_34 & 0x3f) + 1) * (t1_3 + 1) - 1));
        WRITE_U16(arg2, 0xd8, (uint16_t)(((a1_34 & 0x3f) + 1) * t6_4));
        WRITE_U32(arg2, 0xdc, ((a1_34 & 0x3f) + 1) * v1_16);
        WRITE_U32(arg2, 0xe0, (uint32_t)(t3_3 - a2_4) / ((((uint32_t)a1_34 >> 6) & 0x3ffU) + 1U) * (uint32_t)t8_6);
    }
    a1_42 = arg1[0x19];
    if ((((uint32_t)a2 >> 0x10) & 3U) + 8U >= (uint32_t)t0_2) {
        t0_2 = ((((uint32_t)a2 >> 0x10) & 3U) + 8U);
    }
    WRITE_U8(arg2, 0x10c, (uint8_t)t0_2);
    WRITE_U8(arg2, 0xee, (uint8_t)((uint32_t)a1_42 >> 0x10));
    WRITE_U32(arg2, 0x104, (uint32_t)a0_36 - v0_16 + (((uint32_t)arg1[0xb] >> 0xc) & 0x3ffU) + 1U + (uint32_t)t3_3);
    WRITE_U16(arg2, 0xec, (uint16_t)a1_42);
    WRITE_U32(arg2, 0xfc, v1_16 * v0_16);
    v1_18 = arg1[0x1a];
    v0_18 = arg1[0x6e];
    a1_43 = arg1[0x60];
    a0_40 = arg1[0x61];
    result = (int16_t)(v0_18 & 0xff);
    WRITE_U32(arg2, 0xf0, ((uint32_t)v1_18 >> 0x1c) & 3U);
    WRITE_U16(arg2, 0xf4, (uint16_t)(v1_18 & 0x3ff));
    WRITE_U16(arg2, 0x110, (uint16_t)a1_43);
    WRITE_U16(arg2, 0x112, (uint16_t)((uint32_t)a1_43 >> 0x10));
    WRITE_U16(arg2, 0x114, (uint16_t)a0_40);
    WRITE_U16(arg2, 0x116, (uint16_t)((uint32_t)a0_40 >> 0x10));
    WRITE_U8(arg2, 0x118, (uint8_t)(((uint32_t)v0_18 >> 0x1c) & 1U));
    WRITE_U16(arg2, 0x11a, (uint16_t)result);
    return result;
}

int32_t JpegParamToCtrlRegs(char *arg1, int32_t *arg2)
{
    uint32_t v1;
    int32_t t0_1;
    int32_t v0_4;
    int32_t result;
    uint32_t a3_3;
    int32_t v1_8;
    int32_t a0_2;

    if (arg1 == 0 || arg2 == 0) {
        __assert("pJP && pCmdRegs",
                 "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/EncSliceParam.c",
                 0x229, "JpegParamToCtrlRegs", &_gp);
        return CtrlRegsToJpegParam(arg2, arg1);
    }

    v1 = READ_U8(arg1, 0);
    t0_1 = (*arg2 & 0xfffffffc) | (READ_S32(arg1, 8) & 3);
    *arg2 = t0_1;
    v0_4 = ((((((uint32_t)READ_U8(arg1, 5) & 3U) << 4) | (t0_1 & 0xffffffcf)) & 0xfffffeffU) |
            (v1 << 8)) &
           0xfffffdffU;
    v0_4 |= (uint32_t)READ_U8(arg1, 1) << 9;
    *arg2 = v0_4;
    result = (v0_4 & 0xcfff) | (((uint32_t)READ_U8(arg1, 4) & 3U) << 0xc) | ((uint32_t)READ_U16(arg1, 2) << 0x10);
    a3_3 = ((uint32_t)READ_U16(arg1, 0x10) << 0x10) | READ_U16(arg1, 0x12);
    v1_8 = ((uint32_t)(READ_U16(arg1, 0xe) - 1U)) | ((uint32_t)(READ_U16(arg1, 0xc) - 1U) << 0x10);
    a0_2 = (arg2[3] & 0xffff0000) | READ_U16(arg1, 0x14);
    *arg2 = result;
    arg2[1] = v1_8;
    arg2[2] = (int32_t)a3_3;
    arg2[3] = a0_2;
    return result;
}

int32_t CtrlRegsToJpegParam(int32_t *arg1, char *arg2)
{
    int32_t v1_4 = *arg1;
    int32_t v0_1 = *arg1;
    int32_t v1_2 = arg1[1];
    int32_t a2 = arg1[2];
    int16_t t1 = READ_U16(arg1, 2);
    int16_t a3 = (int16_t)arg1[3];
    uint8_t result = (uint8_t)(((uint32_t)v0_1 >> 9) & 1U);

    WRITE_U8(arg2, 5, (uint8_t)(((uint32_t)v1_4 >> 4) & 3U));
    WRITE_U32(arg2, 8, v1_4 & 3);
    WRITE_U8(arg2, 4, (uint8_t)(((uint32_t)v0_1 >> 0xc) & 3U));
    WRITE_U8(arg2, 0, (uint8_t)(((uint32_t)v0_1 >> 8) & 1U));
    WRITE_U8(arg2, 1, result);
    WRITE_U16(arg2, 2, (uint16_t)t1);
    WRITE_U16(arg2, 0xe, (uint16_t)(v1_2 + 1));
    WRITE_U16(arg2, 0xc, (uint16_t)(((uint32_t)v1_2 >> 0x10) + 1U));
    WRITE_U16(arg2, 0x10, (uint16_t)((uint32_t)a2 >> 0x10));
    WRITE_U16(arg2, 0x12, (uint16_t)a2);
    WRITE_U16(arg2, 0x14, (uint16_t)a3);
    return result;
}

void *RequestsBuffer_Init(void *arg1)
{
    WRITE_S32(arg1, 0xea88, 0);
    return &g_dummy_elf_header;
}

void *RequestsBuffer_Pop(void *arg1)
{
    int32_t t1 = READ_S32(arg1, 0xea88);

    WRITE_S32(arg1, 0xea88, (t1 + 1) % 0x13);
    return (uint8_t *)arg1 + t1 * 0xc58;
}

void EndRequestsBuffer_Init(void *arg1)
{
    WRITE_S32(arg1, 0xe8, 0);
}

void *EndRequestsBuffer_Pop(void *arg1)
{
    int32_t v0_3 = READ_S32(arg1, 0xe8);

    WRITE_S32(arg1, 0xe8, 0);
    return (uint8_t *)arg1 + v0_3 * 0xe8;
}

int32_t AL_ModuleArray_AddModule(void *arg1, int32_t arg2, int32_t arg3)
{
    int32_t *v0_1 = (int32_t *)((uint8_t *)arg1 + (READ_S32(arg1, 0x80) << 3));
    int32_t result;

    v0_1[0] = arg2;
    v0_1[1] = arg3;
    result = READ_S32(arg1, 0x80) + 1;
    WRITE_S32(arg1, 0x80, result);
    return result;
}

int32_t AL_ModuleArray_IsEmpty(void *arg1)
{
    return READ_U32(arg1, 0x80) < 1U ? 1 : 0;
}

static int32_t setSteps_noentropy(void *arg1, void *arg2)
{
    WRITE_U32(arg2, 0x84, READ_U32(arg1, 0x3c));
    WRITE_U32(arg2, 0x88, 0);
    AddOneModuleByCore((int32_t)(intptr_t)arg2, arg1, 0);
    AddOneModuleByCore((int32_t)(intptr_t)((uint8_t *)arg2 + 0x8c), arg1, 0);
    return 1;
}

SetStepsFn *getNoEntropyChannel(SetStepsFn *arg1)
{
    *arg1 = setSteps_noentropy;
    return arg1;
}

static int32_t setSteps_syncentropy(void *arg1, void *arg2)
{
    uint32_t a0 = READ_U32(arg1, 0x3c);
    uint32_t v0 = READ_U32(arg1, 0x40);

    WRITE_U32(arg2, 0x84, 0);
    if ((int32_t)v0 >= (int32_t)a0) {
        v0 = a0;
    }
    WRITE_U32(arg2, 0x88, v0);
    AddOneModuleByCore((int32_t)(intptr_t)arg2, arg1, 0);
    AddOneModuleByCore((int32_t)(intptr_t)arg2, arg1, 1);
    AddOneModuleByCore((int32_t)(intptr_t)((uint8_t *)arg2 + 0x8c), arg1, 0);
    return 1;
}

SetStepsFn *getSyncEntropyChannel(SetStepsFn *arg1)
{
    *arg1 = setSteps_syncentropy;
    return arg1;
}

static int32_t setSteps_synccropentropy(void *arg1, void *arg2)
{
    uint32_t v0 = READ_U32(arg1, 0x3c);

    WRITE_U32(arg2, 0x84, 0);
    if ((int32_t)v0 >= 6) {
        v0 = 5;
    }
    WRITE_U32(arg2, 0x88, v0);
    AddOneModuleByCore((int32_t)(intptr_t)arg2, arg1, 0);
    AddOneModuleByCore((int32_t)(intptr_t)arg2, arg1, 1);
    AddOneModuleByCore((int32_t)(intptr_t)((uint8_t *)arg2 + 0x8c), arg1, 0);
    return 1;
}

SetStepsFn *getSyncCropEntropyChannel(SetStepsFn *arg1)
{
    *arg1 = setSteps_synccropentropy;
    return arg1;
}

static int32_t setSteps_asyncentropy(void *arg1, void *arg2)
{
    uint32_t s2 = READ_U32(arg1, 0x3c);
    uint32_t v1 = READ_U32(arg1, 0x40);

    WRITE_U32(arg2, 0x84, READ_U32(arg1, 0x3c));
    WRITE_U32(arg2, 0x88, 0);
    AddOneModuleByCore((int32_t)(intptr_t)arg2, arg1, 0);
    AddOneModuleByCore((int32_t)(intptr_t)((uint8_t *)arg2 + 0x8c), arg1, 0);
    WRITE_U32(arg2, 0x194, 0);
    if ((int32_t)v1 < (int32_t)s2) {
        s2 = v1;
    }
    WRITE_U32(arg2, 0x198, s2);
    if (s2 != 0) {
        int32_t s0_1 = 0;

        do {
            AL_ModuleArray_AddModule((uint8_t *)arg2 + 0x19c, s0_1, 1);
            AL_ModuleArray_AddModule((uint8_t *)arg2 + 0x110, s0_1, 1);
            s0_1 += 1;
        } while (s2 != (uint32_t)s0_1);
    }
    return 2;
}

SetStepsFn *getAsyncEntropyChannel(SetStepsFn *arg1)
{
    *arg1 = setSteps_asyncentropy;
    return arg1;
}

uint32_t AddOneModuleByCore(int32_t arg1, void *arg2, int32_t arg3)
{
    uint32_t i = READ_U32(arg2, 0x3c);

    if (i != 0) {
        int32_t s0_1 = 0;

        do {
            AL_ModuleArray_AddModule((void *)(intptr_t)arg1, s0_1, arg3);
            s0_1 += 1;
            i = (uint32_t)(s0_1 < (int32_t)READ_U32(arg2, 0x3c));
        } while (i != 0);
    }
    return i;
}
