/* T40 ISP-OSD device probe: two H.264 channels (0: 3840x2160 main, 1: 640x360
 * scaled sub), an ISP-drawn picture region per channel (IMP_ISP_Tuning_*OsdRgn)
 * and an ISP mask (cover) per channel, shown at 25 % and hidden at 65 % of the
 * run.  Works with the vendor libimp and OpenIMP (dlopen "libimp.so"; set
 * LD_LIBRARY_PATH for OpenIMP).  Sensor setup: Eufy T40XP (sc830ai).
 * usage: isp_osd_probe <seconds> <outdir>   (writes ch0.h264, ch1.h264)
 * stderr: EVT lines with the frame counts at the switches and readbacks. */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <imp/imp_common.h>
#include <imp/imp_system.h>
#include <imp/imp_isp.h>
#include <imp/imp_framesource.h>
#include <imp/imp_encoder.h>

#define FN(ret, name, args) static ret (*p_##name) args
FN(int32_t, IMP_ISP_Open, (void));
FN(int32_t, IMP_ISP_Close, (void));
FN(int32_t, IMP_ISP_AddSensor, (IMPVI_NUM, IMPSensorInfo *));
FN(int32_t, IMP_ISP_DelSensor, (IMPVI_NUM, IMPSensorInfo *));
FN(int32_t, IMP_ISP_EnableSensor, (IMPVI_NUM, IMPSensorInfo *));
FN(int32_t, IMP_ISP_DisableSensor, (IMPVI_NUM));
FN(int32_t, IMP_ISP_EnableTuning, (void));
FN(int32_t, IMP_ISP_DisableTuning, (void));
FN(int, IMP_System_Init, (void));
FN(int, IMP_System_Exit, (void));
FN(int, IMP_System_Bind, (IMPCell *, IMPCell *));
FN(int, IMP_System_UnBind, (IMPCell *, IMPCell *));
FN(int, IMP_FrameSource_CreateChn, (int, IMPFSChnAttr *));
FN(int, IMP_FrameSource_DestroyChn, (int));
FN(int, IMP_FrameSource_SetChnAttr, (int, const IMPFSChnAttr *));
FN(int, IMP_FrameSource_EnableChn, (int));
FN(int, IMP_FrameSource_DisableChn, (int));
FN(int, IMP_Encoder_CreateGroup, (int));
FN(int, IMP_Encoder_DestroyGroup, (int));
FN(int, IMP_Encoder_SetDefaultParam, (IMPEncoderChnAttr *, IMPEncoderProfile,
                                       IMPEncoderRcMode, uint16_t, uint16_t,
                                       uint32_t, uint32_t, uint32_t, uint32_t,
                                       int, uint32_t));
FN(int, IMP_Encoder_CreateChn, (int, const IMPEncoderChnAttr *));
FN(int, IMP_Encoder_DestroyChn, (int));
FN(int, IMP_Encoder_RegisterChn, (int, int));
FN(int, IMP_Encoder_UnRegisterChn, (int));
FN(int, IMP_Encoder_StartRecvPic, (int));
FN(int, IMP_Encoder_StopRecvPic, (int));
FN(int, IMP_Encoder_PollingStream, (int, uint32_t));
FN(int, IMP_Encoder_GetStream, (int, IMPEncoderStream *, bool));
FN(int, IMP_Encoder_ReleaseStream, (int, IMPEncoderStream *));
FN(int, IMP_ISP_Tuning_SetOsdPoolSize, (int));
FN(int, IMP_ISP_Tuning_CreateOsdRgn, (int, IMPIspOsdAttrAsm *));
FN(int, IMP_ISP_Tuning_SetOsdRgnAttr, (int, int, IMPIspOsdAttrAsm *));
FN(int, IMP_ISP_Tuning_GetOsdRgnAttr, (int, int, IMPIspOsdAttrAsm *));
FN(int, IMP_ISP_Tuning_ShowOsdRgn, (int, int, int));
FN(int, IMP_ISP_Tuning_DestroyOsdRgn, (int, int));
FN(int32_t, IMP_ISP_Tuning_SetMask, (IMPVI_NUM, IMPISPMASKAttr *));
FN(int32_t, IMP_ISP_GetSingleOSDAttr, (IMPVI_NUM, IMPISPSingleOSDAttr *));

static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

#define CK(x) do { int r_ = (x); if (r_) { fprintf(stderr, "FAIL %s = %d\n", #x, r_); goto out; } } while (0)

/* 128x64 BGRA picture: left half red, right half blue, 8-px white frame,
 * a black 16x16 square at (24,24) as an orientation mark. */
#define PW 128
#define PH 64
static uint8_t pic[PW * PH * 4];

static void make_pic(void)
{
    int x, y;

    for (y = 0; y < PH; y++) {
        for (x = 0; x < PW; x++) {
            uint8_t *p = &pic[(y * PW + x) * 4];
            uint8_t r = x < PW / 2 ? 255 : 0, g = 0, b = x < PW / 2 ? 0 : 255;

            if (x < 8 || y < 8 || x >= PW - 8 || y >= PH - 8)
                r = g = b = 255;
            if (x >= 24 && x < 40 && y >= 24 && y < 40)
                r = g = b = 0;
            p[0] = b; p[1] = g; p[2] = r; p[3] = 255;
        }
    }
}

static void fill_rgn(IMPIspOsdAttrAsm *a, int chx, int handle, int left, int top)
{
    memset(a, 0, sizeof(*a));
    a->type = ISP_OSD_REG_PIC;
    a->stsinglepicAttr.chx = chx;
    a->stsinglepicAttr.pic_num = handle;
    a->stsinglepicAttr.osd_type = IMP_ISP_PIC_ARGB_8888;
    a->stsinglepicAttr.osd_argb_type = IMP_ISP_ARGB_TYPE_BGRA;
    a->stsinglepicAttr.osd_pixel_alpha_disable = IMPISP_TUNING_OPS_MODE_DISABLE;
    a->stsinglepicAttr.pic.osd_enable = 1;
    a->stsinglepicAttr.pic.osd_left = left;
    a->stsinglepicAttr.pic.osd_top = top;
    a->stsinglepicAttr.pic.osd_width = PW;
    a->stsinglepicAttr.pic.osd_height = PH;
    a->stsinglepicAttr.pic.osd_image = (char *)pic;
    a->stsinglepicAttr.pic.osd_stride = PW * 4;
}

static void set_mask(int on)
{
    IMPISPMASKAttr m;
    int r;

    memset(&m, 0, sizeof(m));
    if (on) {
        /* chx 0: green 160x96 at (480,64); chx 1: green 48x32 at (240,32) */
        m.mask_chx[0][0].mask_en = 1;
        m.mask_chx[0][0].mask_pos_left = 480;
        m.mask_chx[0][0].mask_pos_top = 64;
        m.mask_chx[0][0].mask_width = 160;
        m.mask_chx[0][0].mask_height = 96;
        m.mask_chx[0][0].mask_type = IMPISP_MASK_TYPE_RGB;
        m.mask_chx[0][0].mask_value.mask_argb.g_value = 255;
        m.mask_chx[1][0] = m.mask_chx[0][0];
        m.mask_chx[1][0].mask_pos_left = 240;
        m.mask_chx[1][0].mask_pos_top = 32;
        m.mask_chx[1][0].mask_width = 48;
        m.mask_chx[1][0].mask_height = 32;
    }
    r = p_IMP_ISP_Tuning_SetMask(IMPVI_MAIN, &m);
    fprintf(stderr, "EVT mask %d ret %d\n", on, r);
}

int main(int argc, char **argv)
{
    void *h;
    int secs, i, c, fps = 20, w[2] = {3840, 640}, hh[2] = {2160, 360};
    IMPSensorInfo s;
    IMPFSChnAttr fs;
    IMPEncoderChnAttr ea;
    IMPCell src[2], dst[2];
    unsigned frames[2] = {0, 0};
    double t0, t1;
    int st_fs[2] = {0}, st_grp[2] = {0}, st_chn[2] = {0}, st_bind[2] = {0};
    int st_en[2] = {0}, st_start[2] = {0};
    int hdl[2] = {-1, -1}, phase = 0;
    FILE *f[2] = {0};
    char path[256];
    double t_on, t_off;

    if (argc < 3) {
        fprintf(stderr, "usage: osdtest seconds outdir\n");
        return 2;
    }
    secs = atoi(argv[1]);
    t_on = secs * 0.25;
    t_off = secs * 0.65;
    make_pic();
    for (c = 0; c < 2; c++) {
        snprintf(path, sizeof(path), "%s/ch%d.h264", argv[2], c);
        f[c] = fopen(path, "wb");
        if (!f[c]) { perror(path); return 2; }
    }

    if (!getenv("LD_LIBRARY_PATH")) { dlopen("libalog.so", RTLD_NOW | RTLD_GLOBAL); dlopen("libsysutils.so", RTLD_NOW | RTLD_GLOBAL); }
    h = dlopen("libimp.so", RTLD_NOW | RTLD_GLOBAL);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }
#define L(name) do { *(void **)&p_##name = dlsym(h, #name); if (!p_##name) { fprintf(stderr, "dlsym %s\n", #name); return 2; } } while (0)
    L(IMP_ISP_Open); L(IMP_ISP_Close); L(IMP_ISP_AddSensor); L(IMP_ISP_DelSensor);
    L(IMP_ISP_EnableSensor); L(IMP_ISP_DisableSensor); L(IMP_ISP_EnableTuning);
    L(IMP_ISP_DisableTuning); L(IMP_System_Init); L(IMP_System_Exit);
    L(IMP_System_Bind); L(IMP_System_UnBind); L(IMP_FrameSource_CreateChn);
    L(IMP_FrameSource_DestroyChn); L(IMP_FrameSource_SetChnAttr);
    L(IMP_FrameSource_EnableChn); L(IMP_FrameSource_DisableChn);
    L(IMP_Encoder_CreateGroup); L(IMP_Encoder_DestroyGroup);
    L(IMP_Encoder_SetDefaultParam); L(IMP_Encoder_CreateChn);
    L(IMP_Encoder_DestroyChn); L(IMP_Encoder_RegisterChn);
    L(IMP_Encoder_UnRegisterChn); L(IMP_Encoder_StartRecvPic);
    L(IMP_Encoder_StopRecvPic); L(IMP_Encoder_PollingStream);
    L(IMP_Encoder_GetStream); L(IMP_Encoder_ReleaseStream);
    L(IMP_ISP_Tuning_SetOsdPoolSize); L(IMP_ISP_Tuning_CreateOsdRgn);
    L(IMP_ISP_Tuning_SetOsdRgnAttr); L(IMP_ISP_Tuning_GetOsdRgnAttr);
    L(IMP_ISP_Tuning_ShowOsdRgn); L(IMP_ISP_Tuning_DestroyOsdRgn);
    L(IMP_ISP_Tuning_SetMask); L(IMP_ISP_GetSingleOSDAttr);

    memset(&s, 0, sizeof(s));
    strcpy(s.name, "sc830ai");
    s.cbus_type = TX_SENSOR_CONTROL_INTERFACE_I2C;
    strcpy(s.i2c.type, "sc830ai");
    s.i2c.addr = 0x30;
    s.i2c.i2c_adapter_id = 1;
    s.rst_gpio = 91;
    s.pwdn_gpio = -1;
    s.power_gpio = -1;
    s.video_interface = IMPISP_SENSOR_VI_MIPI_CSI0;
    s.mclk = IMPISP_SENSOR_MCLK1;
    s.default_boot = 4;

    fprintf(stderr, "EVT poolsize ret %d\n", p_IMP_ISP_Tuning_SetOsdPoolSize(256 * 1024));
    CK(p_IMP_ISP_Open());
    CK(p_IMP_ISP_AddSensor(IMPVI_MAIN, &s));
    CK(p_IMP_ISP_EnableSensor(IMPVI_MAIN, &s));
    CK(p_IMP_System_Init());
    CK(p_IMP_ISP_EnableTuning());

    for (c = 0; c < 2; c++) {
        memset(&fs, 0, sizeof(fs));
        fs.picWidth = w[c];
        fs.picHeight = hh[c];
        fs.pixFmt = PIX_FMT_NV12;
        if (c) {
            fs.scaler.enable = 1;
            fs.scaler.outwidth = w[c];
            fs.scaler.outheight = hh[c];
        }
        fs.outFrmRateNum = fps;
        fs.outFrmRateDen = 1;
        fs.nrVBs = 2;
        fs.type = FS_PHY_CHANNEL;
        CK(p_IMP_FrameSource_CreateChn(c, &fs)); st_fs[c] = 1;
        CK(p_IMP_FrameSource_SetChnAttr(c, &fs));
        CK(p_IMP_Encoder_CreateGroup(c)); st_grp[c] = 1;
        CK(p_IMP_Encoder_SetDefaultParam(&ea, IMP_ENC_PROFILE_AVC_HIGH, IMP_ENC_RC_MODE_FIXQP,
                                         w[c], hh[c], fps, 1, fps * 2, 2, 28, 0));
        CK(p_IMP_Encoder_CreateChn(c, &ea)); st_chn[c] = 1;
        CK(p_IMP_Encoder_RegisterChn(c, c));
        src[c].deviceID = DEV_ID_FS; src[c].groupID = c; src[c].outputID = 0;
        dst[c].deviceID = DEV_ID_ENC; dst[c].groupID = c; dst[c].outputID = 0;
        CK(p_IMP_System_Bind(&src[c], &dst[c])); st_bind[c] = 1;
    }
    for (c = 0; c < 2; c++) {
        CK(p_IMP_FrameSource_EnableChn(c)); st_en[c] = 1;
        CK(p_IMP_Encoder_StartRecvPic(c)); st_start[c] = 1;
    }

    /* regions: FS chn 0 -> ISP chx 0 at (64,64); FS chn 1 -> chx 1 at (32,32) */
    for (c = 0; c < 2; c++) {
        IMPIspOsdAttrAsm a, g;

        hdl[c] = p_IMP_ISP_Tuning_CreateOsdRgn(c, NULL);
        fprintf(stderr, "EVT create chn%d handle %d\n", c, hdl[c]);
        if (hdl[c] < 0)
            goto out;
        fill_rgn(&a, c, hdl[c], c ? 32 : 64, c ? 32 : 64);
        fprintf(stderr, "EVT setattr chn%d ret %d\n", c,
                p_IMP_ISP_Tuning_SetOsdRgnAttr(c, hdl[c], &a));
        memset(&g, 0x5a, sizeof(g));
        i = p_IMP_ISP_Tuning_GetOsdRgnAttr(c, hdl[c], &g);
        fprintf(stderr, "EVT getattr chn%d ret %d type %d chx %d pic %d en %d %u,%u %ux%u stride %u img %s\n",
                c, i, g.type, g.stsinglepicAttr.chx, g.stsinglepicAttr.pic_num,
                g.stsinglepicAttr.pic.osd_enable, g.stsinglepicAttr.pic.osd_left,
                g.stsinglepicAttr.pic.osd_top, g.stsinglepicAttr.pic.osd_width,
                g.stsinglepicAttr.pic.osd_height, g.stsinglepicAttr.pic.osd_stride,
                g.stsinglepicAttr.pic.osd_image == (char *)pic ? "user" :
                g.stsinglepicAttr.pic.osd_image ? "copy" : "null");
    }

    t0 = now_s();
    t1 = t0;
    while (t1 - t0 < secs) {
        for (c = 0; c < 2; c++) {
            IMPEncoderStream st;

            if (p_IMP_Encoder_PollingStream(c, 1000) != 0)
                continue;
            if (p_IMP_Encoder_GetStream(c, &st, 1) != 0)
                goto stop;
            for (i = 0; i < (int)st.packCount; i++) {
                uint32_t off = st.pack[i].offset, len = st.pack[i].length;
                uint32_t rem = st.streamSize - off;
                const uint8_t *b = (const uint8_t *)st.virAddr;

                if (len > rem) {
                    fwrite(b + off, 1, rem, f[c]);
                    fwrite(b, 1, len - rem, f[c]);
                } else {
                    fwrite(b + off, 1, len, f[c]);
                }
            }
            p_IMP_Encoder_ReleaseStream(c, &st);
            frames[c]++;
        }
        t1 = now_s();
        if (phase == 0 && t1 - t0 >= t_on) {
            for (c = 0; c < 2; c++)
                fprintf(stderr, "EVT show chn%d ret %d\n", c,
                        p_IMP_ISP_Tuning_ShowOsdRgn(c, hdl[c], 1));
            set_mask(1);
            fprintf(stderr, "EVT ON frames %u %u t %.2f\n", frames[0], frames[1], t1 - t0);
            phase = 1;
        } else if (phase == 1 && t1 - t0 >= (t_on + t_off) / 2) {
            for (c = 0; c < 2; c++) {
                IMPISPSingleOSDAttr k;

                memset(&k, 0, sizeof(k));
                k.chx = c;
                k.pic_num = hdl[c];
                i = p_IMP_ISP_GetSingleOSDAttr(IMPVI_MAIN, &k);
                fprintf(stderr, "EVT kget chx%d ret %d en %d %u,%u %ux%u stride %u img %s type %d argb %d alpha %d\n",
                        c, i, k.pic.osd_enable, k.pic.osd_left, k.pic.osd_top,
                        k.pic.osd_width, k.pic.osd_height, k.pic.osd_stride,
                        k.pic.osd_image ? "set" : "null", k.osd_type,
                        k.osd_argb_type, k.osd_pixel_alpha_disable);
            }
            phase = 2;
        } else if (phase == 2 && t1 - t0 >= t_off) {
            for (c = 0; c < 2; c++)
                fprintf(stderr, "EVT hide chn%d ret %d\n", c,
                        p_IMP_ISP_Tuning_ShowOsdRgn(c, hdl[c], 0));
            set_mask(0);
            fprintf(stderr, "EVT OFF frames %u %u t %.2f\n", frames[0], frames[1], t1 - t0);
            phase = 3;
        }
    }
stop:
    fprintf(stderr, "RESULT frames %u %u secs %.2f\n", frames[0], frames[1], t1 - t0);
out:
    for (c = 0; c < 2; c++)
        if (hdl[c] >= 0)
            fprintf(stderr, "EVT destroy chn%d ret %d\n", c,
                    p_IMP_ISP_Tuning_DestroyOsdRgn(c, hdl[c]));
    for (c = 0; c < 2; c++) {
        if (st_start[c]) p_IMP_Encoder_StopRecvPic(c);
        if (st_en[c]) p_IMP_FrameSource_DisableChn(c);
    }
    for (c = 0; c < 2; c++) {
        if (st_bind[c]) p_IMP_System_UnBind(&src[c], &dst[c]);
        if (st_chn[c]) { p_IMP_Encoder_UnRegisterChn(c); p_IMP_Encoder_DestroyChn(c); }
        if (st_grp[c]) p_IMP_Encoder_DestroyGroup(c);
        if (st_fs[c]) p_IMP_FrameSource_DestroyChn(c);
    }
    p_IMP_ISP_DisableTuning();
    p_IMP_System_Exit();
    p_IMP_ISP_DisableSensor(IMPVI_MAIN);
    p_IMP_ISP_DelSensor(IMPVI_MAIN, &s);
    p_IMP_ISP_Close();
    for (c = 0; c < 2; c++)
        fclose(f[c]);
    fprintf(stderr, "DONE\n");
    return 0;
}
