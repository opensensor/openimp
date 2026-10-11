/* Clean standalone OpenIMP T40 ISP/FrameSource implementation.
 *
 * This unit intentionally has no runtime symbol bridge or OEM libimp dependency.
 * It speaks the stock T40 tx-isp and frame-channel ABI directly.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#include "openimp_profile.h"
#include "trace_control.h"
#include "dma_alloc.h"
/* T41 and T40 share the frame-channel front end: no capture thread, frames
 * leave the driver when a consumer dequeues them.  The IVS frame feeder and
 * the SnapFrame frame lend (t41_snap.h) therefore work the same on both. */
#if defined(PLATFORM_T41) || defined(PLATFORM_T40)
#define P1_CAPTURE_FEEDER 1
#endif
#if defined(P1_CAPTURE_FEEDER)
#include "t31/openimp_t31_ivs.h"
#endif
#include "t40/openimp_p2_dma.h"
#include "video_drop.h"
#if defined(P1_CAPTURE_FEEDER)
#include "t40/t41_snap.h"
#endif

#define OPENIMP_P1_MAGIC        0x50315434U /* "P1T4" */
#define OPENIMP_FS_CHANNELS     4
#define OPENIMP_FS_BUFFERS      4

#define TISP_VIDIOC_DRIVER_VERSION        0x80045401U
#define TISP_VIDIOC_ENUMINPUT             0xc0045402U
#define TISP_VIDIOC_G_INPUT               0x80085403U
#define TISP_VIDIOC_S_INPUT               0xc0085404U
#define TISP_VIDIOC_REGISTER_SENSOR       0x80645405U
#define TISP_VIDIOC_UNREGISTER_SENSOR     0x80645406U
#define TISP_VIDIOC_PREPARE_SENSOR        0x80085407U
#define TISP_VIDIOC_FINISH_SENSOR         0x80085408U
#define TISP_VIDIOC_START_SENSOR          0x80085409U
#define TISP_VIDIOC_STOP_SENSOR           0x8008540aU
#define TISP_VIDIOC_ENABLE_SENSOR         0xc008540bU
#define TISP_VIDIOC_DISABLE_SENSOR        0xc008540cU
#define TISP_VIDIOC_SET_DEFAULT_BIN_PATH  0xc004542aU
#if !defined(PLATFORM_T41)
/* vendor T40 1.3.1 libimp disassembly: IMP_ISP_SetCameraInputMode /
 * SetCameraInputSelect on the main ISP node (the size field of the number
 * says 4 although the mode argument is 24 bytes, as the vendor sends it) */
#define TISP_VIDIOC_SET_CAMERA_INPUT_MODE   0xc0045415U
#define TISP_VIDIOC_SET_CAMERA_INPUT_SELECT 0xc0045419U
#endif
#define TISP_VIDIOC_SET_MDNS_BUF_INFO     0x800c540fU
#define TISP_VIDIOC_GET_MDNS_BUF_INFO     0x800c5410U

#if defined(PLATFORM_T41)
#define TISP_VIDIOC_SET_FRAME_FORMAT      0xc0745451U
#else
#define TISP_VIDIOC_SET_FRAME_FORMAT      0xc0705451U
#endif
#define TISP_VIDIOC_REQBUFS               0xc0145453U
#define TISP_VIDIOC_QBUF                  0xc0445455U
#define TISP_VIDIOC_DQBUF                 0xc0445456U
#define TISP_VIDIOC_STREAMON              0xc0045457U
#define TISP_VIDIOC_STREAMOFF             0xc0045458U
#define TISP_VIDIOC_WAIT_FRAME            0xc004545aU

#define TISP_BUF_TYPE_VIDEO_CAPTURE 1U
#define TISP_MEMORY_USERPTR         2U
/*
 * The stock T40 module shipped on the Wyze Cam 3 Pro does not use the enum
 * values published in the available T40 SDK headers.  Its
 * frame_channel_vidioc_set_fmt() accepts field=4 and colorspace=8 (and
 * normalizes a zero field to 4).  These are the values emitted by the OEM
 * userspace ABI and must also be used in queued buffer descriptors.
 */
#define TISP_FIELD_STOCK_PROGRESSIVE 4U
#define TISP_COLORSPACE_STOCK        8U
#define TISP_PIX_FMT_NV12_ENUM      10U
#define TISP_OUTPUT_FMT_NV12         0U
#define TISP_BUFFER_WORDS           17

typedef enum {
    IMPVI_MAIN = 0,
    IMPVI_SEC = 1,
    IMPVI_THR = 2,
    IMPVI_BUTT
} IMPVI_NUM;

typedef struct {
    char type[20];
    int32_t addr;
    int32_t i2c_adapter_id;
} IMPI2CInfo;

typedef struct {
    char modalias[32];
    int32_t bus_num;
} IMPSPIInfo;

typedef struct {
    char name[32];
    int32_t cbus_type;
    union {
        IMPI2CInfo i2c;
        IMPSPIInfo spi;
    };
    int32_t rst_gpio;
    int32_t pwdn_gpio;
    int32_t power_gpio;
    uint16_t sensor_id;
    uint16_t reserved;
    int32_t video_interface;
    int32_t mclk;
    int32_t default_boot;
} IMPSensorInfo;

typedef struct {
    int32_t enable;
    int32_t left;
    int32_t top;
    int32_t width;
    int32_t height;
} IMPFSChnCrop;

typedef struct {
    int32_t enable;
    int32_t outwidth;
    int32_t outheight;
} IMPFSChnScaler;

typedef struct {
    int32_t i2d_enable;
    int32_t flip_enable;
    int32_t mirr_enable;
    int32_t rotate_enable;
    int32_t rotate_angle;
} IMPFSI2DAttr;

typedef struct {
    IMPFSI2DAttr i2dattr;
    int32_t picWidth;
    int32_t picHeight;
    int32_t pixFmt;
    IMPFSChnCrop crop;
    IMPFSChnScaler scaler;
    int32_t outFrmRateNum;
    int32_t outFrmRateDen;
    int32_t nrVBs;
    int32_t type;
    IMPFSChnCrop fcrop;
#if defined(PLATFORM_T41)
    int32_t mirr_enable;
#endif
} IMPFSChnAttr;

typedef struct {
    int32_t index;
    int32_t pool_idx;
    uint32_t width;
    uint32_t height;
    uint32_t pixfmt;
    uint32_t size;
    uint32_t phyAddr;
    uint32_t virAddr;
#if defined(PLATFORM_T41)
    uint32_t direct_phyAddr;
#endif
    void *pool;
    int64_t timeStamp;
} IMPFrameInfo;

#if defined(PLATFORM_T40)
/* vendor T40 1.3.1 imp_common.h: no direct_phyAddr, pool at 0x20 */
_Static_assert(sizeof(IMPFrameInfo) == 0x30,
               "T40 IMPFrameInfo ABI mismatch");
_Static_assert(offsetof(IMPFrameInfo, pool) == 0x20,
               "T40 IMPFrameInfo.pool ABI mismatch");
_Static_assert(offsetof(IMPFrameInfo, timeStamp) == 0x28,
               "T40 IMPFrameInfo.timeStamp ABI mismatch");
#endif

#if defined(PLATFORM_T41)
_Static_assert(sizeof(IMPFSChnAttr) == 0x68,
               "T41 IMPFSChnAttr ABI mismatch");
_Static_assert(offsetof(IMPFSChnAttr, picWidth) == 0x14,
               "T41 IMPFSChnAttr.picWidth ABI mismatch");
_Static_assert(offsetof(IMPFSChnAttr, fcrop) == 0x50,
               "T41 IMPFSChnAttr.fcrop ABI mismatch");
_Static_assert(sizeof(IMPFrameInfo) == 0x30,
               "T41 IMPFrameInfo ABI mismatch");
_Static_assert(offsetof(IMPFrameInfo, direct_phyAddr) == 0x20,
               "T41 IMPFrameInfo.direct_phyAddr ABI mismatch");
_Static_assert(offsetof(IMPFrameInfo, pool) == 0x24,
               "T41 IMPFrameInfo.pool ABI mismatch");
_Static_assert(offsetof(IMPFrameInfo, timeStamp) == 0x28,
               "T41 IMPFrameInfo.timeStamp ABI mismatch");
#endif

typedef struct {
    int32_t maxdepth;
    int32_t type;
} IMPFSChnFifoAttr;

struct tisp_initarg {
    int32_t enable;
    int32_t vinum;
};

struct tisp_buf_info {
    uint32_t vinum;
    uint32_t paddr;
    uint32_t size;
};

struct tisp_bin_path {
    int32_t vinum;
    char path[64];
};

struct tisp_pix_format {
    uint32_t width;
    uint32_t height;
    uint32_t pixelformat;
    uint32_t field;
    uint32_t bytesperline;
    uint32_t sizeimage;
    uint32_t colorspace;
    uint32_t priv;
    uint32_t flags;
    uint32_t ycbcr_enc;
    uint32_t quantization;
    uint32_t xfer_func;
};

struct tisp_frame_format {
    uint32_t type;
    struct tisp_pix_format pix;
    uint32_t crop_enable;
    uint32_t crop_top;
    uint32_t crop_left;
    uint32_t crop_width;
    uint32_t crop_height;
    uint32_t scaler_enable;
    uint32_t scaler_out_width;
    uint32_t scaler_out_height;
    uint32_t rate_bits;
    uint32_t rate_mask;
    uint32_t fcrop_enable;
    uint32_t fcrop_top;
    uint32_t fcrop_left;
    uint32_t fcrop_width;
    uint32_t fcrop_height;
#if defined(PLATFORM_T41)
    uint32_t flip_enable;
#endif
};

#if defined(PLATFORM_T41)
_Static_assert(sizeof(struct tisp_frame_format) == 0x74,
               "T41 frame-channel format ABI mismatch");
#else
_Static_assert(sizeof(struct tisp_frame_format) == 0x70,
               "T40 frame-channel format ABI mismatch");
#endif

struct openimp_dma_state {
    int fd;
    uint32_t base;
    uint32_t size;
    uint32_t next;
    void *mapping;
};

struct openimp_fs_buffer {
    uint32_t physical;
    void *virtual_address;
    uint32_t size;
    uint32_t queued;
    IMPFrameInfo frame;
};

struct openimp_fs_channel {
    int fd;
    int pool_id;
    uint32_t created;
    uint32_t enabled;
    uint32_t depth;
    IMPFSChnFifoAttr fifo_attr;
    int32_t maxdelay;               /* SetMaxDelay (vendor +772) */
    int32_t delay;                  /* SetDelay / SetChnFifoAttr (+776) */
    uint32_t buffer_count;
    uint32_t sizeimage;
    uint32_t frames_dequeued;
#if defined(P1_CAPTURE_FEEDER)
    uint64_t last_dequeue_us;       /* by a consumer other than the IVS
                                     * feeder, CLOCK_MONOTONIC */
    struct t41_snap_state snap;     /* SnapFrame frame lend */
#endif
    IMPFSChnAttr attr;
    struct openimp_fs_buffer buffers[OPENIMP_FS_BUFFERS];
};

struct openimp_p1_state {
    uint32_t magic;
    int isp_fd;
    int tuning_fd;
    uint32_t isp_open;
    uint32_t sensor_added;
    uint32_t sensor_enabled;
    uint32_t tuning_enabled;
    int32_t last_errno;
    uint32_t last_ioctl;
    IMPSensorInfo sensor;
    struct tisp_bin_path bin_paths[IMPVI_BUTT];
    uint32_t bin_path_set[IMPVI_BUTT];
    struct tisp_buf_info mdns;
    void *mdns_virtual;
#if !defined(PLATFORM_T41)
    uint32_t camera_input[6];       /* last SetCameraInputMode (vendor +612) */
#endif
    struct openimp_dma_state dma;
    struct openimp_fs_channel channels[OPENIMP_FS_CHANNELS];
};

static struct openimp_p1_state p1;
static volatile uint32_t p1_lock;

extern int64_t IMP_System_GetTimeStamp(void);
#if defined(PLATFORM_T41)
extern int64_t OpenIMP_P0_NormalizeMonotonicTimeStamp(uint64_t timestamp);
#endif

/* Bench-only phase markers: behind OPENIMP_DEBUG_TRACE like the other
 * OpenIMP debug traces (they used to go to stderr unconditionally). */
static void trace_p1(const char *text)
{
    if (!openimp_debug_trace_enabled())
        return;
    (void)write(2, text, strlen(text));
}

static void lock_p1(void)
{
    while (__sync_lock_test_and_set(&p1_lock, 1)) {
        while (p1_lock)
            ;
    }
}

static void unlock_p1(void)
{
    __sync_synchronize();
    p1_lock = 0;
}

static void prepare_p1(void)
{
    int i;

    if (p1.magic == OPENIMP_P1_MAGIC)
        return;
    memset(&p1, 0, sizeof(p1));
    p1.magic = OPENIMP_P1_MAGIC;
    p1.isp_fd = -1;
    p1.tuning_fd = -1;
    p1.dma.fd = -1;
    for (i = 0; i < OPENIMP_FS_CHANNELS; i++) {
        p1.channels[i].fd = -1;
        p1.channels[i].pool_id = -1;
    }
}

static int record_ioctl(int fd, uint32_t command, void *argument)
{
    int result = ioctl(fd, command, argument);

    p1.last_ioctl = command;
    p1.last_errno = result < 0 ? errno : 0;
    if (result < 0 && !(command == TISP_VIDIOC_DQBUF &&
                        (errno == EAGAIN || errno == ENODATA ||
                         errno == EINTR)))
        syslog(LOG_ERR, "openimp-p1: ioctl fd=%d cmd=0x%08x failed: %d",
               fd, command, errno);
    return result;
}

static int dma_init(void)
{
    if (p1.dma.mapping)
        return 0;
    return OpenIMP_P2_DMARegion(&p1.dma.base, &p1.dma.size, &p1.dma.mapping);
}

static void dma_deinit(void)
{
    /* Mapping belongs to the shared arena, not this FrameSource owner. */
    memset(&p1.dma, 0, sizeof(p1.dma));
    p1.dma.fd = -1;
}

static int dma_alloc(uint32_t size, const char *tag, uint32_t *physical,
                     void **virtual_address)
{
    IMPDMABufferInfo allocation;

    if (!size || size > INT32_MAX || !physical || !virtual_address)
        return -1;
    if (dma_init() < 0)
        return -1;
    if (DMA_AllocDescriptor(&allocation, (int)size, tag) < 0)
        return -1;
    *physical = allocation.phys_addr;
    *virtual_address = (void *)(uintptr_t)allocation.virt_addr;
    memset(*virtual_address, 0, size);
    OpenIMP_P2_DMAState(NULL, &p1.dma.next);
    return 0;
}

static int free_capture_buffers(struct openimp_fs_channel *chn)
{
    unsigned int i;
    int result = 0;

    for (i = 0; i < OPENIMP_FS_BUFFERS; ++i) {
        if (chn->buffers[i].physical && DMA_FreePhys(chn->buffers[i].physical) < 0) {
            result = -1;
            continue;
        }
        memset(&chn->buffers[i], 0, sizeof(chn->buffers[i]));
    }
    if (result)
        return result;
    chn->buffer_count = 0;
    chn->sizeimage = 0;
    return 0;
}

/* Only release physical storage after the driver acknowledges queue removal. */
static int release_capture_queue(struct openimp_fs_channel *chn)
{
    uint32_t request[5] = {0, TISP_BUF_TYPE_VIDEO_CAPTURE, TISP_MEMORY_USERPTR};

    if (record_ioctl(chn->fd, TISP_VIDIOC_REQBUFS, request) < 0)
        return -1;
    return free_capture_buffers(chn);
}

int IMP_ISP_Open(void)
{
    int fd;

    lock_p1();
    prepare_p1();
    if (p1.isp_open) {
        unlock_p1();
        return 0;
    }
    fd = open("/dev/tx-isp", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        p1.last_errno = errno;
        unlock_p1();
        return -1;
    }
    p1.isp_fd = fd;
    p1.isp_open = 1;
    unlock_p1();
    return 0;
}

int IMP_ISP_Close(void)
{
    int i;

    lock_p1();
    prepare_p1();
    if (p1.sensor_added || p1.sensor_enabled || p1.tuning_enabled) {
        unlock_p1();
        return -1;
    }
    for (i = 0; i < OPENIMP_FS_CHANNELS; i++) {
        if (p1.channels[i].created || p1.channels[i].enabled) {
            unlock_p1();
            return -1;
        }
    }
    if (p1.tuning_fd >= 0)
        close(p1.tuning_fd);
    if (p1.isp_fd >= 0)
        close(p1.isp_fd);
    p1.tuning_fd = -1;
    p1.isp_fd = -1;
    p1.isp_open = 0;
#if !defined(PLATFORM_T41)
    memset(p1.camera_input, 0, sizeof(p1.camera_input));
#endif
    dma_deinit();
    unlock_p1();
    return 0;
}

#if !defined(PLATFORM_T41)
/* Multi camera system (T40 only).  IMPISPCameraInputMode is 24 bytes:
 * sensor_num, dual_mode, {en, switch_con, switch_con_num}, joint_mode.
 * Checks and result codes follow the vendor T40 1.3.1 libimp: -8 ISP not
 * open, -9 NULL argument, -4 value out of range, -10 select outside
 * IMPISP_DUALSENSOR_DUAL_SELECT_MODE, -1 ioctl failure. */
#define CAMIN_WORDS 6
#define CAMIN_SELECT_MODE 2U

static int camera_input_valid(const uint32_t *m)
{
    uint32_t joint = m[5];

    if (m[0] >= 4 || m[1] >= 5 || joint > 16384U)
        return 0;
    if ((joint & 0xfU) >= 5 || ((joint >> 4) & 0xfU) >= 5 ||
        ((joint >> 8) & 0xfU) >= 5)
        return 0;
    /* either the fs0+fs3 / fs1+fs4 nibbles or the fs0+fs1 / fs3+fs4 ones */
    return (joint & 0x0f0fU) == 0 || (joint & 0xf0f0U) == 0;
}

int IMP_ISP_SetCameraInputMode(void *mode)
{
    int result = -1;

    lock_p1();
    prepare_p1();
    if (!p1.isp_open || p1.isp_fd < 0)
        result = -8;
    else if (!mode)
        result = -9;
    else if (!camera_input_valid(mode))
        result = -4;
    else if (record_ioctl(p1.isp_fd, TISP_VIDIOC_SET_CAMERA_INPUT_MODE,
                          mode) == 0) {
        memcpy(p1.camera_input, mode, sizeof(p1.camera_input));
        result = 0;
    }
    unlock_p1();
    return result;
}

int IMP_ISP_GetCameraInputMode(void *mode)
{
    int result = 0;

    lock_p1();
    prepare_p1();
    if (!p1.isp_open)
        result = -8;
    else if (!mode)
        result = -9;
    else
        memcpy(mode, p1.camera_input, sizeof(p1.camera_input));
    unlock_p1();
    return result;
}

int IMP_ISP_SetCameraInputSelect(int vinum)
{
    int result = -1;
    int32_t arg = vinum;

    lock_p1();
    prepare_p1();
    if (!p1.isp_open || p1.isp_fd < 0)
        result = -8;
    else if (p1.camera_input[1] != CAMIN_SELECT_MODE)
        result = -10;
    else if ((uint32_t)vinum >= 2U)
        result = -9;
    else if (record_ioctl(p1.isp_fd, TISP_VIDIOC_SET_CAMERA_INPUT_SELECT,
                          &arg) == 0)
        result = 0;
    unlock_p1();
    return result;
}
#endif

int IMP_ISP_AddSensor(IMPVI_NUM num, IMPSensorInfo *info)
{
    struct tisp_initarg input;
    struct tisp_buf_info mdns;

    if (num < IMPVI_MAIN || num >= IMPVI_BUTT || !info)
        return -1;
    lock_p1();
    prepare_p1();
    if (!p1.isp_open || p1.sensor_added) {
        unlock_p1();
        return -1;
    }
    if (record_ioctl(p1.isp_fd, TISP_VIDIOC_REGISTER_SENSOR, info) < 0) {
        unlock_p1();
        return -1;
    }
    if (p1.bin_path_set[num] &&
        record_ioctl(p1.isp_fd, TISP_VIDIOC_SET_DEFAULT_BIN_PATH,
                     &p1.bin_paths[num]) < 0) {
        unlock_p1();
        return -1;
    }
    memset(&input, 0, sizeof(input));
    input.enable = 1;
    input.vinum = num;
    if (record_ioctl(p1.isp_fd, TISP_VIDIOC_S_INPUT, &input) < 0) {
        unlock_p1();
        return -1;
    }
    memset(&mdns, 0, sizeof(mdns));
    mdns.vinum = (uint32_t)num;
    if (record_ioctl(p1.isp_fd, TISP_VIDIOC_GET_MDNS_BUF_INFO, &mdns) < 0) {
        unlock_p1();
        return -1;
    }
    if (mdns.size && dma_alloc(mdns.size, "isp-mdns", &mdns.paddr,
                               &p1.mdns_virtual) < 0) {
        unlock_p1();
        return -1;
    }
    if (record_ioctl(p1.isp_fd, TISP_VIDIOC_SET_MDNS_BUF_INFO, &mdns) < 0) {
        if (mdns.paddr)
            DMA_FreePhys(mdns.paddr);
        p1.mdns_virtual = NULL;
        unlock_p1();
        return -1;
    }
    p1.mdns = mdns;
    p1.sensor = *info;
    p1.sensor_added = 1;
    unlock_p1();
    return 0;
}

int IMP_ISP_DelSensor(IMPVI_NUM num, IMPSensorInfo *info)
{
    struct tisp_initarg input;
    int result;

    if (num < IMPVI_MAIN || num >= IMPVI_BUTT || !info)
        return -1;
    lock_p1();
    prepare_p1();
    if (!p1.sensor_added || p1.sensor_enabled) {
        unlock_p1();
        return -1;
    }
    memset(&input, 0, sizeof(input));
    input.vinum = num;
    result = record_ioctl(p1.isp_fd, TISP_VIDIOC_S_INPUT, &input);
#if defined(PLATFORM_T41)
    /* T41 creates the sensor I2C client during REGISTER_SENSOR.  Release it
     * after deselecting the input so a new producer can register the same
     * OS04D10 without unloading the ISP module or rebooting the camera. */
    if (result == 0)
        result = record_ioctl(p1.isp_fd, TISP_VIDIOC_UNREGISTER_SENSOR,
                              info);
#else
    /* The stock T40 driver owns the sensor subdevice for the lifetime of the
     * sensor kernel module.  Its legacy UNREGISTER_SENSOR delegation tears
     * down that module-owned object and is not part of a userspace close.
     * Deselecting the input plus clearing this library's ownership is the
     * safe, repeatable DelSensor operation.
     */
#endif
    if (result == 0) {
        p1.sensor_added = 0;
        if (p1.mdns.paddr)
            DMA_FreePhys(p1.mdns.paddr);
        memset(&p1.sensor, 0, sizeof(p1.sensor));
        memset(&p1.mdns, 0, sizeof(p1.mdns));
        p1.mdns_virtual = NULL;
    }
    unlock_p1();
    return result;
}

int IMP_ISP_EnableSensor(IMPVI_NUM num, IMPSensorInfo *info)
{
    struct tisp_initarg input;
    int result;

    if (num < IMPVI_MAIN || num >= IMPVI_BUTT || !info)
        return -1;
    lock_p1();
    prepare_p1();
    if (!p1.sensor_added) {
        unlock_p1();
        return -1;
    }
    if (p1.sensor_enabled) {
        unlock_p1();
        return 0;
    }
    input.enable = 1;
    input.vinum = num;
    result = record_ioctl(p1.isp_fd, TISP_VIDIOC_G_INPUT, &input);
    input.enable = 1;
    input.vinum = num;
    if (result == 0)
        result = record_ioctl(p1.isp_fd, TISP_VIDIOC_PREPARE_SENSOR,
                              &input);
    if (result == 0)
        result = record_ioctl(p1.isp_fd, TISP_VIDIOC_START_SENSOR, &input);
    if (result == 0)
        result = record_ioctl(p1.isp_fd, TISP_VIDIOC_ENABLE_SENSOR, &input);
    if (result == 0)
        p1.sensor_enabled = 1;
    unlock_p1();
    return result;
}

int IMP_ISP_DisableSensor(IMPVI_NUM num)
{
    struct tisp_initarg input;
    int result;

    if (num < IMPVI_MAIN || num >= IMPVI_BUTT)
        return -1;
    lock_p1();
    prepare_p1();
    if (!p1.sensor_enabled) {
        unlock_p1();
        return 0;
    }
    input.enable = 0;
    input.vinum = num;
    result = record_ioctl(p1.isp_fd, TISP_VIDIOC_DISABLE_SENSOR, &input);
    if (result == 0)
        result = record_ioctl(p1.isp_fd, TISP_VIDIOC_STOP_SENSOR, &input);
    if (result == 0)
        result = record_ioctl(p1.isp_fd, TISP_VIDIOC_FINISH_SENSOR, &input);
    if (result == 0)
        p1.sensor_enabled = 0;
    unlock_p1();
    return result;
}

int OpenIMP_P1_SetDefaultBinPath(IMPVI_NUM num, const char *path)
{
    size_t length;

    if (num < IMPVI_MAIN || num >= IMPVI_BUTT || !path)
        return -1;
    length = strlen(path);
    if (length >= sizeof(p1.bin_paths[num].path))
        return -1;
    lock_p1();
    prepare_p1();
    if (!p1.isp_open || p1.sensor_added) {
        unlock_p1();
        return -1;
    }
    memset(&p1.bin_paths[num], 0, sizeof(p1.bin_paths[num]));
    p1.bin_paths[num].vinum = num;
    memcpy(p1.bin_paths[num].path, path, length + 1U);
    p1.bin_path_set[num] = 1;
    unlock_p1();
    return 0;
}

/* Vendor IMP_ISP_GetDefaultBinPath hands out "the absolute path to the Bin
 * file" of the running user-defined ISP (T40 1.3.1/en imp_isp.h, "must be
 * called after the sensor is added").  libimp owns exactly one such path: the
 * record the caller passed before AddSensor, which OpenIMP_P1_AddSensor forwards
 * to the kernel through TISP_VIDIOC_SET_DEFAULT_BIN_PATH - that record, and
 * nothing else, is the path in effect.  There is no libimp default to fall back
 * to: on the open stack the T40 driver loads the module parameter
 * t40_tuning_bin_path (default "/usr/share/sensor/gc4653-t40.bin",
 * driver/t40/tx_isp_t40_recovered.c:9095, load site :73063 - it ignores the
 * user-space path completely) and the T41 driver falls back to
 * "/etc/sensor/<sensor>-t41.bin" (driver/t41/tx_isp_t41_recovered.c:54266).
 * Both live in the kernel, so when OpenIMP was never given a path the only
 * honest answer is an error, not an empty string with retval 0. */
int OpenIMP_P1_GetDefaultBinPath(IMPVI_NUM num, char *path, size_t size)
{
    size_t length;

    if (num < IMPVI_MAIN || num >= IMPVI_BUTT || path == NULL || size == 0U)
        return -1;
    lock_p1();
    if (!p1.bin_path_set[num]) {
        unlock_p1();
        errno = ENODATA;
        return -1;
    }
    length = strlen(p1.bin_paths[num].path);
    if (length >= size) {
        unlock_p1();
        errno = ERANGE;
        return -1;
    }
    memcpy(path, p1.bin_paths[num].path, length + 1U);
    unlock_p1();
    return 0;
}

int IMP_ISP_EnableTuning(void)
{
    int fd;

    lock_p1();
    prepare_p1();
    if (!p1.sensor_enabled) {
        unlock_p1();
        return -1;
    }
    if (p1.tuning_enabled) {
        unlock_p1();
        return 0;
    }
    fd = open("/dev/isp-m0", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        p1.last_errno = errno;
        unlock_p1();
        return -1;
    }
    p1.tuning_fd = fd;
    p1.tuning_enabled = 1;
    unlock_p1();
    return 0;
}

/* Vendor: with tuning enabled, store the video-loss callback (NULL clears
 * it); the monitor is src/core/video_drop.c. */
int IMP_ISP_Tuning_SetVideoDrop(void *attr)
{
    uint32_t enabled;

    lock_p1();
    prepare_p1();
    enabled = p1.tuning_enabled;
    unlock_p1();
    if (!enabled)
        return -1;
    return openimp_video_drop_set((void (*)(void))attr);
}

int IMP_ISP_DisableTuning(void)
{
    lock_p1();
    prepare_p1();
    if (p1.tuning_fd >= 0)
        close(p1.tuning_fd);
    p1.tuning_fd = -1;
    p1.tuning_enabled = 0;
    unlock_p1();
    return 0;
}

/*
 * Frame delay cache (IMP_FrameSource_SetMaxDelay / SetDelay / GetTimedFrame).
 * The vendor keeps the last `delay` frames of a channel and
 * GetTimedFrame hands out the one closest to a time stamp.  OpenIMP has no
 * capture thread, so the cache is fed by the frames the consumers dequeue
 * (GetFrame, thus also the IVS feeder) and, for a blocking GetTimedFrame, by
 * the call itself.  A cached frame is a copy: the capture buffer goes back
 * to the driver as usual.  GetFrame itself is not delayed.
 */
#define FS_ERR_CHN      ((int)0x80010001u)  /* IMP_ERR_FS_INVALID_CHNID */
#define FS_ERR_PARAM    ((int)0x80010002u)  /* IMP_ERR_FS_ILLEGAL_PARAM */
#define FS_ERR_STATE    ((int)0x80010040u)  /* IMP_ERR_FS_NOT_SUPPORT */
#define FS_ERR_EMPTY    ((int)0x80010400u)  /* IMP_ERR_FS_BUF_EMPTY */
#define FS_ERR_TIMEOUT  ((int)0x80012000u)  /* IMP_ERR_FS_OVERTIME */
#define FS_DELAY_MAX    100
#define FS_TIMED_WAIT_US 2000000

typedef struct {
    uint64_t ts;
    uint64_t minus;
    uint64_t plus;
} IMPFrameTimestamp;

struct fs_delay_entry {
    IMPFrameInfo info;
    uint8_t *data;
    size_t data_cap;
    int valid;
};

static struct fs_delay_cache {
    pthread_mutex_t lock;
    struct fs_delay_entry *ent;
    uint32_t cap;
    uint32_t head;                  /* next slot to write */
    int on;                         /* channel enabled (set by Enable) */
} p1_dly[OPENIMP_FS_CHANNELS] = {
    [0 ... OPENIMP_FS_CHANNELS - 1] = { .lock = PTHREAD_MUTEX_INITIALIZER }
};

static void fs_delay_free_locked(struct fs_delay_cache *c)
{
    uint32_t i;

    for (i = 0; i < c->cap; i++)
        free(c->ent[i].data);
    free(c->ent);
    c->ent = NULL;
    c->cap = 0;
    c->head = 0;
}

static void fs_delay_free(int channel)
{
    pthread_mutex_lock(&p1_dly[channel].lock);
    p1_dly[channel].on = 0;
    fs_delay_free_locked(&p1_dly[channel]);
    pthread_mutex_unlock(&p1_dly[channel].lock);
}

/* Keep `cap` frames (0 frees the cache); a changed size drops the content. */
static void fs_delay_resize_locked(struct fs_delay_cache *c, uint32_t cap)
{
    if (c->cap != cap) {
        fs_delay_free_locked(c);
        if (cap) {
            c->ent = calloc(cap, sizeof(*c->ent));
            if (c->ent)
                c->cap = cap;
        }
    }
}

static void fs_delay_resize(int channel, uint32_t cap)
{
    struct fs_delay_cache *c = &p1_dly[channel];

    pthread_mutex_lock(&c->lock);
    fs_delay_resize_locked(c, cap);
    pthread_mutex_unlock(&c->lock);
}

static void fs_delay_set_on(int channel, int on)
{
    pthread_mutex_lock(&p1_dly[channel].lock);
    p1_dly[channel].on = on;
    pthread_mutex_unlock(&p1_dly[channel].lock);
}

/* Called by GetFrame with the frame it is about to return; the capture
 * buffer stays owned by the caller until ReleaseFrame. */
static void fs_delay_store(int channel, uint32_t want, const IMPFrameInfo *f,
                           const void *virt, uint32_t size)
{
    struct fs_delay_cache *c = &p1_dly[channel];
    struct fs_delay_entry *e;
    uint8_t *grown;

    if (!want || !virt || !size)
        return;
    if (want > FS_DELAY_MAX)
        want = FS_DELAY_MAX;
    pthread_mutex_lock(&c->lock);
    /* a GetFrame racing with Disable/Destroy must not re-create the cache */
    if (c->on)
        fs_delay_resize_locked(c, want);
    if (c->on && c->cap && c->ent) {
        e = &c->ent[c->head];
        if (e->data_cap < size) {
            grown = realloc(e->data, size);
            if (grown) {
                e->data = grown;
                e->data_cap = size;
            }
        }
        if (e->data_cap >= size) {
            memcpy(e->data, virt, size);
            e->info = *f;
            e->info.virAddr = 0;
            e->info.size = size;
            e->valid = 1;
            c->head = (c->head + 1) % c->cap;
        }
    }
    pthread_mutex_unlock(&c->lock);
}

/* Vendor: SetMaxDelay needs a created, not yet enabled channel; value
 * 0..100.  Stores only (no hardware). */
int IMP_FrameSource_SetMaxDelay(int channel, int maxcnt)
{
    if (channel < 0 || channel >= 7)
        return FS_ERR_CHN;
    if (maxcnt < 0 || maxcnt > FS_DELAY_MAX)
        return FS_ERR_PARAM;
    if (channel >= OPENIMP_FS_CHANNELS)
        return FS_ERR_STATE;
    lock_p1();
    prepare_p1();
    if (!p1.channels[channel].created || p1.channels[channel].enabled) {
        unlock_p1();
        return FS_ERR_STATE;
    }
    p1.channels[channel].maxdelay = maxcnt;
    unlock_p1();
    return 0;
}

int IMP_FrameSource_GetMaxDelay(int channel, int *maxcnt)
{
    if (channel < 0 || channel >= 7)
        return FS_ERR_CHN;
    if (!maxcnt)
        return FS_ERR_PARAM;
    if (channel >= OPENIMP_FS_CHANNELS)
        return FS_ERR_STATE;
    lock_p1();
    prepare_p1();
    if (!p1.channels[channel].created) {
        unlock_p1();
        return FS_ERR_STATE;
    }
    *maxcnt = p1.channels[channel].maxdelay;
    unlock_p1();
    return 0;
}

/* Vendor: any created channel, 0..100 and not above the max delay. */
int IMP_FrameSource_SetDelay(int channel, int cnt)
{
    uint32_t keep;

    if (channel < 0 || channel >= 7)
        return FS_ERR_CHN;
    if (cnt < 0 || cnt > FS_DELAY_MAX)
        return FS_ERR_PARAM;
    if (channel >= OPENIMP_FS_CHANNELS)
        return FS_ERR_STATE;
    lock_p1();
    prepare_p1();
    if (!p1.channels[channel].created) {
        unlock_p1();
        return FS_ERR_STATE;
    }
    if (cnt > p1.channels[channel].maxdelay) {
        unlock_p1();
        return FS_ERR_PARAM;
    }
    p1.channels[channel].delay = cnt;
    keep = (uint32_t)cnt;
    unlock_p1();
    fs_delay_resize(channel, keep);
    return 0;
}

int IMP_FrameSource_GetDelay(int channel, int *cnt)
{
    if (channel < 0 || channel >= 7)
        return FS_ERR_CHN;
    if (!cnt)
        return FS_ERR_PARAM;
    if (channel >= OPENIMP_FS_CHANNELS)
        return FS_ERR_STATE;
    lock_p1();
    prepare_p1();
    if (!p1.channels[channel].created) {
        unlock_p1();
        return FS_ERR_STATE;
    }
    *cnt = p1.channels[channel].delay;
    unlock_p1();
    return 0;
}

extern int IMP_FrameSource_GetFrame(int channel, IMPFrameInfo **frame);
extern int IMP_FrameSource_ReleaseFrame(int channel, IMPFrameInfo *frame);

static uint64_t fs_mono_us(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u;
}

/* The cached frame closest to ts inside [ts - minus, ts + plus].  Returns
 * 1 and copies it (info and, with `out`, the picture) or 0; *newest gets
 * the newest cached time stamp (-1: none). */
static int fs_delay_find(int channel, const IMPFrameTimestamp *t, void *out,
                         IMPFrameInfo *info, int64_t *newest)
{
    struct fs_delay_cache *c = &p1_dly[channel];
    const struct fs_delay_entry *best = NULL;
    uint64_t best_d = 0;
    uint32_t i;
    int found = 0;

    *newest = -1;
    pthread_mutex_lock(&c->lock);
    for (i = 0; i < c->cap; i++) {
        const struct fs_delay_entry *e = &c->ent[i];
        uint64_t ts, d;

        if (!e->valid || e->info.timeStamp < 0)
            continue;
        ts = (uint64_t)e->info.timeStamp;
        if (e->info.timeStamp > *newest)
            *newest = e->info.timeStamp;
        if (ts < t->ts) {
            d = t->ts - ts;
            if (d > t->minus)
                continue;
        } else {
            d = ts - t->ts;
            if (d > t->plus)
                continue;
        }
        if (!best || d < best_d) {
            best = e;
            best_d = d;
        }
    }
    if (best) {
        *info = best->info;
        if (out) {
            memcpy(out, best->data, best->info.size);
            info->virAddr = (uint32_t)(uintptr_t)out;
        } else {
            info->virAddr = (uint32_t)(uintptr_t)best->data;
        }
        found = 1;
    }
    pthread_mutex_unlock(&c->lock);
    return found;
}

/* Vendor GetTimedFrame: needs an enabled channel with a max delay; the
 * picture is copied to `framedata` when given (it must hold one frame),
 * otherwise virAddr points into the cache entry, valid until the cache
 * wraps or the channel is disabled (the vendor T41 skips the copy for NULL
 * and returns the info the same way).  `block`: wait up to 2 s for a frame in the window. */
int IMP_FrameSource_GetTimedFrame(int channel, IMPFrameTimestamp *framets,
                                  int block, void *framedata,
                                  IMPFrameInfo *frame)
{
    uint64_t deadline;
    int64_t newest;
    int maxdelay, delay, enabled;

    if (channel < 0 || channel >= 7)
        return FS_ERR_CHN;
    if (!framets || !frame)
        return FS_ERR_PARAM;
    if (channel >= OPENIMP_FS_CHANNELS)
        return FS_ERR_STATE;
    lock_p1();
    prepare_p1();
    enabled = p1.channels[channel].enabled;
    maxdelay = p1.channels[channel].maxdelay;
    delay = p1.channels[channel].delay;
    unlock_p1();
    if (!enabled || !maxdelay || !delay)
        return FS_ERR_STATE;
    deadline = fs_mono_us() + FS_TIMED_WAIT_US;
    for (;;) {
        IMPFrameInfo *pull = NULL;

        if (fs_delay_find(channel, framets, framedata, frame, &newest))
            return 0;
        if (!block)
            return FS_ERR_EMPTY;
        /* everything cached is already newer than the window */
        if (newest >= 0 &&
            (uint64_t)newest > framets->ts &&
            (uint64_t)newest - framets->ts > framets->plus)
            return FS_ERR_EMPTY;
        if (fs_mono_us() > deadline)
            return FS_ERR_TIMEOUT;
        /* pull one frame: GetFrame feeds the cache */
        if (IMP_FrameSource_GetFrame(channel, &pull) == 0 && pull)
            (void)IMP_FrameSource_ReleaseFrame(channel, pull);
        else
            usleep(2000);
    }
}

/* Vendor GetI2dAttr / SetI2dAttr store the five words in the channel
 * attribute (+44) and touch no hardware: the attribute is read when the
 * channel is enabled.  Flip together with mirror, or flip/mirror together
 * with rotate, is refused as the vendor does (bitwise on the raw words). */
int IMP_FrameSource_GetI2dAttr(int channel, IMPFSI2DAttr *attr)
{
    if (channel < 0 || channel >= 7)
        return FS_ERR_CHN;
    if (!attr)
        return FS_ERR_PARAM;
    if (channel >= OPENIMP_FS_CHANNELS)
        return FS_ERR_STATE;
    lock_p1();
    prepare_p1();
    if (!p1.channels[channel].created) {
        unlock_p1();
        return FS_ERR_STATE;
    }
    *attr = p1.channels[channel].attr.i2dattr;
    unlock_p1();
    return 0;
}

int IMP_FrameSource_SetI2dAttr(int channel, IMPFSI2DAttr *attr)
{
    if (channel < 0 || channel >= 7)
        return FS_ERR_CHN;
    if (!attr)
        return FS_ERR_PARAM;
    if ((attr->flip_enable & attr->mirr_enable) ||
        ((attr->flip_enable | attr->mirr_enable) & attr->rotate_enable))
        return FS_ERR_STATE;
    if (channel >= OPENIMP_FS_CHANNELS)
        return FS_ERR_STATE;
    lock_p1();
    prepare_p1();
    if (!p1.channels[channel].created) {
        unlock_p1();
        return FS_ERR_STATE;
    }
    p1.channels[channel].attr.i2dattr = *attr;
    unlock_p1();
    return 0;
}

int FrameSourceInit(void)
{
    lock_p1();
    prepare_p1();
    unlock_p1();
    return 0;
}

int IMP_FrameSource_CreateChn(int channel, IMPFSChnAttr *attr)
{
    char path[20];
    struct openimp_fs_channel *chn;
    int pool_id;
    int fd;

    if (channel < 0 || channel >= OPENIMP_FS_CHANNELS || !attr)
        return -1;
    lock_p1();
    prepare_p1();
    chn = &p1.channels[channel];
    if (chn->created) {
        unlock_p1();
        return -1;
    }
    memcpy(path, "/dev/framechan0", 16);
    path[14] = (char)('0' + channel);
    path[15] = '\0';
    fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        p1.last_errno = errno;
        unlock_p1();
        return -1;
    }
    pool_id = chn->pool_id;
    memset(chn, 0, sizeof(*chn));
    chn->fd = fd;
    chn->pool_id = pool_id;
    chn->created = 1;
    chn->attr = *attr;
    unlock_p1();
    return 0;
}

int IMP_FrameSource_SetChnAttr(int channel, const IMPFSChnAttr *attr)
{
    if (channel < 0 || channel >= OPENIMP_FS_CHANNELS || !attr)
        return -1;
    lock_p1();
    prepare_p1();
    if (p1.channels[channel].enabled) {
        /* the vendor stores the attribute, but the hardware format is
         * only programmed by EnableChn: a running channel accepts the
         * unchanged attribute (and I2D / frame rate words) only */
        IMPFSChnAttr cur = p1.channels[channel].attr;
        IMPFSChnAttr want = *attr;

        cur.i2dattr = want.i2dattr;
        cur.outFrmRateNum = want.outFrmRateNum;
        cur.outFrmRateDen = want.outFrmRateDen;
        if (memcmp(&cur, &want, sizeof(cur)) != 0) {
            unlock_p1();
            return -1;
        }
    }
    p1.channels[channel].attr = *attr;
    unlock_p1();
    return 0;
}

int IMP_FrameSource_GetChnAttr(int channel, IMPFSChnAttr *attr)
{
    if (channel < 0 || channel >= OPENIMP_FS_CHANNELS || !attr)
        return -1;
    lock_p1();
    prepare_p1();
    if (!p1.channels[channel].created) {
        unlock_p1();
        return -1;
    }
    *attr = p1.channels[channel].attr;
    unlock_p1();
    return 0;
}

int IMP_FrameSource_SetFrameDepth(int channel, int depth)
{
    if (channel < 0 || channel >= OPENIMP_FS_CHANNELS || depth < 0)
        return -1;
    lock_p1();
    prepare_p1();
    p1.channels[channel].depth = (uint32_t)depth;
    unlock_p1();
    return 0;
}

int IMP_FrameSource_GetFrameDepth(int channel, int *depth)
{
    if (channel < 0 || channel >= OPENIMP_FS_CHANNELS || !depth)
        return -1;
    lock_p1();
    prepare_p1();
    *depth = (int)p1.channels[channel].depth;
    unlock_p1();
    return 0;
}

int IMP_FrameSource_SetPool(int channel, int pool_id)
{
    if (channel < 0 || channel >= OPENIMP_FS_CHANNELS || pool_id < 0)
        return -1;
    lock_p1();
    prepare_p1();
    if (!p1.channels[channel].created || p1.channels[channel].enabled) {
        unlock_p1();
        return -1;
    }
    p1.channels[channel].pool_id = pool_id;
    unlock_p1();
    return 0;
}

int IMP_FrameSource_GetPool(int channel)
{
    int pool_id;

    if (channel < 0 || channel >= OPENIMP_FS_CHANNELS)
        return -1;
    lock_p1();
    prepare_p1();
    pool_id = p1.channels[channel].pool_id;
    unlock_p1();
    return pool_id;
}

int IMP_FrameSource_SetChnFifoAttr(int channel, IMPFSChnFifoAttr *attr)
{
    int delay, maxdelay;

    if (channel < 0 || channel >= OPENIMP_FS_CHANNELS || !attr ||
        attr->maxdepth < 0)
        return -1;
    lock_p1();
    prepare_p1();
    if (!p1.channels[channel].created || p1.channels[channel].enabled) {
        unlock_p1();
        return -1;
    }
    p1.channels[channel].fifo_attr = *attr;
    /* vendor: the FIFO depth is the frame delay (+776); the max delay
     * (+772) is not touched */
    p1.channels[channel].delay = attr->maxdepth;
    delay = attr->maxdepth;
    maxdelay = p1.channels[channel].maxdelay;
    unlock_p1();
    if (delay > maxdelay)
        delay = maxdelay;       /* no max delay: no cache */
    fs_delay_resize(channel, (uint32_t)(delay > FS_DELAY_MAX ?
                                         FS_DELAY_MAX : delay));
    return 0;
}

int IMP_FrameSource_GetChnFifoAttr(int channel, IMPFSChnFifoAttr *attr)
{
    if (channel < 0 || channel >= OPENIMP_FS_CHANNELS || !attr)
        return -1;
    lock_p1();
    prepare_p1();
    if (!p1.channels[channel].created) {
        unlock_p1();
        return -1;
    }
    *attr = p1.channels[channel].fifo_attr;
    unlock_p1();
    return 0;
}

static void fill_frame_format(struct tisp_frame_format *format,
                              const IMPFSChnAttr *attr)
{
    memset(format, 0, sizeof(*format));
    format->type = TISP_BUF_TYPE_VIDEO_CAPTURE;
    format->pix.width = (uint32_t)attr->picWidth;
    format->pix.height = (uint32_t)attr->picHeight;
    /*
     * IMP exposes PIX_FMT_NV12 as enum value 10, but the stock T40
     * frame-channel ABI consumes enum tisp_output_fmt.  The stock module's
     * isp_output_fmt table and SDK both identify semi-planar Y/CbCr 4:2:0 as
     * output format 0.
     */
    format->pix.pixelformat = TISP_OUTPUT_FMT_NV12;
    format->pix.field = TISP_FIELD_STOCK_PROGRESSIVE;
    format->pix.colorspace = TISP_COLORSPACE_STOCK;
    format->crop_enable = (uint32_t)attr->crop.enable;
    format->crop_top = (uint32_t)attr->crop.top;
    format->crop_left = (uint32_t)attr->crop.left;
    format->crop_width = (uint32_t)attr->crop.width;
    format->crop_height = (uint32_t)attr->crop.height;
    format->scaler_enable = (uint32_t)attr->scaler.enable;
    format->scaler_out_width = (uint32_t)attr->scaler.outwidth;
    format->scaler_out_height = (uint32_t)attr->scaler.outheight;
    /*
     * These two driver fields are not the public IMP numerator and
     * denominator.  They program the mscaler frame-drop loop and bit mask.
     * Stock libimp uses loop=0/mask=1 for the physical FrameSource channel,
     * both when the encoder consumes every sensor frame and when its output
     * rate is lower.  Encoder-side pacing performs any configured drop while
     * keeping the ISP (and its temporal filters) running at sensor cadence.
     */
    format->rate_bits = 0u;
    format->rate_mask = 1u;
    format->fcrop_enable = (uint32_t)attr->fcrop.enable;
    format->fcrop_top = (uint32_t)attr->fcrop.top;
    format->fcrop_left = (uint32_t)attr->fcrop.left;
    format->fcrop_width = (uint32_t)attr->fcrop.width;
    format->fcrop_height = (uint32_t)attr->fcrop.height;
#if defined(PLATFORM_T41)
    format->flip_enable = (uint32_t)(attr->mirr_enable != 0);
#endif
}

static void fill_qbuf(uint32_t *words, uint32_t index, uint32_t physical,
                      uint32_t size)
{
    memset(words, 0, TISP_BUFFER_WORDS * sizeof(*words));
    words[0] = index;
    words[1] = TISP_BUF_TYPE_VIDEO_CAPTURE;
    words[12] = TISP_MEMORY_USERPTR;
    words[13] = physical;
    words[14] = size;
}

#if defined(P1_CAPTURE_FEEDER)
/*
 * T41 has no capture thread: frames leave the driver only when a consumer
 * calls IMP_FrameSource_GetFrame, and the IVS sees them there.  A bound IVS
 * channel (timps' motion on an idle stream) would otherwise get nothing,
 * whereas the vendor IVS group receives the framesource's frames whether
 * or not an encoder pulls them.  This thread takes a frame through the
 * ordinary GetFrame/ReleaseFrame path for a channel with a receiving IVS
 * binding when no other consumer has dequeued one for three frame
 * intervals, so it is quiet while an encoder consumes the channel; the
 * driver hands each buffer to one DQBUF only, so it never shares a buffer
 * with the encoder, and the IVS sees every frame either of them takes.
 */
#define P1_IVS_POLL_US    20000u

static pthread_once_t p1_ivs_feeder_once = PTHREAD_ONCE_INIT;
static __thread int p1_in_ivs_feeder;   /* GetFrame called by the feeder */

int IMP_FrameSource_GetFrame(int channel, IMPFrameInfo **frame);
int IMP_FrameSource_ReleaseFrame(int channel, IMPFrameInfo *frame);

static uint64_t p1_monotonic_us(void)
{
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000u + (uint64_t)now.tv_nsec / 1000u;
}

/* No consumer for three frame intervals (100..500 ms): the channel is
 * idle and the feeder takes over at the full frame rate, so the move
 * algorithm (one result per skipFrameCnt frames) keeps the rate the
 * application expects; timps calls a second without a result a stall. */
static uint64_t p1_ivs_idle_us(const struct openimp_fs_channel *chn)
{
    uint64_t idle = 120000u;

    if (chn->attr.outFrmRateNum > 0 && chn->attr.outFrmRateDen > 0)
        idle = 3000000ull * (uint32_t)chn->attr.outFrmRateDen /
               (uint32_t)chn->attr.outFrmRateNum;
    if (idle < 100000u)
        idle = 100000u;
    if (idle > 500000u)
        idle = 500000u;
    return idle;
}

static void *p1_ivs_feeder(void *arg)
{
    (void)arg;
    p1_in_ivs_feeder = 1;
    for (;;) {
        int channel;
        int fed = 0;

        for (channel = 0; channel < OPENIMP_FS_CHANNELS; channel++) {
            IMPFrameInfo *frame = NULL;
            int due;

            lock_p1();
            due = p1.channels[channel].enabled &&
                  p1_monotonic_us() - p1.channels[channel].last_dequeue_us >=
                      p1_ivs_idle_us(&p1.channels[channel]);
            unlock_p1();
            if (!due || !openimp_t31_ivs_source_active(channel))
                continue;
            /* GetFrame waits for the next frame: one frame per pass, at
             * the channel's rate */
            if (IMP_FrameSource_GetFrame(channel, &frame) == 0 && frame) {
                (void)IMP_FrameSource_ReleaseFrame(channel, frame);
                fed = 1;
            }
        }
        if (!fed)
            usleep(P1_IVS_POLL_US);
    }
    return NULL;
}

static void p1_ivs_feeder_start(void)
{
    pthread_attr_t attr;
    pthread_t thread;

    if (pthread_attr_init(&attr) != 0)
        return;
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&thread, &attr, p1_ivs_feeder, NULL) != 0)
        syslog(LOG_ERR, "openimp-p1: IVS frame feeder thread not started");
    pthread_attr_destroy(&attr);
}

/* SnapFrame waits here for a consumer's next dequeue (t41_snap.h). */
static pthread_mutex_t p1_snap_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t p1_snap_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t p1_snap_serial[OPENIMP_FS_CHANNELS] = {
    [0 ... OPENIMP_FS_CHANNELS - 1] = PTHREAD_MUTEX_INITIALIZER
};

static void p1_snap_wake(void)
{
    pthread_mutex_lock(&p1_snap_mutex);
    pthread_cond_broadcast(&p1_snap_cond);
    pthread_mutex_unlock(&p1_snap_mutex);
}
#else
#define p1_monotonic_us() 0u
#endif

/* Last IMP_FrameSource_GetFrame call, CLOCK_MONOTONIC ms (wraps). */
static uint32_t p1_reader_ms;

static uint32_t p1_now_ms(void)
{
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint32_t)now.tv_sec * 1000u + (uint32_t)(now.tv_nsec / 1000000);
}

/* IMP_ISP_Tuning_SetVideoDrop (video_drop.h): there is no capture thread
 * here, frames are dequeued by their readers, so frames are expected while
 * a channel is enabled and something asked for one within 1.5 s (the IVS
 * feeder counts too). */
int openimp_fs_video_demand(void)
{
    uint32_t reader = __atomic_load_n(&p1_reader_ms, __ATOMIC_RELAXED);
    int channel;
    int enabled = 0;

    lock_p1();
    prepare_p1();
    for (channel = 0; channel < OPENIMP_FS_CHANNELS; channel++)
        if (p1.channels[channel].enabled)
            enabled = 1;
    unlock_p1();
    return enabled && reader && p1_now_ms() - reader < 1500u;
}

int IMP_FrameSource_EnableChn(int channel)
{
    struct openimp_fs_channel *chn;
    struct tisp_frame_format format;
    uint32_t request[5];
    uint32_t words[TISP_BUFFER_WORDS];
    uint32_t type;
    uint32_t count;
    uint32_t i;
    int result = -1;
    char capture_tag[16];

    if (channel < 0 || channel >= OPENIMP_FS_CHANNELS)
        return -1;
    /* "capture<n>": the T41 allocator keeps capture queues apart from the
     * long-lived buffers and names the channel in shortage reports. */
    snprintf(capture_tag, sizeof(capture_tag), "capture%d", channel);
    lock_p1();
    prepare_p1();
    chn = &p1.channels[channel];
    if (!chn->created || chn->fd < 0) {
        syslog(LOG_ERR,
               "openimp-p1: enable chn=%d invalid lifecycle created=%u fd=%d",
               channel, chn->created, chn->fd);
        unlock_p1();
        return -1;
    }
    if (chn->enabled) {
        unlock_p1();
        return 0;
    }
    if (chn->buffer_count && release_capture_queue(chn) < 0) {
        unlock_p1();
        return -1;
    }
    if (chn->attr.picWidth <= 0 || chn->attr.picHeight <= 0 ||
        chn->attr.pixFmt != (int32_t)TISP_PIX_FMT_NV12_ENUM) {
        syslog(LOG_ERR,
               "openimp-p1: enable chn=%d invalid format %dx%d pixfmt=%d expected=%u",
               channel, chn->attr.picWidth, chn->attr.picHeight,
               chn->attr.pixFmt, TISP_PIX_FMT_NV12_ENUM);
        unlock_p1();
        return -1;
    }
    fill_frame_format(&format, &chn->attr);
    trace_p1("P1_INNER SET_FMT_BEGIN\n");
    if (record_ioctl(chn->fd, TISP_VIDIOC_SET_FRAME_FORMAT, &format) < 0)
        goto done;
    trace_p1("P1_INNER SET_FMT_END\n");
    if (!format.pix.sizeimage) {
        syslog(LOG_ERR, "openimp-p1: enable chn=%d SET_FMT returned size 0",
               channel);
        goto done;
    }
    memset(request, 0, sizeof(request));
    count = chn->attr.nrVBs > 0 ? (uint32_t)chn->attr.nrVBs : 2U;
    if (count < 2U)
        count = 2U;
    if (count > OPENIMP_FS_BUFFERS)
        count = OPENIMP_FS_BUFFERS;
    request[0] = count;
    request[1] = TISP_BUF_TYPE_VIDEO_CAPTURE;
    request[2] = TISP_MEMORY_USERPTR;
    trace_p1("P1_INNER REQBUFS_BEGIN\n");
    if (record_ioctl(chn->fd, TISP_VIDIOC_REQBUFS, request) < 0)
        goto done;
    trace_p1("P1_INNER REQBUFS_END\n");
    count = request[0];
    if (!count || count > OPENIMP_FS_BUFFERS) {
        syslog(LOG_ERR,
               "openimp-p1: enable chn=%d REQBUFS returned count=%u",
               channel, count);
        goto done;
    }
    chn->buffer_count = count;
    chn->sizeimage = format.pix.sizeimage;
    for (i = 0; i < count; i++) {
        struct openimp_fs_buffer *buffer = &chn->buffers[i];

        trace_p1("P1_INNER ALLOC_BEGIN\n");
        if (dma_alloc(chn->sizeimage, capture_tag, &buffer->physical,
                      &buffer->virtual_address) < 0) {
            syslog(LOG_ERR,
                   "openimp-p1: enable chn=%d DMA alloc index=%u size=%u failed",
                   channel, i, chn->sizeimage);
            goto done;
        }
        trace_p1("P1_INNER ALLOC_END\n");
        buffer->size = chn->sizeimage;
        fill_qbuf(words, i, buffer->physical, buffer->size);
        trace_p1("P1_INNER QBUF_BEGIN\n");
        if (record_ioctl(chn->fd, TISP_VIDIOC_QBUF, words) < 0)
            goto done;
        trace_p1("P1_INNER QBUF_END\n");
        buffer->queued = 1;
    }
    type = TISP_BUF_TYPE_VIDEO_CAPTURE;
    trace_p1("P1_INNER STREAMON_BEGIN\n");
    if (record_ioctl(chn->fd, TISP_VIDIOC_STREAMON, &type) < 0)
        goto done;
    trace_p1("P1_INNER STREAMON_END\n");
    chn->enabled = 1;
#if defined(P1_CAPTURE_FEEDER)
    chn->last_dequeue_us = p1_monotonic_us();
#endif
    result = 0;
done:
    if (result && chn->buffer_count)
        release_capture_queue(chn);
    unlock_p1();
    if (!result)
        fs_delay_set_on(channel, 1);
#if defined(P1_CAPTURE_FEEDER)
    if (!result)
        (void)pthread_once(&p1_ivs_feeder_once, p1_ivs_feeder_start);
#endif
    return result;
}

int IMP_FrameSource_GetFrame(int channel, IMPFrameInfo **frame)
{
    struct openimp_fs_channel *chn;
    struct openimp_fs_buffer *buffer;
    uint32_t words[TISP_BUFFER_WORDS];
    uint32_t index;
    int attempts;
    int delay_cache = 0;
    IMPFrameInfo cache_frame;
    OpenIMPProfileStamp wait_profile;
#if defined(P1_CAPTURE_FEEDER)
    int snap_taken;
#endif

    if (channel < 0 || channel >= OPENIMP_FS_CHANNELS || !frame)
        return -1;
    *frame = NULL;
    lock_p1();
    prepare_p1();
    chn = &p1.channels[channel];
    if (!chn->enabled) {
        unlock_p1();
        return -1;
    }
    unlock_p1();
    {
        uint32_t now_ms = p1_now_ms();

        __atomic_store_n(&p1_reader_ms, now_ms ? now_ms : 1u,
                         __ATOMIC_RELAXED);
    }

    wait_profile = openimp_profile_begin();

    for (attempts = 0; attempts < 1000; attempts++) {
        OpenIMPProfileStamp dqbuf_profile;

        /*
         * Dequeue a frame which the ISP has already completed before asking
         * the driver for another one.  The stock T40 WAIT_FRAME ioctl reports
         * the ISP core edge rather than the frame-channel buffer becoming
         * dequeueable; using it between DQBUF attempts can skip that ready
         * edge and hold a 30 fps channel near 15 fps.  A short bounded DQBUF
         * retry keeps buffer ownership tied to the actual frame channel.
         */
        memset(words, 0, sizeof(words));
        words[1] = TISP_BUF_TYPE_VIDEO_CAPTURE;
        words[12] = TISP_MEMORY_USERPTR;
        dqbuf_profile = openimp_profile_begin();
        if (record_ioctl(chn->fd, TISP_VIDIOC_DQBUF, words) >= 0) {
            openimp_profile_end(OPENIMP_PROFILE_FRAME_SOURCE_DQBUF,
                                dqbuf_profile);
            break;
        }
        openimp_profile_end(OPENIMP_PROFILE_FRAME_SOURCE_DQBUF,
                            dqbuf_profile);
        if (errno != EAGAIN && errno != ENODATA && errno != EINTR) {
            openimp_profile_end(OPENIMP_PROFILE_FRAME_SOURCE_WAIT,
                                wait_profile);
            return -1;
        }
        usleep(1000);
    }
    openimp_profile_count(OPENIMP_PROFILE_DQBUF_RETRIES,
                          (uint64_t)attempts);
    if (attempts == 1000) {
        openimp_profile_end(OPENIMP_PROFILE_FRAME_SOURCE_WAIT, wait_profile);
        return -1;
    }
    index = words[0];
    lock_p1();
    /* DQBUF ran without the P1 lock: DisableChn (another thread, or the
     * T41 IVS feeder racing a stream stop) may have released the queue
     * meanwhile. Only a buffer of a still enabled channel that was queued
     * to the driver is a frame; anything else would hand the IVS and the
     * encoder a zeroed record (virAddr 0). */
    if (!chn->enabled || index >= chn->buffer_count ||
        !chn->buffers[index].queued || !chn->buffers[index].physical) {
        unlock_p1();
        openimp_profile_end(OPENIMP_PROFILE_FRAME_SOURCE_WAIT, wait_profile);
        return -1;
    }
    buffer = &chn->buffers[index];
    buffer->queued = 0;
    memset(&buffer->frame, 0, sizeof(buffer->frame));
    buffer->frame.index = (int32_t)index;
    buffer->frame.pool_idx = channel;
    buffer->frame.width = (uint32_t)chn->attr.picWidth;
    buffer->frame.height = (uint32_t)chn->attr.picHeight;
#if defined(PLATFORM_T41)
    buffer->frame.pixfmt = (uint32_t)chn->attr.pixFmt;
#else
    /* Vendor T40 1.3.x: the frame record's pixfmt word reads 0 for an NV12
     * channel (first device run); the encoder takes everything but NV21 as
     * NV12, so 0 is safe. */
    buffer->frame.pixfmt = 0;
#endif
    buffer->frame.size = buffer->size;
    buffer->frame.phyAddr = buffer->physical;
    buffer->frame.virAddr = (uint32_t)(uintptr_t)buffer->virtual_address;
#if defined(PLATFORM_T41)
    buffer->frame.direct_phyAddr = buffer->physical;
#endif
    buffer->frame.pool = chn;
#if defined(PLATFORM_T41)
    /* The T41 frame-channel wire timeval is CLOCK_MONOTONIC_RAW, at
     * words 5/6 (seconds/microseconds). Preserve the completion instant,
     * including real gaps, in P0's clock domain instead of timing DQBUF. */
    buffer->frame.timeStamp = words[6] < 1000000U ?
        OpenIMP_P0_NormalizeMonotonicTimeStamp(
            (uint64_t)words[5] * 1000000U + words[6]) : -1;
    if (buffer->frame.timeStamp < 0)
        buffer->frame.timeStamp = IMP_System_GetTimeStamp();
#else
    buffer->frame.timeStamp = IMP_System_GetTimeStamp();
#endif
    chn->frames_dequeued++;
    /* vendor: the cache exists only with a max delay (SetMaxDelay); a bare
     * SetChnFifoAttr must not make every GetFrame copy a frame */
    delay_cache = chn->maxdelay ?
        (chn->delay < chn->maxdelay ? chn->delay : chn->maxdelay) : 0;
    openimp_video_drop_note_frame();
#if defined(P1_CAPTURE_FEEDER)
    if (!p1_in_ivs_feeder)
        chn->last_dequeue_us = p1_monotonic_us();
    snap_taken = t41_snap_on_dequeue(&chn->snap, index);
#endif
    *frame = &buffer->frame;
    cache_frame = buffer->frame;
    unlock_p1();
    if (delay_cache > 0)
        fs_delay_store(channel, (uint32_t)delay_cache, &cache_frame,
                       (const void *)(uintptr_t)cache_frame.virAddr,
                       cache_frame.size);
#if defined(P1_CAPTURE_FEEDER)
    if (snap_taken)
        p1_snap_wake();
    /* T41 has no capture thread: IVS sees each frame as its consumer
     * dequeues it, as the T31 capture thread hands it every frame
     * (openimp_t31_ivs.c copies or pre-processes it synchronously). */
    openimp_t31_ivs_capture(channel, &buffer->frame);
#endif
    openimp_profile_end(OPENIMP_PROFILE_FRAME_SOURCE_WAIT, wait_profile);
    return 0;
}

int IMP_FrameSource_ReleaseFrame(int channel, IMPFrameInfo *frame)
{
    struct openimp_fs_channel *chn;
    struct openimp_fs_buffer *buffer;
    uint32_t words[TISP_BUFFER_WORDS];
    uint32_t index;
    int result;

    if (channel < 0 || channel >= OPENIMP_FS_CHANNELS || !frame ||
        frame->index < 0)
        return -1;
    index = (uint32_t)frame->index;
    lock_p1();
    prepare_p1();
    chn = &p1.channels[channel];
    if (!chn->enabled || index >= chn->buffer_count) {
        unlock_p1();
        return -1;
    }
    buffer = &chn->buffers[index];
    if (buffer->queued || frame != &buffer->frame) {
        unlock_p1();
        return -1;
    }
#if defined(P1_CAPTURE_FEEDER)
    /* SnapFrame is still copying this frame: requeued when it is done. */
    result = t41_snap_on_release(&chn->snap, index);
    if (result) {
        unlock_p1();
        return result < 0 ? -1 : 0;
    }
#endif
    fill_qbuf(words, index, buffer->physical, buffer->size);
    result = record_ioctl(chn->fd, TISP_VIDIOC_QBUF, words);
    if (result >= 0)
        buffer->queued = 1;
    unlock_p1();
    return result < 0 ? result : 0;
}

#if defined(P1_CAPTURE_FEEDER)
/*
 * Vendor T41 IMP_FrameSource_SnapFrame(chn, fmt, width, height, buffer,
 * info): one frame of an enabled channel, NV12 at the channel resolution,
 * copied into the caller's buffer; no SetFrameDepth needed; 0 or -1.
 *
 * The frame is borrowed, never taken: the next frame any consumer
 * (encoder, IVS feeder, application GetFrame) dequeues is held while it is
 * copied, and that consumer's release is deferred until the copy is done,
 * so the encoder sees every frame.  Its buffer goes back to the ISP one
 * copy later (a few ms per snapshot).  Only when no consumer dequeues
 * within the IVS idle interval (the channel is idle) does SnapFrame dequeue
 * a frame itself - there is no encoder to lose it then.
 *
 * info is the frame's record with index -1 (not releasable), virAddr the
 * caller's buffer, no physical address, size the packed NV12 size.
 */
int IMP_FrameSource_SnapFrame(int channel, int fmt, int width, int height,
                              void *framedata, IMPFrameInfo *info)
{
    struct openimp_fs_channel *chn;
    struct openimp_fs_buffer *buffer;
    struct timespec deadline;
    IMPFrameInfo record;
    uint64_t wait_us;
    const void *source;
    uint32_t source_size;
    int pulled = 0, ready = 0, enabled, requeue, result = -1;
    uint32_t words[TISP_BUFFER_WORDS];

    if (channel < 0 || channel >= OPENIMP_FS_CHANNELS || !framedata ||
        !info || fmt != (int)TISP_PIX_FMT_NV12_ENUM || width <= 0 ||
        height <= 0)
        return -1;
    lock_p1();
    prepare_p1();
    chn = &p1.channels[channel];
    if (!chn->enabled || chn->attr.picWidth != width ||
        chn->attr.picHeight != height ||
        chn->attr.pixFmt != (int32_t)TISP_PIX_FMT_NV12_ENUM) {
        unlock_p1();
        syslog(LOG_ERR, "openimp-p1: SnapFrame chn=%d %dx%d fmt %d does "
               "not match the enabled channel", channel, width, height, fmt);
        return -1;
    }
    wait_us = p1_ivs_idle_us(chn);
    unlock_p1();

    pthread_mutex_lock(&p1_snap_serial[channel]);
    pthread_mutex_lock(&p1_snap_mutex);
    lock_p1();
    chn->snap.waiting = 1;
    unlock_p1();
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_nsec += (long)(wait_us % 1000000u) * 1000L;
    deadline.tv_sec += (time_t)(wait_us / 1000000u) +
                       deadline.tv_nsec / 1000000000L;
    deadline.tv_nsec %= 1000000000L;
    for (;;) {
        lock_p1();
        ready = chn->snap.ready;
        enabled = chn->enabled && chn->snap.waiting;
        unlock_p1();
        if (ready || !enabled)
            break;
        if (pthread_cond_timedwait(&p1_snap_cond, &p1_snap_mutex,
                                   &deadline) != ETIMEDOUT)
            continue;
        if (pulled)
            break;
        /* Idle channel: dequeue one frame here; the hook lends it. */
        pulled = 1;
        pthread_mutex_unlock(&p1_snap_mutex);
        {
            IMPFrameInfo *frame = NULL;

            if (IMP_FrameSource_GetFrame(channel, &frame) == 0 && frame)
                (void)IMP_FrameSource_ReleaseFrame(channel, frame);
        }
        pthread_mutex_lock(&p1_snap_mutex);
    }
    pthread_mutex_unlock(&p1_snap_mutex);

    lock_p1();
    if (!chn->snap.ready) {
        (void)t41_snap_finish(&chn->snap);
        unlock_p1();
        pthread_mutex_unlock(&p1_snap_serial[channel]);
        return -1;
    }
    buffer = &chn->buffers[chn->snap.index];
    record = buffer->frame;
    source = buffer->virtual_address;
    source_size = buffer->size;
    unlock_p1();

    /* ISP DMA wrote the pixels: drop stale cache lines of this buffer
     * (page aligned rmem, so no neighbour shares a line). */
    if (DMA_RmemFlushCache((void *)source, source_size, 2) == 0 &&
        t41_snap_copy_nv12(framedata, source, source_size,
                           (uint32_t)width, (uint32_t)height) == 0)
        result = 0;

    lock_p1();
    requeue = t41_snap_finish(&chn->snap);
    if (requeue && chn->enabled) {
        fill_qbuf(words, (uint32_t)record.index, buffer->physical,
                  buffer->size);
        if (record_ioctl(chn->fd, TISP_VIDIOC_QBUF, words) >= 0)
            buffer->queued = 1;
    }
    unlock_p1();
    pthread_mutex_unlock(&p1_snap_serial[channel]);
    if (result)
        return -1;
    *info = record;
    info->index = -1;
    info->size = t41_snap_nv12_bytes((uint32_t)width, (uint32_t)height);
    info->phyAddr = 0;
    info->virAddr = (uint32_t)(uintptr_t)framedata;
#if defined(PLATFORM_T41)
    info->direct_phyAddr = 0;
#endif
    info->pool = NULL;
    return 0;
}
#endif

int IMP_FrameSource_DisableChn(int channel)
{
    struct openimp_fs_channel *chn;
    uint32_t type;
    int result = 0;

    if (channel < 0 || channel >= OPENIMP_FS_CHANNELS)
        return -1;
    lock_p1();
    prepare_p1();
    chn = &p1.channels[channel];
    if (!chn->enabled && !chn->buffer_count) {
        unlock_p1();
        return 0;
    }
#if defined(P1_CAPTURE_FEEDER)
    /* A SnapFrame copy reads a capture buffer: never free it underneath
     * (the copy is bounded, one frame). */
    while (chn->snap.ready) {
        unlock_p1();
        usleep(1000);
        lock_p1();
    }
    chn->snap.waiting = 0;
#endif
    if (chn->enabled) {
        type = TISP_BUF_TYPE_VIDEO_CAPTURE;
        result = record_ioctl(chn->fd, TISP_VIDIOC_STREAMOFF, &type);
        if (result < 0) {
            unlock_p1();
            return result;
        }
    }
    chn->enabled = 0;
    result = release_capture_queue(chn);
    unlock_p1();
    fs_delay_free(channel);
    return result;
}

int IMP_FrameSource_DestroyChn(int channel)
{
    struct openimp_fs_channel *chn;

    if (channel < 0 || channel >= OPENIMP_FS_CHANNELS)
        return -1;
    lock_p1();
    prepare_p1();
    chn = &p1.channels[channel];
    if (chn->enabled) {
        unlock_p1();
        return -1;
    }
    if (chn->buffer_count && release_capture_queue(chn) < 0) {
        unlock_p1();
        return -1;
    }
    if (chn->fd >= 0)
        close(chn->fd);
    memset(chn, 0, sizeof(*chn));
    chn->fd = -1;
    chn->pool_id = -1;
    unlock_p1();
    fs_delay_free(channel);
    return 0;
}

int OpenIMP_P1_GetState(uint32_t *isp_flags, uint32_t *channel_mask,
                        uint32_t *frames, uint32_t *last_ioctl,
                        int32_t *last_errno, uint32_t *rmem_base,
                        uint32_t *rmem_used)
{
    uint32_t mask = 0;
    uint32_t count = 0;
    int i;

    if (!isp_flags || !channel_mask || !frames || !last_ioctl ||
        !last_errno || !rmem_base || !rmem_used)
        return -1;
    lock_p1();
    prepare_p1();
    for (i = 0; i < OPENIMP_FS_CHANNELS; i++) {
        if (p1.channels[i].enabled)
            mask |= 1U << i;
        count += p1.channels[i].frames_dequeued;
    }
    *isp_flags = (p1.isp_open ? 1U : 0U) |
                 (p1.sensor_added ? 2U : 0U) |
                 (p1.sensor_enabled ? 4U : 0U) |
                 (p1.tuning_enabled ? 8U : 0U);
    *channel_mask = mask;
    *frames = count;
    *last_ioctl = p1.last_ioctl;
    *last_errno = p1.last_errno;
    *rmem_base = p1.dma.base;
    *rmem_used = p1.dma.next;
    OpenIMP_P2_DMAState(rmem_base, rmem_used);
    unlock_p1();
    return 0;
}

/* Raw ioctl on the main ISP node (the vendor ISPDevice fd) for the P3
 * entries which do not go through the tuning node: sensor register access,
 * frame drop, WDR switch. */
int OpenIMP_P1_IspIOCtl(uint32_t command, void *argument)
{
    int result;

    lock_p1();
    prepare_p1();
    if (!p1.sensor_enabled || p1.isp_fd < 0) {
        unlock_p1();
        errno = ENODEV;
        return -1;
    }
    result = record_ioctl(p1.isp_fd, command, argument);
    unlock_p1();
    return result;
}

/* The sensor the ISP runs: name (32 bytes) and control bus type. */
int OpenIMP_P1_GetSensorName(char name[32], int32_t *cbus_type)
{
    lock_p1();
    prepare_p1();
    if (!p1.sensor_enabled) {
        unlock_p1();
        return -1;
    }
    memcpy(name, p1.sensor.name, 32);
    *cbus_type = p1.sensor.cbus_type;
    unlock_p1();
    return 0;
}

/* P3 control-plane hook.  Keep ownership of the tuning descriptor in P1 so
 * later units do not duplicate ISP lifetime state or reach into this private
 * structure.  The helper is intentionally not part of the public IMP ABI. */
int OpenIMP_P1_TuningIOCtl(uint32_t command, void *argument)
{
    int result;

    lock_p1();
    prepare_p1();
    if (!p1.tuning_enabled || p1.tuning_fd < 0) {
        unlock_p1();
        errno = ENODEV;
        return -1;
    }
    result = record_ioctl(p1.tuning_fd, command, argument);
    unlock_p1();
    return result;
}
