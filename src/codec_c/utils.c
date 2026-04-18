#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

void c_free(void *arg1);

int32_t c_log_default(const char *arg1, int32_t arg2, const char *arg3, va_list arg4)
{
    const char *a3;

    if (arg2 == 1) {
        a3 = "warning";
    } else if (arg2 < 2) {
        if (arg2 != 0)
            a3 = "unknown";
        else
            a3 = "error";
    } else if (arg2 == 2) {
        a3 = "info";
    } else if (arg2 != 3) {
        a3 = "unknown";
    } else {
        a3 = "debug";
    }

    fprintf(stderr, "%s[%s]: ", arg1, a3);
    return vfprintf(stderr, arg3, arg4);
}

int32_t c_log(int32_t arg1, const char *arg2, ...)
{
    int32_t result;
    va_list arg_8;

    result = arg1 < 3 ? 1 : 0;
    va_start(arg_8, arg2);
    if (result == 0) {
        va_end(arg_8);
        return result;
    }

    result = c_log_default("icommon", arg1, arg2, arg_8);
    va_end(arg_8);
    return result;
}

int64_t c_mdate(void)
{
    struct timespec var_10;
    int32_t v1_2;

    clock_gettime(1, &var_10);
    v1_2 = (int32_t)var_10.tv_nsec / 0x3e8;
    return (int64_t)v1_2 + ((int64_t)(int32_t)var_10.tv_sec * 0xf4240LL);
}

void *c_malloc(size_t arg1, size_t arg2)
{
    int32_t v0;
    void *result;

    v0 = getpagesize();
    if (v0 < (int32_t)arg2) {
        c_log(0, "align over pagesize:%d\n", v0);
        return 0;
    }

    result = memalign(arg2, arg1);
    if (result != 0)
        return result;

    c_log(0, "memalign size:%d (aligned %d) failed\n", (int32_t)arg1, (int32_t)arg2);
    return result;
}

void *c_malloc_check_zero(size_t arg1, size_t arg2, const char *arg3, ...)
{
    va_list arg_c;
    void *result;

    va_start(arg_c, arg3);
    result = c_malloc(arg1, arg2);
    if (result == 0) {
        c_log_default("icommon", 0, arg3, arg_c);
        va_end(arg_c);
        abort();
        c_free(0);
        return 0;
    }

    *(uint8_t *)result = 0;
    *((uint8_t *)result + 1) = 0;
    *((uint8_t *)result + 2) = 0;
    *((uint8_t *)result + 3) = 0;
    va_end(arg_c);
    return result;
}

void c_free(void *arg1)
{
    if (arg1 == 0)
        return;
    free(arg1);
}

int32_t c_virt_to_phys(int32_t arg1)
{
    return arg1;
}

int32_t c_phys_to_virt(int32_t arg1)
{
    return arg1;
}

int32_t c_align(int32_t arg1, int32_t arg2)
{
    return (-arg2) & (arg2 - 1 + arg1);
}

int32_t c_clip3(int32_t arg1, int32_t arg2, int32_t arg3)
{
    int32_t v0;

    v0 = arg2;
    if (arg1 >= arg2) {
        v0 = arg3;
        if (arg3 >= arg1)
            return arg1;
    }
    return v0;
}

double c_clip3f(double arg1, double arg2, double arg3)
{
    double f0;

    f0 = arg2;
    if (!(arg1 < arg2)) {
        f0 = arg1;
        if (arg3 < arg1)
            f0 = arg3;
    }
    return f0;
}

int32_t c_median(int32_t arg1, int32_t arg2, int32_t arg3)
{
    int32_t v1_1;
    int32_t v1;
    int32_t a1;
    int32_t a2;
    int32_t a1_1;
    int32_t a0_1;

    v1_1 = arg1 - arg2;
    v1 = (v1_1 >> 0x1f) & v1_1;
    a1 = arg2 + v1;
    a2 = a1 - arg3;
    a1_1 = a1 - ((a2 >> 0x1f) & a2);
    a0_1 = arg1 - v1 - a1_1;
    return ((a0_1 >> 0x1f) & a0_1) + a1_1;
}

void c_reduce_fraction(int32_t *arg1, int32_t *arg2)
{
    uint32_t a2;
    uint32_t a3_1;
    uint32_t v0_1;
    uint32_t v1_1;

    a3_1 = (uint32_t)*arg1;
    if (a3_1 == 0)
        return;

    v1_1 = (uint32_t)*arg2;
    if (v1_1 == 0)
        return;

    if (v1_1 == 0)
        __builtin_trap();
    v0_1 = a3_1 % v1_1;
    if (v0_1 == 0)
        goto label_34a30;

    while (1) {
        if (v0_1 == 0)
            __builtin_trap();
        a2 = v1_1 % v0_1;
        if (a2 == 0)
            break;
        v1_1 = v0_1;
        v0_1 = a2;
    }

    v1_1 = v0_1;
label_34a30:
    if (v1_1 == 0)
        __builtin_trap();
    *arg1 = (int32_t)(a3_1 / v1_1);
    if (v1_1 == 0)
        __builtin_trap();
    *arg2 = (int32_t)((uint32_t)*arg2 / v1_1);
}

uint32_t c_reduce_fraction64(uint32_t *arg1, uint32_t *arg2)
{
    uint32_t result;
    uint32_t s0;
    uint32_t s1;
    uint32_t s4;
    uint32_t s5;
    uint32_t s6_1;
    uint32_t s7_1;
    uint64_t lhs;
    uint64_t rhs;
    uint64_t rem;
    uint64_t div;

    s5 = arg1[0];
    s4 = arg1[1];
    result = s5 | s4;
    if (result == 0)
        return result;

    s7_1 = arg2[0];
    s6_1 = arg2[1];
    result = s7_1 | s6_1;
    if (result == 0)
        return result;

    lhs = ((uint64_t)s4 << 0x20) | s5;
    rhs = ((uint64_t)s6_1 << 0x20) | s7_1;
    rem = lhs % rhs;
    s1 = (uint32_t)rem;
    s0 = (uint32_t)(rem >> 0x20);
    if ((s0 | s1) == 0) {
        s1 = s7_1;
        s0 = s6_1;
        goto label_34b28;
    }

    while (1) {
        lhs = ((uint64_t)s6_1 << 0x20) | s7_1;
        rhs = ((uint64_t)s0 << 0x20) | s1;
        rem = lhs % rhs;
        s7_1 = s1;
        s6_1 = s0;
        if (rem == 0)
            break;
        s1 = (uint32_t)rem;
        s0 = (uint32_t)(rem >> 0x20);
    }

    s1 = s7_1;
    s0 = s6_1;

label_34b28:
    div = (((uint64_t)s4 << 0x20) | s5) / (((uint64_t)s0 << 0x20) | s1);
    arg1[0] = (uint32_t)div;
    arg1[1] = (uint32_t)(div >> 0x20);
    div = (((uint64_t)arg2[1] << 0x20) | arg2[0]) / (((uint64_t)s0 << 0x20) | s1);
    result = (uint32_t)div;
    arg2[0] = (uint32_t)div;
    arg2[1] = (uint32_t)(div >> 0x20);
    return result;
}

int32_t c_save_to_file(int32_t arg1, const void *arg2, int32_t arg3)
{
    ssize_t v0;

    v0 = write(arg1, arg2, (size_t)arg3);
    if (arg3 == (int32_t)v0)
        return arg3;

    c_log(0, "%s buf:%p size:%d failed:%s\n", "c_save_to_file", arg2, arg3,
          v0 < 0 ? strerror(errno) : "");
    return -1;
}

int32_t fsort64(float *arg1, int32_t arg2, int32_t arg3)
{
    float *a0;
    float *a2;
    float *v1_1;
    float f0;
    float f1;
    int32_t a3;
    int32_t result;
    int32_t s0;
    int32_t s3;
    int32_t v0;

    result = arg2 << 2;
    if (arg2 >= arg3)
        return result;

    s3 = arg3;
    f1 = *(float *)((char *)arg1 + result);
    s0 = arg2;
    v0 = s3;
    a0 = (float *)((char *)arg1 + ((uint32_t)v0 << 2));

label_34cb8:
    f0 = *a0;
    if (!(f0 <= f1))
        goto label_34d24;
    a3 = s0 + 1;
    v0 -= 1;
    if (s0 < v0) {
        a0 = (float *)((char *)a0 - 4);
        goto label_34cb8;
    }

label_34cd8:
    *(float *)((char *)arg1 + ((uint32_t)s0 << 2)) = f1;
    fsort64(arg1, arg2, s0 - 1);
    arg2 = s0 + 1;
    if (arg2 < s3) {
        result = arg2 << 2;
        f1 = *(float *)((char *)arg1 + result);
        s0 = arg2;
        v0 = s3;
        a0 = (float *)((char *)arg1 + ((uint32_t)v0 << 2));
        goto label_34cb8;
    }

    return result;

label_34d24:
    a3 = s0 + 1;
    *(float *)((char *)arg1 + ((uint32_t)s0 << 2)) = f0;
    if (!(a3 < v0)) {
        s0 = a3;
        goto label_34cd8;
    }

    a2 = (float *)((char *)arg1 + ((uint32_t)(s0 + 1) << 2));
    f0 = *a2;
    if (!(f1 < f0)) {
        s0 = a3;
        v0 -= 1;
        *a0 = f0;
        if (s0 < v0) {
            a0 = (float *)((char *)arg1 + ((uint32_t)v0 << 2));
            goto label_34cb8;
        }
        *a2 = f1;
        goto label_34ce4;
    }

    v1_1 = (float *)((char *)arg1 + ((uint32_t)(s0 + 2) << 2));
    s0 = a3 + 1;

label_34d58:
    a2 = v1_1;
    if (s0 == v0)
        goto label_34cd8;
    v1_1 = (float *)((char *)v1_1 + 4);
    f0 = *((float *)((char *)v1_1 - 4));
    if (f1 < f0) {
        s0 += 1;
        goto label_34d58;
    }

    s0 -= 1;
    v0 -= 1;
    *a0 = f0;
    if (s0 < v0) {
        a0 = (float *)((char *)arg1 + ((uint32_t)v0 << 2));
        goto label_34cb8;
    }
    *a2 = f1;

label_34ce4:
    fsort64(arg1, arg2, s0 - 1);
    arg2 = s0 + 1;
    if (arg2 < s3) {
        result = arg2 << 2;
        f1 = *(float *)((char *)arg1 + result);
        s0 = arg2;
        v0 = s3;
        a0 = (float *)((char *)arg1 + ((uint32_t)v0 << 2));
        goto label_34cb8;
    }

    return result;
}
