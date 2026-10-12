/* GetFrame with every buffer held by the caller must fail after a bounded
 * wait without touching the (blocking) driver queue. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define ioctl test_ioctl
#include "../../src/t40/openimp_p1.c"
#undef ioctl

static int dq_calls, queued_in_driver = 4, next_idx;
static unsigned char *pic[4];

volatile uint32_t openimp_video_drop_frames;
int openimp_video_drop_set(void (*cb)(void)) { (void)cb; return 0; }
int openimp_log_level;
int OpenIMP_P2_DMAState(uint32_t *b, uint32_t *u) { (void)b; (void)u; return 0; }
int DMA_AllocDescriptor(IMPDMABufferInfo *i, int sz, const char *t) { (void)i; (void)sz; (void)t; return -1; }
int DMA_AllocDescriptorTop(IMPDMABufferInfo *i, int sz, const char *t) { (void)i; (void)sz; (void)t; return -1; }
int DMA_FreePhys(uint32_t p) { (void)p; return 0; }
int DMA_RmemFlushCache(void *v, uint32_t s, int d) { (void)v; (void)s; (void)d; return 0; }
int OpenIMP_P2_DMARegion(uint32_t *b, uint32_t *s, void **m) { (void)b; (void)s; (void)m; return -1; }
int openimp_t31_ivs_source_active(int fs_chn) { (void)fs_chn; return 0; }
void openimp_t31_ivs_capture(int fs_chn, const void *frame) { (void)fs_chn; (void)frame; }
int64_t IMP_System_GetTimeStamp(void) { return 1; }
int64_t OpenIMP_P0_NormalizeMonotonicTimeStamp(uint64_t t) { return (int64_t)t; }

int test_ioctl(int fd, unsigned long command, ...)
{
    va_list a; uint32_t *w;
    assert(fd == 42);
    va_start(a, command); w = va_arg(a, uint32_t *); va_end(a);
    if (command == TISP_VIDIOC_DQBUF) {
        dq_calls++;
        assert(queued_in_driver > 0 && "DQBUF on an empty queue would block in the kernel");
        queued_in_driver--;
        w[0] = (uint32_t)(next_idx++ % 4);
        return 0;
    }
    assert(command == TISP_VIDIOC_QBUF);
    queued_in_driver++;
    return 0;
}

int main(void)
{
    struct openimp_fs_channel *chn;
    IMPFrameInfo *h[4], *f = NULL;
    struct timespec t0, t1;
    double dt;
    int i;

    prepare_p1();
    chn = &p1.channels[1];
    memset(chn, 0, sizeof(*chn));
    chn->fd = 42; chn->created = 1; chn->pool_id = -1; chn->enabled = 1;
    chn->attr.picWidth = 8; chn->attr.picHeight = 8;
    for (i = 0; i < 4; i++) {
        pic[i] = calloc(1, 96);
        chn->buffers[i].physical = 0x6000000u + (uint32_t)i * 0x1000u;
        chn->buffers[i].virtual_address = pic[i];
        chn->buffers[i].size = 96;
        chn->buffers[i].queued = 1;
    }
    chn->buffer_count = 4;

    for (i = 0; i < 4; i++)
        assert(IMP_FrameSource_GetFrame(1, &h[i]) == 0 && h[i]);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    assert(IMP_FrameSource_GetFrame(1, &f) == -1 && !f);   /* 5th: no hang */
    clock_gettime(CLOCK_MONOTONIC, &t1);
    dt = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;
    assert(dt < 10.0);
    assert(dq_calls == 4);
    assert(IMP_FrameSource_ReleaseFrame(1, h[0]) == 0);
    assert(IMP_FrameSource_GetFrame(1, &f) == 0 && f);
    assert(IMP_FrameSource_ReleaseFrame(1, f) == 0);
    for (i = 1; i < 4; i++) assert(IMP_FrameSource_ReleaseFrame(1, h[i]) == 0);
    printf("framesource_hold_test OK (exhausted GetFrame returned -1 after %.2f s)\n", dt);
    return 0;
}
