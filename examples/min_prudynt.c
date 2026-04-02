#define _POSIX_C_SOURCE 199309L
#include <sys/time.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>

#include "imp/imp_common.h"
#include "imp/imp_system.h"
#include "imp/imp_isp.h"
#include <stdarg.h>

/*
 * Stubs to satisfy stock libsysutils.so unresolved symbols.
 * Many vendor builds expect these to be provided by the main app/libimp.
 */
__attribute__((visibility("default")))
int IMP_Log_Get_Option(void) {
    /* 0 = default/no special options */
    return 0;
}

__attribute__((visibility("default")))
void imp_log_fun(int level, const char *tag, const char *fmt, ...) {
    (void)level;
    va_list ap;
    va_start(ap, fmt);
    if (tag && *tag) fprintf(stderr, "%s: ", tag);
    if (fmt) vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

#include "imp/imp_framesource.h"
#include "imp/imp_encoder.h"

#define CHK(expr, msg) do { \
    int __r = (expr); \
    if (__r < 0) { fprintf(stderr, "[E] %s failed (%d) at %s:%d\n", msg, __r, __FILE__, __LINE__); goto fail; } \
} while (0)

static int64_t monotonic_us(void) {
#if defined(CLOCK_MONOTONIC)
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
#else
    struct timeval tv; gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000LL + tv.tv_usec;
#endif
}

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s [width height fps bitrate_kbps duration_sec]\n", prog);
    fprintf(stderr, "Example: %s 1920 1080 25 2000 10\n", prog);
}

int main(int argc, char **argv) {
    int width = 1920, height = 1080, fps = 25, bitrate_kbps = 2000, duration_sec = 10;
    if (argc >= 6) {
        width = atoi(argv[1]); height = atoi(argv[2]); fps = atoi(argv[3]);
        bitrate_kbps = atoi(argv[4]); duration_sec = atoi(argv[5]);
        if (width <= 0 || height <= 0 || fps <= 0 || bitrate_kbps <= 0 || duration_sec <= 0) {
            usage(argv[0]); return 2;
        }
    } else if (argc != 1) {
        usage(argv[0]); return 2;
        }

    fprintf(stdout, "[I] min_prudynt: %dx%d@%dfps, bitrate=%dkbps, duration=%ds\n",
            width, height, fps, bitrate_kbps, duration_sec);

    // 1) ISP Open -> AddSensor -> EnableSensor
    CHK(IMP_ISP_Open(), "IMP_ISP_Open");

    IMPSensorInfo sensor; memset(&sensor, 0, sizeof(sensor));
    // Populate sensor info to satisfy tx-isp expected struct for TX_ISP_ADD_SENSOR
    strncpy(sensor.name, "gc2053", sizeof(sensor.name) - 1);
    sensor.cbus_type = TX_SENSOR_CONTROL_INTERFACE_I2C;
    strncpy(sensor.i2c.type, "gc2053", sizeof(sensor.i2c.type) - 1);
    sensor.i2c.addr = 0x37;           // from /proc/jz/sensor (gc2053)
    sensor.i2c.i2c_adapter = 0;       // default bus (matches streamer default)
    sensor.rst_gpio = 91;             // default used by streamer when /proc not present
    sensor.sensor_id = 0x2053;        // chip id (optional)
    CHK(IMP_ISP_AddSensor(&sensor), "IMP_ISP_AddSensor");
    CHK(IMP_ISP_EnableSensor(), "IMP_ISP_EnableSensor");

    // 2) System Init
    CHK(IMP_System_Init(), "IMP_System_Init");

    // 3) Enable ISP tuning and set prudynt-like defaults
    CHK(IMP_ISP_EnableTuning(), "IMP_ISP_EnableTuning");
    CHK(IMP_ISP_Tuning_SetISPRunningMode(IMPISP_RUNNING_MODE_DAY), "SetISPRunningMode");
    CHK(IMP_ISP_Tuning_SetSensorFPS((uint32_t)fps, 1), "SetSensorFPS");
    CHK(IMP_System_RebaseTimeStamp(monotonic_us()), "RebaseTS");

    // 4) FrameSource ch0 create+enable (NV12)
    int fsCh = 0;
    IMPFSChnAttr fs_attr; memset(&fs_attr, 0, sizeof(fs_attr));
    fs_attr.picWidth = width; fs_attr.picHeight = height; fs_attr.pixFmt = PIX_FMT_NV12;
    fs_attr.outFrmRateNum = fps; fs_attr.outFrmRateDen = 1;
    fs_attr.nrVBs = 2; fs_attr.type = FS_PHY_CHANNEL;
    fs_attr.crop.enable = 0; fs_attr.scaler.enable = 0;
    CHK(IMP_FrameSource_CreateChn(fsCh, &fs_attr), "FS_CreateChn");

    IMPFSChnFifoAttr fifo_attr = (IMPFSChnFifoAttr){0};
    fifo_attr.maxdepth = 0; fifo_attr.depth = 0;
    CHK(IMP_FrameSource_SetChnFifoAttr(fsCh, &fifo_attr), "FS_SetChnFifoAttr");
    CHK(IMP_FrameSource_SetFrameDepth(fsCh, 0), "FS_SetFrameDepth");

    // 5) Encoder group+channel (H264 baseline CBR)
    int encGrp = 0, encCh = 0;

    // Optional low-memory friendly settings before channel creation
    CHK(IMP_Encoder_SetMaxStreamCnt(encCh, 2), "ENC_SetMaxStreamCnt");
    CHK(IMP_Encoder_SetStreamBufSize(encCh, 256 * 1024), "ENC_SetStreamBufSize");

    IMPEncoderChnAttr chnAttr; memset(&chnAttr, 0, sizeof(chnAttr));
    CHK(IMP_Encoder_SetDefaultParam(&chnAttr,
        IMP_ENC_PROFILE_AVC_BASELINE,
        IMP_ENC_RC_MODE_CBR,
        (uint16_t)width, (uint16_t)height,
        (uint32_t)fps, 1,
        (uint32_t)fps,
        2,   // uMaxSameSenceCnt
        -1,  // iInitialQP
        (uint32_t)bitrate_kbps * 1000U),
        "ENC_SetDefaultParam");

    // Prepare CHNAttr for CreateChn (header uses CHNAttr there)
    IMPEncoderCHNAttr chnAttrForCreate; memset(&chnAttrForCreate, 0, sizeof(chnAttrForCreate));
    chnAttrForCreate.encAttr = chnAttr.encAttr;
    chnAttrForCreate.rcAttr = chnAttr.rcAttr;

    // Entropy for Baseline is CAVLC by default; safe to skip
    CHK(IMP_Encoder_CreateChn(encCh, &chnAttrForCreate), "ENC_CreateChn");
    CHK(IMP_Encoder_SetChnGopLength(encCh, fps), "ENC_SetChnGopLength");
    CHK(IMP_Encoder_RequestIDR(encCh), "ENC_RequestIDR");
    CHK(IMP_Encoder_CreateGroup(encGrp), "ENC_CreateGroup");
    CHK(IMP_Encoder_RegisterChn(encGrp, encCh), "ENC_RegisterChn");

    // 6) Bind FS->ENC
    IMPCell fs_cell = { DEV_ID_FS, fsCh, 0 };
    IMPCell enc_cell = { DEV_ID_ENC, encGrp, 0 };
    CHK(IMP_System_Bind(&fs_cell, &enc_cell), "System_Bind FS->ENC");

    // 7) Start streaming
    CHK(IMP_FrameSource_EnableChn(fsCh), "FS_EnableChn");
    CHK(IMP_Encoder_StartRecvPic(encCh), "ENC_StartRecvPic");

    // 8) Poll/Get/Write stream for duration
    const char *out_path = "/tmp/min_prudynt_out.h264";
    FILE *out = fopen(out_path, "wb");
    if (!out) { perror("fopen out"); goto fail; }

    fprintf(stdout, "[I] Writing H.264 Annex-B stream to %s for %d seconds...\n", out_path, duration_sec);
    int64_t t_end = monotonic_us() + (int64_t)duration_sec * 1000000LL;
    int frames = 0;

    while (monotonic_us() < t_end) {
        int ret = IMP_Encoder_PollingStream(encCh, 1000);
        if (ret < 0) {
            // timeout or error; continue to try
            continue;
        }
        IMPEncoderStream stream; memset(&stream, 0, sizeof(stream));
        if (IMP_Encoder_GetStream(encCh, &stream, true) < 0)
            continue;

        // Write all packs in this frame
        for (uint32_t i = 0; i < stream.packCount; i++) {
            IMPEncoderPack *pk = &stream.pack[i];
            // In OpenIMP implementation, pack[i].virAddr points to data
            if (pk->length > 0 && pk->virAddr != 0) {
                size_t n = fwrite((void*)(uintptr_t)pk->virAddr, 1, pk->length, out);
                if (n != pk->length) {
                    fprintf(stderr, "[W] Short write: %zu/%u\n", n, pk->length);
                }
            }
        }
        frames++;
        IMP_Encoder_ReleaseStream(encCh, &stream);
    }

    fprintf(stdout, "[I] Done. Wrote ~%d frames to %s\n", frames, out_path);
    fclose(out);

    // 9) Tear-down (reverse order)
    CHK(IMP_Encoder_StopRecvPic(encCh), "ENC_StopRecvPic");
    CHK(IMP_FrameSource_DisableChn(fsCh), "FS_DisableChn");

    CHK(IMP_System_UnBind(&fs_cell, &enc_cell), "System_UnBind");

    CHK(IMP_Encoder_UnRegisterChn(encCh), "ENC_UnRegisterChn");
    CHK(IMP_Encoder_DestroyChn(encCh), "ENC_DestroyChn");
    CHK(IMP_Encoder_DestroyGroup(encGrp), "ENC_DestroyGroup");

    CHK(IMP_FrameSource_DestroyChn(fsCh), "FS_DestroyChn");

    CHK(IMP_ISP_DisableTuning(), "ISP_DisableTuning");
    CHK(IMP_ISP_DisableSensor(), "ISP_DisableSensor");
    CHK(IMP_ISP_DelSensor(&sensor), "ISP_DelSensor");

    CHK(IMP_System_Exit(), "System_Exit");
    CHK(IMP_ISP_Close(), "ISP_Close");

    fprintf(stdout, "[I] min_prudynt: complete\n");
    return 0;

fail:
    fprintf(stderr, "[E] min_prudynt: aborted; attempting partial cleanup...\n");
    // Best-effort cleanup; ignore errors
    IMP_Encoder_StopRecvPic(0);
    IMP_FrameSource_DisableChn(0);
    IMP_System_Exit();
    IMP_ISP_DisableTuning();
    IMP_ISP_DisableSensor();
    IMP_ISP_Close();
    return 1;
}

