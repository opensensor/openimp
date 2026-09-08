/* Exercise the actual P1 GetFrame adapter with a private DQBUF fixture. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#define ioctl test_ioctl
#include "../../src/t40/openimp_p1.c"
#undef ioctl

static uint32_t seconds, microseconds;
static uint64_t normalized_input;
static const uint64_t timestamp_base = 100000000;

int test_ioctl(int fd, unsigned long command, ...)
{
    va_list args;
    uint32_t *words;

    assert(fd == 42 && command == TISP_VIDIOC_DQBUF);
    va_start(args, command);
    words = va_arg(args, uint32_t *);
    va_end(args);
    assert(words[1] == TISP_BUF_TYPE_VIDEO_CAPTURE);
    words[0] = 0;
    words[5] = seconds;
    words[6] = microseconds;
    return 0;
}

int64_t IMP_System_GetTimeStamp(void) { return 1234567; }
int64_t OpenIMP_P0_NormalizeMonotonicTimeStamp(uint64_t timestamp)
{
    normalized_input = timestamp;
    return timestamp < timestamp_base ? -1 : (int64_t)(timestamp - timestamp_base);
}

static void check_frame(int channel, uint32_t sec, uint32_t usec, int64_t expected)
{
    IMPFrameInfo *frame = NULL;
    struct openimp_fs_channel *chn = &p1.channels[channel];

    chn->fd = 42;
    chn->enabled = 1;
    chn->buffer_count = 1;
    chn->buffers[0].queued = 1;
    chn->buffers[0].physical = 0x6000000;
    chn->buffers[0].virtual_address = (void *)(uintptr_t)0x20000000;
    seconds = sec;
    microseconds = usec;
    assert(IMP_FrameSource_GetFrame(channel, &frame) == 0);
    assert(frame == &chn->buffers[0].frame);
    assert(!chn->buffers[0].queued);
    assert(frame->timeStamp == expected);
    assert(frame->pool_idx == channel && frame->direct_phyAddr == 0x6000000);
    if (usec < 1000000)
        assert(normalized_input == (uint64_t)sec * 1000000 + usec);
}

int main(void)
{
    prepare_p1();
    check_frame(0, 100, 0, 0);
    check_frame(1, 100, 40022, 40022);
    check_frame(0, 100, 120066, 120066); /* A real missing-frame gap. */
    check_frame(1, 101, 10, 1000010);
    check_frame(0, 99, 999999, 1234567); /* Before P0 initialization/rebase. */
    check_frame(0, 100, 1000000, 1234567); /* Invalid timeval. */
    check_frame(0, 0, 0, 1234567); /* Missing timestamp. */
    check_frame(1, 4000000, 999999, 3999900999999LL); /* No 32-bit multiply. */
    puts("T41 P1 capture timestamps: passed");
    return 0;
}
