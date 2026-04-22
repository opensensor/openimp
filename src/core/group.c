#include <stdint.h>
#include <string.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>

#include "core/globals.h"
#include "core/module.h"

int IMP_Log_Get_Option(void); /* forward decl, ported by T<N> later */
void imp_log_fun(int level, int option, int type, ...); /* forward decl, ported by T<N> later */
int32_t VBMReleaseFrame(int chn, void *frame); /* forward decl, ported by T<N> later */
Module *AllocModule(char *arg1, int32_t arg2); /* forward decl, ported by T<N> later */
void FreeModule(Module *arg1); /* forward decl, ported by T<N> later */
extern uint32_t g_block_info_addr; /* forward decl, ported by T<N> later */

static void group_trace(const char *fmt, ...)
{
    int fd = open("/dev/kmsg", O_WRONLY);
    if (fd < 0) return;

    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n > 0) write(fd, buf, (size_t)n);
    close(fd);
}

int32_t group_update(Subject *arg1, int32_t *arg2)
{
    prctl(0xf, "group_update");

    {
        group_trace("libimp/GRP: group_update subject=%p module=%p frame_slot=%p frame=%p cb=%p\n",
                    arg1,
                    arg1 ? arg1->next : NULL,
                    arg2,
                    arg2 ? (void *)(uintptr_t)*arg2 : NULL,
                    arg1 ? *(void **)((char *)arg1 + 0x1c) : NULL);
        int32_t result = (*(int32_t (**)(Subject *, int32_t *))((char *)arg1 + 0x1c))(arg1, arg2);
        group_trace("libimp/GRP: group_update result=%d observers=%d\n",
                    result,
                    arg1 && arg1->next ? *(int32_t *)((char *)arg1->next + 0x3c) : -1);

        if (result < 0 || *(int32_t *)((char *)arg1->next + 0x3c) == 0) {
            int32_t device_id = *(int32_t *)((char *)arg1->data + 0x20);
            void *frame = arg2 ? (void *)(uintptr_t)*arg2 : NULL;

            if (device_id != 0 && device_id != 6 &&
                frame != NULL && VBMReleaseFrame(device_id, frame) < 0) {
                imp_log_fun(6, IMP_Log_Get_Option(), 2, "Group",
                    "/home/user/git/proj/sdk-lv3/src/imp/core/group.c", 0x51,
                    "group_update",
                    "%s(%d)VBMReleaseFrame failed, group->module->name = %s, frame->pool_idx=%d, frame->index=%d\n",
                    "group_update", 0x51,
                    arg1 && arg1->next ? ((Module *)arg1->next)->name : "?",
                    arg2[1], *arg2);
            }
        }

        return result;
    }
}

Module *get_module(int32_t arg1, int32_t arg2)
{
    return g_modules[arg1][arg2];
}

int32_t get_module_location(Module *arg1, int32_t *arg2, int32_t *arg3)
{
    int32_t var_t2 = 0;
    Module **var_t1 = &g_modules[0][0];

    while (1) {
        Module **var_v0_1 = var_t1;
        int32_t var_v1_1 = 0;

        while (1) {
            Module *var_a3_1 = *var_v0_1;
            var_v0_1 = &var_v0_1[1];

            if (var_a3_1 == arg1) {
                if (arg2 != 0) {
                    *arg2 = var_t2;
                }

                if (arg3 == 0) {
                    return 0;
                }

                *arg3 = var_v1_1;
                return 0;
            }

            var_v1_1 += 1;
            if (var_v1_1 == 6) {
                break;
            }
        }

        var_t2 += 1;
        var_t1 = &var_t1[6];
        if (var_t2 == 6) {
            break;
        }
    }

    return -1;
}

void *clear_all_modules(void)
{
    uint8_t *i = (uint8_t *)g_modules;

    while (i != (uint8_t *)&g_block_info_addr) {
        memset(i, 0, 0x18);
        i += 0x18;
    }

    return i;
}

Subject *create_group(int32_t arg1, int32_t arg2, char *arg3, void *arg4)
{
    Module *var_v0 = AllocModule(arg3, 0x20);

    if (var_v0 == NULL) {
        return NULL;
    }

    var_v0->observer_cb = arg4;
    var_v0->channel = arg2;
    *(int32_t (**)(Subject *, int32_t *))((char *)var_v0 + 0x50) = group_update;
    var_v0->subject_node.next = (Subject *)var_v0;
    g_modules[arg1][arg2] = var_v0;
    return &var_v0->subject_node;
}

int32_t destroy_group(Subject *arg1, int32_t arg2)
{
    g_modules[arg2][*(int32_t *)((char *)arg1 + 8)] = NULL;
    FreeModule((Module *)arg1->next);
    return 0;
}
