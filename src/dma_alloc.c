/**
 * DMA Allocator Implementation
 * Based on reverse engineering of libimp.so v1.1.6
 * Integrates with kernel driver for physical memory allocation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <pthread.h>

#include "dma_alloc.h"
#include "imp_log_int.h"

/* Best-effort check that a pointer looks like a C string within max bytes */
static int is_probably_cstring(const char *p, size_t max)
{
    if (!p) return 0;
    for (size_t i = 0; i < max; ++i) {
        unsigned char c = (unsigned char)p[i];
        if (c == '\0') return 1;
        if (c < 0x09 || (c > 0x0d && c < 0x20)) return 0; /* control chars */
    }
    return 0;
}

/* Internal DMA buffer record. Exported/OEM-facing info uses IMPDMABufferInfo. */
typedef struct {
    char name[96];              /* 0x00-0x5f: Buffer name */
    char tag[32];               /* 0x60-0x7f: Tag */
    void *virt_addr;            /* Native virtual address */
    uint32_t phys_addr;         /* 0x84: Physical address */
    uint32_t size;              /* 0x88: Buffer size */
    uint32_t flags;             /* 0x8c: Flags */
    uint32_t pool_id;           /* 0x90: Pool ID */
} DMABufferRecord;

/* ioctl commands for memory allocation */
#define IOCTL_MEM_ALLOC     0xc0104d01  /* Allocate memory */
#define IOCTL_MEM_FREE      0xc0104d02  /* Free memory */
#define IOCTL_MEM_GET_PHY   0xc0104d03  /* Get physical address */
#define IOCTL_MEM_FLUSH     0xc0104d04  /* Flush cache */

/* Mainline T31 exposes coherent allocations through the AVPU device as a
 * last-resort fallback.  Keep the legacy /dev/rmem and bootloader-reserved
 * arena paths ahead of it so the encoder can own the AVPU channel. */
#define AVPU_GET_DMA_MMAP   _IOWR('q', 26, struct avpu_dma_info)
#define AVPU_FLUSH_CACHE    _IOWR('q', 14, int)

struct avpu_dma_info {
    uint32_t fd;       /* page-aligned mmap offset returned by the driver */
    uint32_t size;
    uint32_t phy_addr;
} __attribute__((aligned(4)));

struct avpu_flush_cache_info {
    uint32_t addr;
    uint32_t len;
    uint32_t dir;
};

/* Memory allocation request structure */
typedef struct {
    uint32_t size;              /* Requested size */
    uint32_t align;             /* Alignment */
    uint32_t phys_addr;         /* Output: physical address */
    uint32_t flags;             /* Allocation flags */
} mem_alloc_req_t;

/* Global buffer registry */
#define MAX_DMA_BUFFERS 128
static DMABufferRecord *g_buffer_registry[MAX_DMA_BUFFERS] = {NULL};
static pthread_mutex_t g_registry_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Global state */
static int g_mem_fd = -1;
static pthread_mutex_t g_dma_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_alloc_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_dma_initialized = 0;
static int g_rmem_supported = 0;  /* set when a DMA-capable backend is ready */

/* RMEM-specific globals (for /dev/rmem bump allocator) */
static int g_is_rmem = 0;
static int g_is_avpu = 0;
static int g_is_devmem_rmem = 0;
static uint32_t g_rmem_base_phys;
static size_t g_rmem_size;
static void *g_rmem_virt_base = NULL;
static size_t g_rmem_offset = 0; /* bump pointer */
static char g_chosen_dev_path[64] = {0};

static const uint32_t kCompatMaxAllocSize = 256u * 1024u * 1024u;

int IMP_FlushCache(void *virt_addr, uint32_t size);

/**
 * Register buffer in global registry
 */
static int register_buffer(DMABufferRecord *buf) {
    pthread_mutex_lock(&g_registry_mutex);

    for (int i = 0; i < MAX_DMA_BUFFERS; i++) {
        if (g_buffer_registry[i] == NULL) {
            g_buffer_registry[i] = buf;
            pthread_mutex_unlock(&g_registry_mutex);
            return 0;
        }
    }

    pthread_mutex_unlock(&g_registry_mutex);
    LOG_DMA("register_buffer: registry full");
    return -1;
}

/**
 * Unregister buffer from global registry
 */
static void unregister_buffer(DMABufferRecord *buf) {
    pthread_mutex_lock(&g_registry_mutex);

    for (int i = 0; i < MAX_DMA_BUFFERS; i++) {
        if (g_buffer_registry[i] == buf) {
            g_buffer_registry[i] = NULL;
            break;
        }
    }

    pthread_mutex_unlock(&g_registry_mutex);
}

/**
 * Lookup buffer by physical address
 */
static DMABufferRecord* lookup_buffer_by_phys(uint32_t phys_addr) {
    pthread_mutex_lock(&g_registry_mutex);

    for (int i = 0; i < MAX_DMA_BUFFERS; i++) {
        if (g_buffer_registry[i] != NULL &&
            g_buffer_registry[i]->phys_addr == phys_addr) {
            DMABufferRecord *buf = g_buffer_registry[i];
            pthread_mutex_unlock(&g_registry_mutex);
            return buf;
        }
    }

    pthread_mutex_unlock(&g_registry_mutex);
    return NULL;
}

static DMABufferRecord* lookup_buffer_containing_phys(uint32_t phys_addr, uint32_t *offset_out) {
    pthread_mutex_lock(&g_registry_mutex);

    for (int i = 0; i < MAX_DMA_BUFFERS; i++) {
        DMABufferRecord *buf = g_buffer_registry[i];
        if (buf == NULL) {
            continue;
        }
        if (phys_addr >= buf->phys_addr && phys_addr < buf->phys_addr + buf->size) {
            if (offset_out != NULL) {
                *offset_out = phys_addr - buf->phys_addr;
            }
            pthread_mutex_unlock(&g_registry_mutex);
            return buf;
        }
    }

    pthread_mutex_unlock(&g_registry_mutex);
    return NULL;
}

static DMABufferRecord* lookup_buffer_containing_virt(const void *virt_addr, uint32_t *offset_out) {
    uintptr_t virt = (uintptr_t)virt_addr;

    pthread_mutex_lock(&g_registry_mutex);

    for (int i = 0; i < MAX_DMA_BUFFERS; i++) {
        DMABufferRecord *buf = g_buffer_registry[i];
        if (buf == NULL || buf->virt_addr == NULL) {
            continue;
        }

        uintptr_t base = (uintptr_t)buf->virt_addr;
        uintptr_t end = base + buf->size;
        if (virt >= base && virt < end) {
            if (offset_out != NULL) {
                *offset_out = (uint32_t)(virt - base);
            }
            pthread_mutex_unlock(&g_registry_mutex);
            return buf;
        }
    }

    pthread_mutex_unlock(&g_registry_mutex);
    return NULL;
}

static void fill_dma_info(IMPDMABufferInfo *info_out, const DMABufferRecord *buf)
{
    if (info_out == NULL || buf == NULL) {
        return;
    }

    memset(info_out, 0, sizeof(*info_out));
    memcpy(info_out->name, buf->name, sizeof(info_out->name));
    memcpy(info_out->tag, buf->tag, sizeof(info_out->tag));
    info_out->virt_addr = (uint32_t)(uintptr_t)buf->virt_addr;
    info_out->phys_addr = buf->phys_addr;
    info_out->size = buf->size;
    info_out->flags = buf->flags;
    info_out->pool_id = buf->pool_id;
}

static int size_arg_is_reasonable(intptr_t size)
{
    return size > 0 && (uint64_t)size <= kCompatMaxAllocSize;
}

static int tag_arg_looks_valid(const char *tag)
{
    return tag != NULL && is_probably_cstring(tag, 32);
}

static int looks_like_pointer_style_alloc(uintptr_t arg1, intptr_t arg2, const char *arg3)
{
    if (arg1 == 0 || arg1 > kCompatMaxAllocSize) {
        return 0;
    }
    if (!size_arg_is_reasonable(arg2)) {
        return 1;
    }
    return !tag_arg_looks_valid(arg3);
}

static int looks_like_pointer_style_pool_alloc(uintptr_t arg2, intptr_t arg3, const char *arg4)
{
    if (arg2 == 0 || arg2 > kCompatMaxAllocSize) {
        return 0;
    }
    if (!size_arg_is_reasonable(arg3)) {
        return 1;
    }
    return !tag_arg_looks_valid(arg4);
}

/**
 * Initialize DMA allocator
 */
static int rmem_range_valid(uint32_t base, size_t size)
{
    uint64_t end = (uint64_t)base + (uint64_t)size;

    return base != 0 && size != 0 &&
           (base & 0xfffu) == 0 && (size & 0xfffu) == 0 &&
           end <= (UINT64_C(1) << 32);
}

static int parse_uint32_exact(const char *value, uint32_t *result)
{
    char *endp = NULL;
    unsigned long parsed;

    if (value == NULL || *value == '\0' || result == NULL)
        return -1;
    errno = 0;
    parsed = strtoul(value, &endp, 0);
    if (errno != 0 || endp == value || *endp != '\0' || parsed > UINT32_MAX)
        return -1;
    *result = (uint32_t)parsed;
    return 0;
}

static int parse_size_exact(const char *value, size_t *result)
{
    char *endp = NULL;
    unsigned long parsed;

    if (value == NULL || *value == '\0' || result == NULL)
        return -1;
    errno = 0;
    parsed = strtoul(value, &endp, 0);
    if (errno != 0 || endp == value || *endp != '\0' || parsed == 0)
        return -1;
    *result = (size_t)parsed;
    return 0;
}

static int configure_rmem_from_env(void)
{
    const char *env_base = getenv("OPENIMP_RMEM_BASE");
    const char *env_size = getenv("OPENIMP_RMEM_SIZE");
    uint32_t base;
    size_t size;

    if ((!env_base || !*env_base) && (!env_size || !*env_size))
        return 0;
    if (!env_base || !*env_base || !env_size || !*env_size ||
        parse_uint32_exact(env_base, &base) != 0 ||
        parse_size_exact(env_size, &size) != 0 ||
        !rmem_range_valid(base, size)) {
        LOG_DMA("RMEM environment override rejected: both page-aligned base and size are required");
        return -1;
    }

    g_rmem_base_phys = base;
    g_rmem_size = size;
    LOG_DMA("RMEM overridden by env: base=0x%08x size=%zu (0x%zx)",
            g_rmem_base_phys, g_rmem_size, g_rmem_size);
    return 1;
}

static int configure_rmem_from_cmdline(void)
{
    char cmdline[1024];
    char *field;
    char *saveptr = NULL;
    char *endp;
    unsigned long amount;
    unsigned long base;
    uint64_t multiplier = 1;
    uint64_t bytes;
    FILE *fp;

    fp = fopen("/proc/cmdline", "r");
    if (fp == NULL)
        return 0;
    if (fgets(cmdline, sizeof(cmdline), fp) == NULL) {
        fclose(fp);
        return 0;
    }
    fclose(fp);

    field = strtok_r(cmdline, " \t\r\n", &saveptr);
    while (field != NULL && strncmp(field, "rmem=", 5) != 0)
        field = strtok_r(NULL, " \t\r\n", &saveptr);
    if (field == NULL)
        return 0;
    field += 5;
    errno = 0;
    amount = strtoul(field, &endp, 0);
    if (errno != 0 || endp == field)
        goto invalid;

    if (*endp == 'M' || *endp == 'm') {
        multiplier = 1024u * 1024u;
        endp++;
    } else if (*endp == 'K' || *endp == 'k') {
        multiplier = 1024u;
        endp++;
    }
    if (*endp != '@' || amount > UINT64_MAX / multiplier)
        goto invalid;
    bytes = (uint64_t)amount * multiplier;

    field = endp + 1;
    errno = 0;
    base = strtoul(field, &endp, 0);
    if (errno != 0 || endp == field || *endp != '\0' ||
        base > UINT32_MAX || bytes > SIZE_MAX)
        goto invalid;

    if (bytes == 0) {
        LOG_DMA("RMEM disabled by kernel command line");
        return 0;
    }
    if (!rmem_range_valid((uint32_t)base, (size_t)bytes))
        goto invalid;

    g_rmem_base_phys = (uint32_t)base;
    g_rmem_size = (size_t)bytes;
    LOG_DMA("RMEM parsed from cmdline: base=0x%08x size=%zu (0x%zx)",
            g_rmem_base_phys, g_rmem_size, g_rmem_size);
    return 1;

invalid:
    LOG_DMA("RMEM kernel command line value is malformed or unsafe");
    return -1;
}

static int dma_init(void) {
    int cmdline_config;
    int env_config;
    int rmem_configured;

    if (g_dma_initialized) {
        return g_rmem_supported ? 0 : -1;
    }

    pthread_mutex_lock(&g_dma_mutex);

    if (g_dma_initialized) {
        pthread_mutex_unlock(&g_dma_mutex);
        return g_rmem_supported ? 0 : -1;
    }

    /* The bootloader is authoritative for the reserved arena.  Environment
     * values remain a useful explicit override for development profiles. */
    cmdline_config = configure_rmem_from_cmdline();
    env_config = configure_rmem_from_env();
    if (cmdline_config < 0 || env_config < 0) {
        LOG_DMA("DMA init: refusing an invalid reserved-memory configuration");
        goto unavailable;
    }
    rmem_configured = cmdline_config > 0 || env_config > 0;

    /* Check if RMEM should be disabled (T31 workaround for kernel bugs) */
    const char *disable_rmem_env = getenv("OPENIMP_DISABLE_RMEM");
    int disable_rmem = (disable_rmem_env && disable_rmem_env[0] == '1');

    const char *candidates[] = {
        "/dev/memalloc",
        "/dev/ion-ingenic",
        "/dev/ion",
        "/dev/jz-mm",
        "/dev/mmem",
        "/dev/isp-mem",
        "/dev/vicbuf",
        NULL
    };

    g_mem_fd = -1;
    if (!disable_rmem && rmem_configured) {
        int fd = open("/dev/rmem", O_RDWR | O_CLOEXEC);

        if (fd >= 0) {
            void *base = mmap(NULL, g_rmem_size, PROT_READ | PROT_WRITE,
                              MAP_SHARED, fd, (off_t)g_rmem_base_phys);

            if (base != MAP_FAILED) {
                g_mem_fd = fd;
                g_rmem_supported = 1;
                g_rmem_virt_base = base;
                g_is_rmem = 1;
                strncpy(g_chosen_dev_path, "/dev/rmem",
                        sizeof(g_chosen_dev_path) - 1);
                LOG_DMA("DMA init: /dev/rmem mapped at %p size=%zu base_phys=0x%08x",
                        base, g_rmem_size, g_rmem_base_phys);
            } else {
                LOG_DMA("DMA init: mmap of /dev/rmem failed (%s)",
                        strerror(errno));
                close(fd);
            }
        }
    } else if (disable_rmem) {
        LOG_DMA("DMA init: skipping /dev/rmem (OPENIMP_DISABLE_RMEM=1)");
    } else {
        LOG_DMA("DMA init: skipping /dev/rmem without a valid reserved-memory range");
    }

    for (int i = 0; candidates[i] != NULL; i++) {
        int fd;

        if (g_mem_fd >= 0)
            break;
        fd = open(candidates[i], O_RDWR | O_CLOEXEC);
        if (fd >= 0) {
            g_mem_fd = fd;
            g_rmem_supported = 1;
            strncpy(g_chosen_dev_path, candidates[i], sizeof(g_chosen_dev_path) - 1);
            LOG_DMA("DMA init: using %s", candidates[i]);
            break;
        }
    }

    if (g_mem_fd < 0 && rmem_configured &&
        g_rmem_base_phys != 0 && g_rmem_size != 0) {
        int fd = open("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC);
        if (fd >= 0) {
            void *base = mmap(NULL, g_rmem_size, PROT_READ | PROT_WRITE,
                              MAP_SHARED, fd, (off_t)g_rmem_base_phys);
            if (base != MAP_FAILED) {
                g_mem_fd = fd;
                g_rmem_supported = 1;
                g_rmem_virt_base = base;
                g_is_rmem = 1;
                g_is_devmem_rmem = 1;
                strncpy(g_chosen_dev_path, "/dev/mem",
                        sizeof(g_chosen_dev_path) - 1);
                LOG_DMA("DMA init: reserved arena mapped uncached through /dev/mem at %p size=%zu base_phys=0x%08x",
                        base, g_rmem_size, g_rmem_base_phys);
            } else {
                LOG_DMA("DMA init: reserved /dev/mem mmap failed (%s)",
                        strerror(errno));
                close(fd);
            }
        }
    }

    if (g_mem_fd < 0) {
        int fd = open("/dev/avpu", O_RDWR | O_CLOEXEC);
        if (fd >= 0) {
            g_mem_fd = fd;
            g_rmem_supported = 1;
            g_is_avpu = 1;
            strncpy(g_chosen_dev_path, "/dev/avpu",
                    sizeof(g_chosen_dev_path) - 1);
            LOG_DMA("DMA init: using AVPU coherent allocator");
        } else {
            g_rmem_supported = 0;
            LOG_DMA("DMA init: no DMA-capable allocator is available");
        }
    }

    g_dma_initialized = 1;
    pthread_mutex_unlock(&g_dma_mutex);
    return g_rmem_supported ? 0 : -1;

unavailable:
    g_rmem_supported = 0;
    g_dma_initialized = 1;
    pthread_mutex_unlock(&g_dma_mutex);
    return -1;
}

static int dma_free_buffer(DMABufferRecord *buf)
{
    if (buf == NULL) {
        return -1;
    }

    LOG_DMA("Free: phys=0x%x virt=%p", buf->phys_addr, buf->virt_addr);

    if (buf->virt_addr != NULL) {
        if ((buf->flags & 0x2) && g_is_rmem) {
            /* RMEM bump allocations are not individually freed (no-op) */
        } else if ((buf->flags & 0x4) && g_is_avpu) {
            /* The AVPU channel owns the coherent allocation.  Unmapping the
             * userspace alias here is sufficient; the driver releases the
             * allocation when the channel fd closes. */
            munmap(buf->virt_addr, buf->size);
        } else if ((buf->flags & 0x1) && g_rmem_supported && g_mem_fd >= 0) {
            mem_alloc_req_t req;
            munmap(buf->virt_addr, buf->size);
            memset(&req, 0, sizeof(req));
            req.size = buf->size;
            req.phys_addr = buf->phys_addr;
            if (ioctl(g_mem_fd, IOCTL_MEM_FREE, &req) != 0) {
                LOG_DMA("Free: IOCTL_MEM_FREE failed for phys=0x%x (%s)", buf->phys_addr, strerror(errno));
            }
        } else {
            free(buf->virt_addr);
        }
    }

    unregister_buffer(buf);
    free(buf);
    return 0;
}

static int dma_alloc_descriptor_internal(int pool_id, IMPDMABufferInfo *info_out, int size, const char *tag)
{
    if (info_out == NULL || size <= 0) {
        LOG_DMA("Alloc: invalid parameters");
        return -1;
    }

    if (dma_init() < 0) {
        LOG_DMA("Alloc: initialization failed");
        return -1;
    }

    DMABufferRecord *buf = (DMABufferRecord*)calloc(1, sizeof(DMABufferRecord));
    if (buf == NULL) {
        LOG_DMA("Alloc: calloc failed");
        return -1;
    }

    if (tag_arg_looks_valid(tag)) {
        strncpy(buf->tag, tag, sizeof(buf->tag) - 1);
    } else {
        buf->tag[0] = '\0';
    }
    buf->name[0] = '\0';
    buf->size = (uint32_t)size;
    buf->pool_id = (pool_id >= 0) ? (uint32_t)pool_id : 0;

    /* The reserved arena is a bump allocator.  Serializing the complete
     * backend operation prevents concurrent callers from receiving the same
     * physical pages. */
    pthread_mutex_lock(&g_alloc_mutex);
    if (g_rmem_supported && g_mem_fd >= 0) {
        if (g_is_rmem && g_rmem_virt_base != NULL) {
            size_t align = 4096;
            size_t off = (g_rmem_offset + (align - 1)) & ~(align - 1);
            if (off + (size_t)size <= g_rmem_size) {
                buf->virt_addr = (void*)((uintptr_t)g_rmem_virt_base + off);
                buf->phys_addr = g_rmem_base_phys + (uint32_t)off;
                buf->flags |= 0x2;
                g_rmem_offset = off + (size_t)size;
                LOG_DMA("Alloc: %s size=%d phys=0x%x virt=%p (rmem off=0x%zx)",
                        buf->name[0] ? buf->name : "(unnamed)", size, buf->phys_addr, buf->virt_addr, off);
            } else {
                LOG_DMA("Alloc: /dev/rmem out of memory (requested=%d, used=%zu/%zu); refusing unsafe fallback",
                        size, g_rmem_offset, g_rmem_size);
            }
        } else if (g_is_avpu) {
            struct avpu_dma_info info;
            void *virt;

            memset(&info, 0, sizeof(info));
            info.size = (uint32_t)size;
            if (ioctl(g_mem_fd, AVPU_GET_DMA_MMAP, &info) == 0 &&
                info.phy_addr != 0) {
                virt = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED,
                            g_mem_fd, (off_t)info.fd);
                if (virt != MAP_FAILED) {
                    buf->virt_addr = virt;
                    buf->phys_addr = info.phy_addr;
                    buf->flags |= 0x4;
                    LOG_DMA("Alloc: %s size=%d phys=0x%x virt=%p (avpu coherent off=0x%x)",
                            buf->name[0] ? buf->name : "(unnamed)", size,
                            buf->phys_addr, buf->virt_addr, info.fd);
                } else {
                    LOG_DMA("Alloc: AVPU mmap failed for phys=0x%x off=0x%x (%s)",
                            info.phy_addr, info.fd, strerror(errno));
                }
            } else {
                LOG_DMA("Alloc: AVPU_GET_DMA_MMAP failed (%s)", strerror(errno));
            }
        } else {
            mem_alloc_req_t req;
            memset(&req, 0, sizeof(req));
            req.size = (uint32_t)size;
            req.align = 4096;
            if (ioctl(g_mem_fd, IOCTL_MEM_ALLOC, &req) == 0 && req.phys_addr != 0) {
                void *virt = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, g_mem_fd, (off_t)req.phys_addr);
                if (virt != MAP_FAILED) {
                    buf->virt_addr = virt;
                    buf->phys_addr = req.phys_addr;
                    buf->flags |= 0x1;
                    LOG_DMA("Alloc: %s size=%d phys=0x%x virt=%p (kernel)",
                            buf->name[0] ? buf->name : "(unnamed)", size, buf->phys_addr, buf->virt_addr);
                } else {
                    LOG_DMA("Alloc: mmap failed for phys=0x%x (%s)", req.phys_addr, strerror(errno));
                    if (ioctl(g_mem_fd, IOCTL_MEM_FREE, &req) != 0)
                        LOG_DMA("Alloc: failed to release phys=0x%x after mmap failure (%s)",
                                req.phys_addr, strerror(errno));
                }
            } else {
                LOG_DMA("Alloc: IOCTL_MEM_ALLOC failed (%s)", strerror(errno));
            }
        }
    }

    if (buf->virt_addr == NULL) {
        pthread_mutex_unlock(&g_alloc_mutex);
        LOG_DMA("Alloc: no DMA-backed storage for %s size=%d",
                buf->name[0] ? buf->name : "(unnamed)", size);
        free(buf);
        return -1;
    }
    pthread_mutex_unlock(&g_alloc_mutex);

    if (register_buffer(buf) < 0) {
        LOG_DMA("Alloc: failed to register buffer");
        if ((buf->flags & 0x2) && g_is_rmem) {
            /* RMEM bump allocations cannot be individually rolled back. */
        } else if ((buf->flags & 0x4) && g_is_avpu) {
            munmap(buf->virt_addr, buf->size);
        } else if ((buf->flags & 0x1) && g_rmem_supported && g_mem_fd >= 0) {
            munmap(buf->virt_addr, buf->size);
        } else {
            free(buf->virt_addr);
        }
        free(buf);
        return -1;
    }

    fill_dma_info(info_out, buf);
    return 0;
}

uintptr_t IMP_Alloc(void *name_or_size, intptr_t size, char *tag) {
    uintptr_t arg1 = (uintptr_t)name_or_size;

    if (looks_like_pointer_style_alloc(arg1, size, tag)) {
        IMPDMABufferInfo info;
        memset(&info, 0, sizeof(info));
        if (dma_alloc_descriptor_internal(-1, &info, (int)arg1, "compat") != 0) {
            return (uintptr_t)NULL;
        }
        LOG_DMA("Alloc compat: size=%u virt=0x%08x phys=0x%08x", (unsigned)arg1, info.virt_addr, info.phys_addr);
        return (uintptr_t)info.virt_addr;
    }

    return (uintptr_t)dma_alloc_descriptor_internal(-1, (IMPDMABufferInfo*)name_or_size, (int)size, tag);
}

uintptr_t IMP_PoolAlloc(int pool_id, void *name_or_size, intptr_t size, char *tag) {
    uintptr_t arg2 = (uintptr_t)name_or_size;

    if (looks_like_pointer_style_pool_alloc(arg2, size, tag)) {
        IMPDMABufferInfo info;
        memset(&info, 0, sizeof(info));
        if (dma_alloc_descriptor_internal(pool_id, &info, (int)arg2, "compat_pool") != 0) {
            return (uintptr_t)NULL;
        }
        LOG_DMA("PoolAlloc compat: pool=%d size=%u virt=0x%08x phys=0x%08x", pool_id, (unsigned)arg2, info.virt_addr, info.phys_addr);
        return (uintptr_t)info.virt_addr;
    }

    return (uintptr_t)dma_alloc_descriptor_internal(pool_id, (IMPDMABufferInfo*)name_or_size, (int)size, tag);
}

int IMP_Free(uintptr_t phys_or_virt_addr) {
    DMABufferRecord *buf = NULL;

    if (phys_or_virt_addr == 0) {
        return -1;
    }

    if (phys_or_virt_addr <= UINT32_MAX) {
        buf = lookup_buffer_by_phys((uint32_t)phys_or_virt_addr);
    }
    if (buf == NULL) {
        buf = lookup_buffer_containing_virt((const void*)phys_or_virt_addr, NULL);
    }
    if (buf == NULL && phys_or_virt_addr <= UINT32_MAX) {
        buf = lookup_buffer_containing_phys((uint32_t)phys_or_virt_addr, NULL);
    }
    if (buf == NULL) {
        LOG_DMA("Free: buffer not found in registry (arg=%p)", (void*)phys_or_virt_addr);
        return 0;
    }

    return dma_free_buffer(buf);
}

/**
 * IMP_Get_Info - Get buffer information
 * Based on decompilation at 0x16754
 */
int IMP_Get_Info(void *info_out, uint32_t phys_addr) {
    if (info_out == NULL || phys_addr == 0) {
        return -1;
    }

    /* Look up buffer by physical address */
    DMABufferRecord *buf = lookup_buffer_by_phys(phys_addr);
    if (buf == NULL) {
        LOG_DMA("Get_Info: buffer not found for phys=0x%x", phys_addr);
        return -1;
    }

    fill_dma_info((IMPDMABufferInfo*)info_out, buf);

    LOG_DMA("Get_Info: phys=0x%x, virt=%p, size=%u",
            phys_addr, buf->virt_addr, buf->size);

    return 0;
}

/**
 * IMP_FrameSource_GetPool - Get pool ID for a channel
 * Based on decompilation
 */
int IMP_FrameSource_GetPool(int chn) {
    (void)chn;

    /* Return -1 to indicate no pool available */
    /* This will cause VBM to use IMP_Alloc instead of IMP_PoolAlloc */
    return -1;
}

/**
 * Flush cache for DMA buffer
 */
int IMP_Flush_Cache(uint32_t phys_addr, uint32_t size) {
    if (phys_addr == 0 || size == 0) {
        return -1;
    }

    return IMP_FlushCache(DMA_PhysToVirt(phys_addr), size);
}

int DMA_AllocDescriptor(IMPDMABufferInfo *info_out, int size, const char *tag)
{
    return dma_alloc_descriptor_internal(-1, info_out, size, tag);
}

int DMA_PoolAllocDescriptor(int pool_id, IMPDMABufferInfo *info_out, int size, const char *tag)
{
    return dma_alloc_descriptor_internal(pool_id, info_out, size, tag);
}

int DMA_FreePhys(uint32_t phys_addr)
{
    return IMP_Free((uintptr_t)phys_addr);
}

void *DMA_PhysToVirt(uint32_t phys_addr)
{
    uint32_t offset = 0;
    DMABufferRecord *buf;

    if (phys_addr == 0) {
        return NULL;
    }

    buf = lookup_buffer_containing_phys(phys_addr, &offset);
    if (buf != NULL && buf->virt_addr != NULL) {
        return (void*)((uintptr_t)buf->virt_addr + offset);
    }

    if (dma_init() == 0 && g_is_rmem && g_rmem_virt_base != NULL &&
        phys_addr >= g_rmem_base_phys &&
        phys_addr < g_rmem_base_phys + g_rmem_size) {
        return (void*)((uintptr_t)g_rmem_virt_base + (phys_addr - g_rmem_base_phys));
    }

    return NULL;
}

uint32_t DMA_VirtToPhys(const void *virt_addr)
{
    uint32_t offset = 0;
    DMABufferRecord *buf;
    uintptr_t virt;

    if (virt_addr == NULL) {
        return 0;
    }

    buf = lookup_buffer_containing_virt(virt_addr, &offset);
    if (buf != NULL) {
        return buf->phys_addr + offset;
    }

    virt = (uintptr_t)virt_addr;
    if (dma_init() == 0 && g_is_rmem && g_rmem_virt_base != NULL) {
        uintptr_t base = (uintptr_t)g_rmem_virt_base;
        uintptr_t end = base + g_rmem_size;
        if (virt >= base && virt < end) {
            return g_rmem_base_phys + (uint32_t)(virt - base);
        }
    }

    return (uint32_t)virt;
}



int DMA_Get_RMEM_Base(uint32_t *base_phys_out)
{
    if (base_phys_out == NULL)
        return -1;
    if (g_is_rmem && g_rmem_virt_base != NULL) {
        *base_phys_out = g_rmem_base_phys;
        return 0;
    }
    return -1;
}

int DMA_Is_RMEM(void)
{
    return (g_is_rmem && g_rmem_virt_base != NULL) ? 1 : 0;
}

/* IMP_FlushCache - flush CPU cache for DMA coherency
 * Stub: on MIPS with coherent DMA, this is often a no-op */
int IMP_FlushCache(void *virt_addr, uint32_t size) {
    if (virt_addr == NULL || size == 0) {
        return -1;
    }

    if (dma_init() == 0 && g_rmem_supported && !g_is_rmem && g_mem_fd >= 0) {
        uint32_t phys_addr = DMA_VirtToPhys(virt_addr);
        if (phys_addr != 0) {
            mem_alloc_req_t req;
            memset(&req, 0, sizeof(req));
            req.phys_addr = phys_addr;
            req.size = size;
            if (ioctl(g_mem_fd, IOCTL_MEM_FLUSH, &req) != 0) {
                LOG_DMA("FlushCache: ioctl failed for phys=0x%x (%s)", phys_addr, strerror(errno));
            }
        }
    }

    return 0;
}

void *IMP_Phys_to_Virt(uint32_t phys_addr)
{
    return DMA_PhysToVirt(phys_addr);
}

uint32_t IMP_Virt_to_Phys(void *virt_addr)
{
    return DMA_VirtToPhys(virt_addr);
}

void IMP_PoolFree(void *ptr)
{
    (void)IMP_Free((uintptr_t)ptr);
}

int IMP_PoolFlushCache(void *ptr, uint32_t size)
{
    return IMP_FlushCache(ptr, size);
}

void *IMP_PoolPhys_to_Virt(uint32_t phys_addr)
{
    return DMA_PhysToVirt(phys_addr);
}

uint32_t IMP_PoolVirt_to_Phys(void *virt_addr)
{
    return DMA_VirtToPhys(virt_addr);
}

/* OEM-compatible rmem cache flush: ioctl(rmem_fd, 0xc00c7200, {vaddr, size, dir})
 * This is the exact ioctl the stock libimp's alloc_kmem_flush_cache() uses.
 * The rmem kernel module handles MIPS cache maintenance correctly for DMA. */
#define RMEM_IOCTL_FLUSH_CACHE  0xc00c7200  /* _IOWR('r', 0, 12-byte-struct) */

struct rmem_flush_info {
    unsigned int addr;   /* cached userspace address in the mapped rmem arena */
    unsigned int size;   /* size in bytes */
    unsigned int dir;    /* 1=WBACK, 2=INV */
};

int DMA_RmemFlushCache(void *virt_addr, uint32_t size, int dir)
{
    uintptr_t address;
    uintptr_t virtual_base;
    uint32_t phys_addr;
    static unsigned int flush_count;
    unsigned int index;
    int ret;

    if (!virt_addr || size == 0) return -1;
    if (dma_init() != 0 || g_mem_fd < 0) return -1;

    if (g_is_devmem_rmem) {
        uintptr_t address = (uintptr_t)virt_addr;
        uintptr_t base = (uintptr_t)g_rmem_virt_base;
        uint64_t offset;

        if (address >= base && address - base < g_rmem_size) {
            offset = address - base;
        } else if (address >= g_rmem_base_phys &&
                   address - g_rmem_base_phys < g_rmem_size) {
            offset = address - g_rmem_base_phys;
        } else {
            LOG_DMA("DevmemFlushCache: address %p is outside reserved arena",
                    virt_addr);
            return -1;
        }
        if (offset + size > g_rmem_size) {
            LOG_DMA("DevmemFlushCache: range address=%p size=0x%x exceeds reserved arena",
                    virt_addr, size);
            return -1;
        }

        /* O_SYNC /dev/mem mappings are uncached on MIPS, so CPU stores and
         * device DMA already observe the same physical bytes. */
        index = __sync_add_and_fetch(&flush_count, 1);
        if (index <= 12) {
            LOG_DMA("DevmemFlushCache: uncached virt=%p phys=0x%08x size=0x%x dir=%d [#%u]",
                    virt_addr, g_rmem_base_phys + (uint32_t)offset, size, dir,
                    index);
        }
        return 0;
    }

    if (g_is_avpu) {
        struct avpu_flush_cache_info info;
        DMABufferRecord *buf;

        buf = lookup_buffer_containing_virt(virt_addr, NULL);
        if (buf == NULL)
            buf = lookup_buffer_containing_phys((uint32_t)(uintptr_t)virt_addr,
                                                NULL);
        if (buf == NULL) {
            LOG_DMA("AvpuFlushCache: address %p is outside DMA allocations",
                    virt_addr);
            return -1;
        }

        info.addr = (uint32_t)(uintptr_t)virt_addr;
        info.len = size;
        info.dir = (uint32_t)dir;
        ret = ioctl(g_mem_fd, AVPU_FLUSH_CACHE, &info);
        index = __sync_add_and_fetch(&flush_count, 1);
        if (index <= 12 || ret != 0) {
            LOG_DMA("AvpuFlushCache: virt=%p phys=0x%08x size=0x%x dir=%d ret=%d [#%u]",
                    virt_addr, buf->phys_addr, size, dir, ret, index);
        }
        return ret;
    }

    /*
     * T31's 3.10 rmem driver passes info.addr straight to
     * dma_cache_sync().  It therefore requires the cached userspace alias,
     * not the physical DMA address.  Keep the physical translation below
     * solely for validating that the requested range belongs to our arena
     * and for diagnostics.
     */
    address = (uintptr_t)virt_addr;
    virtual_base = (uintptr_t)g_rmem_virt_base;
    if (g_is_rmem &&
        address >= virtual_base &&
        address - virtual_base < g_rmem_size) {
        phys_addr = g_rmem_base_phys + (uint32_t)(address - virtual_base);
    } else if (address >= g_rmem_base_phys &&
               address - g_rmem_base_phys < g_rmem_size) {
        /* Accept a physical address from legacy callers, but convert it back
         * to the cached alias before issuing the ioctl. */
        phys_addr = (uint32_t)address;
        address = virtual_base + (address - g_rmem_base_phys);
    } else {
        LOG_DMA("RmemFlushCache: address %p is outside rmem mapping", virt_addr);
        return -1;
    }

    if ((uint64_t)(phys_addr - g_rmem_base_phys) + size > g_rmem_size) {
        LOG_DMA("RmemFlushCache: range phys=0x%08x size=0x%x exceeds rmem",
                phys_addr, size);
        return -1;
    }

    struct rmem_flush_info info;
    info.addr = (unsigned int)address;
    info.size = size;
    info.dir = (unsigned int)dir;
    ret = ioctl(g_mem_fd, RMEM_IOCTL_FLUSH_CACHE, &info);
    index = __sync_add_and_fetch(&flush_count, 1);
    if (index <= 12 || ret != 0) {
        LOG_DMA("RmemFlushCache: virt=%p phys=0x%08x size=0x%x dir=%d ret=%d [#%u]",
                virt_addr, phys_addr, size, dir, ret, index);
    }
    return ret;
}

/* ========== IMP_MemPool — Pool management (from OEM BN audit) ========== */

#define MAX_MEM_POOLS 16

typedef struct {
    int in_use;
    int pool_id;
    uint32_t size;
    uint32_t phys_base;
    void *virt_base;
} MemPoolEntry;

static MemPoolEntry g_mem_pools[MAX_MEM_POOLS];
static pthread_mutex_t mempool_mutex = PTHREAD_MUTEX_INITIALIZER;

int IMP_MemPool_InitPool(int pool_id, uint32_t size, int flags) {
    IMPDMABufferInfo info;
    MemPoolEntry *pool;

    if (pool_id < 0 || pool_id >= MAX_MEM_POOLS) {
        LOG_DMA("MemPool_InitPool: invalid pool_id %d", pool_id);
        return -1;
    }
    if (size == 0 || size > kCompatMaxAllocSize) {
        LOG_DMA("MemPool_InitPool: invalid size %u", size);
        return -1;
    }

    pthread_mutex_lock(&mempool_mutex);

    pool = &g_mem_pools[pool_id];
    if (pool->in_use) {
        LOG_DMA("MemPool_InitPool: pool %d already in use", pool_id);
        pthread_mutex_unlock(&mempool_mutex);
        return -1;
    }

    /* This path needs both addresses.  Calling the dual-ABI IMP_Alloc with a
     * NULL descriptor returns a status value, not an allocation address. */
    memset(&info, 0, sizeof(info));
    if (DMA_AllocDescriptor(&info, (int)size, "mempool") != 0 ||
        info.phys_addr == 0 || info.virt_addr == 0) {
        LOG_DMA("MemPool_InitPool: DMA allocation failed for pool %d, size %u",
                pool_id, size);
        pthread_mutex_unlock(&mempool_mutex);
        return -1;
    }

    pool->in_use = 1;
    pool->pool_id = pool_id;
    pool->size = size;
    pool->phys_base = info.phys_addr;
    pool->virt_base = (void *)(uintptr_t)info.virt_addr;
    (void)flags;

    pthread_mutex_unlock(&mempool_mutex);
    LOG_DMA("MemPool_InitPool: pool=%d size=%u phys=0x%x",
            pool_id, size, info.phys_addr);
    return 0;
}

int IMP_MemPool_Release(int pool_id) {
    if (pool_id < 0 || pool_id >= MAX_MEM_POOLS) {
        LOG_DMA("MemPool_Release: invalid pool_id %d", pool_id);
        return -1;
    }

    pthread_mutex_lock(&mempool_mutex);

    MemPoolEntry *pool = &g_mem_pools[pool_id];
    if (!pool->in_use) {
        pthread_mutex_unlock(&mempool_mutex);
        return 0;
    }

    if (pool->phys_base != 0) {
        IMP_Free((uintptr_t)pool->phys_base);
    }

    memset(pool, 0, sizeof(*pool));
    pthread_mutex_unlock(&mempool_mutex);
    LOG_DMA("MemPool_Release: pool=%d freed", pool_id);
    return 0;
}

int IMP_MemPool_GetById(int pool_id, void *info_out) {
    if (pool_id < 0 || pool_id >= MAX_MEM_POOLS || info_out == NULL) {
        return -1;
    }

    pthread_mutex_lock(&mempool_mutex);

    MemPoolEntry *pool = &g_mem_pools[pool_id];
    if (!pool->in_use) {
        pthread_mutex_unlock(&mempool_mutex);
        return -1;
    }

    /* OEM returns pool info — fill a generic struct with phys/virt/size */
    IMPDMABufferInfo *out = (IMPDMABufferInfo *)info_out;
    memset(out, 0, sizeof(*out));
    snprintf(out->name, sizeof(out->name), "mempool_%d", pool_id);
    out->phys_addr = pool->phys_base;
    out->virt_addr = (uint32_t)(uintptr_t)pool->virt_base;
    out->size = pool->size;
    out->pool_id = (uint32_t)pool_id;

    pthread_mutex_unlock(&mempool_mutex);
    return 0;
}

/* ========== IMP_Alloc debug/attr functions (from OEM BN audit) ========== */

/* Per-buffer allocation attributes (OEM stores alignment, cache policy, etc.) */
typedef struct {
    uint32_t alignment;
    uint32_t cache_policy;  /* 0=cached, 1=uncached */
} AllocAttr;

static AllocAttr g_alloc_attr = {0x1000, 0}; /* Default: 4K alignment, cached */
static AllocAttr g_pool_alloc_attr = {0x1000, 0};

uintptr_t IMP_Sp_Alloc(int size, char *tag) {
    IMPDMABufferInfo info;

    /* OEM: special-purpose allocation — same as IMP_Alloc but with specific flags.
     * Routes through the same DMA allocator. */
    if (size <= 0)
        return 0;
    memset(&info, 0, sizeof(info));
    if (DMA_AllocDescriptor(&info, size, tag) != 0)
        return 0;
    return (uintptr_t)info.virt_addr;
}

int IMP_Alloc_Set_Attr(uint32_t alignment, uint32_t cache_policy) {
    g_alloc_attr.alignment = (alignment > 0) ? alignment : 0x1000;
    g_alloc_attr.cache_policy = cache_policy;
    LOG_DMA("Alloc_Set_Attr: align=%u, cache=%u", g_alloc_attr.alignment, g_alloc_attr.cache_policy);
    return 0;
}

int IMP_Alloc_Get_Attr(uint32_t *alignment, uint32_t *cache_policy) {
    if (alignment) *alignment = g_alloc_attr.alignment;
    if (cache_policy) *cache_policy = g_alloc_attr.cache_policy;
    return 0;
}

int IMP_Alloc_Dump(void) {
    pthread_mutex_lock(&g_registry_mutex);
    int count = 0;
    size_t total_size = 0;

    LOG_DMA("===== IMP_Alloc_Dump =====");
    for (int i = 0; i < MAX_DMA_BUFFERS; i++) {
        DMABufferRecord *buf = g_buffer_registry[i];
        if (buf != NULL) {
            LOG_DMA("  [%d] phys=0x%08x virt=%p size=%u tag=%s",
                    i, buf->phys_addr, buf->virt_addr, buf->size, buf->tag);
            total_size += buf->size;
            count++;
        }
    }
    LOG_DMA("  Total: %d buffers, %zu bytes", count, total_size);
    LOG_DMA("==========================");

    pthread_mutex_unlock(&g_registry_mutex);
    return 0;
}

int IMP_Alloc_Dump_To_File(const char *path) {
    if (!path) return -1;

    FILE *fp = fopen(path, "w");
    if (!fp) {
        LOG_DMA("Alloc_Dump_To_File: cannot open %s: %s", path, strerror(errno));
        return -1;
    }

    pthread_mutex_lock(&g_registry_mutex);
    int count = 0;
    size_t total_size = 0;

    fprintf(fp, "===== IMP_Alloc_Dump =====\n");
    for (int i = 0; i < MAX_DMA_BUFFERS; i++) {
        DMABufferRecord *buf = g_buffer_registry[i];
        if (buf != NULL) {
            fprintf(fp, "[%d] phys=0x%08x virt=%p size=%u pool=%u tag=%s name=%s\n",
                    i, buf->phys_addr, buf->virt_addr, buf->size,
                    buf->pool_id, buf->tag, buf->name);
            total_size += buf->size;
            count++;
        }
    }
    fprintf(fp, "Total: %d buffers, %zu bytes\n", count, total_size);

    pthread_mutex_unlock(&g_registry_mutex);
    fclose(fp);
    return 0;
}

int IMP_PoolAlloc_Set_Attr(uint32_t alignment, uint32_t cache_policy) {
    g_pool_alloc_attr.alignment = (alignment > 0) ? alignment : 0x1000;
    g_pool_alloc_attr.cache_policy = cache_policy;
    LOG_DMA("PoolAlloc_Set_Attr: align=%u, cache=%u",
            g_pool_alloc_attr.alignment, g_pool_alloc_attr.cache_policy);
    return 0;
}

int IMP_PoolAlloc_Get_Attr(uint32_t *alignment, uint32_t *cache_policy) {
    if (alignment) *alignment = g_pool_alloc_attr.alignment;
    if (cache_policy) *cache_policy = g_pool_alloc_attr.cache_policy;
    return 0;
}

int IMP_PoolAlloc_Dump(void) {
    LOG_DMA("===== IMP_PoolAlloc_Dump =====");

    pthread_mutex_lock(&mempool_mutex);
    for (int i = 0; i < MAX_MEM_POOLS; i++) {
        MemPoolEntry *pool = &g_mem_pools[i];
        if (pool->in_use) {
            LOG_DMA("  Pool[%d]: phys=0x%08x virt=%p size=%u",
                    i, pool->phys_base, pool->virt_base, pool->size);
        }
    }
    pthread_mutex_unlock(&mempool_mutex);

    /* Also dump pool-allocated buffers from the registry */
    pthread_mutex_lock(&g_registry_mutex);
    int count = 0;
    for (int i = 0; i < MAX_DMA_BUFFERS; i++) {
        DMABufferRecord *buf = g_buffer_registry[i];
        if (buf != NULL && buf->pool_id > 0) {
            LOG_DMA("  Buf[%d] pool=%u phys=0x%08x size=%u tag=%s",
                    i, buf->pool_id, buf->phys_addr, buf->size, buf->tag);
            count++;
        }
    }
    LOG_DMA("  Pool buffers: %d", count);
    LOG_DMA("==============================");
    pthread_mutex_unlock(&g_registry_mutex);
    return 0;
}
