/* bstest - T41 IMP_Encoder_SetAvpuBsShare / SetMaxStreamCnt rmem + picture test.
 *
 *   bstest SENSOR I2C_ADDR W H OUTDIR
 * env: BS_SHARE=0|1 (SetAvpuBsShare before CreateChn), BS_SIZE=bytes (0 = default),
 *      BS_CNT=N (SetMaxStreamCnt on both channels), BS_MAIN=1920x1080, BS_SUB=640x360,
 *      BS_SECS=10, BS_FPS=15, BS_KBPS=2000, BS_JPEG=1 (extra JPEG channel 2 sharing ch0, SetbufshareChn)
 * Creates two H.264 CBR channels (main, sub), runs BS_SECS seconds, writes OUTDIR/chN.h264 and
 * prints [M] lines: MemFree and the libimp allocation dump totals after each step.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include <dlfcn.h>
#include <imp/imp_common.h>
#include <imp/imp_system.h>
#include <imp/imp_isp.h>
#include <imp/imp_framesource.h>
#include <imp/imp_encoder.h>

__attribute__((weak)) int IMP_Log_Get_Option(void) { return 0; }
__attribute__((weak)) int imp_log_fun(int level, int option, int type, const char *tag, const char *file,
                                      int line, const char *func, const char *fmt, ...)
{
    va_list ap;
    (void)level; (void)option; (void)type; (void)tag; (void)file; (void)line; (void)func;
    if (!fmt || !getenv("BS_LOG")) return 0;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    return 0;
}

#define CK(e, m) do { int r_ = (e); if (r_ < 0) { printf("[E] %s -> 0x%08x\n", m, (unsigned)r_); exit(1); } } while (0)

static char outdir[256];

static int64_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static long memfree_kb(void)
{
    FILE *f = fopen("/proc/meminfo", "r");
    char l[128];
    long v = -1;
    while (f && fgets(l, sizeof(l), f))
        if (sscanf(l, "MemFree: %ld kB", &v) == 1) break;
    if (f) fclose(f);
    return v;
}

typedef int (*rmem_usage_fn)(uint32_t *, uint32_t *, uint32_t *, const char *, uint32_t *, uint32_t *);
typedef int (*dump_fn)(const char *);

static void mem(const char *tag)
{
    char p[300], l[256];
    FILE *f;
    long tot = -1, strm = 0, strmn = 0, used = -1;

    rmem_usage_fn OpenIMP_Rmem_Usage = (rmem_usage_fn)dlsym(RTLD_DEFAULT, "OpenIMP_P2_DMAUsage");
    dump_fn IMP_Alloc_Dump_To_File = (dump_fn)dlsym(RTLD_DEFAULT, "IMP_Alloc_Dump_To_File");

    if (OpenIMP_Rmem_Usage) {
        uint32_t u = 0, t = 0, g = 0, tb = 0, tc = 0;
        if (OpenIMP_Rmem_Usage(&u, &t, &g, "AVPU_STRM", &tb, &tc) == 0) {
            used = (long)u; strm = (long)tb; strmn = tc;
        }
        printf("[M] %-12s MemFree=%ld kB  rmem_used=%ld  stream_bufs=%ld (%ld B)\n", tag, memfree_kb(), used, strmn, strm);
        return;
    }
    if (IMP_Alloc_Dump_To_File) IMP_Alloc_Dump_To_File("/tmp/continuous_mem_info");
    f = fopen("/tmp/continuous_mem_info", "r");
    while (f && fgets(l, sizeof(l), f)) {
        long t;
        if (sscanf(l, "Mem Used  Size: %ld", &t) == 1) used = t;
    }
    if (f) fclose(f);
    (void)tot; (void)strm; (void)strmn; (void)p;
    printf("[M] %-12s MemFree=%ld kB  rmem_used=%ld (vendor dump)\n", tag, memfree_kb(), used);
    snprintf(p, sizeof(p), "cp /tmp/alloc_manager_info %.100s/alloc-%.40s.txt 2>/dev/null; cp /tmp/continuous_mem_info %.100s/cont-%.40s.txt 2>/dev/null", outdir, tag, outdir, tag);
    (void)system(p);
}

static void parse_wh(const char *s, int *w, int *h)
{
    sscanf(s, "%dx%d", w, h);
}

static int fps0 = 15, kbps = 2000;

static void mkch(int c, int w, int h, int type)
{
    IMPFSChnAttr fa;
    IMPEncoderChnAttr a;
    IMPCell fs = { DEV_ID_FS, 0, c }, enc = { DEV_ID_ENC, c, 0 };
    char *e;
    int r;

    memset(&fa, 0, sizeof(fa));
    fa.picWidth = w; fa.picHeight = h; fa.pixFmt = PIX_FMT_NV12;
    fa.outFrmRateNum = fps0; fa.outFrmRateDen = 1; fa.nrVBs = 2; fa.type = FS_PHY_CHANNEL;
    fa.scaler.enable = 1; fa.scaler.outwidth = w; fa.scaler.outheight = h;
    CK(IMP_FrameSource_CreateChn(c, &fa), "FS_CreateChn");
    memset(&a, 0, sizeof(a));
    r = IMP_Encoder_SetDefaultParam(&a, type == 1 ? IMP_ENC_PROFILE_JPEG : IMP_ENC_PROFILE_AVC_HIGH,
                                    type == 1 ? IMP_ENC_RC_MODE_FIXQP : IMP_ENC_RC_MODE_CBR, w, h, fps0, 1,
                                    fps0 * 2, 2, -1, type == 1 ? 0 : (uint32_t)kbps);
    CK(r, "SetDefaultParam");
    if ((e = getenv("BS_CNT")) && type == 0)
        printf("[OP] ch%d SetMaxStreamCnt(%s) -> 0x%08x\n", c, e, (unsigned)IMP_Encoder_SetMaxStreamCnt(c, atoi(e)));
    if (type == 1 && getenv("BS_JPEG"))
        printf("[OP] ch%d SetbufshareChn(%d,0) -> 0x%08x\n", c, c, (unsigned)IMP_Encoder_SetbufshareChn(c, 0));
    r = IMP_Encoder_CreateChn(c, &a);
    printf("[OP] CreateChn %d (%dx%d type %d) -> 0x%08x\n", c, w, h, type, (unsigned)r);
    if (r) exit(1);
    CK(IMP_Encoder_CreateGroup(c), "CreateGroup");
    CK(IMP_Encoder_RegisterChn(c, c), "RegisterChn");
    CK(IMP_System_Bind(&fs, &enc), "Bind");
    CK(IMP_FrameSource_EnableChn(c), "FS_Enable");
    CK(IMP_Encoder_StartRecvPic(c), "StartRecvPic");
}

int main(int argc, char **argv)
{
    IMPSensorInfo sn;
    int mw = 1920, mh = 1080, sw = 640, sh = 360, secs = 10, c;
    FILE *f[2];
    long bytes[2] = { 0, 0 }, frames[2] = { 0, 0 };
    unsigned char *buf = malloc(4 << 20);
    int64_t end;
    char p[400];

    if (argc < 6) { fprintf(stderr, "usage: %s SENSOR ADDR W H OUTDIR\n", argv[0]); return 2; }
    snprintf(outdir, sizeof(outdir), "%s", argv[5]);
    fprintf(stderr, "bstest: start\n");
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (getenv("BS_MAIN")) parse_wh(getenv("BS_MAIN"), &mw, &mh);
    if (getenv("BS_SUB")) parse_wh(getenv("BS_SUB"), &sw, &sh);
    if (getenv("BS_SECS")) secs = atoi(getenv("BS_SECS"));
    if (getenv("BS_FPS")) fps0 = atoi(getenv("BS_FPS"));
    if (getenv("BS_KBPS")) kbps = atoi(getenv("BS_KBPS"));
    memset(&sn, 0, sizeof(sn));
    strncpy(sn.name, argv[1], sizeof(sn.name) - 1);
    sn.cbus_type = TX_SENSOR_CONTROL_INTERFACE_I2C;
    strncpy(sn.i2c.type, argv[1], sizeof(sn.i2c.type) - 1);
    sn.i2c.addr = (int)strtol(argv[2], NULL, 0);
    sn.rst_gpio = sn.pwdn_gpio = sn.power_gpio = -1;
    CK(IMP_ISP_Open(), "ISP_Open");
    CK(IMP_ISP_AddSensor(IMPVI_MAIN, &sn), "AddSensor");
    CK(IMP_ISP_EnableSensor(IMPVI_MAIN, &sn), "EnableSensor");
    CK(IMP_System_Init(), "System_Init");
    CK(IMP_ISP_EnableTuning(), "EnableTuning");
    mem("start");
    if (getenv("BS_SHARE") && atoi(getenv("BS_SHARE"))) {
        int r = IMP_Encoder_SetAvpuBsShare(true, getenv("BS_SIZE") ? (uint32_t)strtoul(getenv("BS_SIZE"), 0, 0) : 0);
        printf("[OP] SetAvpuBsShare(1,%s) -> 0x%08x\n", getenv("BS_SIZE") ? getenv("BS_SIZE") : "0", (unsigned)r);
        mem("share-on");
    }
    mkch(0, mw, mh, 0);
    mem("main");
    mkch(1, sw, sh, 0);
    mem("main+sub");
    if (getenv("BS_JPEG")) { mkch(2, sw, sh, 1); mem("+jpeg"); }
    for (c = 0; c < 2; c++) {
        snprintf(p, sizeof(p), "%s/ch%d.h264", outdir, c);
        f[c] = fopen(p, "wb");
    }
    end = now_ms() + 1000L * secs;
    while (now_ms() < end) {
        for (c = 0; c < 2; c++) {
            IMPEncoderStream s;
            size_t len = 0;
            uint32_t k;
            if (IMP_Encoder_PollingStream(c, c ? 5 : 100) != 0) continue;
            memset(&s, 0, sizeof(s));
            if (IMP_Encoder_GetStream(c, &s, true) != 0) continue;
            for (k = 0; k < s.packCount; k++) {
                const IMPEncoderPack *pk = &s.pack[k];
                const unsigned char *base = (const unsigned char *)(uintptr_t)s.virAddr;
                size_t rem = s.streamSize > pk->offset ? s.streamSize - pk->offset : 0;
                if (!pk->length || len + pk->length > (4u << 20)) continue;
                if (rem && rem < pk->length) {
                    memcpy(buf + len, base + pk->offset, rem);
                    memcpy(buf + len + rem, base, pk->length - rem);
                } else
                    memcpy(buf + len, base + pk->offset, pk->length);
                len += pk->length;
            }
            if (frames[c] < 4)
                printf("[S] ch%d f%ld virAddr=0x%08x phyAddr=0x%08x size=%u packs=%u\n", c, frames[c], (unsigned)s.virAddr, (unsigned)s.phyAddr, (unsigned)s.streamSize, s.packCount);
            IMP_Encoder_ReleaseStream(c, &s);
            fwrite(buf, 1, len, f[c]);
            bytes[c] += (long)len; frames[c]++;
        }
    }
    for (c = 0; c < 2; c++) {
        fclose(f[c]);
        printf("[R] ch%d frames=%ld bytes=%ld kbps=%.0f\n", c, frames[c], bytes[c], bytes[c] * 8.0 / (secs * 1000.0));
    }
    mem("running");
    for (c = 2; c >= 0; c--) {
        IMPCell fs = { DEV_ID_FS, 0, c }, enc = { DEV_ID_ENC, c, 0 };
        if (c == 2 && !getenv("BS_JPEG")) continue;
        IMP_Encoder_StopRecvPic(c);
        IMP_FrameSource_DisableChn(c);
        IMP_System_UnBind(&fs, &enc);
        IMP_Encoder_UnRegisterChn(c);
        IMP_Encoder_DestroyChn(c);
        IMP_Encoder_DestroyGroup(c);
        IMP_FrameSource_DestroyChn(c);
    }
    mem("destroyed");
    if (getenv("BS_SHARE") && atoi(getenv("BS_SHARE")))
        printf("[OP] SetAvpuBsShare(0,0) -> 0x%08x\n", (unsigned)IMP_Encoder_SetAvpuBsShare(false, 0));
    mem("share-off");
    IMP_System_Exit();
    IMP_ISP_DisableTuning();
    IMP_ISP_DisableSensor(IMPVI_MAIN);
    IMP_ISP_DelSensor(IMPVI_MAIN, &sn);
    IMP_ISP_Close();
    return 0;
}
