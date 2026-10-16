/* Frame delay cache, I2D attribute and SetChnAttr rules of the P1
 * FrameSource, on a private DQBUF/QBUF fixture. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#define ioctl test_ioctl
#include "../../src/t40/openimp_p1.c"
#undef ioctl

#define PIC 96u

static uint32_t next_sec, next_usec;
static int dq_index;
static unsigned char *pic[4];

int openimp_t31_ivs_source_active(int fs_chn) { (void)fs_chn; return 0; }
void openimp_t31_ivs_capture(int fs_chn, const void *frame)
{
    (void)fs_chn;
    (void)frame;
}

int test_ioctl(int fd, unsigned long command, ...)
{
    va_list args;
    uint32_t *words;

    assert(fd == 42);
    va_start(args, command);
    words = va_arg(args, uint32_t *);
    va_end(args);
    if (command == TISP_VIDIOC_DQBUF) {
        words[0] = (uint32_t)dq_index;
        words[5] = next_sec;
        words[6] = next_usec;
        /* the picture tells which frame it is */
        memset(pic[dq_index], (int)(next_usec / 1000u), PIC);
        return 0;
    }
    assert(command == TISP_VIDIOC_QBUF);
    return 0;
}

int64_t IMP_System_GetTimeStamp(void) { return 1234567; }
int64_t OpenIMP_P0_NormalizeMonotonicTimeStamp(uint64_t timestamp)
{
    return (int64_t)timestamp;
}

static void setup_channel(int c)
{
    struct openimp_fs_channel *chn = &p1.channels[c];
    int i;

    memset(chn, 0, sizeof(*chn));
    chn->fd = 42;
    chn->created = 1;
    chn->pool_id = -1;
    chn->attr.picWidth = 8;
    chn->attr.picHeight = 8;
    for (i = 0; i < 4; i++) {
        pic[i] = calloc(1, PIC);
        chn->buffers[i].physical = 0x6000000u + (uint32_t)i * 0x1000u;
        chn->buffers[i].virtual_address = pic[i];
        chn->buffers[i].size = PIC;
        chn->buffers[i].queued = 1;
    }
    chn->buffer_count = 4;
}

static int64_t get_release(int c, unsigned char *first)
{
    IMPFrameInfo *f = NULL;
    int64_t ts;

    assert(IMP_FrameSource_GetFrame(c, &f) == 0 && f);
    ts = f->timeStamp;
    if (first)
        *first = *(unsigned char *)(uintptr_t)f->virAddr;
    assert(IMP_FrameSource_ReleaseFrame(c, f) == 0);
    return ts;
}

int main(void)
{
    IMPFrameTimestamp t;
    IMPFrameInfo info;
    IMPFSI2DAttr i2d;
    IMPFSChnFifoAttr fifo = { 2, 0 };
    unsigned char buf[PIC], first;
    int v, i;

    prepare_p1();
    setup_channel(1);

    /* ranges and states as the vendor checks them */
    assert(IMP_FrameSource_SetMaxDelay(-1, 1) == FS_ERR_CHN);
    assert(IMP_FrameSource_SetMaxDelay(7, 1) == FS_ERR_CHN);
    assert(IMP_FrameSource_SetMaxDelay(1, 101) == FS_ERR_PARAM);
    assert(IMP_FrameSource_SetMaxDelay(3, 1) == FS_ERR_STATE); /* not created */
    assert(IMP_FrameSource_GetDelay(1, NULL) == FS_ERR_PARAM);
    assert(IMP_FrameSource_SetDelay(1, 1) == FS_ERR_PARAM);    /* > max 0 */
    assert(IMP_FrameSource_SetMaxDelay(1, 3) == 0);
    assert(IMP_FrameSource_SetDelay(1, 4) == FS_ERR_PARAM);
    assert(IMP_FrameSource_SetDelay(1, 1) == 0);
    assert(IMP_FrameSource_GetMaxDelay(1, &v) == 0 && v == 3);
    assert(IMP_FrameSource_GetDelay(1, &v) == 0 && v == 1);
    /* the FIFO depth is the delay, the max delay stays */
    assert(IMP_FrameSource_SetChnFifoAttr(1, &fifo) == 0);
    assert(IMP_FrameSource_GetDelay(1, &v) == 0 && v == 2);
    assert(IMP_FrameSource_GetMaxDelay(1, &v) == 0 && v == 3);
    p1.channels[1].enabled = 1;
    fs_delay_set_on(1, 1);
    assert(IMP_FrameSource_SetMaxDelay(1, 3) == FS_ERR_STATE); /* running */
    assert(IMP_FrameSource_SetDelay(1, 3) == 0);               /* allowed */

    /* the cache keeps the last 3 frames */
    memset(&t, 0, sizeof(t));
    assert(IMP_FrameSource_GetTimedFrame(1, &t, 0, NULL, &info) ==
           FS_ERR_EMPTY);
    for (i = 0; i < 5; i++) {
        next_sec = 100;
        next_usec = 10000u * (uint32_t)(i + 1);
        dq_index = i % 4;
        (void)get_release(1, &first);
    }
    t.ts = 100u * 1000000u + 40000u;
    t.minus = 5000;
    t.plus = 5000;
    assert(IMP_FrameSource_GetTimedFrame(1, &t, 0, buf, &info) == 0);
    assert(info.timeStamp == 100 * 1000000LL + 40000 && buf[0] == 40);
    assert(info.virAddr == (uint32_t)(uintptr_t)buf);
    assert(info.width == 8 || info.width == 0);
    /* the frame at 20 ms has left the 3-frame cache */
    t.ts = 100u * 1000000u + 20000u;
    assert(IMP_FrameSource_GetTimedFrame(1, &t, 0, buf, &info) ==
           FS_ERR_EMPTY);
    /* nearest of two inside a wide window */
    t.ts = 100u * 1000000u + 44000u;
    t.minus = t.plus = 20000;
    assert(IMP_FrameSource_GetTimedFrame(1, &t, 0, buf, &info) == 0);
    assert(info.timeStamp == 100 * 1000000LL + 40000);
    /* without a buffer the info points into the cache */
    assert(IMP_FrameSource_GetTimedFrame(1, &t, 0, NULL, &info) == 0);
    assert(info.virAddr != 0 && *(unsigned char *)(uintptr_t)info.virAddr == 40);
    /* newer than the window and not blocking forever */
    t.ts = 100u * 1000000u;
    t.minus = t.plus = 100;
    assert(IMP_FrameSource_GetTimedFrame(1, &t, 1, buf, &info) ==
           FS_ERR_EMPTY);
    /* delay 0 drops the cache and the timed frame needs it */
    assert(IMP_FrameSource_SetDelay(1, 0) == 0);
    assert(IMP_FrameSource_GetTimedFrame(1, &t, 0, buf, &info) ==
           FS_ERR_STATE);

    /* I2D: no real path, an active request is refused (-1) and not stored;
     * the all-zero attribute (identity) is accepted */
    assert(IMP_FrameSource_GetI2dAttr(1, &i2d) == 0 && !i2d.i2d_enable);
    i2d.i2d_enable = 1;
    i2d.flip_enable = 1;
    assert(IMP_FrameSource_SetI2dAttr(1, &i2d) == -1);
    memset(&i2d, 0, sizeof(i2d));
    assert(IMP_FrameSource_GetI2dAttr(1, &i2d) == 0 && i2d.flip_enable == 0);
    i2d.rotate_enable = 1;
    i2d.rotate_angle = 90;
    assert(IMP_FrameSource_SetI2dAttr(1, &i2d) == -1);
    memset(&i2d, 0, sizeof(i2d));
    assert(IMP_FrameSource_SetI2dAttr(1, &i2d) == 0);
    assert(IMP_FrameSource_SetI2dAttr(9, &i2d) == FS_ERR_CHN);

    /* SetChnAttr on a running channel: unchanged accepted, resize refused */
    {
        IMPFSChnAttr a;

        assert(IMP_FrameSource_GetChnAttr(1, &a) == 0);
        assert(IMP_FrameSource_SetChnAttr(1, &a) == 0);
        a.picWidth += 16;
        assert(IMP_FrameSource_SetChnAttr(1, &a) == -1);
    }

    p1.channels[1].enabled = 0;
    assert(IMP_FrameSource_SetDelay(1, 2) == 0);
    fs_delay_free(1);

    /* a cache re-created by a GetFrame racing with Disable: not stored */
    fs_delay_store(1, 2, &info, buf, PIC);
    assert(p1_dly[1].cap == 0 && !p1_dly[1].ent);

    /* only SetChnFifoAttr (no SetMaxDelay): no cache, GetFrame copies nothing */
    setup_channel(2);
    fifo.maxdepth = 50;
    assert(IMP_FrameSource_SetChnFifoAttr(2, &fifo) == 0);
    assert(p1.channels[2].delay == 50 && p1.channels[2].maxdelay == 0);
    p1.channels[2].enabled = 1;
    fs_delay_set_on(2, 1);
    next_sec = 100;
    next_usec = 10000;
    dq_index = 0;
    (void)get_release(2, &first);
    assert(p1_dly[2].cap == 0 && !p1_dly[2].ent);
    /* with a max delay the depth is clamped to it */
    p1.channels[2].enabled = 0;
    assert(IMP_FrameSource_SetMaxDelay(2, 2) == 0);
    p1.channels[2].enabled = 1;
    (void)get_release(2, &first);
    assert(p1_dly[2].cap == 2);
    p1.channels[2].enabled = 0;
    fs_delay_free(2);
    puts("T41 P1 frame delay cache and I2D: passed");
    return 0;
}
