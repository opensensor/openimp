#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include "core/globals.h"
#include "core/module.h"
#include "imp/imp_common.h"

int IMP_Log_Get_Option(void); /* forward decl, ported by T<N> later */
void imp_log_fun(int level, int option, int type, ...); /* forward decl, ported by T<N> later */
Module *get_module(int32_t arg1, int32_t arg2); /* forward decl, ported by T<N> later */
int32_t get_module_location(Module *arg1, int32_t *arg2, int32_t *arg3); /* forward decl, ported by T<N> later */
int32_t BindObserverToSubject(Module *arg1, Module *arg2, void *arg3); /* forward decl, ported by T<N> later */
int32_t UnBindObserverFromSubject(Module *arg1, Module *arg2); /* forward decl, ported by T<N> later */
char *dump_ob_modules(Module *arg1, int32_t arg2); /* forward decl, ported by T<N> later */

static int32_t soc_id_8648 = -1;
static int32_t cppsr_8647 = -1;
static int32_t subsoctype_8649 = -1;
static int32_t subremark_8651 = -1;
static int fatal_kmsg_fd = -1;

static size_t fatal_append_str(char *buf, size_t pos, size_t cap, const char *str)
{
    while (*str != '\0' && pos < cap) {
        buf[pos++] = *str++;
    }
    return pos;
}

static size_t fatal_append_hex(char *buf, size_t pos, size_t cap, uintptr_t value)
{
    static const char hex[] = "0123456789abcdef";
    int shift;

    pos = fatal_append_str(buf, pos, cap, "0x");
    for (shift = (int)(sizeof(uintptr_t) * 8) - 4; shift >= 0 && pos < cap; shift -= 4) {
        buf[pos++] = hex[(value >> shift) & 0xfU];
    }
    return pos;
}

static const char *fatal_sig_name(int sig)
{
    switch (sig) {
    case SIGSEGV:
        return "SIGSEGV";
    case SIGBUS:
        return "SIGBUS";
    case SIGILL:
        return "SIGILL";
    case SIGABRT:
        return "SIGABRT";
    case SIGFPE:
        return "SIGFPE";
    default:
        return "SIGNAL";
    }
}

static void fatal_signal_handler(int sig, siginfo_t *info, void *ucontext)
{
    char buf[160];
    size_t len = 0;

    (void)ucontext;

    len = fatal_append_str(buf, len, sizeof(buf), "libimp/FATAL: ");
    len = fatal_append_str(buf, len, sizeof(buf), fatal_sig_name(sig));
    if (info != NULL) {
        len = fatal_append_str(buf, len, sizeof(buf), " code=");
        len = fatal_append_hex(buf, len, sizeof(buf), (uintptr_t)(uint32_t)info->si_code);
        len = fatal_append_str(buf, len, sizeof(buf), " addr=");
        len = fatal_append_hex(buf, len, sizeof(buf), (uintptr_t)info->si_addr);
    }
    if (len < sizeof(buf)) {
        buf[len++] = '\n';
    }
    if (fatal_kmsg_fd >= 0) {
        write(fatal_kmsg_fd, buf, len);
    }

    _exit(128 + sig);
}

static __attribute__((constructor)) void install_fatal_signal_handlers(void)
{
    struct sigaction sa;

    if (fatal_kmsg_fd < 0) {
        fatal_kmsg_fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
    }

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = fatal_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;

    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
}

static void sysbind_trace(const char *fmt, ...)
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

static const char *const fmt_str_8579[0x24] = {
    "YUV420Planar",
    "YUYV422",
    "UYVY422",
    "YUV422P",
    "YUV444P",
    "YUV410P",
    "YUV411P",
    "GRAY8",
    "MONOWHITE",
    "MONOBLACK",
    "NV12",
    "NV21",
    "RGB24",
    "BGR24",
    "ARGB",
    "RGBA",
    "ABGR",
    "BGRA",
    "RGB565BE",
    "RGB565LE",
    "RGB555BE",
    "RGB555LE",
    "BGR565BE",
    "BGR565LE",
    "BGR555BE",
    "BGR555LE",
    "0RGB",
    "RGB0",
    "0BGR",
    "BGR0",
    "BGGR8",
    "RGGB8",
    "GBRG8",
    "GRBG8",
};

int32_t regrw(int32_t arg1, int32_t *arg2, int32_t arg3)
{
    int32_t open_flags = 0x12;

    if (arg3 == 0) {
        open_flags = 0x10;
    }

    int32_t fd = open("/dev/mem", open_flags);
    if (fd < 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "System",
            "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0x239,
            "regrw", "open /dev/mem failed\n");
        return -1;
    }

    int32_t page_size = getpagesize();
    int32_t page_offset = (page_size - 1) & arg1;
    int32_t map_len = page_size << (((uint32_t)page_size < (uint32_t)(page_offset + 0x20)) ? 1 : 0);
    int32_t result;

    if (arg3 != 0) {
        void *mapped = mmap(0, (size_t)map_len, 3, 1, fd, (off_t)((-page_size) & arg1));
        if (mapped == (void *)-1) {
            result = -1;
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "System",
                "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0x24b,
                "regrw", "mmap failed\n");
        } else {
            *(int32_t *)((char *)mapped + page_offset) = *arg2;
            result = 0;
            if (munmap(mapped, (size_t)map_len) == -1) {
                imp_log_fun(6, IMP_Log_Get_Option(), 2, "System",
                    "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0x258,
                    "regrw", "munmap failed\n");
            }
        }
    } else {
        void *mapped = mmap(0, (size_t)map_len, 1, 1, fd, (off_t)((-page_size) & arg1));
        if (mapped == (void *)-1) {
            result = -1;
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "System",
                "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0x24b,
                "regrw", "mmap failed\n");
        } else {
            *arg2 = *(int32_t *)((char *)mapped + page_offset);
            result = 0;
            if (munmap(mapped, (size_t)map_len) == -1) {
                imp_log_fun(6, IMP_Log_Get_Option(), 2, "System",
                    "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0x258,
                    "regrw", "munmap failed\n");
            }
        }
    }

    close(fd);
    return result;
}

int32_t is_video_has_simd128_proc(void)
{
    FILE *stream = fopen("/proc/cpuinfo", "r");

    if (stream == NULL) {
        imp_log_fun(3, IMP_Log_Get_Option(), 2, "System",
            "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0x2a4,
            "is_video_has_simd128_proc", "[ignored]fopen %s returns NULL\n",
            "/proc/cpuinfo");
    } else {
        while (1) {
            char str[0x204];
            char *haystack = fgets(str, 0x200, stream);

            if (haystack == NULL) {
                imp_log_fun(3, IMP_Log_Get_Option(), 2, "System",
                    "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0x2ab,
                    "is_video_has_simd128_proc", "[ignored]read %s ret is NULL",
                    "/proc/cpuinfo");
                fclose(stream);
                break;
            }

            if ((int32_t)strlen(str) >= 0x18) {
                char *field = strstr(haystack, "ASEs implemented");

                if (field != NULL && strstr(&field[0x11], "mxu_v2") != NULL) {
                    fclose(stream);
                    return 1;
                }
            }
        }
    }

    return 0;
}

uint64_t system_gettime(int32_t arg1)
{
    struct timespec var_10;
    int32_t clock_id;

    if (arg1 == 1) {
        clock_id = 2;
    } else if (arg1 != 0) {
        if (arg1 != 2) {
            return UINT64_C(0xffffffffffffffff);
        }

        clock_id = 3;
    } else {
        if (clock_gettime(4, &var_10) < 0) {
            return 0;
        }

        {
            uint64_t current = (uint64_t)var_10.tv_sec * 1000000ULL + (uint64_t)(var_10.tv_nsec / 1000);
            return current - timestamp_base;
        }
    }

    if (clock_gettime(clock_id, &var_10) >= 0) {
        uint64_t current = (uint64_t)var_10.tv_sec * 1000000ULL + (uint64_t)(var_10.tv_nsec / 1000);
        return current - timestamp_base;
    }

    return 0;
}

int32_t system_get_localtime(char *arg1, int32_t arg2)
{
    if (arg1 == NULL || arg2 < 0x15) {
        int32_t option = IMP_Log_Get_Option();

        imp_log_fun(6, option, 2, "System",
            "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0xf8,
            "system_get_localtime", "%s buf is null or size less than 21\n",
            "system_get_localtime");
        return -1;
    }

    {
        time_t var_c = time(0);
        struct tm var_38;

        if (localtime_r(&var_c, &var_38) != 0) {
            sprintf(arg1, "%04d%02d%02d%02d%02d%02d",
                var_38.tm_year + 0x76c, var_38.tm_mon + 1, var_38.tm_mday,
                var_38.tm_hour, var_38.tm_min, var_38.tm_sec);
            return 0;
        }
    }

    imp_log_fun(6, IMP_Log_Get_Option(), 2, "System",
        "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0xfe,
        "system_get_localtime", "%s localtime_r failed\n", "system_get_localtime");
    return -1;
}

int32_t system_rebasetime(uint64_t arg1)
{
    struct timespec var_18;

    if (clock_gettime(4, &var_18) < 0) {
        return -1;
    }

    {
        uint64_t current = (uint64_t)var_18.tv_sec * 1000000ULL + (uint64_t)(var_18.tv_nsec / 1000);
        timestamp_base = current - arg1;
    }

    return 0;
}

int32_t system_bind(IMPCell *arg1, IMPCell *arg2)
{
    Module *src_module = get_module(arg1->deviceID, arg1->groupID);
    Module *dst_module = get_module(arg2->deviceID, arg2->groupID);

    if (src_module == NULL) {
        int32_t option = IMP_Log_Get_Option();

        imp_log_fun(6, option, 2, "System",
            "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0x11a,
            "system_bind", "%s() error: invalid src channel(%d.%d.%d)\n",
            "system_bind", arg1->deviceID, arg1->groupID, arg1->outputID);
        return -1;
    }

    if (dst_module != NULL) {
        sysbind_trace("libimp/BIND: system_bind src=%s(%p) %d.%d.%d dst=%s(%p) %d.%d.%d outcnt=%d outptr=%p\n",
            src_module, src_module, arg1->deviceID, arg1->groupID, arg1->outputID,
            dst_module, dst_module, arg2->deviceID, arg2->groupID, arg2->outputID,
            *(int32_t *)((char *)src_module + 0x134),
            (char *)src_module + 0x128 + ((arg1->outputID + 4) << 2));
        imp_log_fun(3, IMP_Log_Get_Option(), 2, "System",
            "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0x126,
            "system_bind", "%s(): bind DST-%s(%d.%d.%d) to SRC-%s(%d.%d.%d)\n",
            "system_bind", dst_module, arg2->deviceID, arg2->groupID, arg2->outputID,
            src_module, arg1->deviceID, arg1->groupID, arg1->outputID);

        if (arg1->outputID >= *(int32_t *)((char *)src_module + 0x134)) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "System",
                "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0x12c,
                "system_bind", "%s() error: invalid SRC:%s()\n",
                "system_bind", src_module, arg1->deviceID, arg1->groupID, arg1->outputID);
            return -1;
        }

        sysbind_trace("libimp/BIND: system_bind pre-dispatch src=%s(%p) dst=%s(%p) bindfn=%p outptr=%p count=%d\n",
            src_module->name, src_module, dst_module->name, dst_module,
            *(void **)((char *)src_module + 0x40),
            (char *)src_module + 0x128 + ((arg1->outputID + 4) << 2),
            *(int32_t *)((char *)src_module + 0x3c));
        BindObserverToSubject(src_module, dst_module,
            (char *)src_module + 0x128 + ((arg1->outputID + 4) << 2));
        sysbind_trace("libimp/BIND: system_bind post-dispatch src=%s(%p) dst=%s(%p) count=%d dst_subject=%p\n",
            src_module->name, src_module, dst_module->name, dst_module,
            *(int32_t *)((char *)src_module + 0x3c),
            *(void **)((char *)dst_module + 0x10));
        return 0;
    }

    {
        int32_t option = IMP_Log_Get_Option();

        imp_log_fun(6, option, 2, "System",
            "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0x120,
            "system_bind", "%s() error: invalid dst channel(%d.%d.%d)\n",
            "system_bind", arg2->deviceID, arg2->groupID, arg2->outputID);
    }

    return -1;
}

int32_t system_unbind(IMPCell *arg1, IMPCell *arg2)
{
    Module *src_module = get_module(arg1->deviceID, arg1->groupID);
    Module *dst_module = get_module(arg2->deviceID, arg2->groupID);

    if (src_module == NULL) {
        int32_t option = IMP_Log_Get_Option();

        imp_log_fun(6, option, 2, "System",
            "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0x13e,
            "system_unbind", "%s() error: invalid src channel(%d.%d.%d)\n",
            "system_unbind", arg1->deviceID, arg1->groupID, arg1->outputID);
        return -1;
    }

    if (dst_module != NULL) {
        imp_log_fun(3, IMP_Log_Get_Option(), 2, "System",
            "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0x14a,
            "system_unbind", "%s(): unbind DST-%s(%d.%d.%d) from SRC-%s(%d.%d.%d)\n",
            "system_unbind", dst_module, arg2->deviceID, arg2->groupID, arg2->outputID,
            src_module, arg1->deviceID, arg1->groupID, arg1->outputID);
        UnBindObserverFromSubject(src_module, dst_module);
        return 0;
    }

    {
        int32_t option = IMP_Log_Get_Option();

        imp_log_fun(6, option, 2, "System",
            "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0x144,
            "system_unbind", "%s() error: invalid dst channel(%d.%d.%d)\n",
            "system_unbind", arg2->deviceID, arg2->groupID, arg2->outputID);
    }

    return -1;
}

int32_t system_get_bind_src(IMPCell *arg1, IMPCell *arg2)
{
    Module *dst_module = get_module(arg1->deviceID, arg1->groupID);

    if (dst_module == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "System",
            "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0x156,
            "system_get_bind_src", "%s() error: invalid dst channel\n",
            "system_get_bind_src");
    } else {
        Module *src_module = *(Module **)((char *)dst_module + 0x10);

        if (src_module != NULL) {
            int32_t observer_count = *(int32_t *)((char *)src_module + 0x3c);
            int32_t index = -1;

            if (observer_count > 0) {
                Module **slot = (Module **)((char *)src_module + 0x14);
                index = 0;

                if (dst_module != *(Module **)((char *)src_module + 0x14)) {
                    do {
                        index += 1;
                        slot = (Module **)((char *)slot + 8);

                        if (index == observer_count) {
                            index = -1;
                            break;
                        }
                    } while (dst_module != *(Module **)((char *)slot - 8));
                }
            }

            if (index != -1) {
                arg2->outputID = index;
                return get_module_location(src_module, &arg2->deviceID, &arg2->groupID);
            }

            imp_log_fun(6, IMP_Log_Get_Option(), 2, "System",
                "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0x15d,
                "system_get_bind_src", "%s() error: dst channel(%s) has not been binded\n",
                "system_get_bind_src", dst_module);
            return -1;
        }
    }

    return -1;
}

int32_t system_attach(IMPCell *arg1, IMPCell *arg2)
{
    IMPCell var_28;
    int32_t result = system_get_bind_src(arg2, &var_28);

    if (result < 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "System",
            "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0x184,
            "system_attach", "%s() error: attach %s error\n",
            "system_attach", get_module(arg2->deviceID, arg2->groupID));
        return result;
    }

    result = system_unbind(&var_28, arg2);
    if (result < 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "System",
            "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0x184,
            "system_attach", "%s() error: attach %s error\n",
            "system_attach", get_module(arg2->deviceID, arg2->groupID));
        return result;
    }

    result = system_bind(&var_28, arg1);
    if (result < 0) {
        system_bind(&var_28, arg2);
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "System",
            "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0x184,
            "system_attach", "%s() error: attach %s error\n",
            "system_attach", get_module(arg2->deviceID, arg2->groupID));
        return result;
    }

    result = system_bind(arg1, arg2);
    if (result < 0) {
        system_unbind(&var_28, arg1);
        system_bind(&var_28, arg2);
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "System",
            "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0x184,
            "system_attach", "%s() error: attach %s error\n",
            "system_attach", get_module(arg2->deviceID, arg2->groupID));
    }

    return result;
}

int32_t system_detach(IMPCell *arg1, IMPCell *arg2)
{
    IMPCell var_28;
    int32_t result = system_get_bind_src(arg1, &var_28);

    if (result < 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "System",
            "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0x1a4,
            "system_detach", "%s() error: detach %s error\n",
            "system_detach", get_module(arg2->deviceID, arg2->groupID));
        return result;
    }

    result = system_unbind(&var_28, arg1);
    if (result < 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "System",
            "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0x1a4,
            "system_detach", "%s() error: detach %s error\n",
            "system_detach", get_module(arg2->deviceID, arg2->groupID));
        return result;
    }

    result = system_unbind(arg1, arg2);
    if (result < 0) {
        system_bind(&var_28, arg1);
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "System",
            "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0x1a4,
            "system_detach", "%s() error: detach %s error\n",
            "system_detach", get_module(arg2->deviceID, arg2->groupID));
        return result;
    }

    result = system_bind(&var_28, arg2);
    if (result < 0) {
        system_bind(arg1, arg2);
        system_bind(&var_28, arg1);
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "System",
            "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0x1a4,
            "system_detach", "%s() error: detach %s error\n",
            "system_detach", get_module(arg2->deviceID, arg2->groupID));
    }

    return result;
}

const char *pixfmt_to_string(int32_t arg1)
{
    if ((uint32_t)arg1 >= 0x24) {
        return NULL;
    }

    return fmt_str_8579[arg1];
}

int32_t mygettid(void)
{
    return (int32_t)syscall(0x107e);
}

int32_t write_reg_32(int32_t arg1, int32_t arg2)
{
    int32_t arg_4 = arg2;

    return regrw(arg1, &arg_4, 1);
}

int32_t get_mapped_addr(int32_t arg1, int32_t arg2, int32_t arg3, void **arg4, void **arg5, int32_t *arg6)
{
    int32_t open_flags = 0x10;

    if (arg3 != 0) {
        open_flags = 0x12;
    }

    int32_t fd = open("/dev/mem", open_flags);
    if (fd < 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "System",
            "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0x273,
            "get_mapped_addr", "open /dev/mem failed\n");
        return -1;
    }

    {
        int32_t page_size = getpagesize();
        int32_t page_offset = (page_size - 1) & arg1;
        int32_t map_len;

        if ((uint32_t)page_size < (uint32_t)(arg2 + page_offset)) {
            map_len = page_size << 1;
            *arg6 = map_len;
        } else {
            map_len = page_size;
            *arg6 = page_size;
        }

        {
            int32_t prot = 1;

            if (arg3 != 0) {
                prot = 3;
            }

            {
                void *mapped = mmap(0, (size_t)map_len, prot, 1, fd, (off_t)((-page_size) & arg1));
                *arg5 = mapped;

                if (mapped == (void *)-1) {
                    close(fd);
                    imp_log_fun(6, IMP_Log_Get_Option(), 2, "System",
                        "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0x285,
                        "get_mapped_addr", "mmap failed\n");
                    return -1;
                }

                *arg4 = (char *)mapped + page_offset;
                close(fd);
                return 0;
            }
        }
    }
}

uint32_t read_reg_32_addr(uint32_t *arg1)
{
    return *arg1;
}

int32_t get_cpuinfo(int32_t arg1, int32_t *arg2)
{
    return regrw(arg1, arg2, 0);
}

int32_t read_reg_32(int32_t arg1, int32_t *arg2)
{
    return get_cpuinfo(arg1, arg2);
}

int32_t get_cpu_id(void)
{
    int32_t ret;

    if (soc_id_8648 == -1) {
        ret = get_cpuinfo(0x1300002c, &soc_id_8648);
        if (soc_id_8648 == -1 && ret < 0) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "System",
                "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0x2c8,
                "get_cpu_id", "%s:%d get_cpuinfo error\n", "get_cpu_id", 0x2c8);
            return -1;
        }
    }

    if (cppsr_8647 == -1) {
        ret = get_cpuinfo(0x10000034, &cppsr_8647);
        if (cppsr_8647 == -1 && ret < 0) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "System",
                "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0x2d0,
                "get_cpu_id", "%s:%d get_cpuinfo error\n", "get_cpu_id", 0x2d0);
            return -1;
        }
    }

    if (subsoctype_8649 == -1) {
        ret = get_cpuinfo(0x13540238, &subsoctype_8649);
        if (subsoctype_8649 == -1 && ret < 0) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "System",
                "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0x2d8,
                "get_cpu_id", "%s:%d get_cpuinfo error\n", "get_cpu_id", 0x2d8);
            return -1;
        }
    }

    if (subremark_8651 == -1) {
        ret = get_cpuinfo(0x13540231, &subremark_8651);
        if (subremark_8651 == -1 && ret < 0) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "System",
                "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0x2e0,
                "get_cpu_id", "%s:%d get_cpuinfo error\n", "get_cpu_id", 0x2e0);
            return -1;
        }
    }

    {
        uint32_t soc_id_1 = (uint32_t)soc_id_8648;
        uint32_t family = soc_id_1 >> 0x1c;
        uint32_t id = (soc_id_1 >> 0xc) & 0xffff;
        int32_t result;

        if (family == 1) {
            if (id == 5) {
                uint32_t cppsr_3 = (uint8_t)cppsr_8647;

                if (cppsr_3 == family) {
                    return 0;
                }

                result = 1;
                if (cppsr_3 != 0) {
                    if (cppsr_3 != 0x10) {
                        return -1;
                    }

                    return 2;
                }
            } else {
                uint32_t cppsr_2 = (uint8_t)cppsr_8647;

                if (id == 0x2000) {
                    result = 3;
                    if (cppsr_2 != family) {
                        result = 4;
                        if (cppsr_2 != 0x10) {
                            imp_log_fun(6, IMP_Log_Get_Option(), 2, "System",
                                "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0x2f4,
                                "get_cpu_id", "%s:%d get_cpuinfo cppsr reg 0x%x error\n",
                                "get_cpu_id", 0x2f4, cppsr_8647);
                            return -1;
                        }
                    }
                } else if (id == 0x30) {
                    uint32_t cppsr_1 = (uint8_t)cppsr_8647;

                    result = 5;
                    if (cppsr_1 != family) {
                        if (cppsr_1 != 0x10) {
                            return -1;
                        }

                        return 6;
                    }
                } else if (id == 0x21) {
                    result = 0xb;
                    if (cppsr_2 == family) {
                        uint32_t remark = (uint8_t)subremark_8651;

                        if (remark == 0) {
                            uint32_t subtype = (uint16_t)subsoctype_8649;

                            result = 0xb;
                            if (subtype != 0x3333) {
                                result = 0xc;
                                if (subtype != 0x1111) {
                                    result = 0xd;
                                    if (subtype == 0x5555) {
                                        return 0xe;
                                    }
                                }
                            }
                        } else {
                            result = 0xc;
                            if (remark != cppsr_2) {
                                result = 0xb;
                                if (remark != 3) {
                                    result = 0xc;
                                    if (remark != 7) {
                                        result = 0xb;
                                        if (remark != 0xf) {
                                            return -1;
                                        }
                                    }
                                }
                            }
                        }
                    } else if (cppsr_2 != 0x10) {
                        return -1;
                    }
                } else {
                    if (id != 0x31) {
                        imp_log_fun(6, IMP_Log_Get_Option(), 2, "System",
                            "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0x351,
                            "get_cpu_id", "%s:%d get_cpuinfo id reg 0x%x error\n",
                            "get_cpu_id", 0x351, id);
                        return -1;
                    }

                    {
                        uint32_t remark = (uint8_t)subremark_8651;

                        if (remark == 0) {
                            uint32_t subtype = (uint16_t)subsoctype_8649;

                            result = 0xf;
                            if (subtype != 0x3333) {
                                result = 0x10;
                                if (subtype != 0x1111) {
                                    result = 0x11;
                                    if (subtype != 0x2222) {
                                        result = 0x12;
                                        if (subtype != 0x4444) {
                                            result = 0x14;
                                            if (subtype != 0x5555) {
                                                result = 0x17;
                                                if (subtype != 0x6666) {
                                                    result = 0x13;
                                                    if (subtype != 0xcccc) {
                                                        result = 0x15;
                                                        if (subtype != 0xdddd) {
                                                            result = 0x17;
                                                            if (subtype == 0xeeee) {
                                                                return 0x16;
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        } else {
                            result = 0x10;
                            if (remark != family) {
                                result = 0xf;
                                if (remark != 3) {
                                    result = 0x10;
                                    if (remark != 7) {
                                        result = 0xf;
                                        if (remark != 0xf) {
                                            return -1;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            return result;
        }

        {
            uint32_t subtype = (uint16_t)subsoctype_8649;

            if (subtype == 0x3333) {
                imp_log_fun(5, IMP_Log_Get_Option(), 2, "System",
                    "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0x2fa,
                    "get_cpu_id", "%s:%d get_cpuinfo subtype reg 0x%x error\n",
                    "get_cpu_id", 0x2fa, subtype);
                return 7;
            }

            result = 7;
            if (subtype != 0x1111) {
                result = 8;
                if (subtype != 0x2222) {
                    result = 9;
                    if (subtype != 0x4444) {
                        result = 8;
                        if (subtype == 0x5555) {
                            return 0xa;
                        }
                    }
                }
            }

            return result;
        }
    }
}

int32_t system_init(void)
{
    struct timespec var_38;
    int32_t i = 0;
    int32_t result = 0;

    imp_log_fun(3, IMP_Log_Get_Option(), 2, "System",
        "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0x9c,
        "system_init", "%s()\n", "system_init");

    if (clock_gettime(4, &var_38) < 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "System",
            "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0xa2,
            "system_init", "Time init error\n");
        return -1;
    }

    timestamp_base = (uint64_t)var_38.tv_sec * 1000000ULL + (uint64_t)(var_38.tv_nsec / 1000);
    get_cpu_id();

    extern struct ISPDevice *gISP;
    do {
        /* Diagnostic: trace each sys_funcs init call to /dev/kmsg along with
         * the current value of gISP — if gISP becomes NULL mid-iteration, we
         * can see exactly which init zeroed it. */
        {
            int kfd = open("/dev/kmsg", O_WRONLY);
            if (kfd >= 0) {
                char buf[160];
                int n = snprintf(buf, sizeof(buf),
                    "libimp/SYS: before sys_funcs[%d]=%s init, gISP=%p\n",
                    i, sys_funcs[i].name, (void *)gISP);
                if (n > 0) write(kfd, buf, (size_t)n);
                close(kfd);
            }
        }
        imp_log_fun(3, IMP_Log_Get_Option(), 2, "System",
            "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0xac,
            "system_init", "Calling %s\n", sys_funcs[i].name);
        result = sys_funcs[i].init();
        {
            int kfd = open("/dev/kmsg", O_WRONLY);
            if (kfd >= 0) {
                char buf[160];
                int n = snprintf(buf, sizeof(buf),
                    "libimp/SYS: after  sys_funcs[%d]=%s init=%d, gISP=%p\n",
                    i, sys_funcs[i].name, result, (void *)gISP);
                if (n > 0) write(kfd, buf, (size_t)n);
                close(kfd);
            }
        }
        if (result < 0) {
            int32_t j = i - 1;

            imp_log_fun(6, IMP_Log_Get_Option(), 2, "System",
                "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0xb0,
                "system_init", "%s failed\n", sys_funcs[i].name);

            if (i == 0) {
                break;
            }

            do {
                sys_funcs[j].exit();
                j -= 1;
            } while (j != -1);

            return result;
        }

        i += 1;
    } while (i != 6);

    return result;
}

int32_t system_exit(void)
{
    int32_t i = 0;

    imp_log_fun(3, IMP_Log_Get_Option(), 2, "System",
        "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0xc5,
        "system_exit", "%s\n", "system_exit");
    get_cpu_id();

    do {
        imp_log_fun(3, IMP_Log_Get_Option(), 2, "System",
            "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0xca,
            "system_exit", "Calling %s\n", sys_funcs[i].name);
        sys_funcs[i].exit();
        i += 1;
    } while (i != 6);

    return 0;
}

int32_t is_has_simd128(void)
{
    /* Forced scalar path for now: T53 resize dispatch depends on three
     * top-level Ingenic SIMD128 kernels that are still blocked. Keep the
     * original logic below for future restoration once those ports land. */
#if 0
    if (is_video_has_simd128_proc() == 0) {
        return 0;
    }

    {
        int32_t cpu_id = get_cpu_id();

        if (cpu_id != 0 && ((uint32_t)cpu_id >= 0x18 || ((0xff87dcU >> (cpu_id & 0x1f)) & 1U) == 0)) {
            return 0;
        }

        return access("/tmp/closesimd", 0) < 0;
    }
#endif
    return 0;
}

int32_t is_support_simd128(void)
{
    if (is_video_has_simd128_proc() == 0) {
        return 0;
    }

    {
        int32_t cpu_id = get_cpu_id();

        if (cpu_id != 0 && cpu_id != 3 && (uint32_t)(cpu_id - 7) >= 4) {
            int32_t result = (uint32_t)(cpu_id - 0x10) < 8;

            if (result == 0) {
                return result;
            }

            return access("/tmp/closesimd", 0) < 0;
        }

        return access("/tmp/closesimd", 0) < 0;
    }
}

int32_t modify_phyclk_strength(void)
{
    int32_t var_14;
    int32_t var_18;
    int32_t var_1c;
    int32_t var_20;
    int32_t var_24;
    int32_t var_28;
    int32_t var_2c;
    int32_t var_30;

    read_reg_32(0x10011010, &var_14);
    read_reg_32(0x10011020, &var_18);
    read_reg_32(0x10011030, &var_1c);
    read_reg_32(0x10011040, &var_20);
    read_reg_32(0x10011130, &var_24);
    read_reg_32(0x10000028, &var_28);

    var_14 = ((uint32_t)var_14 >> 8) & 1;
    var_18 = ((uint32_t)var_18 >> 8) & 1;
    var_1c = ((uint32_t)var_1c >> 8) & 1;
    var_20 = ((uint32_t)var_20 >> 8) & 1;
    var_24 = ((uint32_t)var_24 >> 0xe) & 3;
    var_28 = ((uint32_t)var_28 >> 4) & 1;

    read_reg_32(0x10000054, &var_2c);

    if (((uint32_t)var_2c >> 0x1e) == 0) {
        read_reg_32(0x10000010, &var_30);
    } else {
        if (((uint32_t)var_2c >> 0x1e) == 1) {
            read_reg_32(0x10000014, &var_30);
        } else {
            read_reg_32(0x100000e0, &var_30);
        }
    }

    if (var_14 == 0 && var_18 == 0) {
        int32_t v0_6 = var_20;

        if (var_1c == 0) {
            if (v0_6 == 0 && var_28 == 0) {
                int32_t a0_1 = var_30;

                if (var_24 == 0) {
                    uint32_t v1_5 = ((uint32_t)a0_1 >> 0xe) & 0x3f;
                    uint32_t v1_6;
                    uint32_t a0_2;
                    uint32_t lo_4;

                    if (v1_5 == 0) {
                        __builtin_trap();
                    }

                    v1_6 = ((uint32_t)a0_1 >> 0xb) & 7;
                    a0_2 = ((uint32_t)a0_1 >> 8) & 7;

                    if (v1_6 == 0) {
                        __builtin_trap();
                    }

                    if (a0_2 == 0) {
                        __builtin_trap();
                    }

                    lo_4 = ((((uint32_t)a0_1 >> 0x14) & 0xff) * 0x18U) / v1_5 / v1_6 / a0_2 / (((uint32_t)var_2c & 0xffU) + 1U);

                    if (((uint32_t)var_2c & 0xffU) == 0xffffffffU) {
                        __builtin_trap();
                    }

                    if (lo_4 == 0x19) {
                        write_reg_32(0x10011138, 0xc000);
                        write_reg_32(0x10011134, 0x4000);
                    } else if (lo_4 == 0x32) {
                        write_reg_32(0x10011138, 0xc000);
                        write_reg_32(0x10011134, 0x8000);
                    }
                }

                write_reg_32(0x10011138, 0xfcff3000);
                write_reg_32(0x10011134, 0x54551000);
                write_reg_32(0x10011148, 3);
                write_reg_32(0x10011144, 1);
            } else if (v0_6 == 1) {
                write_reg_32(0x10011138, 0x30000);
                write_reg_32(0x10011134, 0x20000);
                write_reg_32(0x10011138, 0x3cfc0000);
                write_reg_32(0x10011134, 0x14540000);
            }
        }
    }

    return 0;
}

int32_t system_bind_dump(void)
{
    int32_t s2 = 0;
    int32_t s0 = 0;
    int32_t var_38;
    int32_t var_3c;

    while (1) {
        Module *module = get_module(s2, s0);

        if (module != NULL) {
            var_38 = s0;
            var_3c = s2;

            imp_log_fun(4, IMP_Log_Get_Option(), 2, "System",
                "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0x1c8,
                "system_bind_dump", "enumerate: %s(%d, %d)\n",
                module, var_3c, var_38);

            if (*(int32_t *)((char *)module + 0x3c) > 0 && *(Module **)((char *)module + 0x10) == NULL) {
                imp_log_fun(4, IMP_Log_Get_Option(), 2, "System",
                    "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0x1d7,
                    "system_bind_dump", "Source:%s(%d, %d)\n",
                    module, s2, s0);
                dump_ob_modules(module, 0);
                return 0;
            }
        }

        s0 += 1;
        if (s0 == 6) {
            s2 += 1;
            s0 = 0;
            if (s2 == 0x18) {
                break;
            }
        }
    }

    imp_log_fun(6, IMP_Log_Get_Option(), 2, "System",
        "/home/user/git/proj/sdk-lv3/src/imp/core/sys_core.c", 0x1d3,
        "system_bind_dump", "%s() error: No source found\n",
        "system_bind_dump", var_3c, var_38);
    return -1;
}
