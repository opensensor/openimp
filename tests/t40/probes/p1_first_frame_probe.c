#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PIX_FMT_NV12 10

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
    void *pool;
    int64_t timeStamp;
} IMPFrameInfo;

typedef int (*void_fn)(void);
typedef int (*isp_sensor_fn)(int, IMPSensorInfo *);
typedef int (*isp_num_fn)(int);
typedef int (*fs_attr_fn)(int, IMPFSChnAttr *);
typedef int (*fs_num_fn)(int);
typedef int (*fs_get_fn)(int, IMPFrameInfo **);
typedef int (*fs_release_fn)(int, IMPFrameInfo *);
typedef int (*state_fn)(uint32_t *, uint32_t *, uint32_t *, uint32_t *,
                        int32_t *, uint32_t *, uint32_t *);

struct api {
    void_fn system_init;
    void_fn system_exit;
    void_fn isp_open;
    void_fn isp_close;
    isp_sensor_fn add_sensor;
    isp_sensor_fn del_sensor;
    isp_sensor_fn enable_sensor;
    isp_num_fn disable_sensor;
    void_fn enable_tuning;
    void_fn disable_tuning;
    fs_attr_fn create_channel;
    fs_num_fn destroy_channel;
    fs_attr_fn set_attr;
    fs_num_fn enable_channel;
    fs_num_fn disable_channel;
    fs_get_fn get_frame;
    fs_release_fn release_frame;
    state_fn get_state;
};

static int maps_have_oem_libimp(void)
{
    char line[512];
    FILE *maps = fopen("/proc/self/maps", "r");

    if (!maps)
        return -1;
    while (fgets(line, sizeof(line), maps)) {
        char *name = strstr(line, "/libimp.so");
        if (name && (name[10] == '\0' || name[10] == '\n' ||
                     name[10] == ' ' || name[10] == '\r')) {
            fclose(maps);
            return 1;
        }
    }
    fclose(maps);
    return 0;
}

static int load_api(void *handle, struct api *api)
{
#define LOAD(member, symbol) do { \
    *(void **)(&api->member) = dlsym(handle, symbol); \
    if (!api->member) { \
        fprintf(stderr, "missing %s: %s\n", symbol, dlerror()); \
        return -1; \
    } \
} while (0)
    memset(api, 0, sizeof(*api));
    LOAD(system_init, "IMP_System_Init");
    LOAD(system_exit, "IMP_System_Exit");
    LOAD(isp_open, "IMP_ISP_Open");
    LOAD(isp_close, "IMP_ISP_Close");
    LOAD(add_sensor, "IMP_ISP_AddSensor");
    LOAD(del_sensor, "IMP_ISP_DelSensor");
    LOAD(enable_sensor, "IMP_ISP_EnableSensor");
    LOAD(disable_sensor, "IMP_ISP_DisableSensor");
    LOAD(enable_tuning, "IMP_ISP_EnableTuning");
    LOAD(disable_tuning, "IMP_ISP_DisableTuning");
    LOAD(create_channel, "IMP_FrameSource_CreateChn");
    LOAD(destroy_channel, "IMP_FrameSource_DestroyChn");
    LOAD(set_attr, "IMP_FrameSource_SetChnAttr");
    LOAD(enable_channel, "IMP_FrameSource_EnableChn");
    LOAD(disable_channel, "IMP_FrameSource_DisableChn");
    LOAD(get_frame, "IMP_FrameSource_GetFrame");
    LOAD(release_frame, "IMP_FrameSource_ReleaseFrame");
    LOAD(get_state, "OpenIMP_P1_GetState");
#undef LOAD
    return 0;
}

static void fill_attr(IMPFSChnAttr *attr, int width, int height)
{
    memset(attr, 0, sizeof(*attr));
    attr->picWidth = width;
    attr->picHeight = height;
    attr->pixFmt = PIX_FMT_NV12;
    attr->scaler.enable = 1;
    attr->scaler.outwidth = width;
    attr->scaler.outheight = height;
    attr->outFrmRateNum = 30;
    attr->outFrmRateDen = 1;
    attr->nrVBs = 2;
    attr->type = 0;
}

static int inspect_frame(int channel, IMPFrameInfo *frame,
                         uint32_t width, uint32_t height,
                         uint32_t *nonzero, uint32_t *checksum)
{
    static const char hex[] = "0123456789abcdef";
    const unsigned char *data;
    char preview_hex[4609];
    uint32_t samples = 0;
    uint32_t sum = 0;
    uint32_t step;
    uint32_t i;

    if (!frame || frame->width != width || frame->height != height ||
        frame->pixfmt != PIX_FMT_NV12 || !frame->virAddr ||
        frame->size < width * height) {
        fprintf(stderr,
                "bad frame ch=%d ptr=%p %ux%u fmt=%u size=%u phys=0x%x virt=0x%x\n",
                channel, (void *)frame, frame ? frame->width : 0,
                frame ? frame->height : 0, frame ? frame->pixfmt : 0,
                frame ? frame->size : 0, frame ? frame->phyAddr : 0,
                frame ? frame->virAddr : 0);
        return -1;
    }
    data = (const unsigned char *)(uintptr_t)frame->virAddr;
    step = frame->size / 4096U;
    if (!step)
        step = 1;
    for (i = 0; i < frame->size && samples < 4096U; i += step) {
        unsigned char value = data[i];
        if (value)
            (*nonzero)++;
        sum = (sum * 16777619U) ^ value;
        samples++;
    }
    *checksum = sum;
    if (!*nonzero) {
        fprintf(stderr, "all sampled bytes are zero on channel %d\n", channel);
        return -1;
    }
    if (getenv("OPENIMP_P1_PREVIEW_HEX")) {
        uint32_t preview_y;

        fprintf(stderr, "P1_PREVIEW_HEX_BEGIN channel=%d width=64 height=36\n",
                channel);
        for (preview_y = 0; preview_y < 36U; preview_y++) {
            uint32_t preview_x;

            for (preview_x = 0; preview_x < 64U; preview_x++) {
                unsigned char value = data[
                    ((preview_y * height / 36U) * width) +
                    (preview_x * width / 64U)];
                size_t output_index =
                    ((size_t)preview_y * 64U + preview_x) * 2U;
                preview_hex[output_index] = hex[value >> 4];
                preview_hex[output_index + 1U] = hex[value & 0xfU];
            }
        }
        preview_hex[4608] = '\0';
        fprintf(stderr, "P1_PREVIEW_HEX_DATA %s\n", preview_hex);
        fprintf(stderr, "P1_PREVIEW_HEX_END channel=%d\n", channel);
        fflush(stderr);
    }
    return 0;
}

static int dump_frame_if_requested(int channel, const IMPFrameInfo *frame)
{
    const char *directory = getenv("OPENIMP_P1_DUMP_DIR");
    const char *dump_raw = getenv("OPENIMP_P1_DUMP_RAW");
    const char *preview_stdout = getenv("OPENIMP_P1_PREVIEW_STDOUT");
    char path[256];
    FILE *output;
    const unsigned char *luma;
    unsigned char *preview;
    size_t preview_size;
    unsigned int scale = 1;
    unsigned int preview_width;
    unsigned int preview_height;
    unsigned int x;
    unsigned int y;
    size_t written;

    int use_stdout = preview_stdout && *preview_stdout;

    if ((!directory || !*directory) && !use_stdout)
        return 0;
    if (!frame || !frame->virAddr || !frame->size)
        return -1;
    while (frame->width / scale > 160U || frame->height / scale > 90U)
        scale++;
    preview_width = frame->width / scale;
    preview_height = frame->height / scale;
    if (!preview_width || !preview_height)
        return -1;
    preview_size = (size_t)preview_width * preview_height;
    preview = malloc(preview_size);
    if (!preview)
        return -1;
    luma = (const unsigned char *)(uintptr_t)frame->virAddr;
    for (y = 0; y < preview_height; y++) {
        for (x = 0; x < preview_width; x++) {
            preview[(size_t)y * preview_width + x] =
                luma[(y * scale * frame->width) + (x * scale)];
        }
    }
    if (use_stdout) {
        strcpy(path, "<stdout>");
        output = stdout;
    } else {
        if (snprintf(path, sizeof(path), "%s/frame%d-%ux%u-preview.pgm",
                     directory, channel, frame->width, frame->height) >=
            (int)sizeof(path))
            goto preview_error;
        output = fopen(path, "wb");
        if (!output) {
            perror(path);
            goto preview_error;
        }
    }
    if (fprintf(output, "P5\n%u %u\n255\n", preview_width,
                preview_height) < 0) {
        if (!use_stdout)
            fclose(output);
        goto preview_error;
    }
    if (fwrite(preview, 1, preview_size, output) != preview_size) {
        if (!use_stdout)
            fclose(output);
        goto preview_error;
    }
    if (use_stdout) {
        if (fflush(output) != 0)
            goto preview_error;
    } else if (fclose(output) != 0) {
        goto preview_error;
    }
    free(preview);
    fprintf(stderr,
            "P1_FRAME_PREVIEW channel=%d path=%s size=%ux%u scale=%u\n",
            channel, path, preview_width, preview_height, scale);

    if (!dump_raw || !*dump_raw || !directory || !*directory)
        return 0;
    if (snprintf(path, sizeof(path), "%s/frame%d-%ux%u.nv12", directory,
                 channel, frame->width, frame->height) >= (int)sizeof(path))
        return -1;
    output = fopen(path, "wb");
    if (!output) {
        perror(path);
        return -1;
    }
    written = fwrite((const void *)(uintptr_t)frame->virAddr, 1,
                     frame->size, output);
    if (fclose(output) != 0 || written != frame->size) {
        fprintf(stderr, "short frame dump %s: %zu/%u\n",
                path, written, frame->size);
        return -1;
    }
    fprintf(stderr, "P1_FRAME_DUMP channel=%d path=%s bytes=%u\n",
            channel, path, frame->size);
    return 0;

preview_error:
    free(preview);
    return -1;
}

static void dump_state(struct api *api, const char *where)
{
    uint32_t flags = 0, mask = 0, frames = 0, command = 0;
    uint32_t base = 0, used = 0;
    int32_t saved_errno = 0;

    api->get_state(&flags, &mask, &frames, &command, &saved_errno,
                   &base, &used);
    fprintf(stderr,
            "P1_STATE where=%s isp=0x%x channels=0x%x frames=%u "
            "last_ioctl=0x%08x errno=%d rmem=0x%08x+0x%x\n",
            where, flags, mask, frames, command, saved_errno, base, used);
}

int main(int argc, char **argv)
{
    const char *library = argc > 1 ? argv[1] : "/tmp/libimp-t40-p1.so";
    struct api api;
    IMPSensorInfo sensor;
    IMPFSChnAttr attrs[2];
    IMPFrameInfo *frames[2] = { NULL, NULL };
    uint32_t nonzero[2] = { 0, 0 };
    uint32_t checksum[2] = { 0, 0 };
    void *handle;
    int enabled[2] = { 0, 0 };
    int created[2] = { 0, 0 };
    int selected_channel = -1;
    unsigned int hold_seconds = 0;
    unsigned int warmup_frames = 0;
    int result = 1;
    int i;

    if (getenv("OPENIMP_P1_CHANNEL")) {
        selected_channel = atoi(getenv("OPENIMP_P1_CHANNEL"));
        if (selected_channel < 0 || selected_channel > 1) {
            fprintf(stderr, "OPENIMP_P1_CHANNEL must be 0 or 1\n");
            return 2;
        }
    }
    if (getenv("OPENIMP_P1_HOLD_SECONDS")) {
        hold_seconds = (unsigned int)atoi(getenv("OPENIMP_P1_HOLD_SECONDS"));
        if (hold_seconds > 300U) {
            fprintf(stderr, "OPENIMP_P1_HOLD_SECONDS must be 0..300\n");
            return 2;
        }
    }
    if (getenv("OPENIMP_P1_WARMUP_FRAMES")) {
        warmup_frames =
            (unsigned int)atoi(getenv("OPENIMP_P1_WARMUP_FRAMES"));
        if (warmup_frames > 3000U) {
            fprintf(stderr, "OPENIMP_P1_WARMUP_FRAMES must be 0..3000\n");
            return 2;
        }
    }

    handle = dlopen(library, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return 2;
    }
    if (load_api(handle, &api) < 0)
        return 3;
    if (maps_have_oem_libimp() != 0) {
        fprintf(stderr, "OEM libimp is mapped before the P1 run\n");
        return 4;
    }

    memset(&sensor, 0, sizeof(sensor));
    strcpy(sensor.name, "gc4653");
    sensor.cbus_type = 1;
    strcpy(sensor.i2c.type, "gc4653");
    sensor.i2c.addr = 0x29;
    sensor.i2c.i2c_adapter_id = 1;
    sensor.rst_gpio = -1;
    sensor.pwdn_gpio = -1;
    sensor.power_gpio = -1;
    sensor.sensor_id = 0x4653;
    sensor.video_interface = 0;
    sensor.mclk = 1;
    sensor.default_boot = 0;
    if (getenv("OPENIMP_P1_MAIN_LOW"))
        fill_attr(&attrs[0], 640, 360);
    else
        fill_attr(&attrs[0], 1920, 1080);
    fill_attr(&attrs[1], 640, 360);

#define CHECK(call, label) do { \
    fprintf(stderr, "P1_BEGIN %s\n", label); \
    fflush(stderr); \
    int check_result = (call); \
    fprintf(stderr, "P1_END %s result=%d\n", label, check_result); \
    fflush(stderr); \
    if (check_result != 0) { \
        fprintf(stderr, "failed: %s -> %d\n", label, check_result); \
        dump_state(&api, label); \
        goto cleanup; \
    } \
} while (0)
    CHECK(api.isp_open(), "ISP_Open");
    CHECK(api.add_sensor(0, &sensor), "ISP_AddSensor");
    CHECK(api.enable_sensor(0, &sensor), "ISP_EnableSensor");
    CHECK(api.system_init(), "System_Init");
    CHECK(api.enable_tuning(), "ISP_EnableTuning");
    for (i = 0; i < 2; i++) {
        if (selected_channel >= 0 && i != selected_channel)
            continue;
        CHECK(api.create_channel(i, &attrs[i]), "FrameSource_CreateChn");
        created[i] = 1;
        CHECK(api.set_attr(i, &attrs[i]), "FrameSource_SetChnAttr");
    }
    for (i = 0; i < 2; i++) {
        if (selected_channel >= 0 && i != selected_channel)
            continue;
        CHECK(api.enable_channel(i), "FrameSource_EnableChn");
        enabled[i] = 1;
    }
    for (i = 0; i < 2; i++) {
        unsigned int warmup;

        if (selected_channel >= 0 && i != selected_channel)
            continue;
        if (warmup_frames)
            fprintf(stderr, "P1_WARMUP_BEGIN channel=%d frames=%u\n",
                    i, warmup_frames);
        for (warmup = 0; warmup < warmup_frames; warmup++) {
            int warmup_result = api.get_frame(i, &frames[i]);

            if (warmup_result != 0) {
                fprintf(stderr,
                        "failed: FrameSource_GetFrame warmup ch=%d "
                        "frame=%u -> %d\n",
                        i, warmup, warmup_result);
                dump_state(&api, "FrameSource_GetFrame warmup");
                goto cleanup;
            }
            warmup_result = api.release_frame(i, frames[i]);
            if (warmup_result != 0) {
                fprintf(stderr,
                        "failed: FrameSource_ReleaseFrame warmup ch=%d "
                        "frame=%u -> %d\n",
                        i, warmup, warmup_result);
                dump_state(&api, "FrameSource_ReleaseFrame warmup");
                goto cleanup;
            }
            frames[i] = NULL;
        }
        if (warmup_frames)
            fprintf(stderr, "P1_WARMUP_END channel=%d frames=%u\n",
                    i, warmup_frames);
        CHECK(api.get_frame(i, &frames[i]), "FrameSource_GetFrame");
        if (inspect_frame(i, frames[i], (uint32_t)attrs[i].picWidth,
                          (uint32_t)attrs[i].picHeight, &nonzero[i],
                          &checksum[i]) < 0) {
            dump_state(&api, "inspect_frame");
            goto cleanup;
        }
        if (dump_frame_if_requested(i, frames[i]) < 0) {
            dump_state(&api, "dump_frame");
            goto cleanup;
        }
        CHECK(api.release_frame(i, frames[i]), "FrameSource_ReleaseFrame");
        frames[i] = NULL;
    }
    if (maps_have_oem_libimp() != 0) {
        fprintf(stderr, "OEM libimp appeared during P1 run\n");
        goto cleanup;
    }
    fprintf(stderr,
            "P1_FRAMES_ACQUIRED main_nonzero=%u main_checksum=0x%08x "
            "sub_nonzero=%u sub_checksum=0x%08x oem_mapped=0\n",
            nonzero[0], checksum[0], nonzero[1], checksum[1]);
    fflush(stderr);
    if (hold_seconds) {
        fprintf(stderr, "P1_HOLD seconds=%u\n", hold_seconds);
        fflush(stderr);
        sleep(hold_seconds);
    }
    result = 0;

cleanup:
    for (i = 1; i >= 0; i--) {
        if (frames[i]) {
            fprintf(stderr, "P1_CLEAN_BEGIN ReleaseFrame ch=%d\n", i);
            fflush(stderr);
            fprintf(stderr, "P1_CLEAN_END ReleaseFrame ch=%d result=%d\n",
                    i, api.release_frame(i, frames[i]));
            fflush(stderr);
        }
        if (enabled[i]) {
            fprintf(stderr, "P1_CLEAN_BEGIN DisableChn ch=%d\n", i);
            fflush(stderr);
            fprintf(stderr, "P1_CLEAN_END DisableChn ch=%d result=%d\n",
                    i, api.disable_channel(i));
            fflush(stderr);
        }
        if (created[i]) {
            fprintf(stderr, "P1_CLEAN_BEGIN DestroyChn ch=%d\n", i);
            fflush(stderr);
            fprintf(stderr, "P1_CLEAN_END DestroyChn ch=%d result=%d\n",
                    i, api.destroy_channel(i));
            fflush(stderr);
        }
    }
    fprintf(stderr, "P1_CLEAN_BEGIN DisableTuning\n"); fflush(stderr);
    fprintf(stderr, "P1_CLEAN_END DisableTuning result=%d\n",
            api.disable_tuning()); fflush(stderr);
    fprintf(stderr, "P1_CLEAN_BEGIN DisableSensor\n"); fflush(stderr);
    fprintf(stderr, "P1_CLEAN_END DisableSensor result=%d\n",
            api.disable_sensor(0)); fflush(stderr);
    fprintf(stderr, "P1_CLEAN_BEGIN DelSensor\n"); fflush(stderr);
    fprintf(stderr, "P1_CLEAN_END DelSensor result=%d\n",
            api.del_sensor(0, &sensor)); fflush(stderr);
    fprintf(stderr, "P1_CLEAN_BEGIN ISP_Close\n"); fflush(stderr);
    fprintf(stderr, "P1_CLEAN_END ISP_Close result=%d\n",
            api.isp_close()); fflush(stderr);
    fprintf(stderr, "P1_CLEAN_BEGIN System_Exit\n"); fflush(stderr);
    fprintf(stderr, "P1_CLEAN_END System_Exit result=%d\n",
            api.system_exit()); fflush(stderr);
    dump_state(&api, "cleanup");
    if (result == 0) {
        FILE *pass_output = getenv("OPENIMP_P1_PREVIEW_STDOUT") ?
            stderr : stdout;
        fprintf(pass_output,
                "OPENIMP_P1_PASS main_nonzero=%u main_checksum=0x%08x "
                "sub_nonzero=%u sub_checksum=0x%08x oem_mapped=0\n",
                nonzero[0], checksum[0], nonzero[1], checksum[1]);
    }
    dlclose(handle);
    return result;
#undef CHECK
}
