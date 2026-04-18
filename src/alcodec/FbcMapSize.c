#include <stdint.h>

extern void __assert(const char *assertion, const char *file, int line,
                     const char *function) __attribute__((noreturn));

static void getTileDimension(void)
{
    __assert("0",
             "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common/FbcMapSize.c",
             0x13, "getTileDimension");
}

int32_t GetFbcMapPitch(int32_t arg1, int32_t arg2)
{
    int32_t v0 = 0x40;

    if (arg2 != 2) {
        if (arg2 == 3)
            return (int32_t)((0x20u * (((uint32_t)(arg1 + 0xfff)) >> 0xc)) & 0xffe0u);

        getTileDimension();
    }

    return (int32_t)(((uint32_t)v0 * (((uint32_t)(arg1 + 0xfff)) >> 0xc)) & 0xffe0u);
}

int32_t GetFbcMapSize(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4)
{
    int32_t result;

    if (arg4 == 2)
        goto label_4c260;

    if (arg4 != 3) {
        getTileDimension();
        goto label_4c260;
    }

    {
        int32_t v1 = arg3 + 3;

        if (arg3 >= 0)
            v1 = arg3;

        result = (int32_t)((0x20u * (((uint32_t)(arg2 + 0xfff)) >> 0xc)) & 0xffe0u) * (v1 >> 2);

        if (arg1 == 1)
            return result + (int32_t)((uint32_t)result >> 1);

        if (arg1 == 2)
            return result << 1;

        return result;
    }

label_4c260:
    {
        int32_t v1_3 = arg3 + 3;

        if (arg3 >= 0)
            v1_3 = arg3;

        result = (int32_t)((0x40u * (((uint32_t)(arg2 + 0xfff)) >> 0xc)) & 0xffe0u) * (v1_3 >> 2);

        if (arg1 == 1)
            return result + (int32_t)((uint32_t)result >> 1);
    }

    if (arg1 == 2)
        return result << 1;

    return result;
}
