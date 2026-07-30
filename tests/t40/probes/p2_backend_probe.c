#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PIX_FMT_NV12 10

typedef struct { char type[20]; int32_t addr; int32_t i2c_adapter_id; } IMPI2CInfo;
typedef struct { char modalias[32]; int32_t bus_num; } IMPSPIInfo;
typedef struct {
    char name[32]; int32_t cbus_type;
    union { IMPI2CInfo i2c; IMPSPIInfo spi; };
    int32_t rst_gpio, pwdn_gpio, power_gpio;
    uint16_t sensor_id, reserved;
    int32_t video_interface, mclk, default_boot;
} IMPSensorInfo;
typedef struct { int32_t enable, left, top, width, height; } IMPFSChnCrop;
typedef struct { int32_t enable, outwidth, outheight; } IMPFSChnScaler;
typedef struct { int32_t i2d_enable, flip_enable, mirr_enable, rotate_enable, rotate_angle; } IMPFSI2DAttr;
typedef struct {
    IMPFSI2DAttr i2dattr; int32_t picWidth, picHeight, pixFmt;
    IMPFSChnCrop crop; IMPFSChnScaler scaler;
    int32_t outFrmRateNum, outFrmRateDen, nrVBs, type;
    IMPFSChnCrop fcrop;
} IMPFSChnAttr;
typedef struct {
    int32_t index, pool_idx; uint32_t width, height, pixfmt, size;
    uint32_t phyAddr, virAddr; void *pool; int64_t timeStamp;
} IMPFrameInfo;
typedef struct {
    uint32_t phys_addr, virt_addr, length;
    uint64_t timestamp;
    uint32_t frame_type, slice_type, reserved[8];
} HWStreamBuffer;

struct api {
    int (*system_init)(void); int (*system_exit)(void);
    int (*isp_open)(void); int (*isp_close)(void);
    int (*add_sensor)(int, IMPSensorInfo *); int (*del_sensor)(int, IMPSensorInfo *);
    int (*enable_sensor)(int, IMPSensorInfo *); int (*disable_sensor)(int);
    int (*enable_tuning)(void); int (*disable_tuning)(void);
    int (*create_channel)(int, IMPFSChnAttr *); int (*destroy_channel)(int);
    int (*set_attr)(int, const IMPFSChnAttr *); int (*enable_channel)(int);
    int (*disable_channel)(int); int (*get_frame)(int, IMPFrameInfo **);
    int (*release_frame)(int, IMPFrameInfo *);
    int (*codec_defaults)(void *); int (*codec_create)(void **, void *);
    int (*codec_destroy)(void *); int (*codec_process)(void *, void *, void *);
    int (*codec_get_stream)(void *, void **, void **);
    int (*codec_release_stream)(void *, void *, void *);
};

static int load_api(void *handle, struct api *a)
{
#define LOAD(field, name) do { \
    *(void **)(&a->field) = dlsym(handle, name); \
    if (!a->field) { fprintf(stderr, "P2_DLSYM_FAIL %s: %s\n", name, dlerror()); return -1; } \
} while (0)
    memset(a, 0, sizeof(*a));
    LOAD(system_init, "IMP_System_Init"); LOAD(system_exit, "IMP_System_Exit");
    LOAD(isp_open, "IMP_ISP_Open"); LOAD(isp_close, "IMP_ISP_Close");
    LOAD(add_sensor, "IMP_ISP_AddSensor"); LOAD(del_sensor, "IMP_ISP_DelSensor");
    LOAD(enable_sensor, "IMP_ISP_EnableSensor"); LOAD(disable_sensor, "IMP_ISP_DisableSensor");
    LOAD(enable_tuning, "IMP_ISP_EnableTuning"); LOAD(disable_tuning, "IMP_ISP_DisableTuning");
    LOAD(create_channel, "IMP_FrameSource_CreateChn"); LOAD(destroy_channel, "IMP_FrameSource_DestroyChn");
    LOAD(set_attr, "IMP_FrameSource_SetChnAttr"); LOAD(enable_channel, "IMP_FrameSource_EnableChn");
    LOAD(disable_channel, "IMP_FrameSource_DisableChn"); LOAD(get_frame, "IMP_FrameSource_GetFrame");
    LOAD(release_frame, "IMP_FrameSource_ReleaseFrame");
    LOAD(codec_defaults, "AL_Codec_Encode_SetDefaultParam");
    LOAD(codec_create, "AL_Codec_Encode_Create"); LOAD(codec_destroy, "AL_Codec_Encode_Destroy");
    LOAD(codec_process, "AL_Codec_Encode_Process"); LOAD(codec_get_stream, "AL_Codec_Encode_GetStream");
    LOAD(codec_release_stream, "AL_Codec_Encode_ReleaseStream");
#undef LOAD
    return 0;
}

static void fill_attr(IMPFSChnAttr *a, int width, int height)
{
    memset(a, 0, sizeof(*a)); a->picWidth = width; a->picHeight = height;
    a->pixFmt = PIX_FMT_NV12; a->scaler.enable = 1;
    a->scaler.outwidth = width; a->scaler.outheight = height;
    a->outFrmRateNum = 30; a->outFrmRateDen = 1; a->nrVBs = 2;
}

static int has_oem_libimp(void)
{
    char line[512]; FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return -1;
    while (fgets(line, sizeof(line), f)) {
        char *p = strstr(line, "/libimp.so");
        if (p && (p[10] == '\n' || p[10] == ' ' || p[10] == '\0')) { fclose(f); return 1; }
    }
    fclose(f); return 0;
}

static unsigned count_start_codes(const unsigned char *p, uint32_t size)
{
    unsigned count = 0; uint32_t i;
    for (i = 0; i + 4 < size; i++)
        if (p[i] == 0 && p[i + 1] == 0 &&
            (p[i + 2] == 1 || (p[i + 2] == 0 && p[i + 3] == 1))) count++;
    return count;
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "/tmp/libimp-t40-p2-backend.so";
    int target_frames = argc > 2 ? atoi(argv[2]) : 120;
    const char *output_path = argc > 3 ? argv[3] : "/tmp/openimp-p2-sub.h264";
    struct api a; IMPSensorInfo sensor; IMPFSChnAttr attr[2];
    IMPFrameInfo *frame = NULL; unsigned char params[0x794];
    void *handle = NULL, *codec = NULL, *raw_stream = NULL, *user = NULL;
    FILE *bitstream = NULL;
    unsigned long total_bytes = 0, total_starts = 0;
    int created[2] = {0, 0}, enabled[2] = {0, 0}, result = 1, i, encoded = 0;

    if (target_frames <= 0 || target_frames > 10000) return 2;

    handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) { fprintf(stderr, "P2_DLOPEN_FAIL %s: %s\n", path, dlerror()); return 2; }
    if (load_api(handle, &a) < 0) return 2;
    if (has_oem_libimp() != 0) { fprintf(stderr, "P2_OEM_MAPPING_REJECTED\n"); return 2; }
    memset(&sensor, 0, sizeof(sensor)); strcpy(sensor.name, "gc4653");
    sensor.cbus_type = 1; strcpy(sensor.i2c.type, "gc4653");
    sensor.i2c.addr = 0x29; sensor.i2c.i2c_adapter_id = 1;
    sensor.rst_gpio = sensor.pwdn_gpio = sensor.power_gpio = -1;
    sensor.sensor_id = 0x4653; sensor.mclk = 1;
    fill_attr(&attr[0], 1920, 1080); fill_attr(&attr[1], 640, 360);

#define CHECK(call, label) do { int r; fprintf(stderr, "P2_BEGIN %s\n", label); fflush(stderr); r = (call); fprintf(stderr, "P2_END %s result=%d\n", label, r); fflush(stderr); if (r) goto cleanup; } while (0)
    CHECK(a.isp_open(), "ISP_Open"); CHECK(a.add_sensor(0, &sensor), "ISP_AddSensor");
    CHECK(a.enable_sensor(0, &sensor), "ISP_EnableSensor"); CHECK(a.system_init(), "System_Init");
    CHECK(a.enable_tuning(), "ISP_EnableTuning");
    for (i = 0; i < 2; i++) { CHECK(a.create_channel(i, &attr[i]), "FS_Create"); created[i] = 1; CHECK(a.set_attr(i, &attr[i]), "FS_SetAttr"); }
    for (i = 0; i < 2; i++) { CHECK(a.enable_channel(i), "FS_Enable"); enabled[i] = 1; }
    CHECK(a.codec_defaults(params), "Codec_Defaults");
    *(uint16_t *)(params + 0x08) = 640; *(uint16_t *)(params + 0x0a) = 360;
    *(uint16_t *)(params + 0x0c) = 640; *(uint16_t *)(params + 0x0e) = 360;
    *(uint32_t *)(params + 0x20) = 66; *(uint16_t *)(params + 0x78) = 30;
    *(uint16_t *)(params + 0x7a) = 1000; *(uint32_t *)(params + 0x7c) = 1200000;
    *(uint32_t *)(params + 0xb0) = 30; *(uint16_t *)(params + 0x84) = 30;
    *(uint8_t *)(params + 0x86) = 15; *(uint16_t *)(params + 0x88) = 45;
    CHECK(a.codec_create(&codec, params), "Codec_Create");
    bitstream = fopen(output_path, "wb");
    if (!bitstream) { fprintf(stderr, "P2_OUTPUT_OPEN_FAIL %s\n", output_path); goto cleanup; }
    for (encoded = 0; encoded < target_frames; encoded++) {
        CHECK(a.get_frame(1, &frame), "FS_GetFrame");
        CHECK(a.codec_process(codec, frame, frame), "Codec_Process");
        for (i = 0; i < 2000 && !raw_stream; i++) {
            if (a.codec_get_stream(codec, &raw_stream, &user) == 0) break;
            usleep(1000);
        }
        if (!raw_stream) { fprintf(stderr, "P2_NO_STREAM frame=%d\n", encoded); goto cleanup; }
        {
            HWStreamBuffer *s = raw_stream;
            unsigned char *bytes = (unsigned char *)(uintptr_t)s->virt_addr;
            unsigned starts = count_start_codes(bytes, s->length);
            if (encoded < 4 || (encoded % 30) == 29)
                fprintf(stderr, "P2_STREAM frame=%d phys=0x%08x virt=0x%08x len=%u type=%u starts=%u user_match=%d oem=%d\n",
                        encoded, s->phys_addr, s->virt_addr, s->length, s->frame_type,
                        starts, user == frame, has_oem_libimp());
            if (!s->virt_addr || s->length < 64 || starts < 3 || user != frame ||
                has_oem_libimp() != 0 || fwrite(bytes, 1, s->length, bitstream) != s->length)
                goto cleanup;
            total_bytes += s->length;
            total_starts += starts;
        }
        CHECK(a.codec_release_stream(codec, raw_stream, user), "Codec_ReleaseStream");
        raw_stream = NULL; user = NULL;
        CHECK(a.release_frame(1, frame), "FS_ReleaseFrame"); frame = NULL;
    }
    if (fflush(bitstream) != 0) goto cleanup;
    fprintf(stderr, "P2_CONTINUOUS frames=%d bytes=%lu starts=%lu output=%s oem=%d\n",
            encoded, total_bytes, total_starts, output_path, has_oem_libimp());
    result = 0;

cleanup:
    {
        int tuning_rc, sensor_rc, del_rc, isp_rc, system_rc;

    if (raw_stream && codec) a.codec_release_stream(codec, raw_stream, user);
    if (frame) a.release_frame(1, frame);
    if (bitstream) { fclose(bitstream); bitstream = NULL; }
    if (codec) { fprintf(stderr, "P2_CLEAN Codec_Destroy=%d\n", a.codec_destroy(codec)); codec = NULL; }
    for (i = 1; i >= 0; i--) { if (enabled[i]) fprintf(stderr, "P2_CLEAN FS_Disable[%d]=%d\n", i, a.disable_channel(i)); if (created[i]) fprintf(stderr, "P2_CLEAN FS_Destroy[%d]=%d\n", i, a.destroy_channel(i)); }
    tuning_rc = a.disable_tuning();
    sensor_rc = a.disable_sensor(0);
    del_rc = a.del_sensor(0, &sensor);
    isp_rc = a.isp_close();
    system_rc = a.system_exit();
    fprintf(stderr, "P2_CLEAN Tuning=%d Sensor=%d Del=%d ISP=%d System=%d\n",
            tuning_rc, sensor_rc, del_rc, isp_rc, system_rc);
    }
    if (!result) printf("OPENIMP_P2_BACKEND_PASS frames=%d bytes=%lu oem_mapped=0\n",
                        encoded, total_bytes);
    if (handle) dlclose(handle);
    return result;
#undef CHECK
}
