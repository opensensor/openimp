#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

uint32_t JsonCheck = 0xffffffff;

int json_func(void);

void *imp_malloc(size_t arg1, const char *arg2, int32_t arg3)
{
    void *result;

    result = malloc(arg1);
    if (JsonCheck == 0) {
        printf("[malloc memory]function: %s; line: %d\t\n[malloc infos] size: %d, address: 0x%x\n",
            arg2, arg3, arg1, result);
    }

    return result;
}

void *imp_calloc(size_t arg1, size_t arg2, const char *arg3, int32_t arg4)
{
    void *result;

    result = calloc(arg1, arg2);
    if (JsonCheck == 0) {
        printf("[calloc memory]function: %s; line: %d\t\n", arg3, arg4);
        printf("[calloc infos] size: %d, address: 0x%x\n", arg1 * arg2, result);
    }

    return result;
}

void *imp_realloc(void *arg1, size_t arg2, const char *arg3, int32_t arg4)
{
    void *result;

    result = realloc(arg1, arg2);
    if (JsonCheck == 0) {
        printf("[realloc memory]function: %s; line: %d\t\n", arg3, arg4);
        printf("[realloc infos] size: %d, address: 0x%x\n", arg2, result);
    }

    return result;
}

uint32_t imp_free(void *arg1, const char *arg2, int32_t arg3)
{
    uint32_t result;

    free(arg1);
    result = JsonCheck;
    if (result == 0) {
        return printf("[free memory]function: %s; line: %d\t\n", arg2, arg3);
    }

    return result;
}

int32_t sys_wrap(void)
{
    int32_t result;

    result = json_func();
    JsonCheck = (uint32_t)result;
    return result;
}
