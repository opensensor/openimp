/* P2 DMA adapter for the recovered Allegro/AVPU userspace backend.
 *
 * FrameSource and the encoder must allocate from one monotonically advancing
 * T40 rmem arena.  The older OpenIMP allocator assumed a T31 mapping at offset
 * zero; that is fatal on this T40XP.  This adapter starts after P1's live
 * capture allocations and maps /dev/rmem at the physical rmem base.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define P2_RMEM_SIZE (96U * 1024U * 1024U)
#define P2_RMEM_FLUSH_IOCTL 0xc00c7200U

typedef struct {
    char name[96];
    char tag[32];
    uint32_t virt_addr;
    uint32_t phys_addr;
    uint32_t size;
    uint32_t flags;
    uint32_t pool_id;
} IMPDMABufferInfo;

struct p2_flush_info {
    uint32_t address;
    uint32_t length;
    uint32_t direction;
};

extern int OpenIMP_P1_GetState(uint32_t *, uint32_t *, uint32_t *,
                               uint32_t *, int32_t *, uint32_t *, uint32_t *);

static struct {
    int fd;
    uint32_t base;
    uint32_t size;
    uint32_t next;
    void *mapping;
    volatile int lock;
} p2_dma = { -1, 0, P2_RMEM_SIZE, 0, NULL, 0 };

static void p2_lock(void)
{
    while (__sync_lock_test_and_set(&p2_dma.lock, 1))
        usleep(1000);
}

static void p2_unlock(void)
{
    __sync_lock_release(&p2_dma.lock);
}

static uint32_t align_page(uint32_t value)
{
    return (value + 4095U) & ~4095U;
}

static int p2_rmem_from_cmdline(uint32_t *base_out, uint32_t *size_out)
{
    char command_line[1024];
    const char *value;
    char *end;
    unsigned long long size;
    unsigned long base;
    ssize_t count;
    int fd;

    if (!base_out || !size_out)
        return -1;
    fd = open("/proc/cmdline", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    count = read(fd, command_line, sizeof(command_line) - 1u);
    close(fd);
    if (count <= 0)
        return -1;
    command_line[count] = '\0';
    value = strstr(command_line, "rmem=");
    if (!value)
        return -1;
    value += 5;
    errno = 0;
    size = strtoull(value, &end, 0);
    if (errno || end == value || !size)
        return -1;
    if (*end == 'K' || *end == 'k') {
        size *= 1024u;
        ++end;
    } else if (*end == 'M' || *end == 'm') {
        size *= 1024u * 1024u;
        ++end;
    } else if (*end == 'G' || *end == 'g') {
        size *= 1024u * 1024u * 1024u;
        ++end;
    }
    if (*end != '@' || size > UINT32_MAX)
        return -1;
    errno = 0;
    base = strtoul(end + 1, &end, 0);
    if (errno || base > UINT32_MAX ||
        (*end != '\0' && *end != ' ' && *end != '\n'))
        return -1;
    *base_out = (uint32_t)base;
    *size_out = (uint32_t)size;
    return *base_out ? 0 : -1;
}

static int p2_dma_prepare(void)
{
    uint32_t flags = 0, channels = 0, frames = 0, command = 0;
    uint32_t base = 0, used = 0;
    uint32_t command_line_base = 0;
    uint32_t command_line_size = 0;
    int32_t saved_errno = 0;

    if (p2_dma.mapping)
        return 0;
    if (OpenIMP_P1_GetState(&flags, &channels, &frames, &command,
                            &saved_errno, &base, &used) < 0 || !base) {
        if (p2_rmem_from_cmdline(&base, &command_line_size) < 0)
            return -1;
        used = 0;
    } else if (p2_rmem_from_cmdline(&command_line_base,
                                    &command_line_size) == 0 &&
               command_line_base != base) {
        errno = EINVAL;
        return -1;
    }
    if (command_line_size)
        p2_dma.size = command_line_size;
    p2_dma.fd = open("/dev/rmem", O_RDWR | O_SYNC);
    if (p2_dma.fd < 0)
        return -1;
    p2_dma.mapping = mmap(NULL, p2_dma.size, PROT_READ | PROT_WRITE,
                          MAP_SHARED, p2_dma.fd, (off_t)base);
    if (p2_dma.mapping == MAP_FAILED) {
        p2_dma.mapping = NULL;
        close(p2_dma.fd);
        p2_dma.fd = -1;
        return -1;
    }
    p2_dma.base = base;
    p2_dma.next = align_page(used);
    return 0;
}

int DMA_AllocDescriptor(IMPDMABufferInfo *out, int size, const char *tag)
{
    uint32_t start;

    if (!out || size <= 0)
        return -1;
    p2_lock();
    if (p2_dma_prepare() < 0) {
        p2_unlock();
        return -1;
    }
    start = align_page(p2_dma.next);
    if (start > p2_dma.size || (uint32_t)size > p2_dma.size - start) {
        p2_unlock();
        errno = ENOMEM;
        return -1;
    }
    memset(out, 0, sizeof(*out));
    if (tag)
        strncpy(out->tag, tag, sizeof(out->tag) - 1U);
    out->virt_addr = (uint32_t)(uintptr_t)
        ((unsigned char *)p2_dma.mapping + start);
    out->phys_addr = p2_dma.base + start;
    out->size = (uint32_t)size;
    out->flags = 2U;
    p2_dma.next = start + align_page((uint32_t)size);
    p2_unlock();
    return 0;
}

int DMA_RmemFlushCache(void *address, uint32_t length, int direction)
{
    struct p2_flush_info info;

    if (!address || !length)
        return -1;
    p2_lock();
    if (p2_dma_prepare() < 0) {
        p2_unlock();
        return -1;
    }
    info.address = (uint32_t)(uintptr_t)address;
    info.length = length;
    info.direction = (uint32_t)direction;
    direction = ioctl(p2_dma.fd, P2_RMEM_FLUSH_IOCTL, &info);
    p2_unlock();
    return direction;
}

int OpenIMP_P2_DMAState(uint32_t *base, uint32_t *used)
{
    if (base)
        *base = p2_dma.base;
    if (used)
        *used = p2_dma.next;
    return p2_dma.mapping ? 0 : -1;
}

/* Public T40 allocator compatibility.  libimp consumers use both the
 * descriptor ABI (IMP_Alloc(info, size, tag) -> status) and older pointer ABI
 * (IMP_Alloc(size) -> virtual address).  A valid MIPS userspace pointer is
 * well above the largest supported allocation, so the first argument makes
 * the two forms unambiguous on this target. */
uintptr_t IMP_Alloc(void *info_or_size, intptr_t size, char *tag)
{
    uintptr_t first = (uintptr_t)info_or_size;
    IMPDMABufferInfo info;

    if (first > 0 && first <= P2_RMEM_SIZE) {
        if (DMA_AllocDescriptor(&info, (int)first, "imp") != 0)
            return (uintptr_t)0;
        return (uintptr_t)info.virt_addr;
    }
    return (uintptr_t)DMA_AllocDescriptor((IMPDMABufferInfo *)info_or_size,
                                           (int)size, tag);
}

uintptr_t IMP_PoolAlloc(int pool_id, void *info_or_size, intptr_t size,
                        char *tag)
{
    uintptr_t second = (uintptr_t)info_or_size;
    IMPDMABufferInfo info;
    int result;

    if (second > 0 && second <= P2_RMEM_SIZE) {
        result = DMA_AllocDescriptor(&info, (int)second, "pool");
        if (result != 0)
            return (uintptr_t)0;
        info.pool_id = (uint32_t)pool_id;
        return (uintptr_t)info.virt_addr;
    }
    result = DMA_AllocDescriptor((IMPDMABufferInfo *)info_or_size,
                                 (int)size, tag);
    if (result == 0)
        ((IMPDMABufferInfo *)info_or_size)->pool_id = (uint32_t)pool_id;
    return (uintptr_t)result;
}

int IMP_Free(uintptr_t address)
{
    /* P1/P2 deliberately share a monotonic rmem arena.  Individual buffers
     * cannot be returned safely; the whole mapping is reclaimed on process
     * exit.  Treat an in-arena address as a successful logical free. */
    uintptr_t virtual_base;

    if (!address || p2_dma_prepare() != 0)
        return -1;
    virtual_base = (uintptr_t)p2_dma.mapping;
    if ((address >= p2_dma.base && address < p2_dma.base + p2_dma.size) ||
        (address >= virtual_base && address < virtual_base + p2_dma.size))
        return 0;
    return -1;
}

int IMP_FlushCache(void *address, uint32_t length)
{
    return DMA_RmemFlushCache(address, length, 1);
}

void *IMP_Phys_to_Virt(uint32_t physical)
{
    if (p2_dma_prepare() != 0 || physical < p2_dma.base ||
        physical >= p2_dma.base + p2_dma.size)
        return NULL;
    return (unsigned char *)p2_dma.mapping + (physical - p2_dma.base);
}

uint32_t IMP_Virt_to_Phys(void *virtual_address)
{
    uintptr_t address = (uintptr_t)virtual_address;
    uintptr_t base;

    if (p2_dma_prepare() != 0)
        return 0;
    base = (uintptr_t)p2_dma.mapping;
    if (address < base || address >= base + p2_dma.size)
        return 0;
    return p2_dma.base + (uint32_t)(address - base);
}

void IMP_PoolFree(void *address)
{
    (void)IMP_Free((uintptr_t)address);
}

int IMP_PoolFlushCache(void *address, uint32_t length)
{
    return IMP_FlushCache(address, length);
}

void *IMP_PoolPhys_to_Virt(uint32_t physical)
{
    return IMP_Phys_to_Virt(physical);
}

uint32_t IMP_PoolVirt_to_Phys(void *virtual_address)
{
    return IMP_Virt_to_Phys(virtual_address);
}
