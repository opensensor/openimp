#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "core/imp_alloc.h"

int IMP_Log_Get_Option(void);
void imp_log_fun(int level, int option, int type, ...);

/* forward decl, ported by T<N> later */
int32_t memparse(const char *ptr, char **retptr);

static int32_t _fdata = -1;
static uint32_t kmem_length = 0;
static uint32_t kmem_paddr = 0;
static uint32_t kmem_vaddr = 0;

static int32_t get_kmem_info(void)
{
    struct {
        int32_t haystack;
        char str[0x1fc];
    } cmdline = {0};
    FILE *stream;
    const char *log_fmt;
    int32_t log_option;
    int32_t log_line;
    char *rmem;
    char *var_18;
    int32_t parsed_length;
    char *addr_ptr;
    int32_t parsed_addr;

    memset(cmdline.str, 0, sizeof(cmdline.str));
    stream = fopen("/proc/cmdline", "r");
    if (stream == NULL) {
        log_option = IMP_Log_Get_Option();
        log_fmt = "%s open file (%s) error\n";
        log_line = 0x4c;
        goto error;
    }

    if ((int32_t)fread(&cmdline.haystack, 1, 0x200, stream) <= 0) {
        log_option = IMP_Log_Get_Option();
        log_fmt = "%s fread (%s) error\n";
        log_line = 0x52;
        goto error;
    }

    fclose(stream);
    rmem = strstr((char *)&cmdline.haystack, "rmem");
    if (rmem != NULL) {
        parsed_length = memparse(&rmem[5], &var_18);
        addr_ptr = var_18;
        parsed_addr = 0;
        if ((int32_t)*addr_ptr == 0x40) {
            parsed_addr = memparse(&addr_ptr[1], NULL);
        }

        imp_log_fun(3, IMP_Log_Get_Option(), 2, "KMEM Method",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/kmalloc_method.c", 0x64,
            "get_kmem_info", "CMD Line Rmem Size:%d, Addr:0x%08x\n", parsed_length,
            parsed_addr);
        kmem_paddr = (uint32_t)parsed_addr;
        kmem_length = (uint32_t)parsed_length;
        return 0;
    }

    log_option = IMP_Log_Get_Option();
    log_fmt = "%s fread (%s) error\n";
    log_line = 0x5a;

error:
    imp_log_fun(6, log_option, 2, "KMEM Method",
        "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/kmalloc_method.c", log_line,
        "get_kmem_info", log_fmt, "get_kmem_info", "/proc/cmdline");
    return -1;
}

int32_t alloc_kmem_init(void *arg1)
{
    const char *var_3c_2;
    const char *var_38_2;
    int32_t log_option;
    int32_t log_line;
    int32_t fd;
    int32_t map;

    if (arg1 == NULL) {
        log_option = IMP_Log_Get_Option();
        var_38_2 = "alloc_kmem_init";
        var_3c_2 = "%s function param is NULL\n";
        log_line = 0x70;
        goto fail_log;
    }

    if (get_kmem_info() != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "KMEM Method",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/kmalloc_method.c", 0x76,
            "alloc_kmem_init", "get kmem info failed\n");
        return -1;
    }

    if (kmem_paddr == 0 || (int32_t)kmem_length <= 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "KMEM Method",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/kmalloc_method.c", 0x7b,
            "alloc_kmem_init", "%s mmap Addr %x and Size %d error\n", "alloc_kmem_init",
            kmem_paddr, kmem_length);
        return -1;
    }

    fd = open("/dev/rmem", 2);
    _fdata = fd;
    if (fd > 0) {
        map = (int32_t)(intptr_t)mmap(0, kmem_length, 3, 1, fd, kmem_paddr);
        kmem_vaddr = (uint32_t)map;
        if (map != 0) {
            /* offset 0x60: mem_alloc.paddr */
            *(uint32_t *)((char *)arg1 + 0x60) = kmem_paddr;
            /* offset 0x64: mem_alloc.length */
            *(uint32_t *)((char *)arg1 + 0x64) = kmem_length;
            /* offset 0x5c: mem_alloc.vaddr */
            *(uint32_t *)((char *)arg1 + 0x5c) = (uint32_t)map;
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "KMEM Method",
                "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/kmalloc_method.c", 0x98,
                "alloc_kmem_init",
                "alloc->mem_alloc.method = %s\n \t\t\talloc->mem_alloc.vaddr = 0x%08x\n \t\t\talloc->mem_alloc.paddr = 0x%08x\n \t\t\talloc->mem_alloc.length = %d\n",
                (char *)arg1 + 0x3c, *(uint32_t *)((char *)arg1 + 0x5c),
                *(uint32_t *)((char *)arg1 + 0x60), *(uint32_t *)((char *)arg1 + 0x64));
            return 0;
        }

        imp_log_fun(6, IMP_Log_Get_Option(), 2, "KMEM Method",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/kmalloc_method.c", 0x87,
            "alloc_kmem_init", "mmap failed\n");
        return -1;
    }

    log_option = IMP_Log_Get_Option();
    var_38_2 = "/dev/rmem";
    var_3c_2 = "open %s failed\n";
    log_line = 0x81;

fail_log:
    imp_log_fun(6, log_option, 2, "KMEM Method",
        "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/kmalloc_method.c", log_line,
        "alloc_kmem_init", var_3c_2, var_38_2);
    return -1;
}

int32_t alloc_kmem_get_paddr(int32_t arg1)
{
    uint32_t kmem_vaddr_1;

    kmem_vaddr_1 = kmem_vaddr;
    if ((uint32_t)arg1 >= kmem_vaddr_1 && kmem_vaddr_1 + kmem_length >= (uint32_t)arg1) {
        return (int32_t)(kmem_paddr - kmem_vaddr_1 + (uint32_t)arg1);
    }

    imp_log_fun(6, IMP_Log_Get_Option(), 2, "KMEM Method",
        "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/kmalloc_method.c", 0xa0,
        "alloc_kmem_get_paddr", "vaddr input error %08x\n", arg1);
    return 0;
}

int32_t alloc_kmem_get_vaddr(int32_t arg1)
{
    uint32_t kmem_paddr_1;

    kmem_paddr_1 = kmem_paddr;
    if ((uint32_t)arg1 >= kmem_paddr_1 && kmem_paddr_1 + kmem_length >= (uint32_t)arg1) {
        return (int32_t)(kmem_vaddr - kmem_paddr_1 + (uint32_t)arg1);
    }

    imp_log_fun(6, IMP_Log_Get_Option(), 2, "KMEM Method",
        "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/kmalloc_method.c", 0xaa,
        "alloc_kmem_get_vaddr", "paddr input error\n");
    return 0;
}

int32_t alloc_kmem_flush_cache(int32_t arg1, int32_t arg2, int32_t arg3)
{
    int32_t fd;
    int32_t var_18;
    int32_t var_14_1;
    int32_t var_10_1;

    fd = _fdata;
    if (fd < 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "KMEM Method",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/kmalloc_method.c", 0xbb,
            "alloc_kmem_flush_cache", "%s not open\n", "/dev/rmem");
        return -1;
    }

    var_18 = arg1;
    var_14_1 = arg2;
    var_10_1 = arg3;
    return ioctl(fd, 0xc00c7200, &var_18);
}
