#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "alcodec/al_fourcc.h"
#include "alcodec/al_rtos.h"
#include "alcodec/al_types.h"
#include "alcodec/al_utils.h"

extern char _gp;
extern int32_t __assert(const char *expression, const char *file, int32_t line,
                        const char *function, void *caller);

/* forward decl, ported by T<N> later */
int32_t AL_AVC_CheckLevel(int32_t arg1);
/* forward decl, ported by T<N> later */
int32_t AL_HEVC_CheckLevel(int32_t arg1);
/* forward decl, ported by T<N> later */
int32_t AL_AVC_GetMaxCPBSize(int32_t arg1);
/* forward decl, ported by T<N> later */
int32_t AL_HEVC_GetMaxCPBSize(int32_t arg1, int32_t arg2);
/* forward decl, ported by T<N> later */
int32_t AL_AVC_GetLevelFromFrameSize(int32_t arg1);
/* forward decl, ported by T<N> later */
int32_t AL_AVC_GetLevelFromMBRate(int32_t arg1);
/* forward decl, ported by T<N> later */
int32_t AL_AVC_GetLevelFromBitrate(int32_t arg1);
/* forward decl, ported by T<N> later */
int32_t AL_AVC_GetLevelFromDPBSize(int32_t arg1);
/* forward decl, ported by T<N> later */
int32_t AL_HEVC_GetLevelFromFrameSize(int32_t arg1);
/* forward decl, ported by T<N> later */
int32_t AL_HEVC_GetLevelFromPixRate(int32_t arg1);
/* forward decl, ported by T<N> later */
int32_t AL_HEVC_GetLevelFromBitrate(int32_t arg1, int32_t arg2);
/* forward decl, ported by T<N> later */
uint32_t AL_HEVC_GetLevelFromDPBSize(int32_t arg1, int32_t arg2);
/* forward decl, ported by T<N> later */
int32_t AL_HEVC_GetLevelFromTileCols(int32_t arg1);
/* forward decl, ported by T<N> later */
int32_t AL_Constraint_NumCoreIsSane(int32_t arg1, int32_t arg2, int32_t arg3,
                                    int32_t *arg4);
/* forward decl, ported by T<N> later */
int32_t AL_CoreConstraint_Init(int32_t *arg1, int32_t arg2, int32_t arg3,
                               int32_t arg4, int32_t arg5, int32_t arg6);
/* forward decl, ported by T<N> later */
uint32_t AL_CoreConstraint_GetExpectedNumberOfCores(void *arg1, int32_t arg2,
                                                    int32_t arg3, int32_t arg4,
                                                    int32_t arg5);

static const int32_t LAMBDA_FACTORS[6] = {
    0x33, 0x5a, 0x97, 0x97, 0x97, 0x97,
};

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

static void write_u8(void *base, size_t offset, uint8_t value)
{
    memcpy((uint8_t *)base + offset, &value, sizeof(value));
}

static void write_s8(void *base, size_t offset, int8_t value)
{
    memcpy((uint8_t *)base + offset, &value, sizeof(value));
}

static void write_u16(void *base, size_t offset, uint16_t value)
{
    memcpy((uint8_t *)base + offset, &value, sizeof(value));
}

static void write_s16(void *base, size_t offset, int16_t value)
{
    memcpy((uint8_t *)base + offset, &value, sizeof(value));
}

static void write_u32(void *base, size_t offset, uint32_t value)
{
    memcpy((uint8_t *)base + offset, &value, sizeof(value));
}

static void write_s32(void *base, size_t offset, int32_t value)
{
    memcpy((uint8_t *)base + offset, &value, sizeof(value));
}

static void AL_sCheckRange_part_1(int16_t *arg1, int16_t arg2, void *arg3)
{
    if (arg3 != NULL) {
        fwrite("The Specified Range is too high !!\r\n", 1, 0x24,
               (FILE *)arg3);
    }
    *arg1 = arg2;
}

int32_t AL_sSettings_GetMaxCPBSize(void *arg1);
uint32_t AL_Settings_SetDefaultParam(void *arg1);
uint32_t AL_DPBConstraint_GetMaxRef_DefaultGopMngr(int32_t *arg1, int32_t arg2);
int32_t AL_DPBConstraint_GetMaxDPBSize(void *arg1);
int32_t AL_ParamConstraints_CheckResolution(int32_t arg1, int32_t arg2,
                                            int32_t arg3, int32_t arg4);

int32_t AL_sSettings_GetCpbVclFactor(int32_t arg1)
{
    if (arg1 != 0x1920004) {
        if ((uint32_t)arg1 < 0x1920005U) {
            if (arg1 == 0x1000002) {
                return 0x3e8;
            }
            if ((uint32_t)arg1 >= 0x1000003U) {
                if (arg1 == 0x11c8004) {
                    return 0x535;
                }
                if ((uint32_t)arg1 >= 0x11c8005U) {
                    if (arg1 == 0x1820004) {
                        return 0xbb8;
                    }
                    if (arg1 == 0x1908004) {
                        return 0x7d0;
                    }
                    if (arg1 == 0x1808004) {
                        return 0xbb8;
                    }
                    return -1;
                }
                if (arg1 == 0x1020004) {
                    return 0xfa0;
                }
                if (arg1 == 0x1030004) {
                    return 0xfa0;
                }
                if (arg1 != 0x1000003) {
                    return -1;
                }
                return 0x3e8;
            }
            if (arg1 == 0x64) {
                return 0x5dc;
            }
            if ((uint32_t)arg1 < 0x65U) {
                if (arg1 == 0x4d) {
                    return 0x4b0;
                }
                if (arg1 == 0x58) {
                    return 0x4b0;
                }
                if (arg1 == 0x42) {
                    return 0x4b0;
                }
                return -1;
            }
            if (arg1 == 0x7a) {
                return 0x12c0;
            }
            if (arg1 == 0x1000001) {
                return 0x3e8;
            }
            if (arg1 == 0x6e) {
                return 0xe10;
            }
            return -1;
        }
        if (arg1 == 0x1da0004) {
            return 0x3e8;
        }
        if ((uint32_t)arg1 < 0x1da0005U) {
            if (arg1 == 0x1c08004) {
                return 0x9c4;
            }
            if ((uint32_t)arg1 >= 0x1c08005U) {
                if (arg1 == 0x1d08004) {
                    return 0x683;
                }
                if (arg1 == 0x1d20004) {
                    return 0x683;
                }
                if (arg1 == 0x1c20004) {
                    return 0x9c4;
                }
                return -1;
            }
            if (arg1 == 0x19a0004) {
                return 0x5dc;
            }
            if (arg1 == 0x19c8004) {
                return 0x3e8;
            }
            if (arg1 != 0x1988004) {
                return -1;
            }
            return 0x5dc;
        }
        if (arg1 == 0x1f08004) {
            return 0x683;
        }
        if ((uint32_t)arg1 >= 0x1f08005U) {
            if (arg1 == 0x1fa0004) {
                return 0x3e8;
            }
            if (arg1 == 0x1fc8004) {
                return 0x29b;
            }
            if (arg1 == 0x1f20004) {
                return 0x683;
            }
            return -1;
        }
        if (arg1 != 0x1e20004) {
            if (arg1 == 0x1e30004) {
                return 0x7d0;
            }
            if (arg1 == 0x1e08004) {
                return 0x7d0;
            }
            return -1;
        }
        return 0x7d0;
    }
    return 0x3e8;
}

int32_t AL_sSettings_CheckLevel(int32_t arg1, int32_t arg2)
{
    uint32_t a0 = (uint32_t)arg1 >> 0x18;

    if (a0 == 1U) {
        return AL_HEVC_CheckLevel(arg2);
    }
    if (a0 == 0U) {
        return AL_AVC_CheckLevel(arg2);
    }
    return ((a0 ^ 4U) < 1U) ? 1 : 0;
}

int32_t GetHevcMaxTileRow(int32_t arg1)
{
    switch ((uint32_t)((uint8_t)arg1 - 0xaU)) {
    case 0:
    case 0xa:
    case 0xb:
        return 1;
    case 0x14:
        return 2;
    case 0x15:
        return 3;
    case 0x1e:
    case 0x1f:
        return 5;
    case 0x28:
    case 0x29:
    case 0x2a:
        return 0xb;
    case 0x32:
    case 0x33:
    case 0x34:
        return 0x16;
    default:
        printf("level:%d\n", arg1);
        return AL_sSettings_GetMaxCPBSize(
            (void *)(intptr_t)__assert(
                "0",
                "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common_enc/Settings.c",
                0x2a6, "GetHevcMaxTileRow", &_gp));
    }
}

int32_t AL_sSettings_GetMaxCPBSize(void *arg1)
{
    int32_t a2 = read_s32(arg1, 0x1c);
    int32_t v0 = AL_sSettings_GetCpbVclFactor(a2);
    uint32_t a2_1 = (uint32_t)a2 >> 0x18;

    if (a2_1 == 0U) {
        return v0 * AL_AVC_GetMaxCPBSize(read_u8(arg1, 0x20));
    }
    if (a2_1 == 1U) {
        return v0 * AL_HEVC_GetMaxCPBSize(read_u8(arg1, 0x20),
                                          read_u8(arg1, 0x21));
    }
    return 0;
}

int32_t AL_sSettings_SetDefaultJPEGParam(void *arg1)
{
    write_u8(arg1, 0x4e, 4);
    write_u32(arg1, 0x84, 0x64);
    write_u32(arg1, 0xfc, 4);
    write_u16(arg1, 0x82, 1);
    write_u32(arg1, 0xa8, 2);
    write_u16(arg1, 0xac, 1);
    write_u16(arg1, 0xae, 0);
    return 1;
}

int32_t AL_Settings_SetDefaultRCParam(int32_t *arg1)
{
    write_u32(arg1, 0x10, 0x3d0900);
    write_u32(arg1, 0x14, 0x3d0900);
    write_s16(arg1, 0x1c, 0x33);
    write_u16(arg1, 0x0c, 0x1e);
    write_u16(arg1, 0x0e, 0x3e8);
    write_u32(arg1, 0x04, 0x20f58);
    write_u32(arg1, 0x08, 0x41eb0);
    write_u16(arg1, 0x32, 0xff);
    write_u16(arg1, 0x30, 0x1068);
    write_u32(arg1, 0x28, 0x11);
    write_s16(arg1, 0x26, 0xa);
    write_u32(arg1, 0x00, 0);
    write_s16(arg1, 0x18, -1);
    write_s16(arg1, 0x1a, 0);
    write_s16(arg1, 0x1e, -1);
    write_s16(arg1, 0x20, -1);
    write_u16(arg1, 0x22, 0);
    write_s16(arg1, 0x24, 2);
    write_u32(arg1, 0x3c, 0);
    write_u32(arg1, 0x38, 0);
    write_u32(arg1, 0x34, 0);
    return 2;
}

int32_t AL_Settings_SetDefaults(void *arg1)
{
    if (arg1 == NULL) {
        return AL_Settings_SetDefaultParam((void *)(intptr_t)__assert(
            "pSettings",
            "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common_enc/Settings.c",
            0x120, "AL_Settings_SetDefaults", &_gp));
    }

    Rtos_Memset(arg1, 0, 0x754);
    write_u32(arg1, 0x2c, read_u32(arg1, 0x2c) | 0x40000);
    write_u32(arg1, 0x10, 0x188);
    write_u8(arg1, 0x18, 8);
    write_u32(arg1, 0x1c, 0x1000001);
    write_u8(arg1, 0x20, 0x33);
    write_u32(arg1, 0xb0, 0x7fffffff);
    write_u32(arg1, 0x30, 0x1c);
    write_u32(arg1, 0xa8, 2);
    write_u16(arg1, 0xac, 0x1e);
    write_u16(arg1, 0x04, 0);
    write_u16(arg1, 0x06, 0);
    write_u16(arg1, 0x08, 0);
    write_u16(arg1, 0x0a, 0);
    write_u8(arg1, 0x21, 0);
    Rtos_Memset((uint8_t *)arg1 + 0xc0, 0, 4);
    write_u32(arg1, 0xbc, 0);
    AL_Settings_SetDefaultRCParam((int32_t *)((uint8_t *)arg1 + 0x68));
    write_u16(arg1, 0x40, 1);
    write_u32(arg1, 0x100, 5);
    write_u32(arg1, 0x104, 2);
    write_u32(arg1, 0x108, 2);
    write_u32(arg1, 0xc8, 3);
    write_u8(arg1, 0x35, 0xff);
    write_u8(arg1, 0x34, 0xff);
    write_u32(arg1, 0x3c, 0);
    write_u32(arg1, 0xf8, 0);
    write_u32(arg1, 0xf0, 0);
    write_u32(arg1, 0xf4, 0);
    write_u32(arg1, 0xfc, 0);
    write_u32(arg1, 0x118, 0);
    write_u32(arg1, 0x11c, 0);
    Rtos_Memcpy((uint8_t *)arg1 + 0xcc, &LAMBDA_FACTORS, 0x18);
    write_s16(arg1, 0x4a, -1);
    write_s16(arg1, 0x4c, -1);
    write_s16(arg1, 0x46, -1);
    write_s16(arg1, 0x48, -1);
    write_u8(arg1, 0x4e, 5);
    write_u8(arg1, 0x4f, 3);
    write_u8(arg1, 0x50, 5);
    write_u8(arg1, 0x51, 2);
    write_u8(arg1, 0x52, 1);
    write_u8(arg1, 0x53, 1);
    write_u16(arg1, 0x66, 0xf);
    write_u8(arg1, 0xe4, 5);
    write_u32(arg1, 0x10c, 3);
    write_u8(arg1, 0x112, 1);
    write_u32(arg1, 0x124, 1);
    write_u32(arg1, 0x120, 1);
    write_u32(arg1, 0x54, 1);
    write_u32(arg1, 0x58, 0);
    write_u32(arg1, 0x14, 0);
    write_u32(arg1, 0x0c, 0);
    return 0xf;
}

uint32_t AL_Settings_SetDefaultParam(void *arg1)
{
    uint32_t result;

    if ((read_u32(arg1, 0x2c) & 0x8000U) != 0U) {
        write_u32(arg1, 0x90, read_u32(arg1, 0x90) | 4U);
    }
    if (read_u8(arg1, 0xb5) != 0U) {
        write_u8(arg1, 0xb4, 1);
    }
    result = read_u8(arg1, 0x1f);
    if (result == 0U) {
        int32_t a1_1 = read_s32(arg1, 0x10c);

        result = read_u32(arg1, 0x30) & 0xffffff7fU;
        write_u8(arg1, 0x4e, 4);
        write_u32(arg1, 0x30, result);
        if (a1_1 == 3) {
            write_u32(arg1, 0x10c, 0);
        }
        return result;
    }
    if (result == 1U) {
        if (read_s32(arg1, 0x10c) == 3) {
            write_u32(arg1, 0x10c, result);
        }
        result = 1;
        if (read_u8(arg1, 0x3b) >= 2U) {
            write_u8(arg1, 0x3b, 1);
        }
        return 1;
    }
    if (result == 4U) {
        return AL_sSettings_SetDefaultJPEGParam(arg1);
    }
    return result;
}

int32_t AL_Settings_CheckValidity(void *arg1, void *arg2, void *arg3)
{
    /* Entry kmsg trace — dump the fields the validator reads */
    {
        int kfd = open("/dev/kmsg", O_WRONLY);
        if (kfd >= 0) {
            char b[320];
            uint8_t *cb = (uint8_t *)arg2;
            int n = snprintf(b, sizeof(b),
                "libimp/ENC: CheckValidity ENTRY arg1=%p arg2=%p arg3=%p "
                "prof=0x%08x lvl=%u 0x4e=%u 0x4f=%u 0x1f=%u "
                "rc@0x68=%d rc@0x6c=%d sc@0x78=0x%08x "
                "a8=0x%08x ac=%u ae=%u 0x38=%u 0x39=%u 0x40=%u 0x80=%d "
                "a1@0x114=%u a1@0x116=%u\n",
                arg1, arg2, arg3,
                *(uint32_t *)(cb + 0x1c),
                (unsigned)cb[0x20],
                (unsigned)cb[0x4e],
                (unsigned)cb[0x4f],
                (unsigned)cb[0x1f],
                *(int32_t *)(cb + 0x68),
                *(int32_t *)(cb + 0x6c),
                *(uint32_t *)(cb + 0x78),
                *(uint32_t *)(cb + 0xa8),
                (unsigned)*(uint16_t *)(cb + 0xac),
                (unsigned)cb[0xae],
                (unsigned)cb[0x38],
                (unsigned)cb[0x39],
                (unsigned)*(uint16_t *)(cb + 0x40),
                (int)*(int8_t *)(cb + 0x80),
                arg1 ? (unsigned)*(uint16_t *)((uint8_t *)arg1 + 0x114) : 0,
                arg1 ? (unsigned)*(uint16_t *)((uint8_t *)arg1 + 0x116) : 0);
            if (n > 0) write(kfd, b, (size_t)n);
            close(kfd);
        }
    }
    int32_t v0 = read_s32(arg2, 0x1c);
    /* Force s1 to always hit memory — avoids gcc -O2 slot reuse that
     * was making the returned value look like a garbage heap pointer. */
    volatile int32_t s1 = 0;
    #define S1_TRACE() do { \
        int _kfd = open("/dev/kmsg", O_WRONLY); \
        if (_kfd >= 0) { \
            char _b[40]; \
            int _n = snprintf(_b, sizeof(_b), \
                "libimp/ENC chk hit L%d s1=%d\n", __LINE__, (int)s1); \
            if (_n > 0) write(_kfd, _b, _n); \
            close(_kfd); \
        } \
    } while (0)
    int32_t v1_4;
    int32_t v1_12;
    uint32_t v0_5;
    int32_t v1_6;
    uint32_t a2_1;
    int32_t v0_6;
    int32_t s4_2;
    uint32_t v0_85;
    int32_t v1_33;
    uint32_t a1_11;
    int32_t s5_1;
    int32_t v0_18;
    int32_t s5_2;
    uint32_t a0_5;
    int32_t s4_3;
    int32_t v0_59;
    uint32_t a3_17;
    uint32_t v0_57;
    int32_t s4_6;
    uint32_t v0_64;
    int32_t v0_105;
    int32_t a0_11;
    int32_t v0_65;
    int32_t v0_67;
    int32_t s3_1;
    int32_t core_scratch[2];

    if ((uint32_t)v0 < 0x1000004U) {
        if ((uint32_t)v0 >= 0x1000001U || v0 == 0x7a) {
            goto label_4e4f4;
        }
        if ((uint32_t)v0 < 0x7bU) {
            if (v0 == 0x4d) {
                goto label_4e4f4;
            }
            if ((uint32_t)v0 < 0x4eU) {
                if (v0 != 0x42) {
                    goto label_4e188;
                }
                s1 = 0;
                goto label_4e500;
            }
            if (v0 == 0x64 || v0 == 0x6e) {
                goto label_4e4f4;
            }
            goto label_4e188;
        }
        if (v0 == 0x87a) {
            goto label_4e4f4;
        }
        if ((uint32_t)v0 >= 0x87bU) {
            if (v0 == 0x1064) {
                goto label_4e4f4;
            }
            if (v0 != 0x3064) {
                goto label_4e188;
            }
            s1 = 0;
            goto label_4e500;
        }
        if (v0 == 0x242) {
            goto label_4e4f4;
        }
        if (v0 != 0x86e) {
            goto label_4e188;
        }
        s1 = 0;
        goto label_4e500;
    }
    if (v0 == 0x1f08004) {
        goto label_4e4f4;
    }
    if ((uint32_t)v0 < 0x1f08005U) {
        if (v0 == 0x1d20004) {
            goto label_4e4f4;
        }
        if ((uint32_t)v0 >= 0x1d20005U) {
            if (v0 == 0x1da0004 || v0 == 0x1dc8004) {
                goto label_4e4f4;
            }
            goto label_4e188;
        }
        if (v0 != 0x1d08004) {
            goto label_4e188;
        }
        s1 = 0;
        goto label_4e500;
    }
    if (v0 == 0x1fa0004) {
        goto label_4e4f4;
    }
    if ((uint32_t)v0 >= 0x1fa0005U) {
        if (v0 == 0x1fc8004) {
            goto label_4e4f4;
        }
        if (v0 != 0x4000000) {
            goto label_4e188;
        }
        s1 = 0;
        goto label_4e500;
    }
    if (v0 == 0x1f20004) {
        s1 = 0;
        goto label_4e500;
    }

label_4e188:
    s1 = 1;
    if (arg3 == NULL) {
        goto label_4e500;
    }
    fwrite("Invalid parameter: Profile\r\n", 1, 0x1c, (FILE *)arg3);
    v0 = read_s32(arg2, 0x1c);

label_4e4f4:
    s1 = 0;

label_4e500:
    v1_4 = v0 & (int32_t)0xff0000ffU;
    if (v1_4 != 0x6e && v1_4 != 0x1000002 &&
        (v0 & (int32_t)0xff2000ffU) != 0x1000004 && v1_4 != 0x7a) {
        goto label_4e53c;
    }

    v1_12 = read_s32(arg2, 0x10);
    {
        int32_t a2 = (v1_12 >> 4) & 0xf;
        int32_t a0 = v1_12 & 0xf;

        if (a2 >= a0) {
            a0 = a2;
        }
        if (a0 >= 9) {
            int32_t s4_1 = s1 + 1;

            if (arg3 != NULL) {
                fwrite("The hardware IP doesn't support 10-bit encoding\r\n",
                       1, 0x31, (FILE *)arg3);
            }
            s1 = s4_1;
            S1_TRACE();
        }
    }

label_4e548:
    if (((uint32_t)v1_12 >> 8 & 0xfU) == 3U) {
        s1 += 1;
        S1_TRACE();
        if (arg3 != NULL) {
label_4eda4:
            fwrite("The specified ChromaMode is not supported by the IP\r\n",
                   1, 0x35, (FILE *)arg3);
        }
    }
    if (read_u16(arg2, 0x08) != read_u16(arg2, 0x04)) {
        s1 += 1;
        S1_TRACE();
        if (arg3 != NULL) {
label_4e57c:
            fwrite("Different input and output resolutions are not supported by the IP\r\n",
                   1, 0x44, (FILE *)arg3);
        }
    }

label_4e244:
    v0 = read_s32(arg2, 0x1c);
    if (AL_sSettings_CheckLevel(v0, read_u8(arg2, 0x20)) == 0) {
        s1 += 1;
        S1_TRACE();
        if (arg3 != NULL) {
label_4e5bc:
            fwrite("Invalid parameter: Level\r\n", 1, 0x1a, (FILE *)arg3);
        }
    }

label_4e5c8:
    v1_6 = read_s32(arg2, 0x1c);
    v0_5 = (uint32_t)v1_6 >> 0x18;
    if (v0_5 != 0U) {
        if (((v1_6 & (int32_t)0xff0200ffU) != 0x1020004 ||
             (read_u32(arg2, 0xa8) & 4U) == 0U)) {
            goto label_4e290;
        }
    } else if ((((v0_5 != 0U || (v1_6 & 0x800) == 0) &&
                 (v1_6 & (int32_t)0xff0200ffU) != 0x1020004) ||
                (read_u32(arg2, 0xa8) & 4U) == 0U)) {
        goto label_4e290;
    }

label_4e5f8:
    {
        int32_t s4_4 = s1 + 1;

        if (arg3 != NULL) {
            fwrite("Pyramidal and IntraProfile is not supported\r\n", 1, 0x2d,
                   (FILE *)arg3);
        }
        s1 = s4_4;
        S1_TRACE();
    }

label_4e290:
    if (read_u8(arg2, 0x4f) != 3U) {
        s1 += 1;
        S1_TRACE();
        if (arg3 != NULL) {
            fwrite("Invalid parameter: MinCuSize\r\n", 1, 0x1e,
                   (FILE *)arg3);
        }
    }
    v0_5 = read_u8(arg2, 0x1f);
    a2_1 = read_u8(arg2, 0x4e);
    if (v0_5 == 1U) {
        if (a2_1 == 5U) {
            goto label_4e320;
        }
        s1 += 1;
        S1_TRACE();
        if (arg3 != NULL) {
            fwrite("Invalid parameter: MaxCuSize\r\n", 1, 0x1e,
                   (FILE *)arg3);
        }
        a2_1 = read_u8(arg2, 0x4e);
        v0_5 = read_u8(arg2, 0x1f);
    }

label_4e2d0:
    if (v0_5 == 0U) {
        if (a2_1 == 4U) {
            goto label_4eb94;
        }
        s1 += 1;
        S1_TRACE();
        if (arg3 != NULL) {
            fwrite("Invalid parameter: MaxCuSize\r\n", 1, 0x1e,
                   (FILE *)arg3);
        }
        a2_1 = read_u8(arg2, 0x4e);
        v0_5 = read_u8(arg2, 0x1f);
    }
    if (v0_5 != 4U) {
        goto label_4e320;
    }
    if (a2_1 != v0_5) {
        s1 += 1;
        S1_TRACE();
        if (arg3 != NULL) {
            fwrite("Invalid parameter: MaxCuSize\r\n", 1, 0x1e,
                   (FILE *)arg3);
        }
    }
    a1_11 = read_u8(arg2, 0x3c);
    s4_2 = 1 << (a2_1 & 0x1f);
    if (a1_11 != 0U) {
        goto label_4ebf0;
    }
    v0_85 = read_u8(arg2, 0x1f);

label_4eba8:
    v1_33 = (int32_t)(v0_85 ^ 4U);

label_4ebb0:
    v0_6 = 0x64;
    if (v1_33 != 0) {
        v0_6 = 0x33;
    }
    if (v0_6 < read_s8(arg2, 0x80)) {
        s5_1 = s1 + 1;
        if (arg3 != NULL) {
            fwrite("Invalid parameter: SliceQP\r\n", 1, 0x1c,
                   (FILE *)arg3);
        }
        s1 = s5_1;
        S1_TRACE();
    }

label_4e68c:
    if (read_u8(arg2, 0x39) + 0xcU >= 0x19U) {
        s1 += 1;
        S1_TRACE();
        if (arg3 != NULL) {
label_4e6b0:
            fwrite("Invalid parameter: CrQpOffset\r\n", 1, 0x1f,
                   (FILE *)arg3);
        }
    }
    if (read_u8(arg2, 0x38) + 0xcU >= 0x19U) {
        s1 += 1;
        S1_TRACE();
        if (arg3 != NULL) {
label_4e6e8:
            fwrite("Invalid parameter: CbQpOffset\r\n", 1, 0x1f,
                   (FILE *)arg3);
        }
    }
    if (read_u32(arg2, 0x78) >= 0xaU) {
        goto label_4e3ac;
    }

label_4e700:
    v0_18 = read_s32(arg2, 0x68);
    if ((uint32_t)(v0_18 - 1) < 2U) {
        s1 += 1;
        S1_TRACE();
        if (arg3 != NULL) {
            fwrite("Invalid parameter: BitRate\r\n", 1, 0x1c,
                   (FILE *)arg3);
        }
        v0_18 = read_s32(arg2, 0x68);
    }

label_4e3b4:
    if (v0_18 == 3) {
        if (read_u8(arg2, 0xae) != 0U) {
            int32_t s5_3 = s1 + 1;

            if (arg3 != NULL) {
                fwrite("B Picture not allowed in LOW_LATENCY Mode\r\n", 1,
                       0x2b, (FILE *)arg3);
            }
            s1 = s5_3;
            S1_TRACE();
            if (read_u8(arg2, 0xc4) != 0U) {
                s1 += 1;
                S1_TRACE();
            }
        } else if (read_u8(arg2, 0xc4) != 0U) {
            s1 += 1;
            S1_TRACE();
        }
    }

label_4e3c0:
    if (read_u8(arg2, 0xc4) != 0U && read_u8(arg2, 0xae) != 0U) {
        s1 += 1;
        S1_TRACE();
        if (arg3 != NULL) {
            fwrite("B Picture not allowed in subframe latency\r\n", 1, 0x2b,
                   (FILE *)arg3);
        }
    }
    if ((read_u32(arg2, 0xa8) & 2U) == 0U) {
        goto label_4e780;
    }
    if (read_u16(arg2, 0xac) >= 0x3e9U) {
        if (arg3 == NULL) {
            s5_2 = s1 + 2;
            if (read_u8(arg2, 0xae) >= 5U) {
                s1 += 1;
                S1_TRACE();
            }
        } else {
            fwrite("Invalid parameter: Gop.Length\r\n", 1, 0x1f,
                   (FILE *)arg3);
            s5_2 = s1 + 2;
            if (read_u8(arg2, 0xae) >= 5U) {
                goto label_4e468;
            }
            goto label_4ed48;
        }
    } else {
        /* gop.length < 0x3e9 */
        if (read_u8(arg2, 0xae) < 5U) {
            /* HLIL: if (ae u< 5) goto label_3e780  -- skip s5_2 setup */
            goto label_4e780;
        }
        s5_2 = s1 + 1;
        if (arg3 != NULL) {
label_4e468:
            fwrite("Invalid parameter: Gop.NumB\r\n", 1, 0x1d,
                   (FILE *)arg3);
        }
        a0_5 = read_u16(arg2, 0x40);
        if (a0_5 != 0U) {
            if (s4_2 == 0) {
                __builtin_trap();
            }
            s1 = s5_2;
            S1_TRACE();
            s4_3 = (read_u16(arg2, 0x06) +
                     ((((uint32_t)s4_2 >> 0x1f) + s4_2) >> 1)) /
                    s4_2;
            goto label_4e7ac;
        }
        s1 = s5_2;
        S1_TRACE();
        goto label_4ee98;
    }

label_4ed48:
    a0_5 = read_u16(arg2, 0x40);
    s1 += 2;
    S1_TRACE();
    if (a0_5 != 0U) {
        if (s4_2 == 0) {
            __builtin_trap();
        }
        if ((read_u16(arg2, 0x06) +
             ((((uint32_t)s4_2 >> 0x1f) + s4_2) >> 1)) /
                s4_2 >=
            (int32_t)a0_5) {
            s1 = s5_2;
            S1_TRACE();
        } else {
            s1 = s5_2 + 1;
            S1_TRACE();
        }
    }

label_4e780:
    a0_5 = read_u16(arg2, 0x40);
    if (a0_5 != 0U) {
label_4e7a4:
        if (s4_2 == 0) {
            __builtin_trap();
        }
        s4_3 = (read_u16(arg2, 0x06) +
                 ((((uint32_t)s4_2 >> 0x1f) + s4_2) >> 1)) /
                s4_2;
label_4e7ac:
        if (s4_3 < (int32_t)a0_5 || a0_5 >= 0xc9U) {
            goto label_4e7c8;
        }
        goto label_4ee98;
    }
    if (a0_5 < 0xc9U) {
        goto label_4ee98;
    }

label_4e7c8:
    s1 += 1;
    S1_TRACE();
    if (arg3 != NULL) {
        fwrite("Invalid parameter: NumSlices\r\n", 1, 0x1e,
               (FILE *)arg3);
    }

label_4ee98:
    if (read_u8(arg2, 0xc4) != 0U && a0_5 >= 0x21U) {
        goto label_4e7c8;
    }

    /* HLIL @ 0x3e7f4:
     *   if ((*(arg2+0x2c) u>> 0x11 & 1) == 0)  → goto label_3e910 (bypass)
     *   else                                    → NumCore v0_55 check
     * My earlier port had this INVERTED, making every valid encOptions
     * (bit 17 clear) fail the NumCore guard. */
    if ((read_u32(arg2, 0x2c) >> 0x11 & 1U) == 0U) {
        a3_17 = read_u16(arg2, 0x06);
        goto label_4e910;
    } else {
        uint32_t v0_55 = read_u16(arg1, 0x114);

        if (v0_55 >= 0x40U && read_u16(arg2, 0x04) >= v0_55) {
            v0_57 = read_u16(arg1, 0x116);
            s4_6 = s1;
            if (v0_57 >= 8U) {
                goto label_4e82c;
            }
        } else {
            s1 += 1;
            S1_TRACE();
            if (arg3 != NULL) {
                goto label_4e8c0;
            }
            goto label_4e858;
        }
    }

label_4e840:
    s1 += 1;
    S1_TRACE();
    if (arg3 != NULL) {
label_4e8c0:
        fwrite("Vertical clipping range must be between 8 and Picture Height! \r\n",
               1, 0x40, (FILE *)arg3);
    }

label_4e858:
    v0_59 = AL_ParamConstraints_CheckResolution(
        read_s32(arg2, 0x1c), (read_u32(arg2, 0x10) >> 8) & 0xf,
        read_u16(arg2, 0x04), read_u16(arg2, 0x06));
    if (v0_59 != 1) {
        goto label_4e928;
    }
    s1 += 1;
    S1_TRACE();
    if (arg3 != NULL) {
        fwrite("Width shall be multiple of 2 on 420 or 422 chroma mode!\r\n",
               1, 0x39, (FILE *)arg3);
    }
    goto label_4e938;

label_4e82c:
    a3_17 = read_u16(arg2, 0x06);
    s4_6 = s1;
    if (a3_17 >= v0_57) {
        goto label_4e910;
    }
    goto label_4e840;

label_4e910:
    v0_59 = AL_ParamConstraints_CheckResolution(
        read_s32(arg2, 0x1c), (read_u32(arg2, 0x10) >> 8) & 0xf,
        read_u16(arg2, 0x04), a3_17);
    if (v0_59 == 1 || v0_59 == 2) {
        s1 += 1;
        S1_TRACE();
        if (arg3 != NULL) {
            fwrite("Width shall be multiple of 2 on 420 or 422 chroma mode!\r\n",
                   1, 0x39, (FILE *)arg3);
        }
    }

label_4e928:
    if (v0_59 == 2) {
        goto label_4e938;
    }
    goto label_4e938;

label_4e938:
    if ((read_u32(arg2, 0xa8) & 4U) != 0U) {
        uint32_t v1_25 = read_u8(arg2, 0xae);

        if (v1_25 < 0x10U &&
            (((uint32_t)0x80a8U >> (v1_25 & 0x1f)) & 1U) != 0U) {
            v0_64 = read_u8(arg2, 0x1f);
            if (v0_64 != 1U) {
                goto label_4e988;
            }
            goto label_4eaf0;
        }
        s1 += 1;
        S1_TRACE();
        if (arg3 != NULL) {
            fwrite("!! PYRAMIDAL GOP pattern only allows 3, 5, 7, 15 B Frames !!\r\n",
                   1, 0x3e, (FILE *)arg3);
        }
    }

label_4e978:
    v0_64 = read_u8(arg2, 0x1f);
    if (v0_64 == 1U) {
label_4eaf0:
        if ((uint32_t)(read_s8(arg2, 0x39) + read_s8(arg2, 0x37) + 0xc) >=
            0x19U) {
            if (arg3 == NULL) {
                s1 += 1;
                S1_TRACE();
            } else {
                s1 += 1;
                S1_TRACE();
                fwrite("Invalid parameter: CrQpOffset\r\n", 1, 0x1f,
                       (FILE *)arg3);
            }
        }
        if ((uint32_t)(read_s8(arg2, 0x38) + read_s8(arg2, 0x36) + 0xc) >=
            0x19U) {
            s1 += 1;
            S1_TRACE();
            if (arg3 != NULL) {
                fwrite("Invalid parameter: CbQpOffset\r\n", 1, 0x1f,
                       (FILE *)arg3);
            }
        }
        goto label_4ea50;
    }

label_4e988:
    if (v0_64 != 0U) {
        goto label_4ea50;
    }
    {
        int32_t v0_65_local = read_s8(arg2, 0x80);

        if (v0_65_local >= 0 && read_s32(arg1, 0x118) == 0) {
            a0_11 = read_s32(arg1, 0x11c);
            if (a0_11 == 0 &&
                (uint32_t)(read_s8(arg2, 0x37) + v0_65_local) >= 0x34U) {
                s1 += 1;
                S1_TRACE();
                if (arg3 != NULL) {
                    fwrite("Invalid parameter: SliceQP, CrQpOffset\r\n", 1,
                           0x28, (FILE *)arg3);
                }
            }
            v0_67 = read_s8(arg2, 0x80) + read_s8(arg2, 0x36);
            if (read_s32(arg1, 0x118) == 0) {
                a0_11 = read_s32(arg1, 0x11c);
label_4ea10:
                if (a0_11 == 0 && (uint32_t)v0_67 >= 0x34U) {
                    s1 += 1;
                    S1_TRACE();
                    if (arg3 != NULL) {
                        fwrite("Invalid parameter: SliceQP, CbQpOffset\r\n",
                               1, 0x28, (FILE *)arg3);
                    }
                }
            }
        }
    }

label_4ea48:
    if ((read_u32(arg2, 0xa8) & 4U) == 0U || read_u32(arg2, 0x1c) != 0x42U) {
        goto label_4ea50;
    }

label_4ee6c:
    s1 += 1;
    S1_TRACE();
    if (arg3 != NULL) {
        fwrite("!! PYRAMIDAL GOP pattern doesn't allows baseline profile !!\r\n",
               1, 0x3d, (FILE *)arg3);
    }

label_4ea50:
    v0_105 = read_s32(arg1, 0x118);
    if ((uint32_t)(v0_105 - 1) >= 2U || read_u32(arg1, 0x11c) < 2U) {
        goto label_4ea68;
    }
    s3_1 = s1 + 1;
    if (arg3 == NULL) {
        s1 = s3_1;
        S1_TRACE();
    } else {
        fwrite("!! QP control mode not allowed !!\r\n", 1, 0x23,
               (FILE *)arg3);
        s1 = s3_1;
        S1_TRACE();
    }

label_4ea68:
    if (read_s32(arg2, 0x0c) != 0) {
        s1 += 1;
        S1_TRACE();
        if (arg3 != NULL) {
            fwrite("!! Invalid parameter: VideoMode\r\n", 1, 0x21,
                   (FILE *)arg3);
        }
    }
    {
        /* memcpy the volatile slot into a fresh local to defeat any
         * register-spill / slot-reuse theories. */
        int32_t s1_final;
        memcpy(&s1_final, (const void *)&s1, sizeof(s1_final));
        int kfd = open("/dev/kmsg", O_WRONLY);
        if (kfd >= 0) {
            char b[128];
            int n = snprintf(b, sizeof(b),
                "libimp/ENC: CheckValidity return s1=%d &s1=%p\n",
                (int)s1_final, (void *)&s1);
            if (n > 0) write(kfd, b, n);
            close(kfd);
        }
        return s1_final;
    }

label_4e53c:
    v1_12 = read_s32(arg2, 0x10);
    goto label_4e548;

label_4e320:
    a1_11 = read_u8(arg2, 0x3c);
    s4_2 = 1 << (a2_1 & 0x1f);
    if (a1_11 != 0U) {
        goto label_4ebf0;
    }
    v0_85 = read_u8(arg2, 0x1f);
    goto label_4eba8;

label_4eb94:
    a1_11 = read_u8(arg2, 0x3c);
    s4_2 = 1 << (a2_1 & 0x1f);
    if (a1_11 != 0U) {
        goto label_4ebf0;
    }
    v0_85 = read_u8(arg2, 0x1f);
    goto label_4eba8;

label_4ebf0:
    if (AL_Constraint_NumCoreIsSane(read_u16(arg2, 0x04), a1_11, a2_1,
                                    &core_scratch[0]) == 0) {
        s1 += 1;
        S1_TRACE();
        if (arg3 != NULL) {
            fprintf((FILE *)arg3,
                    "Invalid parameter: NumCore. The width should at least be %d CTB per core. With the specified number of core, it is %d CTB per core. (Multi core alignement constraint might be the reason of this error if the CTB are equal)\r\n",
                    core_scratch[0], core_scratch[1]);
        }
    }
    {
        uint32_t v0_87 = read_u8(arg2, 0x1f);

        v1_33 = (int32_t)(v0_87 ^ 4U);
        if (v0_87 == 1U && read_u16(arg2, 0x04) < (uint32_t)(a1_11 << 8)) {
            s1 += 1;
            S1_TRACE();
            if (arg3 != NULL) {
                fwrite("Invalid parameter: NumCore. The width should at least be 256 pixels per core for HEVC conformance.\r\n",
                       1, 0x64, (FILE *)arg3);
            }
            v0_85 = read_u8(arg2, 0x1f);
            goto label_4eba8;
        }
    }
    goto label_4ebb0;

label_4e3ac:
    v0_18 = read_s32(arg2, 0x68);
    goto label_4e3b4;
}

int32_t checkProfileCoherency(int32_t arg1, int32_t arg2, int32_t arg3)
{
    int32_t v1_2;
    int32_t a0;

    if (arg1 == 8) {
        goto label_4f038;
    } else if (arg1 == 0xa) {
        a0 = arg3 & (int32_t)0xff0000ffU;
        if (a0 == 0x6e) {
            goto label_4f010;
        }
        if (a0 != 0x1000002) {
            if ((arg3 & (int32_t)0xff2000ffU) == 0x1000004) {
                goto label_4f038;
            }
            if (a0 != 0x7a) {
                return 0;
            }
            goto label_4f010;
        }
        if (arg2 == 1) {
            return 0U < ((arg3 & (int32_t)0xff0400ffU) - 0x1040004U);
        }
        if (arg2 == 0) {
            a0 = arg3 & (int32_t)0xff0000ffU;
            v1_2 = a0 - 0x64;
label_4f11c:
            if (((uint32_t)0x400401U >> (v1_2 & 0x1f) & 1U) == 0U) {
                return (((uint32_t)arg3 >> 0x18 ^ 4U) < 1U) |
                       ((uint32_t)(a0 - 0x1000004) < 1U);
            }
            return 1;
        }
        goto label_4f024;
    }
    return 0;

label_4f038:
    if (arg2 == 1) {
        return 0U < ((arg3 & (int32_t)0xff0400ffU) - 0x1040004U);
    }
    if (arg2 == 0) {
        a0 = arg3 & (int32_t)0xff0000ffU;
        v1_2 = a0 - 0x64;
        if ((uint32_t)v1_2 < 0x17U) {
            goto label_4f11c;
        }
        return (((uint32_t)arg3 >> 0x18 ^ 4U) < 1U) |
               ((uint32_t)(a0 - 0x1000004) < 1U);
    }

label_4f024:
    if (arg2 == 2) {
        int32_t v0_8 = arg3 & (int32_t)0xff0000ffU;

        if (v0_8 == 0x1000004 && (((uint32_t)arg3 >> 8) & 0xc00U) == 0U) {
            return 1;
        }
        return (((uint32_t)(v0_8 ^ 0x7a) < 1U) |
                ((uint32_t)(arg3 - 0x4000000) < 1U));
    }
    return 0;

label_4f010:
    if (arg2 == 1) {
        return 0U < ((arg3 & (int32_t)0xff0400ffU) - 0x1040004U);
    }
    if (arg2 != 0) {
        goto label_4f024;
    }
    a0 = arg3 & (int32_t)0xff0000ffU;
    v1_2 = a0 - 0x64;
    goto label_4f11c;
}

int32_t getHevcMinimumProfile(int32_t arg1, int32_t arg2)
{
    if (arg1 != 8) {
        if (arg1 != 0xa) {
            goto label_4f1b0;
        }
        if (arg2 == 1) {
            return 0x1000002;
        }
        if (arg2 == 0) {
            return 0x1dc8004;
        }
        if (arg2 != 2) {
            goto label_4f1b0;
        }
        return 0x1d08004;
    }
    while (1) {
        if (arg2 == 1) {
            return 0x1000001;
        }
        if (arg2 == 0) {
            return 0x1fc8004;
        }
        if (arg2 == 2) {
            return 0x1f08004;
        }
label_4f1b0:
        arg2 = __assert(
            "0",
            "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common_enc/Settings.c",
            0, NULL, NULL);
    }
}

int32_t getAvcMinimumProfile(int32_t arg1, int32_t arg2)
{
    if (arg1 == 8) {
        goto label_4f2a8;
    }
    if (arg1 == 0xa) {
        if ((uint32_t)arg2 < 2U) {
            return 0x6e;
        }
        if (arg2 == 2) {
            return 0x7a;
        }
    }
    while (1) {
        arg2 = __assert(
            "0",
            "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common_enc/Settings.c",
            0, NULL, NULL);
label_4f2a8:
        if (arg2 == 1) {
            return 0x242;
        }
        if (arg2 == 0) {
            return 0x64;
        }
        if (arg2 == 2) {
            return 0x7a;
        }
    }
}

int32_t getMinimumProfile(int32_t arg1, int32_t arg2, int32_t arg3)
{
    int32_t v0 = arg2;

    if (arg1 != 0) {
        if (arg1 == 1) {
            return getHevcMinimumProfile(v0, arg3);
        }
        v0 = __assert(
            "0",
            "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common_enc/Settings.c",
            0x31c, "getMinimumProfile", &_gp);
    }
    return getAvcMinimumProfile(v0, arg3);
}

int32_t AL_Settings_CheckCoherency(void *arg1, void *arg2, int32_t arg3,
                                   void *arg4)
{
    int32_t v0;
    uint32_t a1;
    int32_t a2;
    int32_t v0_1;
    int32_t s6 = 0x20;
    int32_t s4 = 8;
    int32_t result_1;
    int32_t fp_2;
    int32_t v0_10;
    int32_t v0_29;
    int32_t a0_6;
    int32_t v0_36;
    int32_t v0_93;
    int32_t v0_99;
    int32_t v0_65;
    int32_t s6_1;
    uint32_t v0_52;
    int32_t result;
    int32_t s4_2;
    int32_t var_40[6];

    if (arg1 == NULL) {
        int32_t *a0_52;
        int32_t a1_18;

        a0_52 = (int32_t *)(intptr_t)__assert(
            "pSettings",
            "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common_enc/Settings.c",
            0x32c, "AL_Settings_CheckCoherency", NULL);
        return AL_DPBConstraint_GetMaxRef_DefaultGopMngr(a0_52, a1_18);
    }

    v0 = read_s32(arg2, 0x10);
    a1 = read_u8(arg2, 0x1f);
    a2 = (v0 >> 4) & 0xf;
    v0_1 = v0 & 0xf;
    if (a2 >= v0_1) {
        v0_1 = a2;
    }
    if (a1 == 0U) {
        s6 = 0x10;
    }
    if (a1 != 0U) {
        s4 = 0x10;
    }
    result_1 = ((int32_t)AL_GetBitDepth((uint32_t)arg3) < (int32_t)v0_1) ? 1
                                                                          : 0;
    if (read_u32(arg2, 0xa8) == 2U) {
        int32_t fp_4 = (int32_t)read_u16(arg2, 0xac) - 2;

        if (fp_4 < 0) {
            fp_4 = 0;
        }
        if ((uint32_t)(fp_4 & 0xffff) < read_u8(arg2, 0xae)) {
            if (arg4 != NULL) {
                fwrite("!! Warning : number of B frames per GOP is too high\r\n",
                       1, 0x35, (FILE *)arg4);
            }
            write_u8(arg2, 0xae, (uint8_t)fp_4);
        }
    }
    fp_2 = (read_u32(arg2, 0x10) >> 8) & 0xf;
    if (checkProfileCoherency(v0_1, fp_2, read_s32(arg1, 0x1c)) == 0) {
        if (arg4 != NULL) {
            fwrite("!! Warning : Adapting profile to support bitdepth and chroma mode\r\n",
                   1, 0x43, (FILE *)arg4);
        }
        write_u32(arg1, 0x1c,
                  getMinimumProfile(read_u8(arg1, 0x1f), v0_1, fp_2));
        result_1 += 1;
    }
    if ((uint32_t)(read_s32(arg1, 0x118) - 1) >= 2U &&
        (uint32_t)(read_s32(arg1, 0x11c) - 1) >= 2U) {
        write_u8(arg2, 0x3a, 0);
    } else {
        uint32_t a0_21 = read_u8(arg2, 0x4e);
        uint32_t v0_81 = read_u8(arg2, 0x3a);
        uint32_t a1_3 = read_u8(arg2, 0x4f);
        int32_t v0_82 = v0_81 < 3U ? 1 : 0;

        if ((int32_t)(a0_21 - v0_81) < (int32_t)a1_3) {
            if (arg4 != NULL) {
                fwrite("!! Warning : CuQpDeltaDepth doesn't match MinCUSize !!\r\n",
                       1, 0x38, (FILE *)arg4);
                a0_21 = read_u8(arg2, 0x4e);
                a1_3 = read_u8(arg2, 0x4f);
            }
            {
                uint32_t v0_84 = (uint32_t)((uint8_t)a0_21 - (uint8_t)a1_3);

                result_1 += 1;
                write_u8(arg2, 0x3a, (uint8_t)v0_84);
                v0_82 = v0_84 < 3U ? 1 : 0;
            }
        }
        if (v0_82 == 0) {
            if (arg4 != NULL) {
                fwrite("!! Warning : CuQpDeltaDepth shall be less then or equal to 2 !\r\n",
                       1, 0x40, (FILE *)arg4);
            }
            write_u8(arg2, 0x3a, 2);
            v0_10 = read_s32(arg2, 0x68);
            result_1 += 1;
            if (v0_10 != 1) {
                goto label_4f470;
            }
        } else {
label_4f45c:
            v0_10 = read_s32(arg2, 0x68);
            if (v0_10 == 1) {
                write_u32(arg2, 0x7c, read_u32(arg2, 0x78));
            }
        }
    }

label_4f470:
    if (v0_10 == 8) {
        write_u32(arg2, 0x78, read_u32(arg2, 0x7c));
    }
    {
        int32_t v1_1 = read_s32(arg2, 0x78);

        if (read_u32(arg2, 0x7c) < (uint32_t)v1_1) {
            if (arg4 != NULL) {
                fwrite("!! Warning specified MaxBitRate has to be greater than or equal to [Target]BitRate and will be adjusted!!\r\n",
                       1, 0x6b, (FILE *)arg4);
            }
            v1_1 = read_s32(arg2, 0x78);
            write_u32(arg2, 0x7c, (uint32_t)v1_1);
            result_1 += 1;
        }
    }
    {
        int32_t v0_13 = AL_GetChromaMode((uint32_t)arg3);
        uint32_t var_40_u32;

        if (read_u8(arg2, 0x1f) < 2U && read_s32(arg2, 0x68) == 1) {
            uint32_t t0_1 = read_u16(arg2, 0x04);
            uint32_t v1_53 = read_u16(arg2, 0x06);
            uint32_t v1_54;
            int32_t v0_123;
            uint64_t prod;

            var_40_u32 = t0_1;
            v0_123 = GetPcmVclNalSize((int32_t)t0_1, (int32_t)v1_53, v0_13,
                                      v0_1);
            v1_54 = read_u16(arg2, 0x74);
            prod = (uint64_t)(uint32_t)(v0_123 << 3) * (uint64_t)v1_54;
            if ((((uint32_t)v0_123 >> 0x1d) * v1_54 + (uint32_t)(prod >> 32)) ==
                    0U &&
                (uint32_t)prod < read_u32(arg2, 0x78)) {
                if (arg4 != NULL) {
                    fwrite("!! Warning specified TargetBitRate is too high for this use case and will be adjusted!!\r\n",
                           1, 0x59, (FILE *)arg4);
                }
                write_u32(arg2, 0x78, (uint32_t)prod);
                write_u32(arg2, 0x7c, (uint32_t)prod);
                result_1 += 1;
            }
            (void)var_40_u32;
        }
    }
    if (s6 < read_s16(arg2, 0x4a)) {
        AL_sCheckRange_part_1((int16_t *)((uint8_t *)arg2 + 0x4a), (int16_t)s6,
                              arg4);
    }
    if (s6 < read_s16(arg2, 0x4c)) {
        AL_sCheckRange_part_1((int16_t *)((uint8_t *)arg2 + 0x4c), (int16_t)s6,
                              arg4);
    }
    if (s4 < read_s16(arg2, 0x46)) {
        AL_sCheckRange_part_1((int16_t *)((uint8_t *)arg2 + 0x46), (int16_t)s4,
                              arg4);
    }
    if (s4 < read_s16(arg2, 0x46)) {
        AL_sCheckRange_part_1((int16_t *)((uint8_t *)arg2 + 0x46), (int16_t)s4,
                              arg4);
    }
    if (s4 < read_s16(arg2, 0x48)) {
        AL_sCheckRange_part_1((int16_t *)((uint8_t *)arg2 + 0x48), (int16_t)s4,
                              arg4);
    }
    if (read_u16(arg2, 0x3e) != 0U) {
        int32_t v0_23 = read_s32(arg2, 0x30);

        if ((v0_23 & 1) != 0) {
            write_u32(arg2, 0x30, (uint32_t)(v0_23 & 0xfffffffe));
            if (arg4 != NULL) {
                fwrite("!! Wavefront Parallel Processing is not allowed with SliceSize; it will be adjusted!!\r\n",
                       1, 0x57, (FILE *)arg4);
            }
            result_1 += 1;
        }
    }
    v0_29 = read_s32(arg2, 0x1c);
    if ((uint32_t)v0_29 >> 0x18 == 0U) {
        if ((v0_29 & 0x800) != 0) {
            goto label_4f598;
        }
    } else {
        goto label_4f618;
    }

label_4f598:
    if (read_u16(arg2, 0xac) >= 2U) {
        write_u16(arg2, 0xac, 0);
        if (arg4 != NULL) {
            fwrite("!! Gop.Length shall be set to 0 or 1 for Intra only profile; it will be adjusted!!\r\n",
                   1, 0x54, (FILE *)arg4);
        }
        result_1 += 1;
    }
    if (read_u32(arg2, 0xb0) >= 2U) {
        write_u32(arg2, 0xb0, 0);
        if (arg4 != NULL) {
            fwrite("!! Gop.uFreqIDR shall be set to 0 or 1 for Intra only profile; it will be adjusted!!\r\n",
                   1, 0x56, (FILE *)arg4);
        }
        result_1 += 1;
    }
    v0_29 = read_s32(arg2, 0x1c);
    if ((uint32_t)v0_29 >> 0x18 != 0U) {
        goto label_4f618;
    }

label_4f618:
    {
        int32_t v1_8 = read_s32(arg2, 0x30);

        write_u8(arg2, 0x50, 3);
        write_s8(arg2, 0x37, 0);
        write_s8(arg2, 0x36, 0);
        if ((v1_8 & 1) != 0) {
            write_u32(arg2, 0x30, (uint32_t)(v1_8 & 0xfffffffe));
        }
    }
    a0_6 = v0_29 & 0xff;
    if ((v0_29 & 0xff) >= 0x64) {
        goto label_4f718;
    }
    {
        int32_t v0_30 = read_s8(arg2, 0x38);
        int32_t v1_12 = read_s8(arg2, 0x39);

        write_u8(arg2, 0x50, 2);
        if (v1_12 != v0_30) {
            write_s8(arg2, 0x39, (int8_t)v0_30);
            if (arg4 != NULL) {
                fwrite("!! The Specified CrQpOffset is not allowed; it will be adjusted!!\r\n",
                       1, 0x43, (FILE *)arg4);
            }
            result_1 += 1;
        }
        if (read_s32(arg1, 0x10c) != 0) {
            write_u32(arg1, 0x10c, 0);
            if (arg4 != NULL) {
                fwrite("!! The specified ScalingList is not allowed; it will be adjusted!!\r\n",
                       1, 0x44, (FILE *)arg4);
            }
            result_1 += 1;
        }
        {
            int32_t v0_32 = read_s32(arg2, 0x10);

            if (((uint32_t)v0_32 >> 8 & 0xfU) != 1U) {
                write_u32(arg2, 0x10, (uint32_t)((v0_32 & 0xf0ff) | 0x100));
                if (arg4 != NULL) {
                    fwrite("!! The specified ChromaMode and Profile are not allowed; they will be adjusted!!\r\n",
                           1, 0x52, (FILE *)arg4);
                }
                v0_29 = read_s32(arg2, 0x1c);
                result_1 += 1;
                a0_6 = v0_29 & 0xff;
            }
        }
    }

label_4f718:
    if (a0_6 == 0x42) {
label_50114:
        if (read_s32(arg2, 0x54) == 1) {
            write_u32(arg2, 0x54, 0);
            if (arg4 != NULL) {
                fwrite("!! CABAC encoding is not allowed in baseline profile; CAVLC will be used instead !!\r\n",
                       1, 0x55, (FILE *)arg4);
            }
            v0_29 = read_s32(arg2, 0x1c);
            result_1 += 1;
        }
    }
    if (read_u8(arg2, 0x3a) != 0U) {
        if (arg4 != NULL) {
            fwrite("!! In AVC CuQPDeltaDepth is not allowed; it will be adjusted !!\r\n",
                   1, 0x41, (FILE *)arg4);
        }
        write_u8(arg2, 0x3a, 0);
        result_1 += 1;
    }
    v0_36 = read_s32(arg1, 0xf8);
    if ((v0_36 & 0x10) != 0 && v0_36 != -1) {
        if (arg4 != NULL) {
            fwrite("!! AVC does not contain Content Light Level SEIs; disabling it !!\r\n",
                   1, 0x43, (FILE *)arg4);
        }
        v0_36 = read_s32(arg1, 0xf8);
        result_1 += 1;
        write_u32(arg1, 0xf8, (uint32_t)(v0_36 & 0xffffffef));
    }

label_4f7c4:
    v0_93 = read_s32(arg2, 0xa8);
    if (v0_93 == 9) {
        goto label_4fd58;
    }

label_4f7d8:
    if ((v0_93 & 4) != 0) {
        uint32_t v0_39 = read_u16(arg2, 0xac);
        int32_t v1_16 = (int32_t)read_u8(arg2, 0xae) + 1;

        if (v1_16 == 0) {
            __builtin_trap();
        }
        if ((int32_t)(v0_39 % (uint32_t)v1_16) != 0) {
            if (v1_16 == 0) {
                __builtin_trap();
            }
            write_u16(arg2, 0xac,
                      (uint16_t)((((int32_t)v0_39 + v1_16 - 1) / v1_16) *
                                 v1_16));
            if (arg4 != NULL) {
                fwrite("!! The specified Gop.Length value in pyramidal gop mode is not reachable, value will be adjusted !!\r\n",
                       1, 0x65, (FILE *)arg4);
            }
            result_1 += 1;
        }
    }

label_4fd58:
    {
        uint32_t v1_34 = read_u16(arg2, 0xac);

        if ((uint32_t)(v1_34 - 1) >= 4U) {
            int16_t v1_35 = 4;

            if ((int32_t)v1_34 < 5) {
                v1_35 = 1;
            }
            write_u16(arg2, 0xac, (uint16_t)v1_35);
            if (arg4 != NULL) {
                fwrite("!! The specified Gop.Length value in low delay B mode is not allowed, value will be adjusted !!\r\n",
                       1, 0x61, (FILE *)arg4);
            }
            v0_93 = read_s32(arg2, 0xa8);
            result_1 += 1;
            goto label_4f7d8;
        }
    }
    if ((read_s32(arg1, 0x118) - 1U) < 2U) {
        v0_99 = read_s8(arg2, 0x80);
        if (v0_99 >= 0) {
            uint32_t v1_39 = read_u8(arg2, 0x1f);

            if (v1_39 == 1U) {
                if ((uint32_t)(read_s8(arg2, 0x39) + v0_99 + read_s8(arg2, 0x37) -
                               3) >= 0x2eU) {
                    write_s8(arg2, 0x37, 0);
                    write_s8(arg2, 0x39, 0);
                    if (arg4 != NULL) {
                        fwrite("!!With this QPControlMode, this CrQPOffset cannot be set for this SliceQP!!\r\n",
                               1, 0x4d, (FILE *)arg4);
                    }
                    v0_99 = read_s8(arg2, 0x80);
                    result_1 += 1;
                }
                if ((uint32_t)(read_s8(arg2, 0x38) + v0_99 + read_s8(arg2, 0x36) -
                               3) >= 0x2eU) {
                    write_s8(arg2, 0x36, 0);
                    write_s8(arg2, 0x38, 0);
                    if (arg4 != NULL) {
                        fwrite("!!With this QPControlMode, this CbQPOffset cannot be set for this SliceQP!!\r\n",
                               1, 0x4d, (FILE *)arg4);
                    }
                    result_1 += 1;
                    result_1 += 1;
                }
            } else if (v1_39 == 0U) {
                if ((uint32_t)(read_s8(arg2, 0x39) + v0_99 - 3) >= 0x2eU) {
                    write_s8(arg2, 0x39, 0);
                    if (arg4 != NULL) {
                        fwrite("!!With this QPControlMode, this CrQPOffset cannot be set for this SliceQP!!\r\n",
                               1, 0x4d, (FILE *)arg4);
                    }
                    v0_99 = read_s8(arg2, 0x80);
                    result_1 += 1;
                }
                if ((uint32_t)(read_s8(arg2, 0x38) + v0_99 - 3) >= 0x2eU) {
                    write_s8(arg2, 0x38, 0);
                    if (arg4 != NULL) {
                        fwrite("!!With this QPControlMode, this CbQPOffset cannot be set for this SliceQP!!\r\n",
                               1, 0x4d, (FILE *)arg4);
                    }
                    result_1 += 1;
                }
            }
        }
    }
    if (read_u32(arg2, 0xb8) != 0U && read_u8(arg2, 0xb4) == 0U) {
        write_u8(arg2, 0xb4, 1);
        if (arg4 != NULL) {
            fwrite("!! Enabling long term references as a long term frequency is provided !!\r\n",
                   1, 0x4a, (FILE *)arg4);
        }
        result_1 += 1;
    }
    if ((read_u32(arg2, 0xa8) & 4U) != 0U && read_u8(arg2, 0xb4) != 0U) {
label_4ff88:
        write_u8(arg2, 0xb4, 0);
        write_u32(arg2, 0xb8, 0);
        if (arg4 != NULL) {
            fwrite("!! Long Term reference are not allowed with PYRAMIDAL GOP, it will be adjusted !!\r\n",
                   1, 0x53, (FILE *)arg4);
        }
        s6_1 = read_s32(arg2, 0x1c);
        v0_52 = (uint32_t)s6_1 >> 0x18;
        result_1 += 1;
        if (v0_52 != 1U) {
            goto label_4f8c8;
        }
    } else {
label_4f8b4:
        s6_1 = read_s32(arg2, 0x1c);
        v0_52 = (uint32_t)s6_1 >> 0x18;
        if (v0_52 == 1U) {
            goto label_4ffe0;
        }
    }

label_4f8c8:
    if (v0_52 == 0U) {
        int32_t s5_5 =
            ((int32_t)(read_u16(arg2, 0x04) + 0xf) >> 4) *
            ((int32_t)(read_u16(arg2, 0x06) + 0xf) >> 4);
        int32_t v0_56 = AL_sSettings_GetCpbVclFactor(s6_1);
        uint32_t lo_3;
        int32_t s7_1;
        int32_t s4_1;
        int32_t v0_59_local;
        int32_t v0_60;
        int32_t v0_61;

        if (v0_56 == 0) {
            __builtin_trap();
        }
        lo_3 = (read_u32(arg2, 0x7c) - 1 + (uint32_t)v0_56) / (uint32_t)v0_56;
        s7_1 = AL_DPBConstraint_GetMaxDPBSize(arg2) * s5_5;
        s4_1 = AL_AVC_GetLevelFromFrameSize(s5_5);
        v0_59_local = AL_AVC_GetLevelFromMBRate(read_u16(arg2, 0x74) * s5_5);
        if (v0_59_local >= s4_1) {
            s4_1 = v0_59_local;
        }
        v0_60 = AL_AVC_GetLevelFromBitrate((int32_t)lo_3);
        if (v0_60 >= s4_1) {
            s4_1 = v0_60;
        }
        v0_61 = AL_AVC_GetLevelFromDPBSize(s7_1);
        if (v0_61 >= s4_1) {
            s4_1 = v0_61;
        }
        s4_2 = s4_1 & 0xff;
    } else {
label_4ffe0:
        int32_t s4_6 = read_u16(arg2, 0x04) * read_u16(arg2, 0x06);
        int32_t v0_106 = AL_sSettings_GetCpbVclFactor(s6_1);
        int32_t s5_6 = 1;
        int32_t s5_7;
        uint32_t lo_4;
        int32_t v0_107;
        uint32_t fp_5;
        int32_t v0_108;
        int32_t s6_4;
        int32_t v0_109;
        int32_t s5_9;
        int32_t v0_110;
        int32_t s4_7;
        int32_t v0_111;
        int32_t v0_112;

        if ((s6_1 & (int32_t)0xff0000ffU) == 0x1000004 &&
            (s6_1 & (int32_t)0xff0080ffU) != 0x1008004) {
            s5_6 = 2;
        }
        s5_7 = v0_106 * s5_6;
        if (s5_7 == 0) {
            __builtin_trap();
        }
        lo_4 = (read_u32(arg2, 0x7c) - 1 + (uint32_t)s5_7) / (uint32_t)s5_7;
        v0_107 = AL_DPBConstraint_GetMaxDPBSize(arg2);
        fp_5 = read_u8(arg2, 0x3c);
        v0_108 = AL_HEVC_GetLevelFromFrameSize(s4_6);
        s6_4 = read_u16(arg2, 0x74) * s4_6;
        v0_109 = AL_HEVC_GetLevelFromPixRate(s6_4);
        s5_9 = v0_108;
        if (v0_109 >= v0_108) {
            s5_9 = v0_109;
        }
        v0_110 =
            AL_HEVC_GetLevelFromBitrate((int32_t)lo_4, read_u8(arg2, 0x21));
        s4_7 = s5_9;
        if (v0_110 >= s5_9) {
            s4_7 = v0_110;
        }
        v0_111 = (int32_t)AL_HEVC_GetLevelFromDPBSize(v0_107, s6_4);
        if (v0_111 >= s4_7) {
            s4_7 = v0_111;
        }
        v0_112 = AL_HEVC_GetLevelFromTileCols((int32_t)fp_5);
        if (v0_112 >= s4_7) {
            s4_7 = v0_112;
        }
        s4_2 = s4_7 & 0xff;
    }

label_4f990:
    if (s4_2 != 0xff) {
        if (read_u8(arg2, 0x20) < (uint32_t)s4_2) {
            if (AL_sSettings_CheckLevel(read_s32(arg2, 0x1c), s4_2) == 0) {
                if (arg4 != NULL) {
                    fwrite("!! The specified configuration requires a level too high for the IP encoder!!\r\n",
                           1, 0x4f, (FILE *)arg4);
                }
                result_1 += 1;
                write_u8(arg2, 0x20, (uint8_t)s4_2);
            } else {
                if (arg4 != NULL) {
                    fwrite("!! The specified Level is too low and will be adjusted !!\r\n",
                           1, 0x3b, (FILE *)arg4);
                }
                result_1 += 1;
                write_u8(arg2, 0x20, (uint8_t)s4_2);
            }
        }
        if (read_u8(arg2, 0x1f) == 1U) {
label_502b0:
            if (read_u8(arg2, 0x20) < 0x28U && read_u8(arg2, 0x21) != 0U) {
                write_u8(arg2, 0x21, 0);
            }
        }
        if (read_s32(arg2, 0x68) != 0) {
            int32_t s4_8 = read_s32(arg2, 0x7c);
            int32_t s5_10 = read_s32(arg2, 0x70);
            int32_t v0_115 = AL_sSettings_GetMaxCPBSize(arg2);
            uint64_t lo_5 = (uint64_t)(uint32_t)s5_10 * (uint64_t)(uint32_t)s4_8;
            uint32_t v0_116 = (uint32_t)(lo_5 / 0x3e8ULL);
            uint32_t v1_51 = (uint32_t)(lo_5 % 0x3e8ULL);

            if (read_s32(arg2, 0x68) != 0 &&
                (v1_51 != 0U || (uint32_t)v0_115 < v0_116)) {
                if (arg4 != NULL) {
                    fwrite("!! Warning specified CPBSize is higher than the Max CPBSize allowed for this level and will be adjusted !!\r\n",
                           1, 0x6c, (FILE *)arg4);
                }
                result_1 += 1;
                write_u32(arg2, 0x70,
                          (uint32_t)(((uint64_t)(uint32_t)v0_115 * 0x3e8ULL) /
                                     (uint32_t)read_s32(arg2, 0x7c)));
            }
        }
        v0_65 = read_s32(arg2, 0x70);
        if ((uint32_t)v0_65 < read_u32(arg2, 0x6c)) {
            if (arg4 != NULL) {
                fwrite("!! Warning specified InitialDelay is bigger than CPBSize and will be adjusted !!\r\n",
                       1, 0x52, (FILE *)arg4);
            }
            write_u32(arg2, 0x6c, read_u32(arg2, 0x70));
            result_1 += 1;
        }
        if (read_u8(arg2, 0x1f) == 1U) {
            int32_t s4_10 = read_u8(arg2, 0x3c);

            AL_CoreConstraint_Init(var_40, 0x2faf0800, 0xa, 0xd48, 0, 0xa20);
            if (s4_10 == 0) {
                s4_10 = (int32_t)AL_CoreConstraint_GetExpectedNumberOfCores(
                    var_40, read_u16(arg2, 0x04), read_u16(arg2, 0x06),
                    read_u16(arg2, 0x74) * 0x3e8, read_u16(arg2, 0x76));
            }
            if (s4_10 >= 2) {
                uint32_t s4_4 = read_u16(arg2, 0x40);
                uint32_t v0_90 = (uint32_t)GetHevcMaxTileRow(read_u8(arg2, 0x20));

                if (v0_90 < s4_4) {
                    int16_t v0_91;

                    if (s4_4 == 0U) {
                        v0_91 = 1;
                    } else {
                        if ((int32_t)s4_4 < (int32_t)v0_90) {
                            v0_90 = s4_4;
                        }
                        v0_91 = (int16_t)(v0_90 & 0xffffU);
                    }
                    write_u16(arg2, 0x40, (uint16_t)v0_91);
                    if (arg4 != NULL) {
                        fwrite("!!With this Configuration, this NumSlices cannot be set!!\r\n",
                               1, 0x3b, (FILE *)arg4);
                    }
                    result_1 += 1;
                }
            }
        }
    } else {
        if (arg4 != NULL) {
            fwrite("!! The specified configuration requires a level too high for the IP encoder!!\r\n",
                   1, 0x4f, (FILE *)arg4);
        }
        return -1;
    }
    if (read_u32(arg2, 0xbc) == 2U) {
        write_u32(arg2, 0x30, read_u32(arg2, 0x30) | 0x40U);
    }
    if (read_u8(arg1, 0xb5) != 0U) {
        int32_t v0_68 = read_s32(arg1, 0xa8);

        if (v0_68 != 2 && v0_68 != 8) {
            write_u8(arg1, 0xb5, 0);
            if (arg4 != NULL) {
                fwrite("!! LTRC is not allowed in this configuration, it is disabled !!\r\n",
                       1, 0x41, (FILE *)arg4);
            }
        }
    }
    if (read_s32(arg2, 0x68) == 0 && read_s8(arg2, 0x80) < 0) {
        write_u8(arg2, 0x80, 0x1e);
    }
    if ((read_u32(arg2, 0x2c) & 0x8000U) != 0U) {
        uint32_t v1_25 = read_u16(arg2, 0x66);
        uint32_t a0_18 = 0xfeU;
        uint32_t v0_74;
        int32_t s3_1;
        int32_t v0_75;
        int32_t v1_26;

        if ((int32_t)v1_25 < 0xff) {
            a0_18 = v1_25;
        }
        v0_74 = a0_18;
        if (v0_74 == 0U) {
            v0_74 = 1;
        }
        s3_1 = (int32_t)(v0_74 & 0xffU);
        if (v1_25 != (uint32_t)s3_1) {
            if (arg4 != NULL) {
                fwrite("!! SRD threshold range is [1:254], value will be adjusted !!\r\n",
                       1, 0x3e, (FILE *)arg4);
            }
            write_u16(arg2, 0x66, (uint16_t)s3_1);
        }
        v0_75 = read_s32(arg2, 0xa0);
        if (v0_75 == 0) {
            v1_26 = read_s32(arg2, 0xa4);
            {
                int32_t a0_19 = read_s32(arg2, 0x9c);

                v0_75 = v1_26;
                write_s32(arg2, 0xa0, v1_26);
                if (a0_19 == 0) {
                    goto label_4fd30;
                }
            }
        } else if (read_s32(arg2, 0x9c) == 0) {
            v1_26 = read_s32(arg2, 0xa4);
label_4fd30:
            write_s32(arg2, 0x9c, v0_75);
        } else {
            v1_26 = read_s32(arg2, 0xa4);
        }
        if (v0_75 == 0) {
            write_s32(arg2, 0xa0, read_s32(arg2, 0x9c));
            v1_26 = read_s32(arg2, 0xa4);
        }
        result = result_1;
        if (v1_26 == 0) {
            write_s32(arg2, 0xa4, read_s32(arg2, 0xa0));
        }
        return result;
    }
    return result_1;
}

uint32_t AL_DPBConstraint_GetMaxRef_DefaultGopMngr(int32_t *arg1, int32_t arg2)
{
    int32_t v1_9 = *arg1;

    if (v1_9 == 9) {
        char v0_5 = 2;

        if (arg2 == 0) {
            v0_5 = 2;
        }
        if (read_u16(arg1, 4) >= 2U) {
            v0_5 = 3;
        }
        return (uint32_t)(uint8_t)(v0_5 + (read_u8(arg1, 0xc) > 0U ? 1 : 0));
    }
    if (read_u8(arg1, 6) == 0U) {
        char v0 = 2;

        if ((v1_9 & 1) == 0) {
            v0 = 1;
        }
        return (uint32_t)(uint8_t)(v0 + (read_u8(arg1, 0xc) > 0U ? 1 : 0));
    }
    return (uint32_t)(uint8_t)(2 + (read_u8(arg1, 0xc) > 0U ? 1 : 0));
}

int32_t AL_DPBConstraint_GetMaxRef_GopMngrCustom(void *arg1, int32_t arg2)
{
    uint32_t t0 = read_u8(arg1, 6);
    uint32_t v1 = 3;
    char result;
    char a2_2;
    char v0_1 = 0;

    if (t0 < 3U) {
        result = 2;
        a2_2 = 0;
    } else {
        while (1) {
            v1 = (uint32_t)(uint8_t)(((uint8_t)(v1 << 1)) + 1);
            a2_2 = (char)(v0_1 + 1);
            if (t0 < v1) {
                break;
            }
            v0_1 = a2_2;
        }
        result = (char)(v0_1 + 3);
        if (arg2 == 0) {
            result = (char)(a2_2 + 3);
        }
        if (read_u8(arg1, 0xc) == 0U) {
            return result;
        }
        return result + 1;
    }
    if (arg2 == 0) {
        result = (char)(a2_2 + 3);
    }
    if (read_u8(arg1, 0xc) == 0U) {
        return result;
    }
    return result + 1;
}

int32_t AL_GetGopMngrType(int32_t arg1)
{
    int32_t result = 0;

    if (arg1 == 0x10) {
        return 0;
    }
    if ((arg1 & 8) == 0) {
        result = 1;
    }
    if ((arg1 & 4) == 0) {
        if ((arg1 & 2) != 0) {
            return 0;
        }
        return 2;
    }
    return result;
}

int32_t AL_DPBConstraint_GetMaxDPBSize(void *arg1)
{
    int32_t v0 = AL_GetGopMngrType(read_s32(arg1, 0xa8));
    uint32_t s1 = read_u8(arg1, 0x1f);
    char result;

    if (v0 == 0) {
        result = (char)AL_DPBConstraint_GetMaxRef_DefaultGopMngr(
            (int32_t *)((uint8_t *)arg1 + 0xa8), (int32_t)s1);
    } else if (v0 == 1) {
        result = (char)AL_DPBConstraint_GetMaxRef_GopMngrCustom(
            (uint8_t *)arg1 + 0xa8, (int32_t)s1);
    } else {
        result = 0;
    }
    if (s1 != 1U) {
        return result;
    }
    return result + 1;
}

int32_t AL_ParamConstraints_CheckResolution(int32_t arg1, int32_t arg2,
                                            int32_t arg3, int32_t arg4)
{
    (void)arg1;

    if ((arg3 & 1) != 0 && (uint32_t)(arg2 - 1) < 2U) {
        return 1;
    }
    if (((uint32_t)arg4 & 1U) == 0U) {
        return 0;
    }
    if (arg2 != 1) {
        return 0;
    }
    return 2;
}

int32_t AL_ParamConstraints_CheckLFBetaOffset(void)
{
    return 1;
}

int32_t AL_ParamConstraints_CheckLFTcOffset(void)
{
    return 1;
}
