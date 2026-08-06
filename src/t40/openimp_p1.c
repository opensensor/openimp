/* Clean standalone OpenIMP T40 ISP/FrameSource implementation.
 *
 * This unit intentionally has no runtime symbol bridge or OEM libimp dependency.
 * It speaks the stock T40 tx-isp and frame-channel ABI directly.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <syslog.h>
#include <unistd.h>

#include "openimp_profile.h"

#define OPENIMP_P1_MAGIC        0x50315434U /* "P1T4" */
#define OPENIMP_FS_CHANNELS     4
#define OPENIMP_FS_BUFFERS      4
#define OPENIMP_DEFAULT_RMEM_BASE 0x06000000U
#define OPENIMP_DEFAULT_RMEM_SIZE (96U * 1024U * 1024U)

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
    uint32_t buffer_count;
    uint32_t sizeimage;
    uint32_t frames_dequeued;
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
    struct openimp_dma_state dma;
    struct openimp_fs_channel channels[OPENIMP_FS_CHANNELS];
};

static struct openimp_p1_state p1;
static volatile uint32_t p1_lock;

extern int64_t IMP_System_GetTimeStamp(void);

static void trace_p1(const char *text)
{
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

static void parse_rmem(uint32_t *base, uint32_t *size)
{
    char text[512];
    char *field;
    char *end;
    unsigned long amount;
    unsigned long address;
    ssize_t length;
    int fd;

    *base = OPENIMP_DEFAULT_RMEM_BASE;
    *size = OPENIMP_DEFAULT_RMEM_SIZE;
    fd = open("/proc/cmdline", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return;
    length = read(fd, text, sizeof(text) - 1);
    close(fd);
    if (length <= 0)
        return;
    text[length] = '\0';
    field = strstr(text, "rmem=");
    if (!field)
        return;
    amount = strtoul(field + 5, &end, 0);
    if (end == field + 5)
        return;
    if (*end == 'M' || *end == 'm') {
        amount *= 1024UL * 1024UL;
        end++;
    } else if (*end == 'K' || *end == 'k') {
        amount *= 1024UL;
        end++;
    }
    if (*end != '@')
        return;
    address = strtoul(end + 1, &end, 0);
    if (!amount || amount > 0xffffffffUL || address > 0xffffffffUL)
        return;
    *size = (uint32_t)amount;
    *base = (uint32_t)address;
}

static int dma_init(void)
{
    const char *start_offset_text;
    uint32_t base;
    uint32_t size;
    uint32_t start_offset = 0;
    void *mapping;
    int fd;

    if (p1.dma.mapping)
        return 0;
    trace_p1("P1_INNER DMA_OPEN\n");
    parse_rmem(&base, &size);
    fd = open("/dev/rmem", O_RDWR | O_CLOEXEC);
    if (fd < 0)
        return -1;
    mapping = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                   (off_t)base);
    if (mapping == MAP_FAILED) {
        close(fd);
        return -1;
    }
    p1.dma.fd = fd;
    p1.dma.base = base;
    p1.dma.size = size;
    start_offset_text = getenv("OPENIMP_RMEM_START_OFFSET");
    if (start_offset_text && *start_offset_text) {
        char *end = NULL;
        unsigned long parsed = strtoul(start_offset_text, &end, 0);

        if (!end || *end != '\0' || parsed >= size ||
            (parsed & 4095UL) != 0) {
            munmap(mapping, size);
            close(fd);
            return -1;
        }
        start_offset = (uint32_t)parsed;
    }
    p1.dma.next = start_offset;
    p1.dma.mapping = mapping;
    trace_p1("P1_INNER DMA_MAPPED\n");
    return 0;
}

static void dma_deinit(void)
{
    if (p1.dma.mapping)
        munmap(p1.dma.mapping, p1.dma.size);
    if (p1.dma.fd >= 0)
        close(p1.dma.fd);
    memset(&p1.dma, 0, sizeof(p1.dma));
    p1.dma.fd = -1;
}

static int dma_alloc(uint32_t size, uint32_t *physical, void **virtual_address)
{
    uint32_t offset;
    uint32_t aligned_size;

    if (!size || !physical || !virtual_address)
        return -1;
    if (dma_init() < 0)
        return -1;
    offset = (p1.dma.next + 4095U) & ~4095U;
    aligned_size = (size + 4095U) & ~4095U;
    if (offset > p1.dma.size || aligned_size > p1.dma.size - offset)
        return -1;
    *physical = p1.dma.base + offset;
    *virtual_address = (void *)((uintptr_t)p1.dma.mapping + offset);
    memset(*virtual_address, 0, aligned_size);
    p1.dma.next = offset + aligned_size;
    return 0;
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
    dma_deinit();
    unlock_p1();
    return 0;
}

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
    if (mdns.size && dma_alloc(mdns.size, &mdns.paddr,
                               &p1.mdns_virtual) < 0) {
        unlock_p1();
        return -1;
    }
    if (record_ioctl(p1.isp_fd, TISP_VIDIOC_SET_MDNS_BUF_INFO, &mdns) < 0) {
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
        unlock_p1();
        return -1;
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
    unlock_p1();
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

    if (channel < 0 || channel >= OPENIMP_FS_CHANNELS)
        return -1;
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
        if (dma_alloc(chn->sizeimage, &buffer->physical,
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
    result = 0;
done:
    unlock_p1();
    return result;
}

int IMP_FrameSource_GetFrame(int channel, IMPFrameInfo **frame)
{
    struct openimp_fs_channel *chn;
    struct openimp_fs_buffer *buffer;
    uint32_t words[TISP_BUFFER_WORDS];
    uint32_t index;
    int attempts;
    OpenIMPProfileStamp wait_profile;

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
    if (index >= chn->buffer_count) {
        openimp_profile_end(OPENIMP_PROFILE_FRAME_SOURCE_WAIT, wait_profile);
        return -1;
    }
    lock_p1();
    buffer = &chn->buffers[index];
    buffer->queued = 0;
    memset(&buffer->frame, 0, sizeof(buffer->frame));
    buffer->frame.index = (int32_t)index;
    buffer->frame.pool_idx = channel;
    buffer->frame.width = (uint32_t)chn->attr.picWidth;
    buffer->frame.height = (uint32_t)chn->attr.picHeight;
    buffer->frame.pixfmt = (uint32_t)chn->attr.pixFmt;
    buffer->frame.size = buffer->size;
    buffer->frame.phyAddr = buffer->physical;
    buffer->frame.virAddr = (uint32_t)(uintptr_t)buffer->virtual_address;
#if defined(PLATFORM_T41)
    buffer->frame.direct_phyAddr = buffer->physical;
#endif
    buffer->frame.pool = chn;
    buffer->frame.timeStamp = IMP_System_GetTimeStamp();
    chn->frames_dequeued++;
    *frame = &buffer->frame;
    unlock_p1();
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
    fill_qbuf(words, index, buffer->physical, buffer->size);
    result = record_ioctl(chn->fd, TISP_VIDIOC_QBUF, words);
    if (result >= 0)
        buffer->queued = 1;
    unlock_p1();
    return result < 0 ? result : 0;
}

int IMP_FrameSource_DisableChn(int channel)
{
    struct openimp_fs_channel *chn;
    uint32_t request[5];
    uint32_t type;
    int result = 0;

    if (channel < 0 || channel >= OPENIMP_FS_CHANNELS)
        return -1;
    lock_p1();
    prepare_p1();
    chn = &p1.channels[channel];
    if (!chn->enabled) {
        unlock_p1();
        return 0;
    }
    type = TISP_BUF_TYPE_VIDEO_CAPTURE;
    result = record_ioctl(chn->fd, TISP_VIDIOC_STREAMOFF, &type);
    memset(request, 0, sizeof(request));
    request[1] = TISP_BUF_TYPE_VIDEO_CAPTURE;
    request[2] = TISP_MEMORY_USERPTR;
    if (record_ioctl(chn->fd, TISP_VIDIOC_REQBUFS, request) < 0 &&
        result == 0)
        result = -1;
    chn->enabled = 0;
    chn->buffer_count = 0;
    chn->sizeimage = 0;
    memset(chn->buffers, 0, sizeof(chn->buffers));
    unlock_p1();
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
    if (chn->fd >= 0)
        close(chn->fd);
    memset(chn, 0, sizeof(*chn));
    chn->fd = -1;
    chn->pool_id = -1;
    unlock_p1();
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
