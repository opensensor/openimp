/* rawcheck - dump uncompressed FrameSource frames (NV12) for the host layout check (rawana).
 *
 *   rawcheck SENSOR I2C_ADDR W H > frames.raw
 *
 * No encoder: ISP + FrameSource only. Per frame the stream on stdout carries a text line
 *   RAW ch=<c> kind=<get|snap> k=<n> w=<w> h=<h> pixfmt=<f> size=<bytes> phy=<hex> vir=<hex>\n
 * followed by <bytes> bytes: the whole buffer of IMP_FrameSource_GetFrame (info->size, incl.
 * padding), or w*h*3/2 packed bytes for IMP_FrameSource_SnapFrame. Everything else goes to
 * stderr (the camera's /tmp is RAM: never write the frames there).
 * Env: RAWCHECK_N frames per channel (3), RAWCHECK_CH1=WxH adds channel 1 (scaler output),
 *      RAWCHECK_OUT=WxH scales channel 0, RAWCHECK_FPS (15), RAWCHECK_SNAP=0 skips SnapFrame,
 *      ROITEST_SENSOR_EXT=bus,rst,vi,mclk,boot (T40 sensor description, as roitest).
 * The sensor test pattern is switched on from the shell (SmartSens 0x4501 bit 3).        */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <imp/imp_common.h>
#include <imp/imp_system.h>
#include <imp/imp_isp.h>
#include <imp/imp_framesource.h>
#if defined(PLATFORM_T40)
# define PLATFORM_T41 1
# define SOC_T40 1
#endif

__attribute__((weak)) int IMP_Log_Get_Option(void) { return 0; }
__attribute__((weak)) int imp_log_fun(int level, int option, int type, const char *tag, const char *file,
                                      int line, const char *func, const char *fmt, ...)
{
    (void)level; (void)option; (void)type; (void)tag; (void)file; (void)line; (void)func; (void)fmt;
    return 0;
}
#define CK(e, m) do { int r_ = (e); if (r_ < 0) { fprintf(stderr, "[E] %s -> %d\n", m, r_); exit(1); } } while (0)

/* the first 0x20 bytes of IMPFrameInfo are the same on every SoC */
struct fv { int index, pool_idx; uint32_t width, height, pixfmt, size, phyAddr, virAddr; };

static FILE *out;

/* GetFrame takes void ** on T2x/T31 and IMPFrameInfo ** on T4x headers */
static int get_frame(int ch, void *pp)
{
    int (*fn)(int, void *);
    void *p = (void *)IMP_FrameSource_GetFrame;
    memcpy(&fn, &p, sizeof(fn));
    return fn(ch, pp);
}

static int rel_frame(int ch, void *f)
{
    int (*fn)(int, void *);
    void *p = (void *)IMP_FrameSource_ReleaseFrame;
    memcpy(&fn, &p, sizeof(fn));
    return fn(ch, f);
}

static void emit(int ch, const char *kind, int k, const struct fv *f, const void *data, size_t n, int w, int h)
{
    fprintf(out, "RAW ch=%d kind=%s k=%d w=%d h=%d pixfmt=%u size=%zu phy=%x vir=%x\n", ch, kind, k, w, h,
            (unsigned)f->pixfmt, n, (unsigned)f->phyAddr, (unsigned)f->virAddr);
    fwrite(data, 1, n, out);
    fflush(out);
}

static void grab(int ch, int w, int h, int n, int snap)
{
    int k;

    IMP_FrameSource_SetFrameDepth(ch, 2);
    for (k = 0; k < n; k++) {
        struct fv *fr = NULL;
        if (get_frame(ch, &fr) < 0 || !fr) {
            fprintf(stderr, "[E] GetFrame ch%d -> none\n", ch);
            break;
        }
        fprintf(stderr, "[I] ch%d get %d: %ux%u pixfmt %u size %u phy %x\n", ch, k, fr->width, fr->height,
                (unsigned)fr->pixfmt, fr->size, (unsigned)fr->phyAddr);
        emit(ch, "get", k, fr, (const void *)(uintptr_t)fr->virAddr, fr->size, w, h);
        rel_frame(ch, fr);
        usleep(200000);
    }
    if (snap) {
        size_t sz = (size_t)w * h * 3 / 2;
        unsigned char *buf = calloc(1, sz + 4096);
        struct fv info[16];   /* room for any SoC's IMPFrameInfo */
        memset(info, 0, sizeof(info));
        if (buf && IMP_FrameSource_SnapFrame(ch, PIX_FMT_NV12, w, h, buf, (IMPFrameInfo *)info) == 0)
            emit(ch, "snap", 0, info, buf, sz, w, h);
        else
            fprintf(stderr, "[E] SnapFrame ch%d failed\n", ch);
        free(buf);
    }
}

int main(int argc, char **argv)
{
    IMPSensorInfo sn;
    IMPFSChnAttr fa;
    int W, H, fps, n, ch1_w = 0, ch1_h = 0, snap;

    if (argc < 5) { fprintf(stderr, "usage: %s SENSOR ADDR W H > frames.raw\n", argv[0]); return 2; }
    W = atoi(argv[3]); H = atoi(argv[4]);
    out = fdopen(dup(1), "wb");
    dup2(2, 1);
    fps = getenv("RAWCHECK_FPS") ? atoi(getenv("RAWCHECK_FPS")) : 15;
    n = getenv("RAWCHECK_N") ? atoi(getenv("RAWCHECK_N")) : 3;
    snap = !getenv("RAWCHECK_SNAP") || atoi(getenv("RAWCHECK_SNAP"));

    memset(&sn, 0, sizeof(sn));
    strncpy(sn.name, argv[1], sizeof(sn.name) - 1);
    sn.cbus_type = TX_SENSOR_CONTROL_INTERFACE_I2C;
    strncpy(sn.i2c.type, argv[1], sizeof(sn.i2c.type) - 1);
    sn.i2c.addr = (int)strtol(argv[2], NULL, 0);
    sn.rst_gpio = sn.pwdn_gpio = sn.power_gpio = -1;
#ifdef SOC_T40
    {
        int bus = 1, rst = 91, vi = 0, mclk = 1, boot = 4;
        if (getenv("ROITEST_SENSOR_EXT"))
            sscanf(getenv("ROITEST_SENSOR_EXT"), "%i,%i,%i,%i,%i", &bus, &rst, &vi, &mclk, &boot);
        sn.i2c.i2c_adapter_id = bus;
        sn.rst_gpio = rst;
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
    fa.outFrmRateNum = fps; fa.outFrmRateDen = 1; fa.nrVBs = 3; fa.type = FS_PHY_CHANNEL;
    if (getenv("RAWCHECK_OUT")) {
        int ow = 0, oh = 0;
        sscanf(getenv("RAWCHECK_OUT"), "%dx%d", &ow, &oh);
        if (ow > 0 && oh > 0) {
            fa.scaler.enable = 1; fa.scaler.outwidth = ow; fa.scaler.outheight = oh;
#if defined(PLATFORM_T41) || defined(PLATFORM_T31)
            fa.picWidth = ow; fa.picHeight = oh;
#endif
            W = ow; H = oh;
        }
    }
    CK(IMP_FrameSource_CreateChn(0, &fa), "FS_CreateChn");
    if (getenv("RAWCHECK_CH1") && sscanf(getenv("RAWCHECK_CH1"), "%dx%d", &ch1_w, &ch1_h) == 2) {
        IMPFSChnAttr f1 = fa;
        f1.picWidth = ch1_w; f1.picHeight = ch1_h;
        f1.scaler.enable = 1; f1.scaler.outwidth = ch1_w; f1.scaler.outheight = ch1_h;
        f1.crop.enable = 0;
#ifdef PLATFORM_T41
        memset(&f1.i2dattr, 0, sizeof(f1.i2dattr));
#endif
        CK(IMP_FrameSource_CreateChn(1, &f1), "FS_CreateChn1");
    }
    CK(IMP_FrameSource_EnableChn(0), "FS_Enable");
    if (ch1_w) CK(IMP_FrameSource_EnableChn(1), "FS_Enable1");
    sleep(3);   /* AE/AWB settle, sensor pattern is switched on meanwhile */
    grab(0, W, H, n, snap);
    if (ch1_w) grab(1, ch1_w, ch1_h, n, snap);

    if (ch1_w) { IMP_FrameSource_DisableChn(1); IMP_FrameSource_DestroyChn(1); }
    IMP_FrameSource_DisableChn(0);
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
