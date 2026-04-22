#include <stdint.h>

extern void __assert(const char *expr, const char *file, int line, const char *func)
    __attribute__((noreturn));

uint32_t AL_GetAllocSize_EncReference(int32_t arg1, int32_t arg2, char arg3, int32_t arg4, char arg5);
uint32_t AL_GetRecPitch(int32_t arg1, int32_t arg2);
int32_t AL_GetEncoderFbcMapPitch(int32_t arg1);
int32_t AL_GetEncoderFbcMapSize(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4);

int32_t AL_EncRecBuffer_FillPlaneDesc(int32_t *arg1, int32_t arg2, int32_t arg3, char arg4, char arg5)
{
    int32_t v0 = *arg1;
    int32_t *s0 = arg1;
    uint32_t s2 = (uint32_t)(uint8_t)arg4;

    if (v0 == 1) {
        s0[1] = (int32_t)AL_GetAllocSize_EncReference(arg2, arg3, (char)s2, 0, 0);

        {
            int32_t v0_7 = (int32_t)AL_GetRecPitch((int32_t)s2, arg2);
            s0[2] = v0_7;
            return v0_7;
        }
    }

    if (v0 != 0) {
        int32_t v0_3;

        if (v0 == 2) {
            v0_3 = (int32_t)AL_GetAllocSize_EncReference(arg2, arg3, (char)s2, 0, 0);
label_4cdfc:
            s0[1] = v0_3;

            {
                int32_t v0_4 = AL_GetEncoderFbcMapPitch(arg2);
                s0[2] = v0_4;
                return v0_4;
            }
        }

        if (v0 == 3) {
            int32_t s2_1 = 8;

            if ((uint32_t)(uint8_t)arg5 != 0) {
                s2_1 = 0x10;
            }

            v0_3 = (int32_t)AL_GetAllocSize_EncReference(arg2, arg3, (char)s2, 0, 0) +
                   AL_GetEncoderFbcMapSize(0, arg2, arg3, s2_1);
            goto label_4cdfc;
        }

        __assert("0",
                 "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common_enc/EncRecBuffer.c",
                 0x2c, "AL_EncRecBuffer_FillPlaneDesc");
    }

    arg1[1] = 0;

    {
        int32_t v0_5 = (int32_t)AL_GetRecPitch((int32_t)s2, arg2);
        s0[2] = v0_5;
        return v0_5;
    }
}
