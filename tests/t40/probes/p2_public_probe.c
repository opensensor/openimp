#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <imp/imp_common.h>
#include <imp/imp_encoder.h>

#define PIX_FMT_NV12 10

typedef struct { char type[20]; int32_t addr; int32_t i2c_adapter_id; } IMPI2CInfo;
typedef struct { char modalias[32]; int32_t bus_num; } IMPSPIInfo;
typedef struct {
    char name[32]; int32_t cbus_type;
    union { IMPI2CInfo i2c; IMPSPIInfo spi; };
    int32_t rst_gpio, pwdn_gpio, power_gpio;
    uint16_t sensor_id, reserved;
    int32_t video_interface, mclk, default_boot;
} P2SensorInfo;
typedef struct { int32_t enable, left, top, width, height; } IMPFSChnCrop;
typedef struct { int32_t enable, outwidth, outheight; } IMPFSChnScaler;
typedef struct { int32_t i2d_enable, flip_enable, mirr_enable, rotate_enable, rotate_angle; } IMPFSI2DAttr;
typedef struct {
    IMPFSI2DAttr i2dattr; int32_t picWidth, picHeight, pixFmt;
    IMPFSChnCrop crop; IMPFSChnScaler scaler;
    int32_t outFrmRateNum, outFrmRateDen, nrVBs, type;
    IMPFSChnCrop fcrop;
} IMPFSChnAttr;

struct api {
    int (*system_init)(void); int (*system_exit)(void);
    int (*isp_open)(void); int (*isp_close)(void);
    int (*add_sensor)(int, P2SensorInfo *); int (*del_sensor)(int, P2SensorInfo *);
    int (*enable_sensor)(int, P2SensorInfo *); int (*disable_sensor)(int);
    int (*enable_tuning)(void); int (*disable_tuning)(void);
    int (*fs_create)(int, IMPFSChnAttr *); int (*fs_destroy)(int);
    int (*fs_set_attr)(int, const IMPFSChnAttr *); int (*fs_enable)(int); int (*fs_disable)(int);
    int (*encoder_init)(void); int (*encoder_exit)(void);
    int (*create_group)(int); int (*destroy_group)(int);
    int (*defaults)(IMPEncoderChnAttr *, IMPEncoderProfile, IMPEncoderRcMode,
                    int, int, int, int, int, int, int, int);
    int (*create_channel)(int, IMPEncoderCHNAttr *); int (*destroy_channel)(int);
    int (*register_channel)(int, int); int (*unregister_channel)(int);
    int (*start)(int); int (*stop)(int); int (*poll)(int, uint32_t);
    int (*get_stream)(int, IMPEncoderStream *, int);
    int (*release_stream)(int, IMPEncoderStream *);
    int (*bind)(IMPCell *, IMPCell *); int (*unbind)(IMPCell *, IMPCell *);
};

typedef struct {
    struct api *api;
    int channel;
    int target;
    int jpeg;
    const char *output;
    int completed;
    unsigned long bytes;
    int failed;
} loop_ctx;

static int load_api(void *handle, struct api *a)
{
#define LOAD(field, name) do { \
    *(void **)(&a->field) = dlsym(handle, name); \
    if (!a->field) { fprintf(stderr, "P2_PUBLIC_DLSYM_FAIL %s: %s\n", name, dlerror()); return -1; } \
} while (0)
    memset(a, 0, sizeof(*a));
    LOAD(system_init, "IMP_System_Init"); LOAD(system_exit, "IMP_System_Exit");
    LOAD(isp_open, "IMP_ISP_Open"); LOAD(isp_close, "IMP_ISP_Close");
    LOAD(add_sensor, "IMP_ISP_AddSensor"); LOAD(del_sensor, "IMP_ISP_DelSensor");
    LOAD(enable_sensor, "IMP_ISP_EnableSensor"); LOAD(disable_sensor, "IMP_ISP_DisableSensor");
    LOAD(enable_tuning, "IMP_ISP_EnableTuning"); LOAD(disable_tuning, "IMP_ISP_DisableTuning");
    LOAD(fs_create, "IMP_FrameSource_CreateChn"); LOAD(fs_destroy, "IMP_FrameSource_DestroyChn");
    LOAD(fs_set_attr, "IMP_FrameSource_SetChnAttr"); LOAD(fs_enable, "IMP_FrameSource_EnableChn");
    LOAD(fs_disable, "IMP_FrameSource_DisableChn");
    LOAD(encoder_init, "EncoderInit"); LOAD(encoder_exit, "EncoderExit");
    LOAD(create_group, "IMP_Encoder_CreateGroup"); LOAD(destroy_group, "IMP_Encoder_DestroyGroup");
    LOAD(defaults, "IMP_Encoder_SetDefaultParam");
    LOAD(create_channel, "IMP_Encoder_CreateChn"); LOAD(destroy_channel, "IMP_Encoder_DestroyChn");
    LOAD(register_channel, "IMP_Encoder_RegisterChn"); LOAD(unregister_channel, "IMP_Encoder_UnRegisterChn");
    LOAD(start, "IMP_Encoder_StartRecvPic"); LOAD(stop, "IMP_Encoder_StopRecvPic");
    LOAD(poll, "IMP_Encoder_PollingStream"); LOAD(get_stream, "IMP_Encoder_GetStream");
    LOAD(release_stream, "IMP_Encoder_ReleaseStream");
    LOAD(bind, "IMP_System_Bind"); LOAD(unbind, "IMP_System_UnBind");
#undef LOAD
    return 0;
}

static void fill_fs_attr(IMPFSChnAttr *a, int width, int height)
{
    memset(a, 0, sizeof(*a));
    a->picWidth = width; a->picHeight = height; a->pixFmt = PIX_FMT_NV12;
    a->scaler.enable = 1; a->scaler.outwidth = width; a->scaler.outheight = height;
    a->outFrmRateNum = 30; a->outFrmRateDen = 1; a->nrVBs = 2;
}

static int has_oem_libimp(void)
{
    char line[512]; FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return -1;
    while (fgets(line, sizeof(line), f)) {
        char *p = strstr(line, "/libimp.so");
        if (p && (p[10] == '\n' || p[10] == ' ' || p[10] == '\0')) {
            fclose(f); return 1;
        }
    }
    fclose(f); return 0;
}

static unsigned start_codes(const unsigned char *p, uint32_t size)
{
    unsigned count = 0; uint32_t i;
    for (i = 0; i + 4 < size; i++)
        if (p[i] == 0 && p[i + 1] == 0 &&
            (p[i + 2] == 1 || (p[i + 2] == 0 && p[i + 3] == 1))) count++;
    return count;
}

static void *run_loop(void *opaque)
{
    loop_ctx *ctx = opaque;
    FILE *output = fopen(ctx->output, "wb");
    int i;

    if (!output) { ctx->failed = 1; return NULL; }
    for (i = 0; i < ctx->target; i++) {
        IMPEncoderStream stream;
        unsigned char *base;
        unsigned char *data;
        uint32_t length;

        if (ctx->api->poll(ctx->channel, 1000) != 0 ||
            ctx->api->get_stream(ctx->channel, &stream, 1) != 0 ||
            stream.packCount != 1 || !stream.pack || !stream.virAddr) {
            fprintf(stderr, "P2_PUBLIC_LOOP_FAIL chn=%d frame=%d stage=poll-get\n", ctx->channel, i);
            ctx->failed = 1; break;
        }
        base = (unsigned char *)(uintptr_t)stream.virAddr;
        data = base + stream.pack[0].offset;
        length = stream.pack[0].length;
        if (length < 4 ||
            (!ctx->jpeg && start_codes(data, length) < (i == 0 ? 3u : 1u)) ||
            (ctx->jpeg && (data[0] != 0xff || data[1] != 0xd8 ||
                           data[length - 2] != 0xff || data[length - 1] != 0xd9)) ||
            fwrite(data, 1, length, output) != length || has_oem_libimp() != 0) {
            fprintf(stderr, "P2_PUBLIC_LOOP_FAIL chn=%d frame=%d stage=validate len=%u\n",
                    ctx->channel, i, length);
            ctx->failed = 1;
            ctx->api->release_stream(ctx->channel, &stream);
            break;
        }
        ctx->bytes += length;
        if (ctx->api->release_stream(ctx->channel, &stream) != 0) {
            fprintf(stderr, "P2_PUBLIC_LOOP_FAIL chn=%d frame=%d stage=release\n", ctx->channel, i);
            ctx->failed = 1; break;
        }
        ctx->completed++;
        if (i < 2 || (i % 10) == 9)
            fprintf(stderr, "P2_PUBLIC_LOOP chn=%d frame=%d len=%u seq=%u jpeg=%d\n",
                    ctx->channel, i, length, stream.seq, ctx->jpeg);
    }
    fclose(output);
    return NULL;
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "/tmp/libimp-t40-p2-backend.so";
    int target = argc > 2 ? atoi(argv[2]) : 30;
    unsigned int loop_mask = argc > 3 ? (unsigned int)strtoul(argv[3], NULL, 0) : 0x0fu;
    struct api api; void *handle = NULL; P2SensorInfo sensor; IMPFSChnAttr fs[2];
    IMPEncoderCHNAttr attrs[4]; IMPCell sources[2], destinations[2];
    int channels[4] = {0, 1, 4, 5}; int groups[4] = {0, 1, 0, 1};
    const char *names[4] = {"/tmp/p2-main.h264", "/tmp/p2-sub.h264",
                            "/tmp/p2-main.jpgs", "/tmp/p2-sub.jpgs"};
    loop_ctx loops[4]; pthread_t threads[4];
    int fs_created[2] = {0, 0}, fs_enabled[2] = {0, 0};
    int group_created[2] = {0, 0}, ch_created[4] = {0, 0};
    int ch_registered[4] = {0, 0}, ch_started[4] = {0, 0};
    int bound[2] = {0, 0}; int result = 1; int i;
    int source_needed[2];
    int clean_encoder, clean_tuning, clean_sensor, clean_del;
    int clean_isp, clean_system, clean_oem;
    unsigned int hold_seconds = 0;
    int main_low = getenv("OPENIMP_P2_MAIN_LOW") != NULL;
    int output_stdout = getenv("OPENIMP_P2_OUTPUT_STDOUT") != NULL;

    if (target <= 0 || target > 1000 || !(loop_mask & 0x0fu)) return 2;
    if (output_stdout && loop_mask != 0x01u) return 2;
    if (output_stdout) names[0] = "/dev/stdout";
    source_needed[0] = (loop_mask & 0x05u) != 0;
    source_needed[1] = (loop_mask & 0x0au) != 0;
    if (getenv("OPENIMP_P2_HOLD_SECONDS")) {
        hold_seconds =
            (unsigned int)atoi(getenv("OPENIMP_P2_HOLD_SECONDS"));
        if (hold_seconds > 300U) return 2;
    }
    handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!handle || load_api(handle, &api) != 0 || has_oem_libimp() != 0) return 2;
    memset(&sensor, 0, sizeof(sensor)); strcpy(sensor.name, "gc4653");
    sensor.cbus_type = 1; strcpy(sensor.i2c.type, "gc4653");
    sensor.i2c.addr = 0x29; sensor.i2c.i2c_adapter_id = 1;
    sensor.rst_gpio = sensor.pwdn_gpio = sensor.power_gpio = -1;
    sensor.sensor_id = 0x4653; sensor.mclk = 1;
    fill_fs_attr(&fs[0], main_low ? 640 : 1920,
                 main_low ? 360 : 1080);
    fill_fs_attr(&fs[1], 640, 360);

#define CHECK(call, label) do { int rc = (call); fprintf(stderr, "P2_PUBLIC %s=%d\n", label, rc); if (rc) goto cleanup; } while (0)
    CHECK(api.isp_open(), "ISP_Open"); CHECK(api.add_sensor(0, &sensor), "ISP_AddSensor");
    CHECK(api.enable_sensor(0, &sensor), "ISP_EnableSensor"); CHECK(api.system_init(), "System_Init");
    CHECK(api.enable_tuning(), "ISP_EnableTuning"); CHECK(api.encoder_init(), "EncoderInit");
    for (i = 0; i < 2; i++) {
        if (!source_needed[i]) continue;
        CHECK(api.fs_create(i, &fs[i]), "FS_Create"); fs_created[i] = 1;
        CHECK(api.fs_set_attr(i, &fs[i]), "FS_SetAttr");
        CHECK(api.create_group(i), "CreateGroup"); group_created[i] = 1;
    }
    CHECK(api.defaults(&attrs[0], IMP_ENC_PROFILE_AVC_HIGH, IMP_ENC_RC_MODE_VBR,
                       main_low ? 640 : 1920, main_low ? 360 : 1080,
                       30, 1, 30, 2, 26, main_low ? 1000 : 3000),
          "DefaultsMain");
    CHECK(api.defaults(&attrs[1], IMP_ENC_PROFILE_AVC_HIGH, IMP_ENC_RC_MODE_VBR,
                       640, 360, 30, 1, 30, 2, 26, 1000), "DefaultsSub");
    CHECK(api.defaults(&attrs[2], IMP_ENC_PROFILE_JPEG, IMP_ENC_RC_MODE_FIXQP,
                       1920, 1080, 24, 1, 0, 0, 75, 0), "DefaultsJpegMain");
    CHECK(api.defaults(&attrs[3], IMP_ENC_PROFILE_JPEG, IMP_ENC_RC_MODE_FIXQP,
                       640, 360, 24, 1, 0, 0, 75, 0), "DefaultsJpegSub");
    for (i = 0; i < 4; i++) {
        if (!(loop_mask & (1u << i))) continue;
        CHECK(api.create_channel(channels[i], &attrs[i]), "CreateChn"); ch_created[i] = 1;
        CHECK(api.register_channel(groups[i], channels[i]), "RegisterChn"); ch_registered[i] = 1;
    }
    for (i = 0; i < 2; i++) {
        if (!source_needed[i]) continue;
        sources[i] = (IMPCell){DEV_ID_FS, i, 0};
        destinations[i] = (IMPCell){DEV_ID_ENC, i, 0};
        CHECK(api.bind(&sources[i], &destinations[i]), "Bind"); bound[i] = 1;
        CHECK(api.fs_enable(i), "FS_Enable"); fs_enabled[i] = 1;
    }
    for (i = 0; i < 4; i++) {
        if (!(loop_mask & (1u << i))) continue;
        CHECK(api.start(channels[i]), "StartRecvPic"); ch_started[i] = 1;
    }
    memset(loops, 0, sizeof(loops));
    for (i = 0; i < 4; i++) {
        if (!(loop_mask & (1u << i))) continue;
        loops[i].api = &api; loops[i].channel = channels[i]; loops[i].target = target;
        loops[i].jpeg = i >= 2; loops[i].output = names[i];
        if (pthread_create(&threads[i], NULL, run_loop, &loops[i]) != 0) goto cleanup;
    }
    for (i = 0; i < 4; i++)
        if (loop_mask & (1u << i)) pthread_join(threads[i], NULL);
    for (i = 0; i < 4; i++) {
        if (!(loop_mask & (1u << i))) continue;
        fprintf(stderr, "P2_PUBLIC_SUMMARY chn=%d frames=%d bytes=%lu failed=%d\n",
                channels[i], loops[i].completed, loops[i].bytes, loops[i].failed);
        if (loops[i].failed || loops[i].completed != target) goto cleanup;
    }
    result = 0;
    if (hold_seconds) {
        fprintf(stderr, "P2_PUBLIC_HOLD seconds=%u\n", hold_seconds);
        fflush(stderr);
        sleep(hold_seconds);
    }

cleanup:
    for (i = 3; i >= 0; i--) if (ch_started[i]) fprintf(stderr, "P2_PUBLIC_CLEAN Stop[%d]=%d\n", channels[i], api.stop(channels[i]));
    for (i = 1; i >= 0; i--) if (fs_enabled[i]) fprintf(stderr, "P2_PUBLIC_CLEAN FS_Disable[%d]=%d\n", i, api.fs_disable(i));
    for (i = 1; i >= 0; i--) if (bound[i]) fprintf(stderr, "P2_PUBLIC_CLEAN UnBind[%d]=%d\n", i, api.unbind(&sources[i], &destinations[i]));
    for (i = 3; i >= 0; i--) {
        if (ch_registered[i]) fprintf(stderr, "P2_PUBLIC_CLEAN UnRegister[%d]=%d\n", channels[i], api.unregister_channel(channels[i]));
        if (ch_created[i]) fprintf(stderr, "P2_PUBLIC_CLEAN DestroyChn[%d]=%d\n", channels[i], api.destroy_channel(channels[i]));
    }
    for (i = 1; i >= 0; i--) {
        if (group_created[i]) fprintf(stderr, "P2_PUBLIC_CLEAN DestroyGroup[%d]=%d\n", i, api.destroy_group(i));
        if (fs_created[i]) fprintf(stderr, "P2_PUBLIC_CLEAN FS_Destroy[%d]=%d\n", i, api.fs_destroy(i));
    }
    clean_encoder = api.encoder_exit();
    clean_tuning = api.disable_tuning();
    clean_sensor = api.disable_sensor(0);
    clean_del = api.del_sensor(0, &sensor);
    clean_isp = api.isp_close();
    clean_system = api.system_exit();
    clean_oem = has_oem_libimp();
    fprintf(stderr, "P2_PUBLIC_CLEAN Encoder=%d Tuning=%d Sensor=%d Del=%d ISP=%d System=%d OEM=%d\n",
            clean_encoder, clean_tuning, clean_sensor, clean_del,
            clean_isp, clean_system, clean_oem);
    if (!result) printf("OPENIMP_P2_PUBLIC_PASS mask=0x%x frames_each=%d oem_mapped=0\n",
                        loop_mask & 0x0fu, target);
    if (handle) dlclose(handle);
    return result;
#undef CHECK
}
