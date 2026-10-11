/* T41 P3 tuning controls: vendor control IDs, envelope and pointer
 * pass-through of the public structures (host test, needs the T41 1.2.0
 * vendor headers via T41_HEADERS). */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <imp/imp_isp.h>

int OpenIMP_P1_TuningIOCtl(uint32_t command, void *argument);
int OpenIMP_P1_SetDefaultBinPath(IMPVI_NUM num, const char *path);
int OpenIMP_P1_GetDefaultBinPath(IMPVI_NUM num, char *path, size_t size);
int OpenIMP_P1_IspIOCtl(uint32_t command, void *argument);
int OpenIMP_P1_GetSensorName(char name[32], int32_t *cbus_type);

/* ISPDevice-fd entries: what the library asked the main ISP node for */
/* IMP_ISP_Tuning_SwitchBin: ISP-enabled flag and the kernel's view */
static int sb_enabled;
static struct { uint32_t enable; char bname[128]; } sb_seen;

int OpenIMP_P1_TuningEnabled(void);
int OpenIMP_P1_TuningEnabled(void)
{
    return sb_enabled;
}

static struct {
    uint32_t command;
    unsigned char arg[64];
    int calls;
    int result;
    int err;                                /* errno the mock reports on failure */
    int32_t bus;
} isp;

int OpenIMP_P1_IspIOCtl(uint32_t command, void *argument)
{
    isp.command = command;
    memcpy(isp.arg, argument,
           (command == 0xc004542cU || command == 0xc004542dU) ? 40 : sizeof(isp.arg));
    isp.calls++;
    if (command == 0x8040540eU)             /* the driver returns the value */
        *(uint32_t *)((char *)argument + 56) = 0x5a;
    if (isp.result)
        errno = isp.err ? isp.err : EIO;
    return isp.result;
}

int OpenIMP_P1_GetSensorName(char name[32], int32_t *cbus_type)
{
    memset(name, 0, 32);
    strcpy(name, "gc5603");
    *cbus_type = isp.bus;
    return 0;
}

static struct {
    uint32_t command;
    int32_t vinum, direction, control;
    uintptr_t payload;
    int calls;
    int result;
    unsigned char data[92];		/* snapshot of the CCM (40) / CSC (92) wire */
    unsigned char reply[92];		/* what a GET returns */
} last;

int OpenIMP_P1_TuningIOCtl(uint32_t command, void *argument)
{
    /* Same layout as the P3 envelope: 16 bytes on the 32-bit target. */
    const struct {
        int32_t vinum, direction, control;
        uintptr_t payload;
    } *request = argument;

    last.command = command;
    last.vinum = request->vinum;
    last.direction = request->direction;
    last.control = request->control;
    last.payload = request->payload;
    last.calls++;
    if (request->control == 0x080000a5)
        memcpy(&sb_seen, (const void *)request->payload, sizeof(sb_seen) > 132 ? 132 : sizeof(sb_seen));
    if (request->control == 0x08000080 || request->control == 0x08000096 ||
        request->control == 0x08000074) {
        size_t n = request->control == 0x08000080 ? 40 :
                   request->control == 0x08000074 ? 24 : 92;

        if (request->direction)
            memcpy((void *)request->payload, last.reply, n);
        else
            memcpy(last.data, (void *)request->payload, n);
    }
    return last.result;
}

int OpenIMP_P1_SetDefaultBinPath(IMPVI_NUM num, const char *path)
{
    (void)num;
    (void)path;
    return 0;
}

int OpenIMP_P1_GetDefaultBinPath(IMPVI_NUM num, char *path, size_t size)
{
    /* The record lives in P1 (openimp_p1.c).  This host test only covers the
     * P3 wrapper, so the stub reports "never set", which is the state a caller
     * without a preceding IMP_ISP_SetDefaultBinPath is in. */
    (void)num;
    (void)path;
    (void)size;
    return -1;
}

#define EXPECT(call, dir, id, ptr)                                            \
    do {                                                                      \
        int before = last.calls;                                              \
        assert((call) == 0);                                                  \
        assert(last.calls == before + 1);                                     \
        assert(last.command == 0xc0105435U);                                  \
        assert(last.vinum == IMPVI_MAIN);                                     \
        assert(last.direction == (dir));                                      \
        assert(last.control == (id));                                         \
        assert(last.payload == (uintptr_t)(ptr));                             \
    } while (0)

int main(void)
{
    IMPISPAEScenceAttr scence;
    IMPISPModuleRatioAttr ratio;
    IMPISPAEExprInfo expr;
    IMPISPCCMAttr ccm;
    IMPISPGammaAttr gamma;
    IMPISPCSCAttr csc;
    IMPISPModuleCtl ctl;
    IMPISPAutoZoom zoom;
    IMPISPWdrOutputMode wdr;
    IMPISPSENSORAttr sensor;
    IMPISPAEWeightAttr weight;
    IMPISPCoefftWb coefft;
    IMPISPMaskBlockAttr mask;
    IMPISPScalerLvAttr scaler;
    IMPISPSensorFps fps;
    int before;

    /* Vendor structure sizes the open-tx-isp routes rely on. */
    assert(sizeof(IMPISPAEScenceAttr) == 52 || sizeof(void *) != 4);
    assert(sizeof(IMPISPModuleRatioAttr) == 128);
    assert(sizeof(IMPISPAEExprInfo) == 232 || sizeof(void *) != 4);

    EXPECT(IMP_ISP_Tuning_SetAeScenceAttr(IMPVI_MAIN, &scence), 0, 0x08000024, &scence);
    EXPECT(IMP_ISP_Tuning_GetAeScenceAttr(IMPVI_MAIN, &scence), 1, 0x08000024, &scence);
    EXPECT(IMP_ISP_Tuning_SetModule_Ratio(IMPVI_MAIN, &ratio), 0, 0x080000a4, &ratio);
    EXPECT(IMP_ISP_Tuning_GetModule_Ratio(IMPVI_MAIN, &ratio), 1, 0x080000a4, &ratio);
    EXPECT(IMP_ISP_Tuning_SetAeExprInfo(IMPVI_MAIN, &expr), 0, 0x08000023, &expr);
    EXPECT(IMP_ISP_Tuning_GetAeExprInfo(IMPVI_MAIN, &expr), 1, 0x08000023, &expr);
    EXPECT(IMP_ISP_Tuning_SetGammaAttr(IMPVI_MAIN, &gamma), 0, 0x08000025, &gamma);
    /* CCM / CSC: converted to the kernel wire (Q16 words / 92-byte table). */
    {
        unsigned int i;
        int32_t w;

        memset(&ccm, 0, sizeof(ccm));
        ccm.ManualEn = 1;
        ccm.SatEn = 0;
        ccm.ColorMatrix[0] = 1.0f; ccm.ColorMatrix[1] = -0.5f; ccm.ColorMatrix[8] = 2.0f;
        before = last.calls;
        assert(IMP_ISP_Tuning_SetCCMAttr(IMPVI_MAIN, &ccm) == 0);
        assert(last.calls == before + 1 && last.direction == 0 && last.control == 0x08000080);
        assert(last.data[0] == 1 && last.data[1] == 0);
        memcpy(&w, last.data + 4, 4); assert(w == 65536);
        memcpy(&w, last.data + 8, 4); assert(w == -32768);
        memcpy(&w, last.data + 36, 4); assert(w == 131072);
        memcpy(last.reply, last.data, 40);
        memset(&ccm, 0, sizeof(ccm));
        assert(IMP_ISP_Tuning_GetCCMAttr(IMPVI_MAIN, &ccm) == 0);
        assert(ccm.ManualEn && !ccm.SatEn && ccm.ColorMatrix[0] == 1.0f &&
               ccm.ColorMatrix[1] == -0.5f && ccm.ColorMatrix[8] == 2.0f);

        /* presets pass only the version */
        memset(&csc, 0, sizeof(csc));
        csc.ColorGamut = IMP_ISP_CG_BT709_LIMITED;
        assert(IMP_ISP_Tuning_SetISPCSCAttr(IMPVI_MAIN, &csc) == 0);
        memcpy(&w, last.data, 4); assert(w == 3);
        /* user table: BT601 full forward matrix, inverse computed */
        csc.ColorGamut = IMP_ISP_CG_USER;
        csc.Matrix.CscCoef[0] = 0.299f; csc.Matrix.CscCoef[1] = 0.587f; csc.Matrix.CscCoef[2] = 0.114f;
        csc.Matrix.CscCoef[3] = -0.1687f; csc.Matrix.CscCoef[4] = -0.3313f; csc.Matrix.CscCoef[5] = 0.5f;
        csc.Matrix.CscCoef[6] = 0.5f; csc.Matrix.CscCoef[7] = -0.4187f; csc.Matrix.CscCoef[8] = -0.0813f;
        csc.Matrix.CscOffset[0] = 0x80; csc.Matrix.CscOffset[1] = 0;
        csc.Matrix.CscClip[0] = 16; csc.Matrix.CscClip[1] = 235; csc.Matrix.CscClip[2] = 17; csc.Matrix.CscClip[3] = 240;
        assert(IMP_ISP_Tuning_SetISPCSCAttr(IMPVI_MAIN, &csc) == 0);
        memcpy(&w, last.data, 4); assert(w == 6);
        memcpy(&w, last.data + 4, 4); assert(w == 19595 || w == 19596);
        memcpy(&w, last.data + 48, 4); assert(w >= 65530 && w <= 65542);	/* Y of Y,U,V -> R: 1.0 */
        memcpy(&w, last.data + 56, 4); assert(w >= 91860 && w <= 91900);	/* V -> R: 1.402 */
        /* header order: offsets Y,UV then clips Ymin,Ymax,UVmin,UVmax */
        assert(last.data[40] == 0x80 && last.data[41] == 0 && last.data[42] == 16 && last.data[43] == 235 &&
               last.data[44] == 17 && last.data[45] == 240);
        assert(!memcmp(last.data + 84, last.data + 40, 4) && !memcmp(last.data + 88, last.data + 44, 4));
        memcpy(last.reply, last.data, 92);
        memset(&csc, 0, sizeof(csc));
        assert(IMP_ISP_Tuning_GetISPCSCAttr(IMPVI_MAIN, &csc) == 0);
        assert(csc.ColorGamut == IMP_ISP_CG_USER && csc.Matrix.CscOffset[0] == 0x80 &&
               csc.Matrix.CscOffset[1] == 0 && csc.Matrix.CscClip[0] == 16 && csc.Matrix.CscClip[1] == 235 &&
               csc.Matrix.CscClip[2] == 17 && csc.Matrix.CscClip[3] == 240 && csc.Matrix.CscCoef[1] > 0.586f && csc.Matrix.CscCoef[1] < 0.588f);
        /* a singular matrix or a coefficient out of range never reaches the driver */
        before = last.calls;
        memset(csc.Matrix.CscCoef, 0, sizeof(csc.Matrix.CscCoef));
        assert(IMP_ISP_Tuning_SetISPCSCAttr(IMPVI_MAIN, &csc) == -1);
        csc.Matrix.CscCoef[0] = 9.0f;
        assert(IMP_ISP_Tuning_SetISPCSCAttr(IMPVI_MAIN, &csc) == -1);
        assert(last.calls == before);
        for (i = 0; i < 9; i++) csc.Matrix.CscCoef[i] = 0.1f * i;
    }
    EXPECT(IMP_ISP_Tuning_SetModuleControl(IMPVI_MAIN, &ctl), 0, 0x08000072, &ctl);
    EXPECT(IMP_ISP_Tuning_SetAutoZoom(IMPVI_MAIN, &zoom), 0, 0x08000077, &zoom);
    EXPECT(IMP_ISP_Tuning_SetWdrOutputMode(IMPVI_MAIN, &wdr), 0, 0x08000054, &wdr);

    /* Controls open-tx-isp T41 routes since claude/t41-connect: pointer
     * pass-through with the stock payload sizes the driver copies. */
    assert(sizeof(IMPISPSENSORAttr) == 20);
    assert(sizeof(IMPISPAEWeightAttr) == 460);
    assert(sizeof(IMPISPCoefftWb) == 6);
    EXPECT(IMP_ISP_Tuning_GetSensorAttr(IMPVI_MAIN, &sensor), 1, 0x08000033, &sensor);
    EXPECT(IMP_ISP_Tuning_SetAeWeight(IMPVI_MAIN, &weight), 0, 0x08000021, &weight);
    EXPECT(IMP_ISP_Tuning_GetAeWeight(IMPVI_MAIN, &weight), 1, 0x08000021, &weight);
    EXPECT(IMP_ISP_Tuning_Awb_SetRgbCoefft(IMPVI_MAIN, &coefft), 0, 0x08000098, &coefft);
    EXPECT(IMP_ISP_Tuning_Awb_GetRgbCoefft(IMPVI_MAIN, &coefft), 1, 0x08000098, &coefft);
    /* MSCA controls (open-tx-isp claude/t41-zoom-mask): pointer pass-through
     * of the 24-byte mask block and the 12-byte scaler level. */
    assert(sizeof(IMPISPMaskBlockAttr) == 24);
    assert(sizeof(IMPISPScalerLvAttr) == 12);
    assert(sizeof(IMPISPAutoZoom) == 60);
    memset(&mask, 0, sizeof(mask));
    memset(&scaler, 0, sizeof(scaler));
    memset(&zoom, 0, sizeof(zoom));
    EXPECT(IMP_ISP_Tuning_SetMaskBlock(IMPVI_MAIN, &mask), 0, 0x08000074, &mask);
    EXPECT(IMP_ISP_Tuning_SetScalerLv(IMPVI_MAIN, &scaler), 0, 0x080000a6, &scaler);
    EXPECT(IMP_ISP_Tuning_GetAutoZoom(IMPVI_MAIN, &zoom), 1, 0x08000077, &zoom);
    {
        /* vendor 1.2.6: enabled RGB block -> YUV in place (BT.601 full,
         * IMP_ISP_Tuning_DumpMask), argb kept; the kernel gets ayuv. */
        mask.chx = 1;
        mask.pinum = 2;
        mask.mask_en = 1;
        mask.mask_pos_top = 100;
        mask.mask_pos_left = 200;
        mask.mask_width = 64;
        mask.mask_height = 32;
        mask.mask_type = IMPISP_MASK_TYPE_RGB;
        mask.mask_value.argb.r_value = 255;
        mask.mask_value.argb.g_value = 0;
        mask.mask_value.argb.b_value = 0;
        EXPECT(IMP_ISP_Tuning_SetMaskBlock(IMPVI_MAIN, &mask), 0, 0x08000074, &mask);
        assert(mask.mask_value.ayuv.y_value == 76);
        assert(mask.mask_value.ayuv.u_value == 84);
        assert(mask.mask_value.ayuv.v_value == 255);
        assert(mask.mask_value.argb.r_value == 255 && mask.mask_value.argb.g_value == 0);
        /* kernel wire: chx, pinum, en, pad, top, left, w, h, type, rgb, yuv */
        assert(last.data[0] == 1 && last.data[1] == 2 && last.data[2] == 1);
        assert(last.data[4] == 100 && last.data[6] == 200 && last.data[8] == 64 && last.data[10] == 32);
        assert(last.data[16] == 255 && last.data[19] == 76 && last.data[20] == 84 && last.data[21] == 255);
        /* a YUV block and a disabled RGB block are passed unchanged */
        mask.mask_type = IMPISP_MASK_TYPE_YUV;
        mask.mask_value.ayuv.y_value = 16;
        assert(IMP_ISP_Tuning_SetMaskBlock(IMPVI_MAIN, &mask) == 0);
        assert(mask.mask_value.ayuv.y_value == 16 && last.data[19] == 16);
        mask.mask_type = IMPISP_MASK_TYPE_RGB;
        mask.mask_en = 0;
        mask.mask_value.ayuv.y_value = 17;
        assert(IMP_ISP_Tuning_SetMaskBlock(IMPVI_MAIN, &mask) == 0);
        assert(mask.mask_value.ayuv.y_value == 17);
        /* Get: kernel answer, then type and argb from the library cache
         * (slot chx * pinum = 2, last set: RGB, 255/0/0) */
        memset(last.reply, 0, sizeof(last.reply));
        last.reply[0] = 1; last.reply[1] = 2;
        last.reply[4] = 100; last.reply[19] = 76;
        last.reply[12] = 9; last.reply[16] = 9;
        memset(&mask, 0xee, sizeof(mask));
        mask.chx = 1;
        mask.pinum = 2;
        EXPECT(IMP_ISP_Tuning_GetMaskBlock(IMPVI_MAIN, &mask), 1, 0x08000074, &mask);
        assert(mask.mask_en == 0 && mask.mask_pos_top == 100 && mask.mask_value.ayuv.y_value == 76);
        assert(mask.mask_type == IMPISP_MASK_TYPE_RGB);
        assert(mask.mask_value.argb.r_value == 255 && mask.mask_value.argb.g_value == 0 &&
               mask.mask_value.argb.b_value == 0);
        before = last.calls;
        assert(IMP_ISP_Tuning_SetMaskBlock(IMPVI_BUTT, &mask) == -1);
        assert(IMP_ISP_Tuning_GetMaskBlock(IMPVI_MAIN, NULL) == -1);
        assert(last.calls == before);
    }

    /* SetSensorFPS: stock s_ctrl 0x08000070 takes num<<16|den inline. */
    fps.num = 15;
    fps.den = 1;
    EXPECT(IMP_ISP_Tuning_SetSensorFPS(IMPVI_MAIN, &fps), 0, 0x08000070,
           (15U << 16) | 1U);
    fps.num = 0x10000;
    before = last.calls;
    assert(IMP_ISP_Tuning_SetSensorFPS(IMPVI_MAIN, &fps) == -1);
    fps.num = 25;
    fps.den = 0;
    assert(IMP_ISP_Tuning_SetSensorFPS(IMPVI_MAIN, &fps) == -1);
    assert(last.calls == before);

    /* Invalid arguments never reach the driver. */
    before = last.calls;
    assert(IMP_ISP_Tuning_SetAeScenceAttr(IMPVI_MAIN, NULL) == -1);
    assert(IMP_ISP_Tuning_SetModule_Ratio(IMPVI_BUTT, &ratio) == -1);
    assert(last.calls == before);

    /* Driver errors (e.g. -EOPNOTSUPP) are returned, not hidden. */
    last.result = -1;
    assert(IMP_ISP_Tuning_SetCCMAttr(IMPVI_MAIN, &ccm) == -1);
    assert(IMP_ISP_Tuning_SetScalerLv(IMPVI_MAIN, &scaler) == -1);
    /* A refused rate is not cached: GetSensorFPS keeps 15/1. */
    fps.num = 10;
    fps.den = 1;
    assert(IMP_ISP_Tuning_SetSensorFPS(IMPVI_MAIN, &fps) == -1);
    fps.num = fps.den = 0;
    assert(IMP_ISP_Tuning_GetSensorFPS(IMPVI_MAIN, &fps) == -1);
    assert(fps.num == 15 && fps.den == 1);

    /* IMP_ISP_GetDefaultBinPath: no IMP_ISP_SetDefaultBinPath preceded it, so
     * the kernel owns the default and libimp cannot know it.  The wrapper must
     * fail instead of answering "success" with an empty string. */
    {
        char bin_path[64];

        memset(bin_path, 'x', sizeof(bin_path));
        assert(IMP_ISP_GetDefaultBinPath(IMPVI_MAIN, bin_path) == -1);
        assert(bin_path[0] == '\0');
        assert(IMP_ISP_GetDefaultBinPath(IMPVI_BUTT, bin_path) == -1);
        assert(IMP_ISP_GetDefaultBinPath(IMPVI_MAIN, NULL) == -1);
    }

    /* AF weight: tuning control 0x8000032, pointer pass-through */
    {
        IMPISPWeight af;

        last.result = 0;
        EXPECT(IMP_ISP_Tuning_SetAfWeight(IMPVI_MAIN, &af), 0, 0x8000032, &af);
        EXPECT(IMP_ISP_Tuning_GetAfWeight(IMPVI_MAIN, &af), 1, 0x8000032, &af);
    }
    /* sensor register: ISP node, 64-byte request {name, type, ..., reg at 48,
     * value at 56}; SPI sensors are refused */
    {
        IMPISPSensorRegister reg;
        uint32_t v;

        last.result = 0;
        isp.bus = 1;
        reg.addr = 0x3107;
        reg.value = 0;
        assert(IMP_ISP_GetSensorRegister(IMPVI_MAIN, &reg) == 0);
        assert(isp.command == 0x8040540eU && isp.calls == 1);
        assert(strcmp((char *)isp.arg, "gc5603") == 0);
        memcpy(&v, isp.arg + 32, 4);
        assert(v == 0);                     /* vinum */
        memcpy(&v, isp.arg + 36, 4);
        assert(v == 1);                     /* bus type */
        memcpy(&v, isp.arg + 48, 4);
        assert(v == 0x3107 && reg.value == 0x5a);
        reg.value = 0x11;
        assert(IMP_ISP_SetSensorRegister(IMPVI_MAIN, &reg) == 0);
        assert(isp.command == 0xc040540dU);
        memcpy(&v, isp.arg + 56, 4);
        assert(v == 0x11);
        isp.bus = 2;
        assert(IMP_ISP_GetSensorRegister(IMPVI_MAIN, &reg) == -1);
        assert(IMP_ISP_GetSensorRegister(IMPVI_BUTT, &reg) == -1);
        assert(isp.calls == 2);
    }
    /* frame drop: three channels, lsize 0..31 */
    {
        IMPISPFrameDropAttr fd, back;

        memset(&fd, 0, sizeof(fd));
        fd.fdrop[1].enable = IMPISP_TUNING_OPS_MODE_ENABLE;
        fd.fdrop[1].lsize = 3;
        fd.fdrop[1].fmark = 0x5;
        isp.calls = 0;
        assert(IMP_ISP_SetFrameDrop(IMPVI_MAIN, &fd) == 0);
        assert(isp.command == 0xc004542cU && isp.calls == 1);
        {   /* 40-byte request: channel word, then entry 1 at 4 + 12 */
            uint32_t w[10];
            memcpy(w, isp.arg, 40);
            assert(w[0] == 0 && w[4] == 1 && w[5] == 3 && w[6] == 0x5);
        }
        fd.fdrop[2].lsize = 32;
        assert(IMP_ISP_SetFrameDrop(IMPVI_MAIN, &fd) == -1);
        assert(isp.calls == 1);
        assert(IMP_ISP_GetFrameDrop(IMPVI_MAIN, &back) == 0);
        assert(isp.command == 0xc004542dU);
        isp.result = -1;
        assert(IMP_ISP_GetFrameDrop(IMPVI_MAIN, &back) == -1);
        isp.result = 0;
    }
    /* WDR switch: ioctl 0x80045413 / 0x80045414, _GET answers from memory */
    {
        IMPISPTuningOpsMode m = IMPISP_TUNING_OPS_MODE_ENABLE, g = IMPISP_TUNING_OPS_MODE_DISABLE;

        assert(IMP_ISP_WDR_ENABLE_GET(IMPVI_MAIN, &g) == 0 && g == IMPISP_TUNING_OPS_MODE_DISABLE);
        assert(IMP_ISP_WDR_ENABLE(IMPVI_MAIN, &m) == 0);
        assert(isp.command == 0x80045413U);
        assert(IMP_ISP_WDR_ENABLE_GET(IMPVI_MAIN, &g) == 0 && g == IMPISP_TUNING_OPS_MODE_ENABLE);
        m = IMPISP_TUNING_OPS_MODE_DISABLE;
        assert(IMP_ISP_WDR_ENABLE(IMPVI_MAIN, &m) == 0);
        assert(isp.command == 0x80045414U);
        isp.result = -1;
        m = IMPISP_TUNING_OPS_MODE_ENABLE;
        assert(IMP_ISP_WDR_ENABLE(IMPVI_MAIN, &m) == -1);
        assert(IMP_ISP_WDR_ENABLE_GET(IMPVI_MAIN, &g) == 0 && g == IMPISP_TUNING_OPS_MODE_DISABLE);
        isp.result = 0;
    }

    /* IMP_ISP_Tuning_SwitchBin (vendor T41 1.2.6 libimp disassembly): control
     * 0x80000a5 set, payload = IMPISPBinAttr (132 bytes), vendor check
     * order and return codes, NUL-terminated copy to the kernel. */
    {
        IMPISPBinAttr bin;
        int n;

        assert(sizeof(IMPISPBinAttr) == 132);
        memset(&bin, 0, sizeof(bin));
        bin.enable = IMPISP_TUNING_OPS_MODE_ENABLE;
        strcpy(bin.bname, "/tmp/night-bin");
        n = last.calls;
        sb_enabled = 1;
        assert(IMP_ISP_Tuning_SwitchBin(IMPVI_MAIN, &bin) == 0);
        assert(last.calls == n + 1);
        assert(last.command == 0xc0105435U);
        assert(last.vinum == IMPVI_MAIN && last.direction == 0);
        assert(last.control == 0x080000a5);
        assert(last.payload != 0 && last.payload != (uintptr_t)&bin);
        assert(sb_seen.enable == 1 && !strcmp(sb_seen.bname, "/tmp/night-bin"));
        /* enable 0 = back to the default bin; reaches the kernel too */
        bin.enable = IMPISP_TUNING_OPS_MODE_DISABLE;
        assert(IMP_ISP_Tuning_SwitchBin(IMPVI_MAIN, &bin) == 0);
        assert(sb_seen.enable == 0 && last.calls == n + 2);
        /* an unterminated name is cut before the kernel sees it */
        memset(bin.bname, 'x', sizeof(bin.bname));
        bin.enable = IMPISP_TUNING_OPS_MODE_ENABLE;
        assert(IMP_ISP_Tuning_SwitchBin(IMPVI_MAIN, &bin) == 0);
        assert(sb_seen.bname[sizeof(bin.bname) - 1] == '\0');
        assert(sb_seen.bname[sizeof(bin.bname) - 2] == 'x');
        assert(bin.bname[sizeof(bin.bname) - 1] == 'x');    /* caller intact */
        n = last.calls;
        /* vendor refusals: no ioctl, vendor codes */
        assert(IMP_ISP_Tuning_SwitchBin(IMPVI_MAIN, NULL) == -4087);
        assert(IMP_ISP_Tuning_SwitchBin((IMPVI_NUM)2, &bin) == -4084);
        bin.enable = (IMPISPTuningOpsMode)2;
        assert(IMP_ISP_Tuning_SwitchBin(IMPVI_MAIN, &bin) == -4086);
        bin.enable = IMPISP_TUNING_OPS_MODE_ENABLE;
        sb_enabled = 0;                                 /* ISP not running */
        assert(IMP_ISP_Tuning_SwitchBin(IMPVI_MAIN, &bin) == -4091);
        sb_enabled = 1;
        assert(last.calls == n);
        /* T41 order: the NULL test comes before the sensor number */
        assert(IMP_ISP_Tuning_SwitchBin((IMPVI_NUM)2, NULL) == -4087);

        last.result = -1;                               /* driver refuses */
        assert(IMP_ISP_Tuning_SwitchBin(IMPVI_MAIN, &bin) == -4090);
        last.result = 0;
    }

    /* IMP_ISP_Set/GetISPBypass (vendor 1.2.6): 8-byte block on the ISP node */
    {
        IMPISPTuningOpsMode en = IMPISP_TUNING_OPS_MODE_ENABLE;
        IMPISPTuningOpsMode got = (IMPISPTuningOpsMode)7;
        int calls;

        isp.result = 0;
        calls = isp.calls;
        assert(IMP_ISP_GetISPBypass(IMPVI_MAIN, &got) == 0 && got == 0);
        assert(IMP_ISP_SetISPBypass(IMPVI_MAIN, NULL) == -4087);
        assert(IMP_ISP_SetISPBypass((IMPVI_NUM)2, &en) == -4084);
        assert(IMP_ISP_GetISPBypass((IMPVI_NUM)2, &got) == -4084);
        assert(IMP_ISP_GetISPBypass(IMPVI_MAIN, NULL) == -4087);
        assert(isp.calls == calls);
        assert(IMP_ISP_SetISPBypass(IMPVI_MAIN, &en) == 0);
        assert(isp.calls == calls + 1 && isp.command == 0x80085418U);
        { uint32_t blk[2]; memcpy(blk, isp.arg, 8);
          assert(blk[0] == 1 && blk[1] == 0); }
        got = (IMPISPTuningOpsMode)7;
        assert(IMP_ISP_GetISPBypass(IMPVI_MAIN, &got) == 0 && got == en);
        /* a refusing kernel: vendor -4095, value kept */
        en = IMPISP_TUNING_OPS_MODE_DISABLE;
        isp.result = -1;
        isp.err = ENOTTY;
        assert(IMP_ISP_SetISPBypass(IMPVI_MAIN, &en) == -4095);
        assert(IMP_ISP_GetISPBypass(IMPVI_MAIN, &got) == 0 &&
               got == IMPISP_TUNING_OPS_MODE_ENABLE);
        /* ISP not up (ENODEV): remembered like the vendor without an ISP device */
        isp.err = ENODEV;
        assert(IMP_ISP_SetISPBypass(IMPVI_MAIN, &en) == 0);
        assert(IMP_ISP_GetISPBypass(IMPVI_MAIN, &got) == 0 && got == en);
        isp.result = 0;
        isp.err = 0;
    }

    puts("t41 p3 controls tests passed");
    return 0;
}
