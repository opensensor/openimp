/* T31 service APIs required by Raptor but not provided by the stock-driver
 * ISP/FrameSource seam.  Keep these implementations T31-only: the T40 build
 * has its own recovered service layer and older Thingino kernels retain their
 * existing vendor libraries. */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "imp/imp_ivs.h"
#include "imp/imp_ivs_base_move.h"
#include "imp/imp_ivs_move.h"
#include "imp/imp_osd.h"
#include "imp/imp_system.h"

#define T31_OSD_GROUPS  16
#define T31_OSD_REGIONS 64
#define T31_IVS_GROUPS  16
#define T31_IVS_CHANNELS 16
#define T31_IVS_MAGIC   0x49565331U

struct t31_osd_group {
    int created;
    int started;
};

struct t31_osd_region {
    int created;
    int group;
    IMPOSDRgnAttr attr;
    IMPOSDGrpRgnAttr group_attr;
};

static pthread_mutex_t osd_lock = PTHREAD_MUTEX_INITIALIZER;
static struct t31_osd_group osd_groups[T31_OSD_GROUPS];
static struct t31_osd_region osd_regions[T31_OSD_REGIONS];
static int osd_pool_size;

static int t31_fail(int error)
{
    errno = error;
    return -1;
}

static int valid_osd_group(int group)
{
    return group >= 0 && group < T31_OSD_GROUPS;
}

static int valid_osd_region(IMPRgnHandle handle)
{
    return handle >= 0 && handle < T31_OSD_REGIONS;
}

int IMP_OSD_SetPoolSize(int size)
{
    if (size < 0)
        return t31_fail(EINVAL);
    pthread_mutex_lock(&osd_lock);
    osd_pool_size = size;
    pthread_mutex_unlock(&osd_lock);
    return 0;
}

int IMP_OSD_CreateGroup(int group)
{
    if (!valid_osd_group(group))
        return t31_fail(EINVAL);
    pthread_mutex_lock(&osd_lock);
    osd_groups[group].created = 1;
    pthread_mutex_unlock(&osd_lock);
    return 0;
}

int IMP_OSD_DestroyGroup(int group)
{
    int i;

    if (!valid_osd_group(group))
        return t31_fail(EINVAL);
    pthread_mutex_lock(&osd_lock);
    if (!osd_groups[group].created) {
        pthread_mutex_unlock(&osd_lock);
        return t31_fail(ENOENT);
    }
    for (i = 0; i < T31_OSD_REGIONS; i++) {
        if (osd_regions[i].created && osd_regions[i].group == group) {
            pthread_mutex_unlock(&osd_lock);
            return t31_fail(EBUSY);
        }
    }
    memset(&osd_groups[group], 0, sizeof(osd_groups[group]));
    pthread_mutex_unlock(&osd_lock);
    return 0;
}

IMPRgnHandle IMP_OSD_CreateRgn(IMPOSDRgnAttr *attr)
{
    int handle;

    pthread_mutex_lock(&osd_lock);
    for (handle = 0; handle < T31_OSD_REGIONS; handle++) {
        if (!osd_regions[handle].created)
            break;
    }
    if (handle == T31_OSD_REGIONS) {
        pthread_mutex_unlock(&osd_lock);
        return t31_fail(ENOSPC);
    }
    memset(&osd_regions[handle], 0, sizeof(osd_regions[handle]));
    osd_regions[handle].created = 1;
    osd_regions[handle].group = -1;
    if (attr)
        osd_regions[handle].attr = *attr;
    pthread_mutex_unlock(&osd_lock);
    return handle;
}

int IMP_OSD_DestroyRgn(IMPRgnHandle handle)
{
    if (!valid_osd_region(handle))
        return t31_fail(EINVAL);
    pthread_mutex_lock(&osd_lock);
    if (!osd_regions[handle].created) {
        pthread_mutex_unlock(&osd_lock);
        return t31_fail(ENOENT);
    }
    if (osd_regions[handle].group >= 0) {
        pthread_mutex_unlock(&osd_lock);
        return t31_fail(EBUSY);
    }
    memset(&osd_regions[handle], 0, sizeof(osd_regions[handle]));
    pthread_mutex_unlock(&osd_lock);
    return 0;
}

int IMP_OSD_RegisterRgn(IMPRgnHandle handle, int group,
                        IMPOSDGrpRgnAttr *attr)
{
    if (!valid_osd_region(handle) || !valid_osd_group(group))
        return t31_fail(EINVAL);
    pthread_mutex_lock(&osd_lock);
    if (!osd_regions[handle].created || !osd_groups[group].created) {
        pthread_mutex_unlock(&osd_lock);
        return t31_fail(ENOENT);
    }
    if (osd_regions[handle].group >= 0 &&
        osd_regions[handle].group != group) {
        pthread_mutex_unlock(&osd_lock);
        return t31_fail(EBUSY);
    }
    osd_regions[handle].group = group;
    if (attr)
        osd_regions[handle].group_attr = *attr;
    else
        memset(&osd_regions[handle].group_attr, 0,
               sizeof(osd_regions[handle].group_attr));
    pthread_mutex_unlock(&osd_lock);
    return 0;
}

int IMP_OSD_UnRegisterRgn(IMPRgnHandle handle, int group)
{
    if (!valid_osd_region(handle) || !valid_osd_group(group))
        return t31_fail(EINVAL);
    pthread_mutex_lock(&osd_lock);
    if (!osd_regions[handle].created ||
        osd_regions[handle].group != group) {
        pthread_mutex_unlock(&osd_lock);
        return t31_fail(ENOENT);
    }
    osd_regions[handle].group = -1;
    memset(&osd_regions[handle].group_attr, 0,
           sizeof(osd_regions[handle].group_attr));
    pthread_mutex_unlock(&osd_lock);
    return 0;
}

int IMP_OSD_SetRgnAttr(IMPRgnHandle handle, IMPOSDRgnAttr *attr)
{
    if (!valid_osd_region(handle) || !attr)
        return t31_fail(EINVAL);
    pthread_mutex_lock(&osd_lock);
    if (!osd_regions[handle].created) {
        pthread_mutex_unlock(&osd_lock);
        return t31_fail(ENOENT);
    }
    osd_regions[handle].attr = *attr;
    pthread_mutex_unlock(&osd_lock);
    return 0;
}

int IMP_OSD_SetRgnAttrWithTimestamp(IMPRgnHandle handle,
                                    IMPOSDRgnAttr *attr, void *timestamp)
{
    (void)timestamp;
    return IMP_OSD_SetRgnAttr(handle, attr);
}

int IMP_OSD_GetRgnAttr(IMPRgnHandle handle, IMPOSDRgnAttr *attr)
{
    if (!valid_osd_region(handle) || !attr)
        return t31_fail(EINVAL);
    pthread_mutex_lock(&osd_lock);
    if (!osd_regions[handle].created) {
        pthread_mutex_unlock(&osd_lock);
        return t31_fail(ENOENT);
    }
    *attr = osd_regions[handle].attr;
    pthread_mutex_unlock(&osd_lock);
    return 0;
}

int IMP_OSD_SetGrpRgnAttr(IMPRgnHandle handle, int group,
                           IMPOSDGrpRgnAttr *attr)
{
    if (!valid_osd_region(handle) || !valid_osd_group(group) || !attr)
        return t31_fail(EINVAL);
    pthread_mutex_lock(&osd_lock);
    if (!osd_regions[handle].created ||
        osd_regions[handle].group != group) {
        pthread_mutex_unlock(&osd_lock);
        return t31_fail(ENOENT);
    }
    osd_regions[handle].group_attr = *attr;
    pthread_mutex_unlock(&osd_lock);
    return 0;
}

int IMP_OSD_GetGrpRgnAttr(IMPRgnHandle handle, int group,
                           IMPOSDGrpRgnAttr *attr)
{
    if (!valid_osd_region(handle) || !valid_osd_group(group) || !attr)
        return t31_fail(EINVAL);
    pthread_mutex_lock(&osd_lock);
    if (!osd_regions[handle].created ||
        osd_regions[handle].group != group) {
        pthread_mutex_unlock(&osd_lock);
        return t31_fail(ENOENT);
    }
    *attr = osd_regions[handle].group_attr;
    pthread_mutex_unlock(&osd_lock);
    return 0;
}

int IMP_OSD_UpdateRgnAttrData(IMPRgnHandle handle,
                              IMPOSDRgnAttrData *data)
{
    if (!valid_osd_region(handle) || !data)
        return t31_fail(EINVAL);
    pthread_mutex_lock(&osd_lock);
    if (!osd_regions[handle].created) {
        pthread_mutex_unlock(&osd_lock);
        return t31_fail(ENOENT);
    }
    osd_regions[handle].attr.data = *data;
    pthread_mutex_unlock(&osd_lock);
    return 0;
}

int IMP_OSD_ShowRgn(IMPRgnHandle handle, int group, int show)
{
    if (!valid_osd_region(handle) || !valid_osd_group(group))
        return t31_fail(EINVAL);
    pthread_mutex_lock(&osd_lock);
    if (!osd_regions[handle].created ||
        osd_regions[handle].group != group) {
        pthread_mutex_unlock(&osd_lock);
        return t31_fail(ENOENT);
    }
    osd_regions[handle].group_attr.show = !!show;
    pthread_mutex_unlock(&osd_lock);
    return 0;
}

int IMP_OSD_Start(int group)
{
    if (!valid_osd_group(group))
        return t31_fail(EINVAL);
    pthread_mutex_lock(&osd_lock);
    if (!osd_groups[group].created) {
        pthread_mutex_unlock(&osd_lock);
        return t31_fail(ENOENT);
    }
    osd_groups[group].started = 1;
    pthread_mutex_unlock(&osd_lock);
    return 0;
}

int IMP_OSD_Stop(int group)
{
    if (!valid_osd_group(group))
        return t31_fail(EINVAL);
    pthread_mutex_lock(&osd_lock);
    if (!osd_groups[group].created) {
        pthread_mutex_unlock(&osd_lock);
        return t31_fail(ENOENT);
    }
    osd_groups[group].started = 0;
    pthread_mutex_unlock(&osd_lock);
    return 0;
}

enum t31_ivs_kind {
    T31_IVS_MOVE,
    T31_IVS_BASE_MOVE
};

struct t31_ivs_interface {
    uint32_t magic;
    enum t31_ivs_kind kind;
    union {
        IMP_IVS_MoveParam move;
        IMP_IVS_BaseMoveParam base_move;
    } param;
    union {
        IMP_IVS_MoveOutput move;
        IMP_IVS_BaseMoveOutput base_move;
    } output;
};

struct t31_ivs_channel {
    int created;
    int group;
    int started;
    struct t31_ivs_interface *interface;
};

static pthread_mutex_t ivs_lock = PTHREAD_MUTEX_INITIALIZER;
static int ivs_groups[T31_IVS_GROUPS];
static struct t31_ivs_channel ivs_channels[T31_IVS_CHANNELS];

static int valid_ivs_group(int group)
{
    return group >= 0 && group < T31_IVS_GROUPS;
}

static int valid_ivs_channel(int channel)
{
    return channel >= 0 && channel < T31_IVS_CHANNELS;
}

static struct t31_ivs_interface *t31_ivs_interface(IMPIVSInterface *interface)
{
    struct t31_ivs_interface *result =
        (struct t31_ivs_interface *)interface;

    if (!result || result->magic != T31_IVS_MAGIC)
        return NULL;
    return result;
}

IMPIVSInterface *IMP_IVS_CreateMoveInterface(IMP_IVS_MoveParam *param)
{
    struct t31_ivs_interface *interface;

    if (!param) {
        t31_fail(EINVAL);
        return NULL;
    }
    interface = calloc(1, sizeof(*interface));
    if (!interface)
        return NULL;
    interface->magic = T31_IVS_MAGIC;
    interface->kind = T31_IVS_MOVE;
    interface->param.move = *param;
    return (IMPIVSInterface *)interface;
}

void IMP_IVS_DestroyMoveInterface(IMPIVSInterface *opaque)
{
    struct t31_ivs_interface *interface = t31_ivs_interface(opaque);

    if (!interface || interface->kind != T31_IVS_MOVE)
        return;
    interface->magic = 0;
    free(interface);
}

IMPIVSInterface *IMP_IVS_CreateBaseMoveInterface(
    IMP_IVS_BaseMoveParam *param)
{
    struct t31_ivs_interface *interface;

    if (!param) {
        t31_fail(EINVAL);
        return NULL;
    }
    interface = calloc(1, sizeof(*interface));
    if (!interface)
        return NULL;
    interface->magic = T31_IVS_MAGIC;
    interface->kind = T31_IVS_BASE_MOVE;
    interface->param.base_move = *param;
    return (IMPIVSInterface *)interface;
}

void IMP_IVS_DestroyBaseMoveInterface(IMPIVSInterface *opaque)
{
    struct t31_ivs_interface *interface = t31_ivs_interface(opaque);

    if (!interface || interface->kind != T31_IVS_BASE_MOVE)
        return;
    interface->magic = 0;
    free(interface);
}

int IMP_IVS_CreateGroup(int group)
{
    if (!valid_ivs_group(group))
        return t31_fail(EINVAL);
    pthread_mutex_lock(&ivs_lock);
    ivs_groups[group] = 1;
    pthread_mutex_unlock(&ivs_lock);
    return 0;
}

int IMP_IVS_DestroyGroup(int group)
{
    int channel;

    if (!valid_ivs_group(group))
        return t31_fail(EINVAL);
    pthread_mutex_lock(&ivs_lock);
    if (!ivs_groups[group]) {
        pthread_mutex_unlock(&ivs_lock);
        return t31_fail(ENOENT);
    }
    for (channel = 0; channel < T31_IVS_CHANNELS; channel++) {
        if (ivs_channels[channel].created &&
            ivs_channels[channel].group == group) {
            pthread_mutex_unlock(&ivs_lock);
            return t31_fail(EBUSY);
        }
    }
    ivs_groups[group] = 0;
    pthread_mutex_unlock(&ivs_lock);
    return 0;
}

int IMP_IVS_CreateChn(int channel, IMPIVSInterface *opaque)
{
    struct t31_ivs_interface *interface = t31_ivs_interface(opaque);

    if (!valid_ivs_channel(channel) || !interface)
        return t31_fail(EINVAL);
    pthread_mutex_lock(&ivs_lock);
    if (ivs_channels[channel].created) {
        pthread_mutex_unlock(&ivs_lock);
        return t31_fail(EEXIST);
    }
    memset(&ivs_channels[channel], 0, sizeof(ivs_channels[channel]));
    ivs_channels[channel].created = 1;
    ivs_channels[channel].group = -1;
    ivs_channels[channel].interface = interface;
    pthread_mutex_unlock(&ivs_lock);
    return 0;
}

int IMP_IVS_DestroyChn(int channel)
{
    if (!valid_ivs_channel(channel))
        return t31_fail(EINVAL);
    pthread_mutex_lock(&ivs_lock);
    if (!ivs_channels[channel].created) {
        pthread_mutex_unlock(&ivs_lock);
        return t31_fail(ENOENT);
    }
    if (ivs_channels[channel].group >= 0 || ivs_channels[channel].started) {
        pthread_mutex_unlock(&ivs_lock);
        return t31_fail(EBUSY);
    }
    memset(&ivs_channels[channel], 0, sizeof(ivs_channels[channel]));
    pthread_mutex_unlock(&ivs_lock);
    return 0;
}

int IMP_IVS_RegisterChn(int group, int channel)
{
    if (!valid_ivs_group(group) || !valid_ivs_channel(channel))
        return t31_fail(EINVAL);
    pthread_mutex_lock(&ivs_lock);
    if (!ivs_groups[group] || !ivs_channels[channel].created) {
        pthread_mutex_unlock(&ivs_lock);
        return t31_fail(ENOENT);
    }
    if (ivs_channels[channel].group >= 0) {
        pthread_mutex_unlock(&ivs_lock);
        return t31_fail(EBUSY);
    }
    ivs_channels[channel].group = group;
    pthread_mutex_unlock(&ivs_lock);
    return 0;
}

int IMP_IVS_UnRegisterChn(int channel)
{
    if (!valid_ivs_channel(channel))
        return t31_fail(EINVAL);
    pthread_mutex_lock(&ivs_lock);
    if (!ivs_channels[channel].created ||
        ivs_channels[channel].group < 0) {
        pthread_mutex_unlock(&ivs_lock);
        return t31_fail(ENOENT);
    }
    if (ivs_channels[channel].started) {
        pthread_mutex_unlock(&ivs_lock);
        return t31_fail(EBUSY);
    }
    ivs_channels[channel].group = -1;
    pthread_mutex_unlock(&ivs_lock);
    return 0;
}

int IMP_IVS_StartRecvPic(int channel)
{
    if (!valid_ivs_channel(channel))
        return t31_fail(EINVAL);
    pthread_mutex_lock(&ivs_lock);
    if (!ivs_channels[channel].created ||
        ivs_channels[channel].group < 0) {
        pthread_mutex_unlock(&ivs_lock);
        return t31_fail(ENOENT);
    }
    ivs_channels[channel].started = 1;
    pthread_mutex_unlock(&ivs_lock);
    return 0;
}

int IMP_IVS_StopRecvPic(int channel)
{
    if (!valid_ivs_channel(channel))
        return t31_fail(EINVAL);
    pthread_mutex_lock(&ivs_lock);
    if (!ivs_channels[channel].created) {
        pthread_mutex_unlock(&ivs_lock);
        return t31_fail(ENOENT);
    }
    ivs_channels[channel].started = 0;
    pthread_mutex_unlock(&ivs_lock);
    return 0;
}

int IMP_IVS_PollingResult(int channel, int timeout_ms)
{
    int started;

    if (!valid_ivs_channel(channel))
        return t31_fail(EINVAL);
    pthread_mutex_lock(&ivs_lock);
    started = ivs_channels[channel].created &&
              ivs_channels[channel].started;
    pthread_mutex_unlock(&ivs_lock);
    if (!started)
        return t31_fail(ENOENT);
    if (timeout_ms > 0) {
        if (timeout_ms > 1000)
            timeout_ms = 1000;
        usleep((unsigned int)timeout_ms * 1000U);
    }
    return 0;
}

int IMP_IVS_GetResult(int channel, void **result)
{
    struct t31_ivs_interface *interface;

    if (!valid_ivs_channel(channel) || !result)
        return t31_fail(EINVAL);
    pthread_mutex_lock(&ivs_lock);
    if (!ivs_channels[channel].created || !ivs_channels[channel].started) {
        pthread_mutex_unlock(&ivs_lock);
        return t31_fail(ENOENT);
    }
    interface = ivs_channels[channel].interface;
    if (interface->kind == T31_IVS_MOVE)
        *result = &interface->output.move;
    else
        *result = &interface->output.base_move;
    pthread_mutex_unlock(&ivs_lock);
    return 0;
}

int IMP_IVS_ReleaseResult(int channel, void *result)
{
    if (!valid_ivs_channel(channel) || !result)
        return t31_fail(EINVAL);
    return 0;
}

int IMP_IVS_ReleaseData(void *address)
{
    if (!address)
        return t31_fail(EINVAL);
    return 0;
}

int IMP_IVS_GetParam(int channel, void *param)
{
    struct t31_ivs_interface *interface;

    if (!valid_ivs_channel(channel) || !param)
        return t31_fail(EINVAL);
    pthread_mutex_lock(&ivs_lock);
    if (!ivs_channels[channel].created) {
        pthread_mutex_unlock(&ivs_lock);
        return t31_fail(ENOENT);
    }
    interface = ivs_channels[channel].interface;
    if (interface->kind == T31_IVS_MOVE)
        *(IMP_IVS_MoveParam *)param = interface->param.move;
    else
        *(IMP_IVS_BaseMoveParam *)param = interface->param.base_move;
    pthread_mutex_unlock(&ivs_lock);
    return 0;
}

int IMP_IVS_SetParam(int channel, void *param)
{
    struct t31_ivs_interface *interface;

    if (!valid_ivs_channel(channel) || !param)
        return t31_fail(EINVAL);
    pthread_mutex_lock(&ivs_lock);
    if (!ivs_channels[channel].created) {
        pthread_mutex_unlock(&ivs_lock);
        return t31_fail(ENOENT);
    }
    interface = ivs_channels[channel].interface;
    if (interface->kind == T31_IVS_MOVE)
        interface->param.move = *(IMP_IVS_MoveParam *)param;
    else
        interface->param.base_move = *(IMP_IVS_BaseMoveParam *)param;
    pthread_mutex_unlock(&ivs_lock);
    return 0;
}

static uint32_t t31_register_access(uint32_t address,
                                    const uint32_t *write_value)
{
    long page_size = sysconf(_SC_PAGESIZE);
    uint32_t page_mask;
    uint32_t page_address;
    uint32_t page_offset;
    volatile uint32_t *reg;
    void *mapping;
    uint32_t result = 0;
    int fd;

    if (page_size <= 0 || (address & 3U))
        return 0;
    page_mask = (uint32_t)page_size - 1U;
    page_address = address & ~page_mask;
    page_offset = address & page_mask;
    fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0)
        return 0;
    mapping = mmap(NULL, (size_t)page_size, PROT_READ | PROT_WRITE,
                   MAP_SHARED, fd, (off_t)page_address);
    if (mapping == MAP_FAILED) {
        close(fd);
        return 0;
    }
    reg = (volatile uint32_t *)((unsigned char *)mapping + page_offset);
    if (write_value) {
        *reg = *write_value;
        __sync_synchronize();
    }
    result = *reg;
    munmap(mapping, (size_t)page_size);
    close(fd);
    return result;
}

uint32_t IMP_System_ReadReg32(uint32_t address)
{
    return t31_register_access(address, NULL);
}

void IMP_System_WriteReg32(uint32_t address, uint32_t value)
{
    (void)t31_register_access(address, &value);
}
