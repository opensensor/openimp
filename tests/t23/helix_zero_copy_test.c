/* Host test of the T23 Helix bridge input path: frames in OpenIMP's
 * reserved memory go to the worker by physical address (no copy), others
 * and OPENIMP_T23_HELIX_COPY=1 are copied, and a worker that cannot reach
 * a frame turns zero-copy off with that frame still encoded. */
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "dma_alloc.h"
#include "t23/openimp_t23_helix_bridge.h"

#define WIDTH 64u
#define HEIGHT 32u
#define FRAME_SIZE (WIDTH * HEIGHT * 3u / 2u)
#define FAKE_UNREACHABLE_PHYS 0xdead0000u

/* --- stubs for the libimp parts the bridge uses --- */
static uintptr_t rmem_begin, rmem_end;
static uintptr_t unreachable_begin;
static unsigned int writebacks;

int openimp_t23_persist_enabled(void) { return 0; }
void openimp_t23_persist_write(const char *message, size_t size)
{
    (void)message;
    (void)size;
}

uint32_t DMA_VirtToPhys(const void *virt)
{
    uintptr_t v = (uintptr_t)virt;

    if (v == unreachable_begin)
        return FAKE_UNREACHABLE_PHYS;
    if (v >= rmem_begin && v < rmem_end)
        return (uint32_t)(v + 0x1000u);
    return (uint32_t)v;     /* the real one's "unknown" answer */
}

int DMA_RmemFlushCache(void *virt, uint32_t size, int dir)
{
    (void)size;
    assert(dir == 1);
    if (DMA_VirtToPhys(virt) == (uint32_t)(uintptr_t)virt)
        return -1;
    writebacks++;
    return 0;
}

/* the worker start reserves a Helix rmem slice: only its address is kept */
#define FAKE_SLICE_PHYS 0x02a00000u

int DMA_AllocDescriptor(IMPDMABufferInfo *info_out, int size, const char *tag)
{
    (void)size;
    (void)tag;
    memset(info_out, 0, sizeof(*info_out));
    info_out->phys_addr = FAKE_SLICE_PHYS;
    return 0;
}

int DMA_FreePhys(uint32_t phys_addr)
{
    return phys_addr == FAKE_SLICE_PHYS ? 0 : -1;
}

void DMA_LogRmem(const char *reason)
{
    (void)reason;
}
/* --- */

static unsigned char *frame_memory(void)
{
    unsigned char *p = mmap(NULL, FRAME_SIZE, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    uint32_t i;

    assert(p != MAP_FAILED);
    for (i = 0; i < FRAME_SIZE; i++)
        p[i] = (unsigned char)(i * 7u + 3u);
    return p;
}

static uint32_t checksum(const unsigned char *p)
{
    uint32_t value = 0u, i;

    for (i = 0; i < FRAME_SIZE; i++)
        value = value * 31u + p[i];
    return value;
}

static void encode(T23HelixBridge *bridge, unsigned char *virt,
                   uint32_t phys, char *kind, unsigned int *flags,
                   uint32_t *value)
{
    IMPFrameInfo frame;
    unsigned char out[16];
    uint32_t length = sizeof(out);

    memset(&frame, 0, sizeof(frame));
    frame.width = WIDTH;
    frame.height = HEIGHT;
    frame.size = FRAME_SIZE;
    frame.virAddr = (uint32_t)(uintptr_t)virt;
    frame.phyAddr = phys;
    assert(OpenIMP_T23_HelixEncodeInto(bridge, &frame, out, &length) == 0);
    assert(length == 6u);
    *kind = (char)out[0];
    *flags = out[1];
    memcpy(value, out + 2, sizeof(*value));
}

static int window_untouched(const T23HelixBridge *bridge)
{
    const unsigned char *p = bridge->shared_buffer;
    uint32_t i;

    for (i = 0; i < bridge->input_size; i++)
        if (p[i])
            return 0;
    return 1;
}

int main(int argc, char **argv)
{
    T23HelixBridge bridge;
    T23EncoderYuvIn input;
    unsigned char *pool, *heap, *unreachable;
    uint32_t pool_phys, value;
    unsigned int flags;
    char kind;
    int copy_mode = argc > 1 && strcmp(argv[1], "copy") == 0;

    assert(argc > 2);
    setenv("OPENIMP_T23_HELIX_HELPER", argv[2], 1);
    if (copy_mode)
        setenv("OPENIMP_T23_HELIX_COPY", "1", 1);
    pool = frame_memory();
    heap = frame_memory();
    unreachable = frame_memory();
    rmem_begin = (uintptr_t)pool;
    rmem_end = rmem_begin + FRAME_SIZE;
    unreachable_begin = (uintptr_t)unreachable;
    pool_phys = (uint32_t)((uintptr_t)pool + 0x1000u);

    memset(&input, 0, sizeof(input));
    memset(&bridge, 0, sizeof(bridge));
    assert(OpenIMP_T23_HelixInitYuv(&bridge, WIDTH, HEIGHT, &input) == 0);

    /* A pool frame: by physical address, written back, nothing copied. */
    encode(&bridge, pool, pool_phys, &kind, &flags, &value);
    if (copy_mode) {
        assert(kind == 'C' && value == checksum(pool) && flags == 0u);
        assert(writebacks == 0u);
    } else {
        assert(kind == 'P' && value == pool_phys);
        assert(flags == T23_HELIX_INIT_ZERO_COPY);
        assert(writebacks == 1u);
        assert(window_untouched(&bridge));
    }

    /* A frame whose physical address does not match its mapping (user
     * memory): copied. */
    encode(&bridge, heap, 0x12345000u, &kind, &flags, &value);
    assert(kind == 'C' && value == checksum(heap));

    if (!copy_mode) {
        /* The worker cannot map it: this frame is copied and zero-copy is
         * off for the session. */
        encode(&bridge, unreachable, FAKE_UNREACHABLE_PHYS, &kind, &flags,
               &value);
        assert(kind == 'C' && value == checksum(unreachable));
        assert(!bridge.zero_copy);
        encode(&bridge, pool, pool_phys, &kind, &flags, &value);
        assert(kind == 'C' && value == checksum(pool));
    }

    OpenIMP_T23_HelixExit(&bridge);
    printf("T23 Helix %s input tests passed\n",
           copy_mode ? "copy" : "zero-copy");
    return 0;
}
