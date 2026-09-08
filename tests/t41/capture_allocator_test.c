/* Real P1 lifecycle + shared DMA ledger, private RAM and a fake driver. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#define ioctl test_ioctl
#include "../../src/t40/openimp_p1.c"
#include "../../src/t40/openimp_p2_dma.c"
#undef ioctl

static unsigned char arena[4 * 1024 * 1024] __attribute__((aligned(4096)));
static unsigned int queued[2], streaming[2];
static unsigned long fail_command;
static int fail_queue_release;

int test_ioctl(int fd, unsigned long command, ...)
{
    va_list args;
    uint32_t *words;
    unsigned int channel = fd - 42;

    assert(channel < 2);
    va_start(args, command);
    words = va_arg(args, uint32_t *);
    va_end(args);
    if (command == fail_command) {
        fail_command = 0;
        errno = EIO;
        return -1;
    }
    switch (command) {
    case TISP_VIDIOC_SET_FRAME_FORMAT: {
        struct tisp_frame_format *format = (void *)words;
        assert(!streaming[channel]);
        format->pix.sizeimage = format->pix.width * format->pix.height * 3 / 2;
        break;
    }
    case TISP_VIDIOC_REQBUFS:
        if (!words[0]) {
            if (streaming[channel] || fail_queue_release) {
                errno = EBUSY;
                return -1;
            }
            queued[channel] = 0;
        }
        break;
    case TISP_VIDIOC_QBUF:
        assert(words[0] < 4 && words[13] >= p2_dma.base);
        ++queued[channel];
        break;
    case TISP_VIDIOC_STREAMON:
        assert(queued[channel] == 2);
        streaming[channel] = 1;
        break;
    case TISP_VIDIOC_STREAMOFF:
        streaming[channel] = 0;
        break;
    default:
        assert(0);
    }
    return 0;
}

static unsigned int live_allocations(void)
{
    unsigned int count = 0, i, j;

    for (i = 0; i < P2_DMA_MAX_ALLOCS; ++i) {
        const struct p2_dma_allocation *a = &p2_dma.allocations[i];
        if (!a->active)
            continue;
        ++count;
        assert(a->start >= p2_dma.floor && a->size <= p2_dma.size - a->start);
        for (j = i + 1; j < P2_DMA_MAX_ALLOCS; ++j) {
            const struct p2_dma_allocation *b = &p2_dma.allocations[j];
            if (b->active)
                assert(a->start + a->size <= b->start ||
                       b->start + b->size <= a->start);
        }
    }
    return count;
}

static void check_codec(const IMPDMABufferInfo *codec)
{
    const unsigned char *bytes = (const void *)(uintptr_t)codec->virt_addr;
    unsigned int i;

    for (i = 0; i < codec->size; ++i)
        assert(bytes[i] == 0xa5);
}

int main(void)
{
    IMPDMABufferInfo codec, extra;
    unsigned int i, channel, steady_used;
    static const unsigned long failures[] = {
        TISP_VIDIOC_SET_FRAME_FORMAT, TISP_VIDIOC_REQBUFS,
        TISP_VIDIOC_QBUF, TISP_VIDIOC_STREAMON,
    };

    /* Only hardware discovery/mapping is replaced. All allocation, free and
     * public FrameSource enable/disable code below is the production code. */
    p2_dma.base = 0x6000000;
    p2_dma.size = sizeof(arena);
    p2_dma.mapping = arena;
    p2_dma.floor = p2_dma.next = 4096;
    prepare_p1();
    for (channel = 0; channel < 2; ++channel) {
        struct openimp_fs_channel *chn = &p1.channels[channel];
        chn->created = 1;
        chn->fd = 42 + channel;
        chn->attr.picWidth = channel ? 160 : 320;
        chn->attr.picHeight = channel ? 120 : 240;
        chn->attr.pixFmt = TISP_PIX_FMT_NV12_ENUM;
        chn->attr.nrVBs = 2;
    }
    assert(!IMP_FrameSource_EnableChn(0));
    assert(!DMA_AllocDescriptor(&codec, 512 * 1024, "codec-live"));
    memset((void *)(uintptr_t)codec.virt_addr, 0xa5, codec.size);
    /* This is the old overlap: capture extends after the encoder took the
     * snapshot of P1's floor. Its zero-fill must not touch the live codec. */
    assert(!IMP_FrameSource_EnableChn(1));
    check_codec(&codec);
    assert(live_allocations() == 5);
    steady_used = p2_dma.next;
    for (i = 0; i < 100; ++i) {
        channel = i & 1;
        assert(!IMP_FrameSource_DisableChn(channel));
        assert(live_allocations() == 3);
        assert(!IMP_FrameSource_EnableChn(channel));
        assert(live_allocations() == 5 && p2_dma.next == steady_used);
        check_codec(&codec);
    }
    /* Geometry changes must allocate a new non-overlapping extent, then
     * release it so returning to the original size has bounded usage. */
    assert(!IMP_FrameSource_DisableChn(1));
    p1.channels[1].attr.picWidth = 800;
    p1.channels[1].attr.picHeight = 480;
    assert(!IMP_FrameSource_EnableChn(1));
    assert(live_allocations() == 5);
    check_codec(&codec);
    assert(!IMP_FrameSource_DisableChn(1));
    p1.channels[1].attr.picWidth = 160;
    p1.channels[1].attr.picHeight = 120;
    assert(!IMP_FrameSource_EnableChn(1));
    assert(p2_dma.next == steady_used);
    fail_command = TISP_VIDIOC_STREAMOFF;
    assert(IMP_FrameSource_DisableChn(1) < 0);
    assert(p1.channels[1].enabled && live_allocations() == 5);
    fail_queue_release = 1;
    assert(IMP_FrameSource_DisableChn(1) < 0);
    assert(!p1.channels[1].enabled && live_allocations() == 5);
    assert(!DMA_AllocDescriptor(&extra, 65536, "queue-still-owned"));
    assert(live_allocations() == 6);
    assert(!DMA_FreePhys(extra.phys_addr));
    fail_queue_release = 0;
    assert(!IMP_FrameSource_DisableChn(1));
    for (i = 0; i < sizeof(failures) / sizeof(failures[0]); ++i) {
        fail_command = failures[i];
        assert(IMP_FrameSource_EnableChn(1) < 0);
        assert(!p1.channels[1].enabled && live_allocations() == 3);
        assert(!IMP_FrameSource_EnableChn(1));
        assert(!IMP_FrameSource_DisableChn(1));
    }
    /* Failed setup plus a driver that cannot relinquish its queue retains
     * the allocations, and a later retry must reclaim them exactly once. */
    fail_command = TISP_VIDIOC_QBUF;
    fail_queue_release = 1;
    assert(IMP_FrameSource_EnableChn(1) < 0);
    assert(live_allocations() == 4);
    fail_queue_release = 0;
    assert(!IMP_FrameSource_EnableChn(1));
    assert(live_allocations() == 5);
    assert(!IMP_FrameSource_DisableChn(0));
    assert(!IMP_FrameSource_DisableChn(1));
    check_codec(&codec);
    assert(live_allocations() == 1);
    assert(!DMA_FreePhys(codec.phys_addr));
    assert(!live_allocations() && p2_dma.next == p2_dma.floor);
    /* Exhaustion rejects setup and rolls back partial captures, without
     * invalidating a codec allocation which owns the remaining arena. */
    assert(!DMA_AllocDescriptor(&codec, sizeof(arena) - 4096, "all-memory"));
    memset((void *)(uintptr_t)codec.virt_addr, 0xa5, codec.size);
    assert(IMP_FrameSource_EnableChn(1) < 0);
    assert(live_allocations() == 1);
    check_codec(&codec);
    assert(!DMA_FreePhys(codec.phys_addr));
    assert(!live_allocations() && p2_dma.next == p2_dma.floor);
    puts("T4 shared capture/encoder allocator: 100 restarts and failure ownership passed");
    return 0;
}
