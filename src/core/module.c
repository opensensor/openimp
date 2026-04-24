#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>

#include "core/module.h"

typedef struct ModuleQueueNode {
    int32_t value;
    struct ModuleQueueNode *next;
} ModuleQueueNode;

int IMP_Log_Get_Option(void); /* forward decl, ported by T<N> later */
void imp_log_fun(int level, int option, int type, ...); /* forward decl, ported by T<N> later */
int VBMUnLockFrame(void *frame); /* forward decl, ported by T<N> later */
uint64_t system_gettime(int clock_type); /* forward decl, ported by T<N> later */

static inline sem_t *module_sem_inputs(Module *module)
{
    return (sem_t *)module->sem_inputs;
}

static inline sem_t *module_sem_outputs(Module *module)
{
    return (sem_t *)module->sem_outputs;
}

static inline pthread_t *module_thread_handle(Module *module)
{
    return (pthread_t *)module->thread;
}

static inline pthread_mutex_t *module_mutex(Module *module)
{
    return (pthread_mutex_t *)module->mutex;
}

static inline ModuleQueueNode **module_queue_free_ptr(Module *module)
{
    /* raw queue pointer at offset 0xd8 */
    return (ModuleQueueNode **)((char *)module + 0xd8);
}

static inline ModuleQueueNode **module_queue_read_ptr(Module *module)
{
    /* raw queue pointer at offset 0xdc */
    return (ModuleQueueNode **)((char *)module + 0xdc);
}

static inline int32_t *module_queue_count_ptr(Module *module)
{
    /* raw queue count at offset 0xe0 */
    return (int32_t *)((char *)module + 0xe0);
}

static inline int32_t *module_in_process_ptr(Module *module)
{
    /* raw in-process flag at offset 0x120 */
    return (int32_t *)((char *)module + 0x120);
}

static inline int32_t *module_dispatch_count_ptr(Module *module)
{
    /* raw dispatch counter at offset 0x124 */
    return (int32_t *)((char *)module + 0x124);
}

static inline void **module_update_dispatch_fn_slot(Module *module)
{
    /* raw callback slot at offset 0x50 */
    return (void **)((char *)module + 0x50);
}

static inline uint32_t module_time_lo(uint64_t value)
{
    return (uint32_t)value;
}

static inline uint32_t module_time_hi(uint64_t value)
{
    return (uint32_t)(value >> 32);
}

static inline uint64_t module_make_u64(uint32_t lo, uint32_t hi)
{
    return ((uint64_t)hi << 32) | lo;
}

static size_t module_append_str(char *buf, size_t pos, size_t cap, const char *str)
{
    while (*str != '\0' && pos < cap) {
        buf[pos++] = *str++;
    }
    return pos;
}

static size_t module_append_hex(char *buf, size_t pos, size_t cap, uintptr_t value)
{
    static const char hex[] = "0123456789abcdef";
    int shift;

    pos = module_append_str(buf, pos, cap, "0x");
    for (shift = (int)(sizeof(uintptr_t) * 8) - 4; shift >= 0 && pos < cap; shift -= 4) {
        buf[pos++] = hex[(value >> shift) & 0xfU];
    }
    return pos;
}

static size_t module_append_dec(char *buf, size_t pos, size_t cap, int32_t value)
{
    char tmp[16];
    size_t used = 0;
    uint32_t uval;

    if (value < 0) {
        if (pos < cap) {
            buf[pos++] = '-';
        }
        uval = (uint32_t)(-(value + 1)) + 1U;
    } else {
        uval = (uint32_t)value;
    }

    do {
        tmp[used++] = (char)('0' + (uval % 10U));
        uval /= 10U;
    } while (uval != 0 && used < sizeof(tmp));

    while (used > 0 && pos < cap) {
        buf[pos++] = tmp[--used];
    }
    return pos;
}

static void module_trace_quick_state(const char *tag, Module *module, int32_t message,
                                     int32_t result, void *dispatch_fn, void *notify_fn)
{
    int fd = open("/dev/kmsg", O_WRONLY);
    char buf[256];
    size_t len = 0;

    if (fd < 0) {
        return;
    }

    len = module_append_str(buf, len, sizeof(buf), "libimp/BIND: ");
    len = module_append_str(buf, len, sizeof(buf), tag);
    len = module_append_str(buf, len, sizeof(buf), " module=");
    len = module_append_hex(buf, len, sizeof(buf), (uintptr_t)module);
    len = module_append_str(buf, len, sizeof(buf), " msg=");
    len = module_append_hex(buf, len, sizeof(buf), (uintptr_t)(uint32_t)message);
    len = module_append_str(buf, len, sizeof(buf), " result=");
    len = module_append_dec(buf, len, sizeof(buf), result);
    len = module_append_str(buf, len, sizeof(buf), " dispatch=");
    len = module_append_hex(buf, len, sizeof(buf), (uintptr_t)dispatch_fn);
    len = module_append_str(buf, len, sizeof(buf), " notify=");
    len = module_append_hex(buf, len, sizeof(buf), (uintptr_t)notify_fn);
    if (len < sizeof(buf)) {
        buf[len++] = '\n';
    }

    write(fd, buf, len);
    close(fd);
}

static void module_trace(const char *fmt, ...)
{
    int fd = open("/dev/kmsg", O_WRONLY);
    if (fd < 0) return;

    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n > 0) write(fd, buf, (size_t)n);
    close(fd);
}

static void module_user_trace(const char *fmt, ...)
{
    char buf[256];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    imp_log_fun(4, IMP_Log_Get_Option(), 2, "Module",
        "/home/user/git/proj/sdk-lv3/src/imp/core/module.c", 0x50,
        "notify_observers", "%s\n", buf);
}

int32_t remove_observer(Module *arg1, Module *arg2)
{
    Module **slot = (Module **)((char *)arg1 + 0x14);
    int32_t i = 0;

    while (i != 5) {
        Module *slot_module = *slot;
        slot = (Module **)((char *)slot + 8);
        if (slot_module == arg2) {
            void *slot_base = (char *)arg1 + ((i << 3) + 0x14);
            *(Module **)slot_base = NULL;
            *(void **)((char *)slot_base + 4) = NULL;
            *(int32_t *)((char *)arg1 + 0x3c) -= 1;
            *(Module **)((char *)arg2 + 0x10) = NULL;
            return 0;
        }
        i += 1;
    }

    printf("%s %s error: Can't find observer: %s\n", arg1->name, "remove_observer", arg2->name);
    return -1;
}

int32_t add_observer(Module *arg1, Module *arg2, void *arg3)
{
    Module **slot = (Module **)((char *)arg1 + 0x14);
    int32_t i = 0;

    while (i != 5) {
        Module *slot_module = *slot;
        slot = (Module **)((char *)slot + 8);
        if (slot_module == NULL) {
            void *slot_base = (char *)arg1 + ((i << 3) + 0x14);
            *(Module **)slot_base = arg2;
            *(void **)((char *)slot_base + 4) = arg3;
            *(int32_t *)((char *)arg1 + 0x3c) += 1;
            *(Module **)((char *)arg2 + 0x10) = arg1;
            module_trace("libimp/BIND: add_observer src=%s(%p) dst=%s(%p) slot=%d outptr=%p count=%d\n",
                         arg1->name, arg1, arg2->name, arg2, i, arg3,
                         *(int32_t *)((char *)arg1 + 0x3c));
            return 0;
        }
        i += 1;
    }

    printf("%s %s error: Can't add more observers\n", arg1->name, "add_observer");
    return -1;
}

static int32_t update(Module *arg1, void *arg2)
{
    int32_t value = *(int32_t *)arg2;

    module_trace("libimp/BIND: update module=%s(%p) frame_slot=%p frame_val=%p\n",
                 arg1 ? arg1->name : "?",
                 arg1,
                 arg2,
                 (void *)(uintptr_t)value);

    if (value != 0) {
        sem_wait(module_sem_outputs(arg1));
        pthread_mutex_lock(module_mutex(arg1));

        ModuleQueueNode *node = *module_queue_free_ptr(arg1);
        ModuleQueueNode *next = node->next;
        int32_t count = *module_queue_count_ptr(arg1) + 1;

        node->value = value;
        *module_queue_free_ptr(arg1) = next;
        *module_queue_count_ptr(arg1) = count;

        module_trace("libimp/BIND: update queued module=%s(%p) node=%p next=%p queued=%d val=%p\n",
                     arg1 ? arg1->name : "?",
                     arg1,
                     node,
                     next,
                     count,
                     (void *)(uintptr_t)value);

        pthread_mutex_unlock(module_mutex(arg1));
        sem_post(module_sem_inputs(arg1));
    }

    return 0;
}

int32_t notify_observers(Module *arg1, void *arg2)
{
    int32_t count = *(int32_t *)((char *)arg1 + 0x3c);
    (void)arg2;

    module_trace("libimp/BIND: notify src=%s(%p) count=%d frame=%p\n",
                 arg1->name, arg1, count, arg2);

    if (count <= 0) {
        module_user_trace("notify src=%s count=0 frame=%p",
                          arg1 ? arg1->name : "?", arg2);
        return 0;
    }

    void **slot = (void **)((char *)arg1 + 0x18);
    int32_t i = 0;

    do {
        int32_t *frame_slot = (int32_t *)*slot;
        Module *observer = *(Module **)((char *)slot - 4);
        module_trace("libimp/BIND: notify slot=%d src=%s obs=%p(%s) frame_slot=%p frame_val=%p\n",
                     i, arg1->name, observer,
                     observer ? observer->name : "?",
                     frame_slot,
                     frame_slot ? (void *)(uintptr_t)*frame_slot : NULL);

        if (frame_slot != NULL && *frame_slot != 0) {
            int32_t (*update_cb)(Module *module, void *frame) =
                *(int32_t (**)(Module *, void *))((char *)observer + 0x4c);
            int32_t update_rc;

            module_user_trace("notify src=%s slot=%d obs=%s frame=%p cb=%p",
                              arg1->name, i,
                              observer ? observer->name : "?",
                              (void *)(uintptr_t)*frame_slot, update_cb);

            update_rc = update_cb(observer, frame_slot);
            if (update_rc < 0) {
                VBMUnLockFrame(*(void **)frame_slot);
                i += 1;

                {
                    int32_t *frame = *(int32_t **)frame_slot;
                    imp_log_fun(6, IMP_Log_Get_Option(), 2, "Module",
                        "/home/user/git/proj/sdk-lv3/src/imp/core/module.c", 0x54,
                        "notify_observers",
                        "%s update failed, frame->pool_idx=%d, frame->index = %d\n",
                        observer->name, frame[1], frame[0]);
                    module_user_trace("notify src=%s slot=%d obs=%s rc=%d",
                                      arg1->name, i - 1, observer->name, update_rc);
                    count = *(int32_t *)((char *)arg1 + 0x3c);
                }
            } else {
                module_user_trace("notify src=%s slot=%d obs=%s rc=%d",
                                  arg1->name, i, observer ? observer->name : "?",
                                  update_rc);
                count = *(int32_t *)((char *)arg1 + 0x3c);
                i += 1;
            }
        } else {
            i += 1;
        }

        slot = (void **)((char *)slot + 8);
    } while (i < count);

    return 0;
}

void *module_thread(void *arg1)
{
    Module *module = (Module *)arg1;
    uint32_t prev_active_hi = 0;
    uint32_t prev_gap_lo = 0;
    uint32_t prev_gap_hi = 0;
    uint32_t prev_active_lo = 0;

    prctl(0xf, module->name);
    {
        uint32_t zero_block[6];
        memset(zero_block, 0, sizeof(zero_block));
        prev_gap_lo = zero_block[0];
        prev_gap_hi = zero_block[1];
        prev_active_lo = zero_block[2];
        prev_active_hi = zero_block[3];
    }

    while (1) {
        *module_in_process_ptr(module) = 0;
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
        sem_wait(module_sem_inputs(module));
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
        *module_in_process_ptr(module) = 1;
        pthread_mutex_lock(module_mutex(module));

        int32_t count = *module_queue_count_ptr(module);
        int32_t message;

        module_trace("libimp/BIND: module_thread wake module=%s(%p) queued=%d read=%p free=%p dispatch=%p notify=%p\n",
                     module->name, module, count,
                     *module_queue_read_ptr(module),
                     *module_queue_free_ptr(module),
                     *module_update_dispatch_fn_slot(module),
                     module->notify_fn);

        if (count != 0) {
            ModuleQueueNode *node = *module_queue_read_ptr(module);
            message = node->value;
            *module_queue_read_ptr(module) = node->next;
            *module_queue_count_ptr(module) = count - 1;
            module_trace("libimp/BIND: module_thread pop module=%s(%p) node=%p msg=%p remain=%d next=%p\n",
                         module->name, module, node, (void *)(uintptr_t)message,
                         count - 1, node ? node->next : NULL);
            pthread_mutex_unlock(module_mutex(module));
            sem_post(module_sem_outputs(module));

            if (*module_update_dispatch_fn_slot(module) == NULL) {
                module_trace("libimp/BIND: module_thread skip-dispatch module=%s(%p) dispatch=NULL\n",
                             module->name, module);
                continue;
            }
        } else {
            int32_t var_88 = 0;
            void *var_84 = NULL;
            int32_t var_80 = 0;
            int32_t var_7c = 0;
            int32_t var_78 = 0;
            int32_t var_74 = 0;

            message = 0;
            imp_log_fun(5, IMP_Log_Get_Option(), 2, "Module",
                "/home/user/git/proj/sdk-lv3/src/imp/core/module.c", 0x73, "read_msg",
                "read msg error: Empty\n",
                var_88, var_84, var_80, var_7c, var_78, var_74, NULL);
            pthread_mutex_unlock(module_mutex(module));
            if (*module_update_dispatch_fn_slot(module) == NULL) {
                module_trace("libimp/BIND: module_thread empty module=%s(%p) dispatch=NULL\n",
                             module->name, module);
                continue;
            }
        }

        int32_t trace_enabled = access("/tmp/module", 0);
        uint32_t gap_start_lo = 0;
        uint32_t gap_start_hi = 0;
        uint32_t active_start_lo = 0;
        uint32_t active_start_hi = 0;
        int32_t callback_result;

        if (trace_enabled == 0) {
            uint64_t gap_start = system_gettime(0);
            gap_start_lo = module_time_lo(gap_start);
            gap_start_hi = module_time_hi(gap_start);

            {
                uint64_t active_start = system_gettime(2);
                active_start_lo = module_time_lo(active_start);
                active_start_hi = module_time_hi(active_start);
            }

            {
                uint64_t gap_elapsed;
                uint64_t active_elapsed;

                if ((int32_t)prev_gap_hi <= 0 && ((int32_t)prev_gap_hi != 0 || prev_gap_lo == 0)) {
                    gap_elapsed = 0;
                } else {
                    uint32_t gap_elapsed_lo = gap_start_lo - prev_gap_lo;
                    uint32_t gap_elapsed_hi = gap_start_hi - prev_gap_hi - (gap_start_lo < gap_elapsed_lo ? 1U : 0U);
                    gap_elapsed = module_make_u64(gap_elapsed_lo, gap_elapsed_hi);
                }

                if ((int32_t)prev_active_hi <= 0 && ((int32_t)prev_active_hi != 0 || prev_active_lo == 0)) {
                    active_elapsed = 0;
                } else {
                    uint32_t active_elapsed_lo = active_start_lo - prev_active_lo;
                    uint32_t active_elapsed_hi = active_start_hi - prev_active_hi -
                        (active_start_lo < active_elapsed_lo ? 1U : 0U);
                    active_elapsed = module_make_u64(active_elapsed_lo, active_elapsed_hi);
                }

                imp_log_fun(3, IMP_Log_Get_Option(), 2, "Module",
                    "/home/user/git/proj/sdk-lv3/src/imp/core/module.c", 0xb2,
                    "module_thread",
                    "line:%d, [%s] gap elapsed:%lluusec, active:%lluusec\n",
                    0xb2, module->name,
                    (unsigned long long)gap_elapsed,
                    (unsigned long long)active_elapsed);
            }

            callback_result =
                (*(int32_t (**)(Subject *, int32_t))((char *)module + 0x50))(&module->subject_node, message);

            {
                uint64_t gap_end = system_gettime(0);
                prev_gap_lo = module_time_lo(gap_end);
                prev_gap_hi = module_time_hi(gap_end);
            }

            {
                uint64_t active_end = system_gettime(2);
                prev_active_lo = module_time_lo(active_end);
                prev_active_hi = module_time_hi(active_end);
            }
        } else {
            module_trace("libimp/BIND: module_thread dispatch module=%s(%p) subject=%p msg=%p cb=%p\n",
                         module->name, module, &module->subject_node,
                         (void *)(uintptr_t)message,
                         *(void **)((char *)module + 0x50));
            callback_result =
                (*(int32_t (**)(Subject *, int32_t))((char *)module + 0x50))(&module->subject_node, message);
        }

        module_trace_quick_state("module_thread dispatch-return",
                                 module, message, callback_result,
                                 *module_update_dispatch_fn_slot(module),
                                 module->notify_fn);
        module_trace("libimp/BIND: module_thread dispatch-done module=%s(%p) msg=%p result=%d notify=%p\n",
                     module->name, module, (void *)(uintptr_t)message, callback_result, module->notify_fn);
        if (callback_result >= 0) {
            if (module->notify_fn == NULL) {
                module_trace_quick_state("module_thread notify-null",
                                         module, message, callback_result,
                                         *module_update_dispatch_fn_slot(module),
                                         NULL);
            } else {
                module_trace_quick_state("module_thread notify-enter",
                                         module, message, callback_result,
                                         *module_update_dispatch_fn_slot(module),
                                         module->notify_fn);
            }
            module_trace("libimp/BIND: module_thread notify module=%s(%p) result=%p\n",
                         module->name, module, (void *)(intptr_t)callback_result);
            if (module->notify_fn != NULL) {
                module->notify_fn(module, (void *)(intptr_t)callback_result);
                module_trace_quick_state("module_thread notify-return",
                                         module, message, callback_result,
                                         *module_update_dispatch_fn_slot(module),
                                         module->notify_fn);
            }
        }

        {
            void (*cleanup_cb)(Subject *, int32_t) =
                *(void (**)(Subject *, int32_t))((char *)module + 0x54);
            if (cleanup_cb != NULL) {
                cleanup_cb(&module->subject_node, message);
            }
        }

        if (trace_enabled == 0) {
            int32_t end_option = IMP_Log_Get_Option();
            uint32_t gap_elapsed_lo = prev_gap_lo - gap_start_lo;
            uint32_t gap_elapsed_hi = prev_gap_hi - gap_start_hi - (prev_gap_lo < gap_elapsed_lo ? 1U : 0U);
            uint32_t active_elapsed_lo = prev_active_lo - active_start_lo;
            uint32_t active_elapsed_hi =
                prev_active_hi - active_start_hi - (prev_active_lo < active_elapsed_lo ? 1U : 0U);
            uint64_t gap_elapsed = module_make_u64(gap_elapsed_lo, gap_elapsed_hi);
            uint64_t active_elapsed = module_make_u64(active_elapsed_lo, active_elapsed_hi);

            imp_log_fun(3, end_option, 2, "Module",
                "/home/user/git/proj/sdk-lv3/src/imp/core/module.c", 0xcb,
                "module_thread",
                "line:%d [%s] elapsed:%lluusec, active:%lluusec\n",
                0xcb, module->name,
                (unsigned long long)gap_elapsed,
                (unsigned long long)active_elapsed);
        }

        *module_dispatch_count_ptr(module) += 1;
    }
}

Module *AllocModule(char *arg1, int32_t arg2)
{
    Module *result = (Module *)calloc((size_t)(arg2 + 0x128), 1);

    if (result == NULL) {
        puts("malloc module error");
        return NULL;
    }

    size_t name_len = strlen(arg1);
    if ((int32_t)name_len >= 0x11) {
        printf("The length of name %d is longer that %d\n", (int32_t)name_len, 0x10);
        free(result);
        return NULL;
    }

    memcpy(result, arg1, name_len + 1);

    {
        int32_t *i = (int32_t *)((char *)result + 0x14);
        *(int32_t *)((char *)result + 0x3c) = 0;
        do {
            *i = 0;
            i[1] = 0;
            i = &i[2];
        } while ((char *)i != (char *)result + 0x3c);
    }

    *(int32_t *)((char *)result + 0x10) = 0;
    sem_init(module_sem_inputs(result), 0, 0);
    sem_init(module_sem_outputs(result), 0, 0x10);

    if (pthread_mutex_init(module_mutex(result), NULL) != 0) {
        puts("pthread_mutex_init() error");
    }

    if (pthread_create(module_thread_handle(result), NULL, module_thread, result) == 0) {
        /* raw queue head link at offset 0x5c */
        *(ModuleQueueNode **)((char *)result + 0x5c) = (ModuleQueueNode *)((char *)result + 0x60);
        *(void **)((char *)result + 0x40) = (void *)add_observer;
        result->subject_head = NULL;

        {
            void *i = (char *)result + 0x68;
            *(void **)((char *)result + 0x44) = (void *)remove_observer;
            *(void **)((char *)result + 0x48) = (void *)notify_observers;
            *(void **)((char *)result + 0x4c) = (void *)update;
            do {
                *(int32_t *)((char *)i - 8) = 0;
                *(void **)((char *)i - 4) = i;
                i = (char *)i + 8;
            } while ((char *)i != (char *)result + 0xe0);
        }

        /* raw queue sentinel/read/free pointers at offsets 0xd4/0xd8/0xdc */
        *(ModuleQueueNode **)((char *)result + 0xd4) = (ModuleQueueNode *)((char *)result + 0x58);
        *(ModuleQueueNode **)((char *)result + 0xd8) = (ModuleQueueNode *)((char *)result + 0x58);
        *(ModuleQueueNode **)((char *)result + 0xdc) = (ModuleQueueNode *)((char *)result + 0x58);
        *module_queue_count_ptr(result) = 0;
        return result;
    }

    printf("module_thread module->name=%s create error:%s\n",
        result->name, strerror(errno));
    free(result);
    return NULL;
}

void FreeModule(Module *arg1)
{
    if (pthread_cancel(*module_thread_handle(arg1)) != 0) {
        printf("%s: pthread_cancel error: %s\n", "FreeModule", strerror(errno));
    }

    pthread_join(*module_thread_handle(arg1), NULL);
    pthread_mutex_destroy(module_mutex(arg1));
    sem_destroy(module_sem_inputs(arg1));
    sem_destroy(module_sem_outputs(arg1));

    if (*(int32_t *)((char *)&arg1->subject_node + 0x20) != 0 &&
        *(int32_t *)((char *)&arg1->subject_node + 0x20) != 6) {
        int32_t count = *module_queue_count_ptr(arg1);
        int32_t i = 0;

        if (count > 0) {
            do {
                ModuleQueueNode *node = *module_queue_read_ptr(arg1);
                int32_t value = node->value;
                ModuleQueueNode *next = node->next;

                *module_queue_count_ptr(arg1) = count - 1;
                i += 1;
                *module_queue_read_ptr(arg1) = next;
                VBMUnLockFrame((void *)(intptr_t)value);
                count = *module_queue_count_ptr(arg1);
            } while (i < count);
        }
    }

    free(arg1);
}

int32_t BindObserverToSubject(Module *arg1, Module *arg2, void *arg3)
{
    if (arg1 == NULL) {
        return puts("module_src is NULL!");
    }

    if (arg2 == NULL) {
        return puts("module_dst is NULL!");
    }

    module_trace("libimp/BIND: BindObserverToSubject src=%s(%p) dst=%s(%p) outptr=%p add_fn=%p count=%d\n",
                 arg1->name, arg1, arg2->name, arg2, arg3,
                 *(void **)((char *)arg1 + 0x40),
                 *(int32_t *)((char *)arg1 + 0x3c));
    return (*(int32_t (**)(Module *, Module *, void *))((char *)arg1 + 0x40))(arg1, arg2, arg3);
}

int32_t UnBindObserverFromSubject(Module *arg1, Module *arg2)
{
    if (arg1 == NULL) {
        return puts("module_src is NULL!");
    }

    if (arg2 == NULL) {
        return puts("module_dst is NULL!");
    }

    return (*(int32_t (**)(Module *, Module *))((char *)arg1 + 0x44))(arg1, arg2);
}

int32_t flush_module_tree_sync(Module *arg1)
{
    int32_t retries = 0x100;
    int32_t nr_module_empty_msg;
    int32_t sem_outputs_val;

    while (1) {
        sem_getvalue(module_sem_outputs(arg1), &sem_outputs_val);
        retries -= 1;
        sem_getvalue(module_sem_inputs(arg1), &nr_module_empty_msg);

        int32_t pending = 0x10 - sem_outputs_val;
        if (pending < nr_module_empty_msg) {
            pending = nr_module_empty_msg;
        }

        if (pending == 0) {
            int32_t result = *module_in_process_ptr(arg1);

            if (result == 0) {
                Module **slot = (Module **)((char *)arg1 + 0x14);
                int32_t i = 0;

                if (*(int32_t *)((char *)arg1 + 0x3c) > 0) {
                    do {
                        Module *observer = *slot;
                        slot = (Module **)((char *)slot + 8);
                        if (flush_module_tree_sync(observer) < 0) {
                            result = -1;
                            imp_log_fun(6, IMP_Log_Get_Option(), 2, "Module",
                                "/home/user/git/proj/sdk-lv3/src/imp/core/module.c", 0x162,
                                "flush_module_tree_sync",
                                "flush module:%s observer-%d error\n", arg1->name, i);
                            break;
                        }
                        i += 1;
                    } while (i < *(int32_t *)((char *)arg1 + 0x3c));
                }

                return result;
            }
        }

        if (retries == 0) {
            break;
        }

        usleep(0x4e20);
    }

    {
        int32_t option = IMP_Log_Get_Option();
        int32_t sem_outputs_now;
        int32_t sem_inputs_now;
        int32_t pending_now;

        sem_getvalue(module_sem_outputs(arg1), &sem_outputs_now);
        sem_getvalue(module_sem_inputs(arg1), &sem_inputs_now);
        pending_now = 0x10 - sem_outputs_now;
        if (pending_now < sem_inputs_now) {
            pending_now = sem_inputs_now;
        }

        imp_log_fun(6, option, 2, "Module",
            "/home/user/git/proj/sdk-lv3/src/imp/core/module.c", 0x159,
            "flush_module_tree_sync",
            "flush module:%s timeout, nr_module_empty_msg=%d in_process=%d\n",
            arg1->name, pending_now, *module_in_process_ptr(arg1));
    }

    return -1;
}

char *dump_ob_modules(Module *arg1, int32_t arg2)
{
    char str[0x40];
    memset(str, 0, sizeof(str));

    int32_t bytes = arg2 << 2;
    int32_t indent_len;

    if (bytes <= 0) {
        indent_len = 0;
    } else {
        uint32_t count = (uint32_t)(bytes - 1) >> 2;
        char *i = &str[1];

        do {
            *(i - 1) = 0x20;
            *i = 0x20;
            i[1] = 0x20;
            i[2] = 0x20;
            i = &i[4];
        } while (i != str + (count << 2) + 5);

        indent_len = (int32_t)((count + 1) << 2);
    }

    int32_t observer_count = *(int32_t *)((char *)arg1 + 0x3c);
    char *result = str + indent_len;
    *result = '\0';

    if (observer_count > 0) {
        Module **slot = (Module **)((char *)arg1 + 0x14);
        int32_t i = 0;
        char *(*recurse)(Module *arg1, int32_t arg2) = dump_ob_modules;

        do {
            Module *observer = *slot;
            slot = (Module **)((char *)slot + 8);

            imp_log_fun(4, IMP_Log_Get_Option(), 2, "System",
                "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0x1b9,
                "dump_ob_modules", "%s%s-%d->%s\n",
                str, arg1->name, i, observer->name);
            i += 1;
            result = recurse(observer, arg2 + 1);
        } while (i < *(int32_t *)((char *)arg1 + 0x3c));
    }

    return result;
}

/* ---- Public observer wrappers ------------------------------------------
 *
 * Forwarded declarations were sprinkled throughout the port without an
 * owning file. The stock binary dispatches via Module.observer_add_fn
 * (offset 0x40) and notify_fn (0x48). These wrappers route through the
 * vtable so the calling sites keep working.
 */

int32_t add_observer_to_module(Module *module, Observer *observer)
{
    if (module == NULL || observer == NULL) return -1;
    int32_t (*fn)(Module *, Observer *) = module->observer_add_fn;
    if (fn == NULL) return -1;
    return fn(module, observer);
}

int32_t remove_observer_from_module(Module *src, Module *dst)
{
    if (src == NULL || dst == NULL) return -1;
    /* The live vendor remover compares the raw destination module pointer
     * stored in the slot array at +0x14; it does not expect an Observer
     * wrapper. Passing a stack probe here guarantees the unbind miss and
     * leaves stale slot entries behind during teardown. */
    int32_t (*fn)(Module *, Module *) = (int32_t (*)(Module *, Module *))src->observer_remove_fn;
    if (fn == NULL) return -1;
    return fn(src, dst);
}
