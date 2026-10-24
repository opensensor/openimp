/* telemtest - IMP_Encoder_Query / telemetry on-device test (T41).
 *   telemtest SENSOR ADDR W H [secs]
 * Phase A: consumer thread drains (PollingStream/GetStream/ReleaseStream), a
 *          sampler thread calls IMP_Encoder_Query every 10 ms; histogram.
 * Phase B: consumer stalled 1.5 s, then Query; consumer resumes.
 * Phase C: Query between GetStream and ReleaseStream (stream held).
 * Phase D: invalid / unregistered cases, EvalInfo, AveBitrate (weak).
 * Stop the streamer first. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <stdarg.h>
#include <imp/imp_common.h>
#include <imp/imp_system.h>
#include <imp/imp_isp.h>
#include <imp/imp_framesource.h>
#include <imp/imp_encoder.h>
#include <openimp/openimp_encoder_telemetry.h>

__attribute__((weak)) int IMP_Log_Get_Option(void) { return 0; }
__attribute__((weak)) int imp_log_fun(int level, int option, int type, const char *tag, const char *file,
                                      int line, const char *func, const char *fmt, ...)
{ (void)level;(void)option;(void)type;(void)tag;(void)file;(void)line;(void)func;(void)fmt; return 0; }
int IMP_Encoder_GetChnEvalInfo(int encChn, void *info) __attribute__((weak));
int OpenIMP_Encoder_GetTelemetry(int encChn, OpenIMPEncoderTelemetry *t) __attribute__((weak));
int IMP_Encoder_GetChnAveBitrate(int encChn, IMPEncoderStream *stream, int frames, int *bitrate) __attribute__((weak));

#define CK(e, m) do { int r_ = (e); if (r_ < 0) { printf("[E] %s -> %d\n", m, r_); exit(1); } } while (0)
static volatile int run = 1, stall = 0;
static volatile unsigned frames, bytes;
static int64_t now_ms(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000; }

static void *consumer(void *a)
{
    (void)a;
    while (run) {
        IMPEncoderStream s; uint32_t k, n = 0;
        if (stall) { usleep(5000); continue; }
        if (IMP_Encoder_PollingStream(0, 200) != 0) continue;
        memset(&s, 0, sizeof s);
        if (IMP_Encoder_GetStream(0, &s, true) != 0) continue;
        for (k = 0; k < s.packCount; k++) n += s.pack[k].length;
        bytes += n; frames++;
        IMP_Encoder_ReleaseStream(0, &s);
    }
    return NULL;
}

static unsigned hist_lp[8], hist_sf[8], hist_wd[2], hist_pk[8], nz_bytes, nsamp, max_bytes;
static void *sampler(void *a)
{
    (void)a;
    while (run) {
        IMPEncoderChnStat q; memset(&q, 0xff, sizeof q);
        if (IMP_Encoder_Query(0, &q) == 0) {
            nsamp++;
            hist_lp[q.leftPics > 7 ? 7 : q.leftPics]++;
            hist_sf[q.leftStreamFrames > 7 ? 7 : q.leftStreamFrames]++;
            hist_pk[q.curPacks > 7 ? 7 : q.curPacks]++;
            hist_wd[q.work_done ? 1 : 0]++;
            if (q.leftStreamBytes) { nz_bytes++; if (q.leftStreamBytes > max_bytes) max_bytes = q.leftStreamBytes; }
        }
        usleep(10000);
    }
    return NULL;
}
static void pq(const char *lab)
{
    IMPEncoderChnStat q; memset(&q, 0xff, sizeof q);
    int r = IMP_Encoder_Query(0, &q);
    printf("[Q] %s rc=%d reg=%d pics=%u bytes=%u frames=%u packs=%u done=%u\n", lab, r, q.registered, q.leftPics, q.leftStreamBytes, q.leftStreamFrames, q.curPacks, q.work_done);
}
static void ph(const char *n, unsigned *h, int c){ int i; printf("[H] %s:", n); for(i=0;i<c;i++) printf(" %u", h[i]); printf("\n"); }

int main(int argc, char **argv)
{
    int W, H, secs = argc > 5 ? atoi(argv[5]) : 4;
    IMPSensorInfo sn; IMPFSChnAttr fa; IMPEncoderChnAttr a;
    IMPCell fs = { DEV_ID_FS, 0, 0 }, enc = { DEV_ID_ENC, 0, 0 };
    pthread_t tc, ts; int64_t t0; unsigned f0, b0;
    if (argc < 5) return 2;
    W = atoi(argv[3]); H = atoi(argv[4]);
    setvbuf(stdout, NULL, _IOLBF, 0);
    memset(&sn, 0, sizeof sn);
    strncpy(sn.name, argv[1], sizeof sn.name - 1);
    sn.cbus_type = TX_SENSOR_CONTROL_INTERFACE_I2C;
    strncpy(sn.i2c.type, argv[1], sizeof sn.i2c.type - 1);
    sn.i2c.addr = (int)strtol(argv[2], NULL, 0);
    sn.rst_gpio = sn.pwdn_gpio = sn.power_gpio = -1;
    CK(IMP_ISP_Open(), "ISP_Open");
    CK(IMP_ISP_AddSensor(IMPVI_MAIN, &sn), "AddSensor");
    CK(IMP_ISP_EnableSensor(IMPVI_MAIN, &sn), "EnableSensor");
    CK(IMP_System_Init(), "System_Init");
    CK(IMP_ISP_EnableTuning(), "EnableTuning");
    memset(&fa, 0, sizeof fa);
    fa.picWidth = W; fa.picHeight = H; fa.pixFmt = PIX_FMT_NV12;
    fa.outFrmRateNum = 15; fa.outFrmRateDen = 1; fa.nrVBs = 2; fa.type = FS_PHY_CHANNEL;
    CK(IMP_FrameSource_CreateChn(0, &fa), "FS_CreateChn");
    CK(IMP_FrameSource_EnableChn(0), "FS_Enable");
    sleep(1);
    printf("[Q] invalid: ch-1 rc=%d, ch0 before create rc=%d\n", IMP_Encoder_Query(-1, (void *)&a), IMP_Encoder_Query(0, (void *)&a));
    memset(&a, 0, sizeof a);
    CK(IMP_Encoder_SetDefaultParam(&a, IMP_ENC_PROFILE_AVC_HIGH, IMP_ENC_RC_MODE_CBR, W, H, 15, 1, 30, 2, -1, 2000), "SetDefaultParam");
    CK(IMP_Encoder_CreateChn(0, &a), "CreateChn");
    pq("created, not registered");
    CK(IMP_Encoder_CreateGroup(0), "CreateGroup");
    CK(IMP_Encoder_RegisterChn(0, 0), "RegisterChn");
    CK(IMP_System_Bind(&fs, &enc), "Bind");
    pq("registered, not started");
    CK(IMP_Encoder_StartRecvPic(0), "StartRecvPic");
    pthread_create(&tc, NULL, consumer, NULL);
    t0 = now_ms(); f0 = frames; b0 = bytes;
    pthread_create(&ts, NULL, sampler, NULL);
    sleep(secs);
    printf("[A] %d s: %u frames, %u bytes, %u Query samples\n", secs, frames - f0, bytes - b0, nsamp);
    ph("leftPics", hist_lp, 8); ph("leftStreamFrames", hist_sf, 8); ph("curPacks", hist_pk, 8); ph("work_done 0/1", hist_wd, 2);
    if (OpenIMP_Encoder_GetTelemetry) {
        OpenIMPEncoderTelemetry t; int r = OpenIMP_Encoder_GetTelemetry(0, &t);
        printf("[T] rc=%d frames=%llu bytes=%llu idr=%llu last=%u enc_us last/avg/max=%u/%u/%u fps=%u.%02u kbps=%u (consumer count %u/%u)\n", r,
               (unsigned long long)t.frames, (unsigned long long)t.bytes, (unsigned long long)t.idr_frames, t.last_frame_bytes,
               t.encode_us_last, t.encode_us_avg, t.encode_us_max, t.fps_x100 / 100, t.fps_x100 % 100, t.avg_kbps, frames - f0, bytes - b0);
    }
    printf("[A] leftStreamBytes nonzero in %u samples (max %u)\n", nz_bytes, max_bytes);
    (void)t0;
    stall = 1; usleep(1500000);
    pq("B stalled 1.5 s"); pq("B stalled 1.5 s (2)");
    stall = 0; usleep(300000); pq("B resumed");
    run = 0; pthread_join(ts, NULL); pthread_join(tc, NULL);
    {   /* C: Query while holding a stream */
        IMPEncoderStream s; memset(&s, 0, sizeof s);
        if (IMP_Encoder_PollingStream(0, 1000) == 0) pq("C polled, not yet fetched");
        if (IMP_Encoder_PollingStream(0, 1000) == 0 && IMP_Encoder_GetStream(0, &s, true) == 0) {
            pq("C stream held (after Get)");
            if (IMP_Encoder_GetChnAveBitrate) { int br = -1; int r = IMP_Encoder_GetChnAveBitrate(0, &s, 5, &br); printf("[D] AveBitrate rc=%d kbps=%d\n", r, br); }
            IMP_Encoder_ReleaseStream(0, &s);
            pq("C after Release");
        } else printf("[C] no stream\n");
    }
    if (IMP_Encoder_GetChnEvalInfo) { char buf[256]; memset(buf, 0, sizeof buf); printf("[D] EvalInfo rc=%d\n", IMP_Encoder_GetChnEvalInfo(0, buf)); } else printf("[D] EvalInfo symbol absent\n");
    { IMPEncoderChnAttr g; memset(&g, 0, sizeof g); printf("[D] GetChnAttr rc=%d w=%u h=%u\n", IMP_Encoder_GetChnAttr(0, &g), g.encAttr.uWidth, g.encAttr.uHeight); }
    CK(IMP_Encoder_StopRecvPic(0), "StopRecvPic");
    pq("stopped");
    IMP_System_UnBind(&fs, &enc);
    IMP_Encoder_UnRegisterChn(0);
    pq("unregistered");
    IMP_Encoder_DestroyChn(0); IMP_Encoder_DestroyGroup(0);
    IMP_FrameSource_DisableChn(0); IMP_FrameSource_DestroyChn(0);
    IMP_System_Exit();
    return 0;
}
