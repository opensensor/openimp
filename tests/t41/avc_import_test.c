/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Exercise the real 32-bit standalone DMA-BUF importer with a pooled device. */
#define _GNU_SOURCE
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/ioctl.h>

static int fake_ioctl(int fd, unsigned long command, ...);
#define ioctl fake_ioctl
#include "../../src/t40/openimp_avc.c"
#undef ioctl

static int gets, puts_count, ioctls, open_error, ioctl_error, zero_address;
static int destroys, destroy_error;

int AL_Codec_Encode_Destroy(void *codec)
{
    assert(codec == (void *)(uintptr_t)0x1234);
    destroys++;
    return destroy_error;
}

int AL_DevicePool_Open(const char *path)
{
    assert(!strcmp(path, "/dev/avpu"));
    gets++;
    if (open_error) { errno = ENODEV; return -1; }
    return 73;
}

int AL_DevicePool_Close(int fd)
{
    assert(fd == 73);
    puts_count++;
    return 0;
}

static int fake_ioctl(int fd, unsigned long command, ...)
{
    struct openimp_avc_dma_info *info;
    va_list args;
    assert(fd == 73 && command == OPENIMP_AVC_GET_DMA_PHY);
    va_start(args, command);
    info = va_arg(args, struct openimp_avc_dma_info *);
    va_end(args);
    assert(info->fd == 12 && info->size == 4096);
    ioctls++;
    if (ioctl_error) { errno = EIO; return -1; }
    info->physical_address = zero_address ? 0 : 0x1234000;
    return 0;
}

int main(void)
{
    uint32_t address = 0;
    OpenIMPAVCEncoder *encoder = calloc(1, sizeof(*encoder));
    assert(encoder);
    encoder->codec = (void *)(uintptr_t)0x1234;
    encoder->submitted = 1;
    assert(OpenIMP_AVC_Destroy(encoder) == -EBUSY && !destroys);
    encoder->submitted = 0;
    destroy_error = -1;
    assert(OpenIMP_AVC_Destroy(encoder) == -EIO && destroys == 1);
    /* A rejected hardware handoff must leave the wrapper intact for retry. */
    assert(encoder->codec == (void *)(uintptr_t)0x1234);
    destroy_error = 0;
    assert(!OpenIMP_AVC_Destroy(encoder) && destroys == 2);
    assert(OpenIMP_AVC_ImportDMABuf(-1, 4096, &address) == -EINVAL);
    assert(!gets && !puts_count);
    assert(!OpenIMP_AVC_ImportDMABuf(12, 4096, &address));
    assert(address == 0x1234000 && gets == 1 && puts_count == 1 && ioctls == 1);
    ioctl_error = 1;
    assert(OpenIMP_AVC_ImportDMABuf(12, 4096, &address) == -EIO);
    assert(gets == 2 && puts_count == 2);
    ioctl_error = 0;
    zero_address = 1;
    assert(OpenIMP_AVC_ImportDMABuf(12, 4096, &address) == -EIO);
    assert(gets == 3 && puts_count == 3);
    open_error = 1;
    assert(OpenIMP_AVC_ImportDMABuf(12, 4096, &address) == -ENODEV);
    assert(gets == 4 && puts_count == 3 && ioctls == 3);
    puts("T41 AVC: pooled importer and failed-destroy ownership PASS");
    return 0;
}
