#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define READ_I32(base, off) (*(int32_t *)((char *)(base) + (off)))
#define WRITE_I32(base, off, val) (*(int32_t *)((char *)(base) + (off)) = (val))
#define WRITE_PTR32(base, off, val) WRITE_I32(base, off, (int32_t)(uintptr_t)(val))

void *buf_list_alloc(int32_t arg1, int32_t arg2, int32_t arg3)
{
    uint32_t s4 = (uint32_t)(arg3 + 3) & 0xfffffffcU;
    int32_t s5 = arg1 * 0x14;
    uint32_t s3 = (uint32_t)(arg2 + 3) & 0xfffffffcU;
    int32_t s0 = arg1 * (int32_t)s4 + s5 + 0x14;
    void *result = calloc((size_t)(arg1 * (int32_t)s3 + s0), 1);

    if (result == NULL) {
        printf("err: (%s:%d)malloc err %s\n",
            "/home/user/git/proj/sdk-lv3/src/imp/audio/abuf.c", 0x40,
            strerror(*__errno_location()));
        return NULL;
    }

    WRITE_I32(result, 0x0, arg1);
    WRITE_I32(result, 0x4, arg2);
    WRITE_I32(result, 0x8, arg3);
    if (arg1 == 0) {
        return result;
    }

    void *v1 = (char *)result + 0x14;
    WRITE_PTR32(result, 0x18, result);
    WRITE_PTR32(result, 0x20, (char *)result + s5 + 0x14);
    WRITE_PTR32(result, 0x24, (char *)result + s0);
    WRITE_I32(result, 0x14, 0);
    WRITE_PTR32(result, 0xc, v1);
    if ((uint32_t)arg1 >= 2U) {
        void *a2 = (char *)result + s5 + 0x14 + s4;
        void *a1 = (char *)result + s0 + s3;
        int32_t a0 = 1;

        do {
            v1 = (char *)v1 + 0x14;
            WRITE_I32(v1, 0x0, a0);
            a0 += 1;
            WRITE_PTR32(v1, 0xc, a2);
            WRITE_PTR32(v1, 0x10, a1);
            WRITE_PTR32(v1, 0x4, result);
            WRITE_I32(v1, 0x8, 0);
            WRITE_PTR32((char *)v1 - 0xc, 0x0, v1);
            a2 = (char *)a2 + s4;
            a1 = (char *)a1 + s3;
        } while (arg1 != a0);

        v1 = (char *)result + s5;
    }

    WRITE_PTR32(result, 0x10, v1);
    return result;
}

int32_t buf_list_free(void *arg1)
{
    if (arg1 == NULL) {
        return printf("warn: (%s:%d)blist == NULL\n",
            "/home/user/git/proj/sdk-lv3/src/imp/audio/abuf.c", 0x63);
    }

    free(arg1);
    return 0;
}

void *buf_list_get_node(int32_t *arg1)
{
    if (arg1 == NULL) {
        printf("err: (%s:%d)blist == NULL\n",
            "/home/user/git/proj/sdk-lv3/src/imp/audio/abuf.c", 0x6d);
        return NULL;
    }

    int32_t v1 = *arg1;
    if (v1 == 0) {
        return NULL;
    }

    if (v1 == 1) {
        int32_t v0_1 = arg1[3];
        arg1[4] = 0;
        arg1[3] = 0;
        *arg1 = 0;
        return (void *)(intptr_t)v0_1;
    }

    void *v0 = (void *)(intptr_t)arg1[3];
    int32_t a1 = *(int32_t *)((char *)v0 + 8);
    *arg1 = v1 - 1;
    arg1[3] = a1;
    return v0;
}

int32_t buf_list_try_get_node(int32_t *arg1)
{
    if (arg1 == NULL) {
        printf("err: (%s:%d)blist == NULL\n",
            "/home/user/git/proj/sdk-lv3/src/imp/audio/abuf.c", 0x87);
        return 0;
    }

    if (*arg1 == 0) {
        return 0;
    }

    return arg1[3];
}

int32_t buf_list_put_node(int32_t *arg1, void *arg2)
{
    if (arg2 == NULL) {
        return printf("err: (%s:%d)n == NULL\n",
            "/home/user/git/proj/sdk-lv3/src/imp/audio/abuf.c", 0x97);
    }

    if (arg1 == NULL) {
        return printf("err: (%s:%d)bl == NULL\n",
            "/home/user/git/proj/sdk-lv3/src/imp/audio/abuf.c", 0x9b);
    }

    int32_t v0 = *arg1;
    *(int32_t *)((char *)arg2 + 8) = 0;
    *(int32_t *)((char *)arg2 + 4) = (int32_t)(intptr_t)arg1;
    if (v0 == 0) {
        arg1[4] = (int32_t)(intptr_t)arg2;
        arg1[3] = (int32_t)(intptr_t)arg2;
        *arg1 = 1;
        return 1;
    }

    *(int32_t *)((char *)(intptr_t)arg1[4] + 8) = (int32_t)(intptr_t)arg2;
    arg1[4] = (int32_t)(intptr_t)arg2;
    *arg1 = v0 + 1;
    return v0 + 1;
}

int32_t buf_list_get_num(int32_t *arg1)
{
    if (arg1 != NULL) {
        return *arg1;
    }

    printf("err: (%s:%d)blist == NULL\n",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/abuf.c", 0xb0);
    return 0;
}

int32_t buf_list_node_get_info(void *arg1)
{
    if (arg1 != NULL) {
        return *(int32_t *)((char *)arg1 + 0xc);
    }

    printf("err: (%s:%d)node == NULL\n",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/abuf.c", 0xbc);
    return 0;
}

int32_t buf_list_node_get_data(void *arg1)
{
    if (arg1 != NULL) {
        return *(int32_t *)((char *)arg1 + 0x10);
    }

    printf("err: (%s:%d)node == NULL\n",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/abuf.c", 0xc8);
    return 0;
}

int32_t buf_list_node_index(int32_t *arg1)
{
    if (arg1 != NULL) {
        return *arg1;
    }

    printf("err: (%s:%d)node == NULL\n",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/abuf.c", 0xd4);
    return -1;
}

int32_t *audio_buf_alloc(int32_t arg1, int32_t arg2, int32_t arg3)
{
    int32_t *result = malloc(8);
    int32_t a2;
    char *a3;

    if (result == NULL) {
        a3 = strerror(*__errno_location());
        a2 = 0xe5;
        goto err;
    }

    void *v0 = buf_list_alloc(arg1, arg2, arg3);
    if (v0 == NULL) {
        a3 = strerror(*__errno_location());
        a2 = 0xea;
        goto err;
    }

    void *v0_1 = buf_list_alloc(0, arg2, arg3);
    if (v0_1 != NULL) {
        result[1] = (int32_t)(uintptr_t)v0_1;
        result[0] = (int32_t)(uintptr_t)v0;
        return result;
    }

    a3 = strerror(*__errno_location());
    a2 = 0xef;

err:
    printf("err: (%s:%d)malloc err %s\n",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/abuf.c", a2, a3);
    return NULL;
}

int32_t audio_buf_free(int32_t *arg1)
{
    void *s0 = arg1;

    if (arg1 == NULL) {
        return printf("warn: (%s:%d)blist == NULL\n",
            "/home/user/git/proj/sdk-lv3/src/imp/audio/abuf.c", 0x119);
    }

    buf_list_free((void *)(intptr_t)arg1[0]);
    buf_list_free((void *)(intptr_t)*(int32_t *)((char *)s0 + 4));
    free(s0);
    return 0;
}

int32_t audio_buf_get_node(int32_t *arg1, int32_t arg2)
{
    if (arg1 == NULL) {
        printf("err: (%s:%d)ab == NULL\n",
            "/home/user/git/proj/sdk-lv3/src/imp/audio/abuf.c", 0x124);
        return 0;
    }

    if (arg2 == 0) {
        return (int32_t)(intptr_t)buf_list_get_node((int32_t *)(intptr_t)*arg1);
    }

    if (arg2 == 1) {
        return (int32_t)(intptr_t)buf_list_get_node((int32_t *)(intptr_t)arg1[1]);
    }

    printf("err: (%s:%d)audio buf type %d\n",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/abuf.c", 0x12e, arg2);
    return 0;
}

int32_t audio_buf_try_get_node(int32_t *arg1, int32_t arg2)
{
    if (arg1 == NULL) {
        printf("err: (%s:%d)ab == NULL\n",
            "/home/user/git/proj/sdk-lv3/src/imp/audio/abuf.c", 0x13a);
        return 0;
    }

    if (arg2 == 0) {
        return buf_list_try_get_node((int32_t *)(intptr_t)*arg1);
    }

    if (arg2 == 1) {
        return buf_list_try_get_node((int32_t *)(intptr_t)arg1[1]);
    }

    printf("err: (%s:%d)audio buf type %d\n",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/abuf.c", 0x144, arg2);
    return 0;
}

int32_t audio_buf_put_node(int32_t *arg1, int32_t arg2, int32_t arg3)
{
    if (arg1 == NULL) {
        return printf("err: (%s:%d)ab == NULL\n",
            "/home/user/git/proj/sdk-lv3/src/imp/audio/abuf.c", 0x150);
    }

    if (arg3 == 0) {
        return buf_list_put_node((int32_t *)(intptr_t)*arg1, (void *)(intptr_t)arg2);
    }

    if (arg3 == 1) {
        return buf_list_put_node((int32_t *)(intptr_t)arg1[1], (void *)(intptr_t)arg2);
    }

    return printf("err: (%s:%d)audio buf type %d\n",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/abuf.c", 0x15a, arg3);
}

int32_t audio_buf_clear(int32_t *arg1)
{
    int32_t a2;
    char *a3;

    if (arg1 == NULL) {
        a3 = strerror(*__errno_location());
        a2 = 0xfe;
        goto err;
    }

    if (*arg1 == 0) {
        a3 = strerror(*__errno_location());
        a2 = 0x103;
        goto err;
    }

    if (arg1[1] != 0) {
        int32_t result;

        while (1) {
            result = audio_buf_get_node(arg1, 1);
            if (result == 0) {
                break;
            }

            audio_buf_put_node(arg1, result, 0);
        }

        return result;
    }

    a3 = strerror(*__errno_location());
    a2 = 0x108;

err:
    return printf("err: (%s:%d)malloc err %s\n",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/abuf.c", a2, a3);
}

int32_t audio_buf_get_num(int32_t *arg1, int32_t arg2)
{
    if (arg1 == NULL) {
        printf("err: (%s:%d)ab == NULL\n",
            "/home/user/git/proj/sdk-lv3/src/imp/audio/abuf.c", 0x166);
        return 0;
    }

    if (arg2 == 0) {
        return buf_list_get_num((int32_t *)(intptr_t)*arg1);
    }

    if (arg2 == 1) {
        return buf_list_get_num((int32_t *)(intptr_t)arg1[1]);
    }

    printf("err: (%s:%d)audio buf type %d\n",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/abuf.c", 0x170, arg2);
    return 0;
}

int32_t audio_buf_node_get_info(void *arg1)
{
    return buf_list_node_get_info(arg1);
}

int32_t audio_buf_node_get_data(void *arg1)
{
    return buf_list_node_get_data(arg1);
}

int32_t audio_buf_node_index(int32_t *arg1)
{
    return buf_list_node_index(arg1);
}
