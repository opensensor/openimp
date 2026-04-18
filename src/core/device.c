#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int32_t IMP_Log_Get_Option(void); /* forward decl, ported by T<N> later */
int32_t imp_log_fun(int32_t level, int32_t option, int32_t type, ...); /* forward decl, ported by T<N> later */
int32_t destroy_group(int32_t group_num, int32_t device_id); /* forward decl, ported by T<N> later */

void *alloc_device(const char *arg1, size_t arg2)
{
    void *result = calloc(arg2 + 0x40, 1);

    if (result == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Device",
            "/home/user/git/proj/sdk-lv3/src/imp/core/device.c", 0x1d,
            "alloc_device", "malloc device error\n");
        return NULL;
    }

    size_t v0_1 = strlen(arg1);

    if (v0_1 < 0x21) {
        memcpy(result, arg1, v0_1 + 1);
        return result;
    }

    imp_log_fun(6, IMP_Log_Get_Option(), 2, "Device",
        "/home/user/git/proj/sdk-lv3/src/imp/core/device.c", 0x24,
        "alloc_device", "The length of name %d is longer that %d\n", v0_1, 0x20,
        NULL);
    free(result);
    return NULL;
}

void free_device(void *arg1)
{
    if (arg1 == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Device",
            "/home/user/git/proj/sdk-lv3/src/imp/core/device.c", 0x34,
            "free_device", "device is NULL\n", NULL);
        return;
    }

    /* raw device group count at offset 0x24 */
    int32_t v1 = *(int32_t *)((char *)arg1 + 0x24);

    if (v1 > 0) {
        /* raw device group array starts at offset 0x28 */
        int32_t *s0_1 = (int32_t *)((char *)arg1 + 0x28);
        int32_t s1_1 = 0;

        do {
            int32_t v0_1 = *s0_1;
            s1_1 += 1;

            if (v0_1 != 0) {
                /* raw device id at offset 0x20 */
                destroy_group(v0_1, *(int32_t *)((char *)arg1 + 0x20));
            }

            v1 = *(int32_t *)((char *)arg1 + 0x24);
            *s0_1 = 0;
            s0_1 = &s0_1[1];
        } while (s1_1 < v1);
    }

    free(arg1);
}
