/* motiontest - IVS motion detection on the OPENIMP_TEST_PATTERN=3 picture (host side: mtana.py).
 *
 *   motiontest SENSOR I2C_ADDR W H SECONDS
 *
 * FrameSource channel 0 (scaled to MT_OUT=WxH, default 640x360, picture = output size) is bound to an
 * IVS move group with an 8x6 grid of ROIs (cell i = row*8+col).  Every IVS result is logged on stderr:
 *     [M] <ms> <hex mask of the cells with motion>      (CLOCK_MONOTONIC ms, as the hook's [TP] lines)
 * Env: MT_SENSE (0..4, default 2), MT_SKIP (skipFrameCnt, 5), MT_FPS (25), MT_OUT=WxH,
 *      ROITEST_SENSOR_EXT as roitest (T40).  OPENIMP_TEST_PATTERN=3 and the events file are the hook's. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <imp/imp_common.h>
#include <imp/imp_system.h>
#include <imp/imp_isp.h>
#include <imp/imp_framesource.h>
#include <imp/imp_ivs.h>
#include <imp/imp_ivs_move.h>
#if defined(PLATFORM_T40)
# define PLATFORM_T41 1
# define SOC_T40 1
#endif
__attribute__((weak)) int IMP_Log_Get_Option(void) { return 0; }
__attribute__((weak)) int imp_log_fun(int level, int option, int type, const char *tag, const char *file,
                                      int line, const char *func, const char *fmt, ...)
{ (void)level; (void)option; (void)type; (void)tag; (void)file; (void)line; (void)func; (void)fmt; return 0; }
#define CK(e, m) do { int r_ = (e); if (r_ < 0) { fprintf(stderr, "[E] %s -> %d\n", m, r_); exit(1); } } while (0)
#define GX 8
#define GY 6

static int64_t now_ms(void)
{ struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000; }

int main(int argc, char **argv)
{
    IMPSensorInfo sn; IMPFSChnAttr fa; IMP_IVS_MoveParam mp; IMPIVSInterface *intf;
    IMPCell fs = { DEV_ID_FS, 0, 0 }, ivs = { DEV_ID_IVS, 0, 0 };
    int W, H, secs, ow = 640, oh = 360, sense, i, results = 0;
    int64_t end;

    if (argc < 6) { fprintf(stderr, "usage: %s SENSOR ADDR W H SECONDS\n", argv[0]); return 2; }
    W = atoi(argv[3]); H = atoi(argv[4]); secs = atoi(argv[5]);
    if (getenv("MT_OUT")) sscanf(getenv("MT_OUT"), "%dx%d", &ow, &oh);
    sense = getenv("MT_SENSE") ? atoi(getenv("MT_SENSE")) : 2;
    setvbuf(stderr, NULL, _IOLBF, 0);

    memset(&sn, 0, sizeof(sn));
    strncpy(sn.name, argv[1], sizeof(sn.name) - 1);
    sn.cbus_type = TX_SENSOR_CONTROL_INTERFACE_I2C;
    strncpy(sn.i2c.type, argv[1], sizeof(sn.i2c.type) - 1);
    sn.i2c.addr = (int)strtol(argv[2], NULL, 0);
    sn.rst_gpio = sn.pwdn_gpio = sn.power_gpio = -1;
#ifdef SOC_T40
    {
        int bus = 1, rst = 91, vi = 0, mclk = 1, boot = 4;
        if (getenv("ROITEST_SENSOR_EXT")) sscanf(getenv("ROITEST_SENSOR_EXT"), "%i,%i,%i,%i,%i", &bus, &rst, &vi, &mclk, &boot);
        sn.i2c.i2c_adapter_id = bus; sn.rst_gpio = rst;
        sn.video_interface = vi ? IMPISP_SENSOR_VI_MIPI_CSI1 : IMPISP_SENSOR_VI_MIPI_CSI0;
        sn.mclk = mclk == 2 ? IMPISP_SENSOR_MCLK2 : IMPISP_SENSOR_MCLK1;
        sn.default_boot = boot;
    }
#endif
#ifdef PLATFORM_T41
    CK(IMP_ISP_Open(), "ISP_Open");
    CK(IMP_ISP_AddSensor(IMPVI_MAIN, &sn), "AddSensor");
    CK(IMP_ISP_EnableSensor(IMPVI_MAIN, &sn), "EnableSensor");
#else
    CK(IMP_ISP_Open(), "ISP_Open");
    CK(IMP_ISP_AddSensor(&sn), "AddSensor");
    CK(IMP_ISP_EnableSensor(), "EnableSensor");
#endif
    CK(IMP_System_Init(), "System_Init");
    CK(IMP_ISP_EnableTuning(), "EnableTuning");

    memset(&fa, 0, sizeof(fa));
    fa.picWidth = W; fa.picHeight = H; fa.pixFmt = PIX_FMT_NV12;
    fa.outFrmRateNum = getenv("MT_FPS") ? atoi(getenv("MT_FPS")) : 25; fa.outFrmRateDen = 1; fa.nrVBs = 3; fa.type = FS_PHY_CHANNEL;
    if (ow != W || oh != H) {
        fa.scaler.enable = 1; fa.scaler.outwidth = ow; fa.scaler.outheight = oh;
#if defined(PLATFORM_T41) || defined(PLATFORM_T31) || defined(PLATFORM_T23)
        fa.picWidth = ow; fa.picHeight = oh;
#endif
    }
    CK(IMP_FrameSource_CreateChn(0, &fa), "FS_CreateChn");

    memset(&mp, 0, sizeof(mp));
    mp.skipFrameCnt = getenv("MT_SKIP") ? atoi(getenv("MT_SKIP")) : 5;
    mp.frameInfo.width = ow; mp.frameInfo.height = oh;
    for (i = 0; i < GX * GY; i++) {
        int c = i % GX, r = i / GX;
        mp.sense[i] = sense;
        mp.roiRect[i].p0.x = c * ow / GX;       mp.roiRect[i].p0.y = r * oh / GY;
        mp.roiRect[i].p1.x = (c + 1) * ow / GX - 1; mp.roiRect[i].p1.y = (r + 1) * oh / GY - 1;
    }
    mp.roiRectCnt = GX * GY;
    CK(IMP_IVS_CreateGroup(0), "IVS_CreateGroup");
    intf = IMP_IVS_CreateMoveInterface(&mp);
    if (!intf) { fprintf(stderr, "[E] CreateMoveInterface\n"); return 1; }
    CK(IMP_IVS_CreateChn(0, intf), "IVS_CreateChn");
    CK(IMP_IVS_RegisterChn(0, 0), "IVS_RegisterChn");
    CK(IMP_System_Bind(&fs, &ivs), "Bind FS->IVS");
    CK(IMP_FrameSource_EnableChn(0), "FS_Enable");
    CK(IMP_IVS_StartRecvPic(0), "IVS_StartRecvPic");
    fprintf(stderr, "[T] motiontest %dx%d sense %d skip %d start %lld\n", ow, oh, sense, mp.skipFrameCnt, (long long)now_ms());

    end = now_ms() + 1000LL * secs;
    while (now_ms() < end) {
        IMP_IVS_MoveOutput *res = NULL;
        uint64_t mask = 0;
        if (IMP_IVS_PollingResult(0, 500) < 0) continue;
        if (IMP_IVS_GetResult(0, (void **)&res) < 0 || !res) continue;
        for (i = 0; i < GX * GY; i++) if (res->retRoi[i]) mask |= 1ULL << i;
        fprintf(stderr, "[M] %lld %llx\n", (long long)now_ms(), (unsigned long long)mask);
        results++;
        IMP_IVS_ReleaseResult(0, res);
    }
    fprintf(stderr, "[T] results %d\n", results);

    IMP_IVS_StopRecvPic(0);
    IMP_System_UnBind(&fs, &ivs);
    IMP_FrameSource_DisableChn(0);
    IMP_IVS_UnRegisterChn(0);
    IMP_IVS_DestroyChn(0);
    IMP_IVS_DestroyMoveInterface(intf);
    IMP_IVS_DestroyGroup(0);
    IMP_FrameSource_DestroyChn(0);
    IMP_System_Exit();
    IMP_ISP_DisableTuning();
#ifdef PLATFORM_T41
    IMP_ISP_DisableSensor(IMPVI_MAIN);
    IMP_ISP_DelSensor(IMPVI_MAIN, &sn);
#else
    IMP_ISP_DisableSensor();
    IMP_ISP_DelSensor(&sn);
#endif
    IMP_ISP_Close();
    return 0;
}
