#include <stdio.h>
#if defined(APITEST_T40)   /* no picture cases on T40: its image functions are covered by area t40dev */
int picture_main(int argc, char **argv) { (void)argc; (void)argv; fprintf(stderr, "picture: not built for T40 (use area t40dev)\n"); return 2; }
#else
/* imgfx - device test: one picture per image-changing IMP function/case.
 *
 *   imgfx SENSOR I2C_ADDR W H OUTDIR [case-filter]
 *
 * FS ch0 = sensor size, FS ch1 = 640x360 (scaled).  Per case: set the
 * function, wait (300 ms + 3 frames, longer for AE/AWB), save ch1 as raw
 * NV12 OUTDIR/<NN>-<case>.nv12, print
 *     [R] <case> set=<ret> get=<readback>      (or "[R] <case> N/A")
 * and restore the value read before the set, so the cases stay independent.
 *
 * One source for all SoCs: build with -DPLATFORM_T10|T20|T21|T23|T31|T41 and
 * the vendor headers of that SoC (ingenic-headers).  Functions that the
 * linked libimp does not export are weak -> the case prints N/A.
 *
 * Env: IMGFX_MODE=night   running mode night while testing (IR-cut open, IR on)
 *      IMGFX_SETTLE_MS    extra settle time added to every case (default 0)
 *      IMGFX_PAIR=1       a second consecutive frame per case (<name>-b.nv12), for temporal-noise measurements
 *      IMGFX_PRE=a,b      blocks put into a working state for the run (dark,defog,nr,dpc; see pre_apply)
 *      IMGFX_CH0CROP=N    save ch0 (sensor size) instead of ch1: the centred N x N window as NV12 (N even),
 *                         for full-resolution noise measurements without full-size files in /tmp (#137)
 *      IMGFX_SKIP_RISKY=1 skip the cases that may wedge a pipeline
 *                         (ISP bypass, FPS change); they run last
 * Run with the camera's streamer (timps/prudynt) stopped.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <math.h>
#include <dlfcn.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "imp/imp_common.h"
#include "imp/imp_system.h"
#include "../save_guard.h"
#include "imp/imp_isp.h"
#include "imp/imp_framesource.h"

#define SUB_W 640
#define SUB_H 360
#define CASE_NA (-0x7fff0000)

/* ------------------------------------------------------------------ SoC */
/* T40 = T41 API family: build with -DPLATFORM_T41 -DAPITEST_T40 (T40 1.3.1 headers) */
#if defined(APITEST_T40)
# define SOC "T40"
#elif defined(PLATFORM_T10)
# define SOC "T10"
# define H_AECOMP 1
# define H_WDRATTR 1
# define H_RAWDRC 1
# define H_ANTIFOG 1
# define H_DPSTR 1
# define H_COLORFX 1
# define H_SCENE 1
# define H_MESH 1
# define H_HVFLIP2 1
# define H_EXT_FCROP 1
# define H_EXT_CSC 1
#elif defined(PLATFORM_T20)
# define SOC "T20"
# define H_AECOMP 1
# define H_WDRATTR 1
# define H_RAWDRC 1
# define H_ANTIFOG 1
# define H_DPSTR 1
# define H_COLORFX 1
# define H_SCENE 1
# define H_MESH 1
# define H_HVFLIP2 1
# define H_EXT_FCROP 1
# define H_EXT_CSC 1
#elif defined(PLATFORM_T21)
# define SOC "T21"
# define H_RAWDRC 1
# define H_ANTIFOG 1
# define H_DPSTR 1
# define H_COLORFX 1
# define H_SCENE 1
# define H_DRCSTR 1
# define H_MODCTL 1
# define H_EXT_FCROP 1
# define H_EXT_CSC 1
#elif defined(PLATFORM_T23) || defined(PLATFORM_T31)
# if defined(PLATFORM_T23)
#  define SOC "T23"
#  define H_SENSFLIP 1
# else
#  define SOC "T31"
# endif
# define H_AECOMP 1
# define H_DRCSTR 1
# define H_DRCEN 1
# define H_DEFOG 1
# define H_DPC 1
# define H_MODCTL 1
# define H_BCSHHUE 1
# define H_CCM 1
# define H_CSC 1
# define H_FCROP 1
# define H_MASK 1
# define H_AUTOZOOM 1
# define H_SCALERLV 1
# define H_AEATTR 1
# define H_AWBCT 1
# define H_HVFLIPE 1
# define H_BLC 1
#elif defined(PLATFORM_T30)
# define SOC "T30"      /* vendor T30 1.0.5 header: no BcshHue/DRC/ModuleControl/CCM; crop + CSC are OpenIMP extensions */
# define H_AECOMP 1
# define H_WDRATTR 1
# define H_RAWDRC 1
# define H_ANTIFOG 1
# define H_DPSTR 1
# define H_COLORFX 1
# define H_SCENE 1
# define H_HVFLIP2 1
# define H_EXT_FCROP 1
# define H_EXT_CSC 1
#elif defined(PLATFORM_T41)
# define SOC "T41"
#else
# error "define PLATFORM_T10|T20|T21|T23|T30|T31|T41"
#endif

#ifdef PLATFORM_T41
# define V0 IMPVI_MAIN,
# define SETU8(f, v) ({ unsigned char t_ = (unsigned char)(v); f(IMPVI_MAIN, &t_); })
#else
# define V0
# define SETU8(f, v) f((unsigned char)(v))
#endif
#define CALLP(f, p) f(V0 p)

/* every vendor function we call is weak: an unexported one -> case N/A */
#define DO_PRAGMA(x) _Pragma(#x)
#define W(f) DO_PRAGMA(weak f)
#define HAVE(f) ((f) != NULL)

W(IMP_ISP_Tuning_SetBrightness) W(IMP_ISP_Tuning_GetBrightness)
W(IMP_ISP_Tuning_SetContrast) W(IMP_ISP_Tuning_GetContrast)
W(IMP_ISP_Tuning_SetSaturation) W(IMP_ISP_Tuning_GetSaturation)
W(IMP_ISP_Tuning_SetSharpness) W(IMP_ISP_Tuning_GetSharpness)
W(IMP_ISP_Tuning_SetISPRunningMode) W(IMP_ISP_Tuning_GetISPRunningMode)
W(IMP_ISP_Tuning_SetAntiFlickerAttr) W(IMP_ISP_Tuning_GetAntiFlickerAttr)
W(IMP_ISP_Tuning_SetSensorFPS) W(IMP_ISP_Tuning_GetSensorFPS)
W(IMP_FrameSource_GetChnAttr) W(IMP_FrameSource_SetChnAttr)
#ifdef PLATFORM_T41
W(IMP_ISP_Tuning_SetBcshHue) W(IMP_ISP_Tuning_GetBcshHue)
W(IMP_ISP_Tuning_SetHVFLIP) W(IMP_ISP_Tuning_GetHVFLIP)
W(IMP_ISP_Tuning_SetGammaAttr) W(IMP_ISP_Tuning_GetGammaAttr)
W(IMP_ISP_Tuning_SetAeScenceAttr) W(IMP_ISP_Tuning_GetAeScenceAttr)
W(IMP_ISP_Tuning_SetAeExprInfo) W(IMP_ISP_Tuning_GetAeExprInfo)
W(IMP_ISP_Tuning_SetAwbAttr) W(IMP_ISP_Tuning_GetAwbAttr)
W(IMP_ISP_Tuning_SetCCMAttr) W(IMP_ISP_Tuning_GetCCMAttr)
W(IMP_ISP_Tuning_SetISPCSCAttr) W(IMP_ISP_Tuning_GetISPCSCAttr)
W(IMP_ISP_Tuning_SetModule_Ratio) W(IMP_ISP_Tuning_GetModule_Ratio)
W(IMP_ISP_Tuning_SetModuleControl) W(IMP_ISP_Tuning_GetModuleControl)
W(IMP_ISP_Tuning_SetMaskBlock) W(IMP_ISP_Tuning_GetMaskBlock)
W(IMP_ISP_Tuning_SetAutoZoom) W(IMP_ISP_Tuning_GetAutoZoom)
W(IMP_ISP_Tuning_SetScalerLv)
#else
W(IMP_ISP_Tuning_SetISPHflip) W(IMP_ISP_Tuning_GetISPHflip)
W(IMP_ISP_Tuning_SetISPVflip) W(IMP_ISP_Tuning_GetISPVflip)
W(IMP_ISP_Tuning_SetISPBypass)
W(IMP_ISP_Tuning_SetGamma) W(IMP_ISP_Tuning_GetGamma)
W(IMP_ISP_Tuning_SetExpr) W(IMP_ISP_Tuning_GetExpr)
W(IMP_ISP_Tuning_SetWB) W(IMP_ISP_Tuning_GetWB)
W(IMP_ISP_Tuning_SetMaxAgain) W(IMP_ISP_Tuning_GetMaxAgain)
W(IMP_ISP_Tuning_SetMaxDgain) W(IMP_ISP_Tuning_GetMaxDgain)
W(IMP_ISP_Tuning_SetHiLightDepress)
W(IMP_ISP_Tuning_SetSinterStrength) W(IMP_ISP_Tuning_SetTemperStrength)
#endif
#ifdef H_AECOMP
W(IMP_ISP_Tuning_SetAeComp) W(IMP_ISP_Tuning_GetAeComp)
#endif
#ifdef H_WDRATTR
W(IMP_ISP_Tuning_SetWDRAttr) W(IMP_ISP_Tuning_GetWDRAttr)
#endif
#ifdef H_RAWDRC
W(IMP_ISP_Tuning_SetRawDRC) W(IMP_ISP_Tuning_GetRawDRC)
#endif
#ifdef H_ANTIFOG
W(IMP_ISP_Tuning_SetAntiFogAttr)
#endif
#ifdef H_DPSTR
W(IMP_ISP_Tuning_SetDPStrength)
#endif
#ifdef H_COLORFX
W(IMP_ISP_Tuning_SetColorfxMode) W(IMP_ISP_Tuning_GetColorfxMode)
#endif
#ifdef H_SCENE
W(IMP_ISP_Tuning_SetSceneMode) W(IMP_ISP_Tuning_GetSceneMode)
#endif
#ifdef H_MESH
W(IMP_ISP_Tuning_SetMeshShadingScale)
#endif
#ifdef H_HVFLIP2
W(IMP_ISP_Tuning_SetISPHVflip) W(IMP_ISP_Tuning_GetISPHVflip)
#endif
#ifdef H_DRCSTR
W(IMP_ISP_Tuning_SetDRC_Strength)
#endif
#ifdef H_DRCEN
W(IMP_ISP_Tuning_EnableDRC)
#endif
#ifdef H_DEFOG
W(IMP_ISP_Tuning_EnableDefog) W(IMP_ISP_Tuning_SetDefog_Strength) W(IMP_ISP_Tuning_GetDefog_Strength)
#endif
#ifdef H_DPC
W(IMP_ISP_Tuning_SetDPC_Strength)
#endif
#if defined(H_MODCTL) && !defined(PLATFORM_T41)
W(IMP_ISP_Tuning_SetModuleControl) W(IMP_ISP_Tuning_GetModuleControl)
#endif
#ifdef H_BCSHHUE
W(IMP_ISP_Tuning_SetBcshHue) W(IMP_ISP_Tuning_GetBcshHue)
#endif
#ifdef H_CCM
W(IMP_ISP_Tuning_SetCCMAttr) W(IMP_ISP_Tuning_GetCCMAttr)
#endif
#ifdef H_CSC
W(IMP_ISP_Tuning_SetCsc_Attr) W(IMP_ISP_Tuning_GetCsc_Attr)
#endif
#ifdef H_FCROP
W(IMP_ISP_Tuning_SetFrontCrop) W(IMP_ISP_Tuning_GetFrontCrop)
#endif
#ifdef H_MASK
W(IMP_ISP_Tuning_SetMask) W(IMP_ISP_Tuning_GetMask)
#endif
#if defined(H_AUTOZOOM) && !defined(PLATFORM_T41)
W(IMP_ISP_Tuning_SetAutoZoom)
#endif
#if defined(H_SCALERLV) && !defined(PLATFORM_T41)
W(IMP_ISP_Tuning_SetScalerLv)
#endif
#ifdef H_AEATTR
W(IMP_ISP_Tuning_SetAeAttr) W(IMP_ISP_Tuning_GetAeAttr)
#endif
#ifdef H_AWBCT
W(IMP_ISP_Tuning_SetAwbCt) W(IMP_ISP_Tuning_GetAWBCt) W(IMP_ISP_Tuning_SetWB_ALGO)
#endif
#ifdef H_HVFLIPE
W(IMP_ISP_Tuning_SetHVFLIP) W(IMP_ISP_Tuning_GetHVFlip)
#endif
#ifdef H_BLC
W(IMP_ISP_Tuning_SetBacklightComp)
#endif
#ifdef H_SENSFLIP
W(IMP_ISP_Tuning_SetSensorHflip) W(IMP_ISP_Tuning_SetSensorVflip)
#endif

/* OpenIMP extensions on T10/T20/T21 (crop + CSC, branch t1x-crop-csc):
 * not in the vendor headers there, so layouts are defined here. */
#ifdef H_EXT_FCROP
typedef struct { unsigned char fcrop_enable; unsigned int fcrop_top, fcrop_left, fcrop_width, fcrop_height; } ExtFrontCrop;
int IMP_ISP_Tuning_SetFrontCrop(ExtFrontCrop *);
int IMP_ISP_Tuning_GetFrontCrop(ExtFrontCrop *);
W(IMP_ISP_Tuning_SetFrontCrop) W(IMP_ISP_Tuning_GetFrontCrop)
#endif
#ifdef H_EXT_CSC
/* mode + 9 coef + 2 offsets + 2 Y clip + 2 UV clip (vendor T31 layout) */
typedef struct { int32_t w[16]; } ExtCscAttr;
int IMP_ISP_Tuning_SetCsc_Attr(ExtCscAttr *);
int IMP_ISP_Tuning_GetCsc_Attr(ExtCscAttr *);
W(IMP_ISP_Tuning_SetCsc_Attr) W(IMP_ISP_Tuning_GetCsc_Attr)
#endif

/* ------------------------------------------------------------- globals */
static const char *outdir = ".";
static FILE *sum;
static int g_sw, g_sh, g_night, g_extra_ms, g_risky;
static IMPSensorInfo g_sensor;

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
    if (sum) { va_start(ap, fmt); vfprintf(sum, fmt, ap); va_end(ap); fflush(sum); }
    fflush(stdout);
}

/* getters that exist in OpenIMP but not in every vendor header */
static int getu32(const char *sym, unsigned int *v)
{
    static void *h;
    int (*f)(unsigned int *);

    if (!h) h = dlopen("libimp.so", RTLD_NOW);
    if (!h) return CASE_NA;
    f = (int (*)(unsigned int *))dlsym(h, sym);
    if (!f) return CASE_NA;
    return f(v);
}

typedef int (*casefn)(int ph, int arg, char *info, size_t n);

/* ------------------------------------------------------- common cases */
/* unsigned-char attribute with Get/Set pair */
#define U8CASE(fn, SETF, GETF) \
static int fn(int ph, int arg, char *info, size_t n) { \
    static unsigned char old; unsigned char cur = 0; int r; \
    if (!HAVE(SETF) || !HAVE(GETF)) return CASE_NA; \
    if (ph) { SETU8(SETF, old); return 0; } \
    CALLP(GETF, &old); r = SETU8(SETF, arg); CALLP(GETF, &cur); \
    snprintf(info, n, "%u->%u", old, cur); return r; }

U8CASE(c_bright, IMP_ISP_Tuning_SetBrightness, IMP_ISP_Tuning_GetBrightness)
U8CASE(c_contrast, IMP_ISP_Tuning_SetContrast, IMP_ISP_Tuning_GetContrast)
U8CASE(c_sat, IMP_ISP_Tuning_SetSaturation, IMP_ISP_Tuning_GetSaturation)
U8CASE(c_sharp, IMP_ISP_Tuning_SetSharpness, IMP_ISP_Tuning_GetSharpness)
#if defined(H_BCSHHUE) || defined(PLATFORM_T41)
U8CASE(c_hue, IMP_ISP_Tuning_SetBcshHue, IMP_ISP_Tuning_GetBcshHue)

/* brightness+contrast+saturation+hue+sharpness at once */
static int c_bcsh(int ph, int arg, char *info, size_t n)
{
    static unsigned char ob, oc, os, oh, osh;
    unsigned char b = 0, c = 0, s = 0, h = 0, sh = 0;
    int r;

    (void)arg;
    if (!HAVE(IMP_ISP_Tuning_SetBcshHue) || !HAVE(IMP_ISP_Tuning_SetBrightness)) return CASE_NA;
    if (ph) {
        SETU8(IMP_ISP_Tuning_SetBrightness, ob); SETU8(IMP_ISP_Tuning_SetContrast, oc);
        SETU8(IMP_ISP_Tuning_SetSaturation, os); SETU8(IMP_ISP_Tuning_SetBcshHue, oh);
        SETU8(IMP_ISP_Tuning_SetSharpness, osh);
        return 0;
    }
    CALLP(IMP_ISP_Tuning_GetBrightness, &ob); CALLP(IMP_ISP_Tuning_GetContrast, &oc);
    CALLP(IMP_ISP_Tuning_GetSaturation, &os); CALLP(IMP_ISP_Tuning_GetBcshHue, &oh);
    CALLP(IMP_ISP_Tuning_GetSharpness, &osh);
    r = SETU8(IMP_ISP_Tuning_SetBrightness, 90);
    r |= SETU8(IMP_ISP_Tuning_SetContrast, 170);
    r |= SETU8(IMP_ISP_Tuning_SetSaturation, 200);
    r |= SETU8(IMP_ISP_Tuning_SetBcshHue, 160);
    r |= SETU8(IMP_ISP_Tuning_SetSharpness, 200);
    CALLP(IMP_ISP_Tuning_GetBrightness, &b); CALLP(IMP_ISP_Tuning_GetContrast, &c);
    CALLP(IMP_ISP_Tuning_GetSaturation, &s); CALLP(IMP_ISP_Tuning_GetBcshHue, &h);
    CALLP(IMP_ISP_Tuning_GetSharpness, &sh);
    snprintf(info, n, "b%u c%u s%u h%u sh%u (was %u %u %u %u %u)", b, c, s, h, sh, ob, oc, os, oh, osh);
    return r;
}
#endif

/* anti-flicker: arg 0 off, 1 50 Hz, 2 60 Hz */
static int c_aflick(int ph, int arg, char *info, size_t n)
{
#ifdef PLATFORM_T41
    static IMPISPAntiflickerAttr old;
    IMPISPAntiflickerAttr a, cur;

    if (!HAVE(IMP_ISP_Tuning_SetAntiFlickerAttr)) return CASE_NA;
    if (ph) { IMP_ISP_Tuning_SetAntiFlickerAttr(IMPVI_MAIN, &old); return 0; }
    memset(&old, 0, sizeof(old)); IMP_ISP_Tuning_GetAntiFlickerAttr(IMPVI_MAIN, &old);
    memset(&a, 0, sizeof(a));
    a.mode = arg ? IMPISP_ANTIFLICKER_NORMAL_MODE : IMPISP_ANTIFLICKER_DISABLE_MODE;
    a.freq = arg == 2 ? 60 : 50;
    { int r = IMP_ISP_Tuning_SetAntiFlickerAttr(IMPVI_MAIN, &a);
      memset(&cur, 0, sizeof(cur)); IMP_ISP_Tuning_GetAntiFlickerAttr(IMPVI_MAIN, &cur);
      snprintf(info, n, "mode %d/%u (was %d/%u)", cur.mode, cur.freq, old.mode, old.freq);
      return r; }
#else
    static unsigned int old;
    unsigned int cur = 99;
    int r;

    if (!HAVE(IMP_ISP_Tuning_SetAntiFlickerAttr)) return CASE_NA;
    if (ph) { IMP_ISP_Tuning_SetAntiFlickerAttr(old); return 0; }
    IMP_ISP_Tuning_GetAntiFlickerAttr((void *)&old);
    r = IMP_ISP_Tuning_SetAntiFlickerAttr(arg);
    IMP_ISP_Tuning_GetAntiFlickerAttr((void *)&cur);
    snprintf(info, n, "%u->%u", old, cur);
    return r;
#endif
}

/* running mode: arg 0 day, 1 night; restore = mode at start */
static int c_runmode(int ph, int arg, char *info, size_t n)
{
    unsigned int m = ph ? (unsigned)g_night : (unsigned)arg, cur = 99;
    int r;

    if (!HAVE(IMP_ISP_Tuning_SetISPRunningMode)) return CASE_NA;
#ifdef PLATFORM_T41
    r = IMP_ISP_Tuning_SetISPRunningMode(IMPVI_MAIN, (void *)&m);
    IMP_ISP_Tuning_GetISPRunningMode(IMPVI_MAIN, (void *)&cur);
#else
    r = IMP_ISP_Tuning_SetISPRunningMode(m);
    IMP_ISP_Tuning_GetISPRunningMode((void *)&cur);
#endif
    if (!ph) snprintf(info, n, "mode now %u", cur);
    return ph ? 0 : r;
}

/* sensor fps: arg = new fps */
static int c_fps(int ph, int arg, char *info, size_t n)
{
    static unsigned int on = 25, od = 1;
    unsigned int cn = 0, cd = 0;
    int r;

    if (!HAVE(IMP_ISP_Tuning_SetSensorFPS)) return CASE_NA;
#ifdef PLATFORM_T41
    { IMPISPSensorFps f;
      if (ph) { f.num = on; f.den = od; IMP_ISP_Tuning_SetSensorFPS(IMPVI_MAIN, &f); return 0; }
      memset(&f, 0, sizeof(f)); IMP_ISP_Tuning_GetSensorFPS(IMPVI_MAIN, &f); on = f.num; od = f.den;
      f.num = arg; f.den = 1;
      r = IMP_ISP_Tuning_SetSensorFPS(IMPVI_MAIN, &f);
      memset(&f, 0, sizeof(f)); IMP_ISP_Tuning_GetSensorFPS(IMPVI_MAIN, &f); cn = f.num; cd = f.den; }
#else
    if (ph) { IMP_ISP_Tuning_SetSensorFPS(on, od); return 0; }
    IMP_ISP_Tuning_GetSensorFPS(&on, &od);
    r = IMP_ISP_Tuning_SetSensorFPS(arg, 1);
    IMP_ISP_Tuning_GetSensorFPS(&cn, &cd);
#endif
    snprintf(info, n, "%u/%u -> %u/%u (picture only saved, rate not measured)", on, od, cn, cd);
    return r;
}

/* FS ch1 scaler crop: arg 0 = mid 50 %, 1 = top-left 50 % (source coordinates) */
static int c_fs1crop(int ph, int arg, char *info, size_t n)
{
    static IMPFSChnAttr old;
    IMPFSChnAttr a;
    int r;

    if (!HAVE(IMP_FrameSource_SetChnAttr)) return CASE_NA;
    if (ph) { IMP_FrameSource_SetChnAttr(1, &old); return 0; }
    memset(&old, 0, sizeof(old));
    IMP_FrameSource_GetChnAttr(1, &old);
    a = old;
    a.crop.enable = 1;
    a.crop.left = arg ? 0 : (g_sw / 4) & ~1; a.crop.top = arg ? 0 : (g_sh / 4) & ~1;
    a.crop.width = (g_sw / 2) & ~1; a.crop.height = (g_sh / 2) & ~1;
    r = IMP_FrameSource_SetChnAttr(1, &a);
    snprintf(info, n, "crop %d,%d %dx%d (was en=%d)", a.crop.left, a.crop.top, a.crop.width, a.crop.height, old.crop.enable);
    return r;
}

/* ------------------------------------------------------ classic SoCs */
#ifndef PLATFORM_T41

/* ISP flip: arg bit0 = H, bit1 = V (IMPISPTuningOpsMode per axis) */
static int c_flip(int ph, int arg, char *info, size_t n)
{
    static unsigned int oh, ov;
    unsigned int ch = 9, cv = 9;
    int r;

    if (!HAVE(IMP_ISP_Tuning_SetISPHflip) || !HAVE(IMP_ISP_Tuning_SetISPVflip)) return CASE_NA;
    if (ph) { IMP_ISP_Tuning_SetISPHflip(oh); IMP_ISP_Tuning_SetISPVflip(ov); return 0; }
    IMP_ISP_Tuning_GetISPHflip((void *)&oh); IMP_ISP_Tuning_GetISPVflip((void *)&ov);
    r = IMP_ISP_Tuning_SetISPHflip(arg & 1);
    r |= IMP_ISP_Tuning_SetISPVflip((arg >> 1) & 1);
    IMP_ISP_Tuning_GetISPHflip((void *)&ch); IMP_ISP_Tuning_GetISPVflip((void *)&cv);
    snprintf(info, n, "h%u v%u (was %u %u)", ch, cv, oh, ov);
    return r;
}

#ifdef H_HVFLIP2
static int c_flip2(int ph, int arg, char *info, size_t n)
{
    static unsigned int oh, ov;
    unsigned int ch = 9, cv = 9;
    int r;

    if (!HAVE(IMP_ISP_Tuning_SetISPHVflip)) return CASE_NA;
    if (ph) { IMP_ISP_Tuning_SetISPHVflip(oh, ov); return 0; }
    IMP_ISP_Tuning_GetISPHVflip((void *)&oh, (void *)&ov);
    r = IMP_ISP_Tuning_SetISPHVflip(arg & 1, (arg >> 1) & 1);
    IMP_ISP_Tuning_GetISPHVflip((void *)&ch, (void *)&cv);
    snprintf(info, n, "h%u v%u (was %u %u)", ch, cv, oh, ov);
    return r;
}
#endif

#ifdef H_HVFLIPE
/* SetHVFLIP enum: 0 normal 1 H 2 V 3 HV */
static int c_flipe(int ph, int arg, char *info, size_t n)
{
    static unsigned int old;
    unsigned int cur = 99;
    int r;

    if (!HAVE(IMP_ISP_Tuning_SetHVFLIP)) return CASE_NA;
    if (ph) { IMP_ISP_Tuning_SetHVFLIP(old); return 0; }
    if (HAVE(IMP_ISP_Tuning_GetHVFlip)) IMP_ISP_Tuning_GetHVFlip((void *)&old);
    r = IMP_ISP_Tuning_SetHVFLIP(arg);
    if (HAVE(IMP_ISP_Tuning_GetHVFlip)) IMP_ISP_Tuning_GetHVFlip((void *)&cur);
    snprintf(info, n, "%u->%u", old, cur);
    return r;
}
#endif

#ifdef H_SENSFLIP
static int c_sflip(int ph, int arg, char *info, size_t n)
{
    int r;

    if (!HAVE(IMP_ISP_Tuning_SetSensorHflip) || !HAVE(IMP_ISP_Tuning_SetSensorVflip)) return CASE_NA;
    if (ph) { IMP_ISP_Tuning_SetSensorHflip(0); IMP_ISP_Tuning_SetSensorVflip(0); return 0; }
    r = IMP_ISP_Tuning_SetSensorHflip(arg & 1);
    r |= IMP_ISP_Tuning_SetSensorVflip((arg >> 1) & 1);
    snprintf(info, n, "sensor h%d v%d (no readback in vendor API; default 0)", arg & 1, (arg >> 1) & 1);
    return r;
}
#endif

/* gamma: arg 0 steep (x^0.3), 1 linear */
static int c_gamma(int ph, int arg, char *info, size_t n)
{
    static IMPISPGamma old;
    IMPISPGamma g, cur;
    int k, r;

    if (!HAVE(IMP_ISP_Tuning_SetGamma)) return CASE_NA;
    if (ph) { IMP_ISP_Tuning_SetGamma(&old); return 0; }
    memset(&old, 0, sizeof(old)); IMP_ISP_Tuning_GetGamma(&old);
    for (k = 0; k < 129; k++)
        g.gamma[k] = arg ? (uint16_t)(k * 4095 / 128) : (uint16_t)(pow(k / 128.0, 0.3) * 4095 + 0.5);
    r = IMP_ISP_Tuning_SetGamma(&g);
    memset(&cur, 0, sizeof(cur)); IMP_ISP_Tuning_GetGamma(&cur);
    snprintf(info, n, "g[32]=%u g[64]=%u g[128]=%u (was %u %u %u)", cur.gamma[32], cur.gamma[64], cur.gamma[128],
             old.gamma[32], old.gamma[64], old.gamma[128]);
    return r;
}

#ifdef H_AECOMP
/* AE compensation: arg = value (default 128) */
static int c_aecomp(int ph, int arg, char *info, size_t n)
{
    static int old;
    int cur = -1, r;

    if (!HAVE(IMP_ISP_Tuning_SetAeComp)) return CASE_NA;
    if (ph) { IMP_ISP_Tuning_SetAeComp(old); return 0; }
    IMP_ISP_Tuning_GetAeComp(&old);
    r = IMP_ISP_Tuning_SetAeComp(arg);
    IMP_ISP_Tuning_GetAeComp(&cur);
    snprintf(info, n, "%d->%d", old, cur);
    return r;
}
#endif

/* manual exposure time via SetExpr: arg = microseconds */
static int c_expr(int ph, int arg, char *info, size_t n)
{
    static IMPISPExpr old;
    IMPISPExpr e, cur;
    int r;

    static uint32_t oag, odg; static int caps; static int rca, rcd; uint32_t nag = 0, ndg = 0;
    if (!HAVE(IMP_ISP_Tuning_SetExpr)) return CASE_NA;
    if (ph) {
        memset(&e, 0, sizeof(e));
        e.s_attr.mode = ISP_CORE_EXPR_MODE_AUTO; e.s_attr.unit = ISP_CORE_EXPR_UNIT_US;
        e.s_attr.time = old.g_attr.integration_time;
        IMP_ISP_Tuning_SetExpr(&e);
        if (caps) { IMP_ISP_Tuning_SetMaxAgain(oag); IMP_ISP_Tuning_SetMaxDgain(odg); caps = 0; }
        return 0;
    }
    memset(&old, 0, sizeof(old)); IMP_ISP_Tuning_GetExpr(&old);
    /* with a manual time the AE still moves the gain to hold the brightness: pin the gain caps to 1 for the case (restored in the end phase) */
    if (HAVE(IMP_ISP_Tuning_GetMaxAgain) && HAVE(IMP_ISP_Tuning_SetMaxAgain) && HAVE(IMP_ISP_Tuning_GetMaxDgain) && HAVE(IMP_ISP_Tuning_SetMaxDgain) &&
        IMP_ISP_Tuning_GetMaxAgain(&oag) == 0 && IMP_ISP_Tuning_GetMaxDgain(&odg) == 0) {
        rca = IMP_ISP_Tuning_SetMaxAgain(1); rcd = IMP_ISP_Tuning_SetMaxDgain(1); caps = 1; usleep(300000);
    }
    memset(&e, 0, sizeof(e));
    e.s_attr.mode = ISP_CORE_EXPR_MODE_MANUAL; e.s_attr.unit = ISP_CORE_EXPR_UNIT_US;
    e.s_attr.time = (uint16_t)arg;
    r = IMP_ISP_Tuning_SetExpr(&e);
    memset(&cur, 0, sizeof(cur)); IMP_ISP_Tuning_GetExpr(&cur);
    if (caps) { IMP_ISP_Tuning_GetMaxAgain(&nag); IMP_ISP_Tuning_GetMaxDgain(&ndg); }
    snprintf(info, n, "mode %d it=%u lines (min %u max %u, %u us/line) was it=%u mode %d; gain caps %u/%u -> set 1/1 ret %d/%d read %u/%u", cur.g_attr.mode,
             cur.g_attr.integration_time, cur.g_attr.integration_time_min, cur.g_attr.integration_time_max,
             cur.g_attr.one_line_expr_in_us, old.g_attr.integration_time, old.g_attr.mode, oag, odg, rca, rcd, nag, ndg);
    return r;
}

/* white balance: arg 0 auto, 1 manual R high, 2 manual B high, 100+m preset mode m */
static int c_awb(int ph, int arg, char *info, size_t n)
{
    static IMPISPWB old;
    IMPISPWB w, cur;
    int r;

    if (!HAVE(IMP_ISP_Tuning_SetWB)) return CASE_NA;
    if (ph) { w = old; w.mode = ISP_CORE_WB_MODE_AUTO; IMP_ISP_Tuning_SetWB(&w); return 0; }
    memset(&old, 0, sizeof(old)); IMP_ISP_Tuning_GetWB(&old);
    w = old;
    if (arg == 0) {
        w.mode = ISP_CORE_WB_MODE_AUTO;
    } else if (arg < 100) {
        unsigned int rg = old.rgain ? old.rgain : 0x100, bg = old.bgain ? old.bgain : 0x100;
        w.mode = ISP_CORE_WB_MODE_MANUAL;
        if (arg == 1) rg = rg * 2; else bg = bg * 2;
        w.rgain = rg > 0xffff ? 0xffff : rg; w.bgain = bg > 0xffff ? 0xffff : bg;
    } else {
        w.mode = arg - 100;
    }
    r = IMP_ISP_Tuning_SetWB(&w);
    memset(&cur, 0, sizeof(cur)); IMP_ISP_Tuning_GetWB(&cur);
    snprintf(info, n, "mode %d r=%u b=%u (was mode %d r=%u b=%u; set r=%u b=%u)", cur.mode, cur.rgain, cur.bgain,
             old.mode, old.rgain, old.bgain, w.rgain, w.bgain);
    return r;
}

#ifdef H_AWBCT
/* arg = colour temperature in K */
static int c_awbct(int ph, int arg, char *info, size_t n)
{
    static unsigned int old;
    unsigned int ct = arg, cur = 0;
    int r;

    if (!HAVE(IMP_ISP_Tuning_SetAwbCt)) return CASE_NA;
    if (ph) {
        IMPISPWB w; memset(&w, 0, sizeof(w)); w.mode = ISP_CORE_WB_MODE_AUTO;
        if (HAVE(IMP_ISP_Tuning_SetWB)) IMP_ISP_Tuning_SetWB(&w);
        return 0;
    }
    if (HAVE(IMP_ISP_Tuning_GetAWBCt)) IMP_ISP_Tuning_GetAWBCt(&old);
    r = IMP_ISP_Tuning_SetAwbCt(&ct);
    if (HAVE(IMP_ISP_Tuning_GetAWBCt)) IMP_ISP_Tuning_GetAWBCt(&cur);
    snprintf(info, n, "ct %u->%u (restore: WB auto)", old, cur);
    return r;
}

/* AWB algorithm: arg 0 normal, 1 grayworld, 2 reweight (restore normal) */
static int c_wbalgo(int ph, int arg, char *info, size_t n)
{
    int r;

    if (!HAVE(IMP_ISP_Tuning_SetWB_ALGO)) return CASE_NA;
    if (ph) { IMP_ISP_Tuning_SetWB_ALGO(0); return 0; }
    r = IMP_ISP_Tuning_SetWB_ALGO(arg);
    snprintf(info, n, "algo %d (no readback; restore = normal)", arg);
    return r;
}
#endif

/* uint32 "strength"/limit setters; pairs below pick the symbols */
static unsigned int clampu(unsigned v) { return v; }

/* arg 0/255 */
#define STRCASE(fn, SETSYM, GETNAME, DEFAULT) \
static int fn(int ph, int arg, char *info, size_t n) { \
    static unsigned int old = DEFAULT; unsigned int cur = 0; int r, g; \
    if (!HAVE(SETSYM)) return CASE_NA; \
    if (ph) { SETSYM(old); return 0; } \
    g = getu32(GETNAME, &old); if (g) old = DEFAULT; \
    r = SETSYM(clampu(arg)); \
    g = getu32(GETNAME, &cur); \
    if (g) snprintf(info, n, "set %d (no readback; restore %u)", arg, old); \
    else snprintf(info, n, "%u->%u", old, cur); \
    return r; }

STRCASE(c_sinter, IMP_ISP_Tuning_SetSinterStrength, "IMP_ISP_Tuning_GetSinterStrength", 128)
STRCASE(c_temper, IMP_ISP_Tuning_SetTemperStrength, "IMP_ISP_Tuning_GetTemperStrength", 128)
STRCASE(c_hld, IMP_ISP_Tuning_SetHiLightDepress, "IMP_ISP_Tuning_GetHiLightDepress", 0)
#ifdef H_DPC
STRCASE(c_dpc, IMP_ISP_Tuning_SetDPC_Strength, "IMP_ISP_Tuning_GetDPC_Strength", 128)
#endif
#ifdef H_DPSTR
STRCASE(c_dpc, IMP_ISP_Tuning_SetDPStrength, "IMP_ISP_Tuning_GetDPStrength", 128)
#endif
#ifdef H_DRCSTR
STRCASE(c_drcstr, IMP_ISP_Tuning_SetDRC_Strength, "IMP_ISP_Tuning_GetDRC_Strength", 128)
#endif
#ifdef H_BLC
STRCASE(c_blc, IMP_ISP_Tuning_SetBacklightComp, "IMP_ISP_Tuning_GetBacklightComp", 0)
#endif

/* max analog / digital gain limit: arg bit 8 = digital; low byte = value
 * (vendor: 0 = 1x, 32 = 2x ...) */
static int c_maxgain(int ph, int arg, char *info, size_t n)
{
    static unsigned int oa, od;
    unsigned int ca = 0, cd = 0;
    int r = 0;

    if (!HAVE(IMP_ISP_Tuning_SetMaxAgain) || !HAVE(IMP_ISP_Tuning_SetMaxDgain)) return CASE_NA;
    if (ph) { IMP_ISP_Tuning_SetMaxAgain(oa); IMP_ISP_Tuning_SetMaxDgain(od); return 0; }
    IMP_ISP_Tuning_GetMaxAgain(&oa); IMP_ISP_Tuning_GetMaxDgain(&od);
    if (arg & 0x100) r = IMP_ISP_Tuning_SetMaxDgain(arg & 0xff);
    else r = IMP_ISP_Tuning_SetMaxAgain(arg & 0xff);
    IMP_ISP_Tuning_GetMaxAgain(&ca); IMP_ISP_Tuning_GetMaxDgain(&cd);
    snprintf(info, n, "again %u->%u dgain %u->%u", oa, ca, od, cd);
    return r;
}

#ifdef H_AEATTR
/* manual AE attr: arg 0 it short, 1 it long, 2 again low, 3 again high, 4 dgain high */
static int c_aeattr(int ph, int arg, char *info, size_t n)
{
    static IMPISPAEAttr old;
    IMPISPAEAttr a, cur;
    IMPISPExpr e;
    unsigned int it = 100;
    int r;

    if (!HAVE(IMP_ISP_Tuning_SetAeAttr)) return CASE_NA;
    if (ph) { IMP_ISP_Tuning_SetAeAttr(&old); return 0; }
    memset(&old, 0, sizeof(old)); IMP_ISP_Tuning_GetAeAttr(&old);
    memset(&e, 0, sizeof(e));
    if (HAVE(IMP_ISP_Tuning_GetExpr) && IMP_ISP_Tuning_GetExpr(&e) == 0 && e.g_attr.integration_time)
        it = e.g_attr.integration_time;
    a = old;
    switch (arg) {
    case 0: a.AeItManualEn = 1; a.AeIt = it / 8 ? it / 8 : 1; break;
    case 1: a.AeItManualEn = 1; a.AeIt = it * 3; break;
    case 2: a.AeAGainManualEn = 1; a.AeAGain = 1024; break;
    case 3: a.AeAGainManualEn = 1; a.AeAGain = 16 * 1024; break;
    default: a.AeDGainManualEn = 1; a.AeDGain = 8 * 1024; break;
    }
    r = IMP_ISP_Tuning_SetAeAttr(&a);
    memset(&cur, 0, sizeof(cur)); IMP_ISP_Tuning_GetAeAttr(&cur);
    snprintf(info, n, "it en%d=%u again en%d=%u dgain en%d=%u (auto it was %u lines)", cur.AeItManualEn, cur.AeIt,
             cur.AeAGainManualEn, cur.AeAGain, cur.AeDGainManualEn, cur.AeDGain, it);
    return r;
}
#endif

#ifdef H_MODCTL
/* module bypass bits, names differ per SoC */
# if defined(PLATFORM_T21)
#  define MODBITS(X) X(ccm, bitBypassCCM) X(gamma, bitBypassGAMMA) X(defog, bitBypassDEFOG) X(lsc, bitBypassLSC) \
    X(dpc, bitBypassDPC) X(sdns, bitBypassSDNS) X(mdns, bitBypassMDNS) X(sharpen, bitBypassYSHARPEN) X(adr, bitBypassADR)
# else
#  define MODBITS(X) X(ccm, bitBypassCCM) X(gamma, bitBypassGAMMA) X(defog, bitBypassDEFOG) X(lsc, bitBypassLSC) \
    X(dpc, bitBypassDPC) X(sdns, bitBypassSDNS) X(mdns, bitBypassMDNS) X(sharpen, bitBypassSP) X(adr, bitBypassADR) \
    X(ydns, bitBypassYDNS) X(bcsh, bitBypassBCSH)
# endif
#define XE(nm, bit) MB_##nm,
enum { MODBITS(XE) MB_N };
#define XC(nm, bit) case MB_##nm: m.bit = 1; break;
#define XU(nm, bit) case MB_N + MB_##nm: m.bit = 0; break;   /* unbypass-<nm>: clear the bit */
static int c_mod(int ph, int arg, char *info, size_t n)
{
    static IMPISPModuleCtl old;
    IMPISPModuleCtl m, cur;
    int r;

    if (!HAVE(IMP_ISP_Tuning_SetModuleControl)) return CASE_NA;
    if (ph) { IMP_ISP_Tuning_SetModuleControl(&old); return 0; }
    memset(&old, 0, sizeof(old)); IMP_ISP_Tuning_GetModuleControl(&old);
    m = old;
    switch (arg) { MODBITS(XC) MODBITS(XU) default: break; }
    unsigned int req = m.key;
    r = IMP_ISP_Tuning_SetModuleControl(&m);
    memset(&cur, 0, sizeof(cur)); IMP_ISP_Tuning_GetModuleControl(&cur);
    snprintf(info, n, "key 0x%08x -> 0x%08x (was 0x%08x) req 0x%08x ret %d", m.key, cur.key, old.key, req, r);
    return r;
}
#endif

#ifdef H_DRCEN
/* DRC on/off (restore: ModuleControl as before, else enable) */
static unsigned int drc_key; static int drc_have;
static int c_drcen(int ph, int arg, char *info, size_t n)
{
    int r;

    if (!HAVE(IMP_ISP_Tuning_EnableDRC)) return CASE_NA;
    if (ph) {
        IMP_ISP_Tuning_EnableDRC(1);
        return 0;
    }
    r = IMP_ISP_Tuning_EnableDRC(arg);
    snprintf(info, n, "EnableDRC(%d) (no readback; restore = enable)", arg);
    (void)drc_key; (void)drc_have;
    return r;
}
#endif

#ifdef H_DEFOG
static int c_defogen(int ph, int arg, char *info, size_t n)
{
    int r;

    if (!HAVE(IMP_ISP_Tuning_EnableDefog)) return CASE_NA;
    if (ph) { IMP_ISP_Tuning_EnableDefog(0); return 0; }
    r = IMP_ISP_Tuning_EnableDefog(arg);
    snprintf(info, n, "EnableDefog(%d) (no readback; restore = disable)", arg);
    return r;
}
static int c_defogstr(int ph, int arg, char *info, size_t n)
{
    static uint8_t old = 128;
    uint8_t v = (uint8_t)arg, cur = 0;
    int r;

    if (!HAVE(IMP_ISP_Tuning_SetDefog_Strength)) return CASE_NA;
    if (ph) { IMP_ISP_Tuning_SetDefog_Strength(&old); return 0; }
    if (HAVE(IMP_ISP_Tuning_GetDefog_Strength)) IMP_ISP_Tuning_GetDefog_Strength(&old);
    IMP_ISP_Tuning_EnableDefog(1);
    r = IMP_ISP_Tuning_SetDefog_Strength(&v);
    if (HAVE(IMP_ISP_Tuning_GetDefog_Strength)) IMP_ISP_Tuning_GetDefog_Strength(&cur);
    snprintf(info, n, "%u->%u (defog enabled for the case, disabled on restore)", old, cur);
    return r;
}
#endif

#ifdef H_ANTIFOG
/* arg 0 off 1 strong 2 medium 3 weak */
static int c_antifog(int ph, int arg, char *info, size_t n)
{
    int r;

    if (!HAVE(IMP_ISP_Tuning_SetAntiFogAttr)) return CASE_NA;
    if (ph) { IMP_ISP_Tuning_SetAntiFogAttr(ANTIFOG_DISABLE); return 0; }
    r = IMP_ISP_Tuning_SetAntiFogAttr(arg);
#ifdef PLATFORM_T30
    /* the firmware (antifog_set_preset, vendor and open alike) applies the preset only while the antifog mode is
     * already on: the first call after a disable switches the mode on, the second one takes the preset */
    if (!r && arg) r = IMP_ISP_Tuning_SetAntiFogAttr(arg);
#endif
    snprintf(info, n, "antifog %d (no readback; restore = disable)", arg);
    return r;
}
#endif

#ifdef H_RAWDRC
/* arg 0 disable, 1 manual strength 255, 2 unlimit */
static int c_rawdrc(int ph, int arg, char *info, size_t n)
{
    static IMPISPDrcAttr old;
    IMPISPDrcAttr a, cur;
    int r;

    if (!HAVE(IMP_ISP_Tuning_SetRawDRC)) return CASE_NA;
    if (ph) { IMP_ISP_Tuning_SetRawDRC(&old); return 0; }
    memset(&old, 0, sizeof(old)); IMP_ISP_Tuning_GetRawDRC(&old);
    a = old;
    if (arg == 0) a.mode = IMPISP_DRC_DISABLE;
    else if (arg == 1) { a.mode = IMPISP_DRC_MANUAL; a.drc_strength = 255; }
    else a.mode = IMPISP_DRC_UNLIMIT;
    r = IMP_ISP_Tuning_SetRawDRC(&a);
    memset(&cur, 0, sizeof(cur)); IMP_ISP_Tuning_GetRawDRC(&cur);
    snprintf(info, n, "mode %d str %u (was %d %u)", cur.mode, cur.drc_strength, old.mode, old.drc_strength);
    return r;
}
#endif

#ifdef H_WDRATTR
static int c_wdr(int ph, int arg, char *info, size_t n)
{
    static unsigned int old;
    unsigned int cur = 9;
    int r;

    if (!HAVE(IMP_ISP_Tuning_SetWDRAttr)) return CASE_NA;
    if (ph) { IMP_ISP_Tuning_SetWDRAttr(old); return 0; }
    IMP_ISP_Tuning_GetWDRAttr((void *)&old);
    r = IMP_ISP_Tuning_SetWDRAttr(arg);
    IMP_ISP_Tuning_GetWDRAttr((void *)&cur);
    snprintf(info, n, "%u->%u", old, cur);
    return r;
}
#endif

#ifdef H_COLORFX
/* arg = IMPISPColorfxMode value (1 bw 2 sepia 3 negative 9 vivid) */
static int c_colorfx(int ph, int arg, char *info, size_t n)
{
    static unsigned int old;
    unsigned int cur = 99;
    int r;

    if (!HAVE(IMP_ISP_Tuning_SetColorfxMode)) return CASE_NA;
    if (ph) { IMP_ISP_Tuning_SetColorfxMode(old); return 0; }
    IMP_ISP_Tuning_GetColorfxMode((void *)&old);
    r = IMP_ISP_Tuning_SetColorfxMode(arg);
    IMP_ISP_Tuning_GetColorfxMode((void *)&cur);
    snprintf(info, n, "%u->%u", old, cur);
    return r;
}
#endif
#ifdef H_SCENE
static int c_scene(int ph, int arg, char *info, size_t n)
{
    static unsigned int old;
    unsigned int cur = 99;
    int r;

    if (!HAVE(IMP_ISP_Tuning_SetSceneMode)) return CASE_NA;
    if (ph) { IMP_ISP_Tuning_SetSceneMode(old); return 0; }
    IMP_ISP_Tuning_GetSceneMode((void *)&old);
    r = IMP_ISP_Tuning_SetSceneMode(arg);
    IMP_ISP_Tuning_GetSceneMode((void *)&cur);
    snprintf(info, n, "%u->%u", old, cur);
    return r;
}
#endif
#ifdef H_MESH
static int c_mesh(int ph, int arg, char *info, size_t n)
{
    int r;

    if (!HAVE(IMP_ISP_Tuning_SetMeshShadingScale)) return CASE_NA;
    if (ph) { IMP_ISP_Tuning_SetMeshShadingScale(IMPISP_SHAD_SCALE_M); return 0; }
    r = IMP_ISP_Tuning_SetMeshShadingScale(arg);
    snprintf(info, n, "scale %d (no readback; restore assumes M)", arg);
    return r;
}
#endif

#if defined(H_FCROP) || defined(H_EXT_FCROP)
/* front crop: arg 0 mid 50 %, 1 top-left 50 % */
static int c_fcrop(int ph, int arg, char *info, size_t n)
{
#ifdef H_FCROP
    static IMPISPFrontCrop old;
    IMPISPFrontCrop fc, g;
#else
    static ExtFrontCrop old;
    ExtFrontCrop fc, g;
#endif
    int r;

    if (!HAVE(IMP_ISP_Tuning_SetFrontCrop)) return CASE_NA;
    if (ph) { IMP_ISP_Tuning_SetFrontCrop(&old); return 0; }
    memset(&old, 0, sizeof(old)); IMP_ISP_Tuning_GetFrontCrop(&old);
    memset(&fc, 0, sizeof(fc));
    fc.fcrop_enable = 1;
    fc.fcrop_top = arg ? 0 : (g_sh / 4) & ~1; fc.fcrop_left = arg ? 0 : (g_sw / 4) & ~1;
    fc.fcrop_width = (g_sw / 2) & ~1; fc.fcrop_height = (g_sh / 2) & ~1;
    r = IMP_ISP_Tuning_SetFrontCrop(&fc);
    memset(&g, 0, sizeof(g)); IMP_ISP_Tuning_GetFrontCrop(&g);
    snprintf(info, n, "en=%d %u,%u %ux%u", g.fcrop_enable, g.fcrop_left, g.fcrop_top, g.fcrop_width, g.fcrop_height);
    return r;
}
#endif

#if defined(H_CSC) || defined(H_EXT_CSC)
/* CSC mode 0..4; 4 = user matrix (BT601 full, U/V sign inverted -> visible hue swap) */
static int c_csc(int ph, int arg, char *info, size_t n)
{
    static const int32_t user[15] = {
        0x132, 0x259, 0x75, 0xad, 0x153, -0x200, -0x200, 0x1ad, 0x53,
        0x00, 0x80, 0x00, 0xff, 0x00, 0xff,
    };
    static int32_t old[16];
    int32_t a[16], b[16];
    int r;

    if (!HAVE(IMP_ISP_Tuning_SetCsc_Attr)) return CASE_NA;
    if (ph) { IMP_ISP_Tuning_SetCsc_Attr((void *)old); return 0; }
    memset(old, 0, sizeof(old)); IMP_ISP_Tuning_GetCsc_Attr((void *)old);
    memset(a, 0, sizeof(a)); a[0] = arg;
    if (arg == 4) memcpy(&a[1], user, sizeof(user));
    r = IMP_ISP_Tuning_SetCsc_Attr((void *)a);
    memset(b, 0, sizeof(b)); IMP_ISP_Tuning_GetCsc_Attr((void *)b);
    snprintf(info, n, "mode %d (was %d) coef0 %d", b[0], old[0], b[1]);
    return r;
}
#endif

#ifdef H_CCM
/* arg 0 swap R/B, 1 mono (all rows 1/3) */
static int c_ccm(int ph, int arg, char *info, size_t n)
{
    static IMPISPCCMAttr old;
#ifdef H_MODCTL
    static IMPISPModuleCtl omc;       /* the IQ file can ship the CCM bypassed (T31 sc2336: key 0x42209, bit 9): a manual matrix has no effect then, so the case switches the CCM on and restores the key */
    static int mc_ok;
#endif
    IMPISPCCMAttr a, cur;
    static const float swap[9] = { 0, 0, 1, 0, 1, 0, 1, 0, 0 };
    int r, k;

    if (!HAVE(IMP_ISP_Tuning_SetCCMAttr)) return CASE_NA;
    if (ph) {
        IMP_ISP_Tuning_SetCCMAttr(&old);
#ifdef H_MODCTL
        if (mc_ok) IMP_ISP_Tuning_SetModuleControl(&omc);
        mc_ok = 0;
#endif
        return 0;
    }
#ifdef H_MODCTL
    if (HAVE(IMP_ISP_Tuning_GetModuleControl) && HAVE(IMP_ISP_Tuning_SetModuleControl)) {
        IMPISPModuleCtl mc;
        memset(&omc, 0, sizeof(omc)); mc_ok = IMP_ISP_Tuning_GetModuleControl(&omc) == 0;
        if (mc_ok && omc.bitBypassCCM) { mc = omc; mc.bitBypassCCM = 0; IMP_ISP_Tuning_SetModuleControl(&mc); usleep(300000); }
    }
#endif
    memset(&old, 0, sizeof(old)); IMP_ISP_Tuning_GetCCMAttr(&old);
    memset(&a, 0, sizeof(a));
    a.ManualEn = IMPISP_TUNING_OPS_MODE_ENABLE; a.SatEn = IMPISP_TUNING_OPS_MODE_DISABLE;
    for (k = 0; k < 9; k++) a.ColorMatrix[k] = arg ? 1.0f / 3 : swap[k];
    r = IMP_ISP_Tuning_SetCCMAttr(&a);
    memset(&cur, 0, sizeof(cur)); IMP_ISP_Tuning_GetCCMAttr(&cur);
    snprintf(info, n, "manual=%d m0=%.2f m4=%.2f m8=%.2f (was manual=%d)%s", cur.ManualEn, cur.ColorMatrix[0],
             cur.ColorMatrix[4], cur.ColorMatrix[8], old.ManualEn,
#ifdef H_MODCTL
             mc_ok && omc.bitBypassCCM ? " ccm-was-bypassed" : "");
#else
             "");
#endif
    return r;
}
#endif

#ifdef H_MASK
/* privacy mask: solid red block, 50 % of the picture, on ch0 and ch1 */
static int c_mask(int ph, int arg, char *info, size_t n)
{
    static IMPISPMASKAttr old;
    IMPISPMASKAttr a, cur;
    int r;

    (void)arg;
    if (!HAVE(IMP_ISP_Tuning_SetMask)) return CASE_NA;
    if (ph) { IMP_ISP_Tuning_SetMask(&old); return 0; }
    memset(&old, 0, sizeof(old)); IMP_ISP_Tuning_GetMask(&old);
    a = old;
    a.mask_type = IMPISP_MASK_TYPE_RGB;
    a.chn0[0].mask_en = 1; a.chn0[0].mask_pos_left = (g_sw / 4) & ~1; a.chn0[0].mask_pos_top = (g_sh / 4) & ~1;
    a.chn0[0].mask_width = (g_sw / 2) & ~1; a.chn0[0].mask_height = (g_sh / 2) & ~1;
    a.chn0[0].mask_value.mask_rgb.Red = 255; a.chn0[0].mask_value.mask_rgb.Green = 0; a.chn0[0].mask_value.mask_rgb.Blue = 0;
    a.chn1[0].mask_en = 1; a.chn1[0].mask_pos_left = SUB_W / 4; a.chn1[0].mask_pos_top = SUB_H / 4;
    a.chn1[0].mask_width = SUB_W / 2; a.chn1[0].mask_height = SUB_H / 2;
    a.chn1[0].mask_value.mask_rgb.Red = 255; a.chn1[0].mask_value.mask_rgb.Green = 0; a.chn1[0].mask_value.mask_rgb.Blue = 0;
    r = IMP_ISP_Tuning_SetMask(&a);
    memset(&cur, 0, sizeof(cur)); IMP_ISP_Tuning_GetMask(&cur);
    snprintf(info, n, "chn0[0] en=%d chn1[0] en=%d %ux%u@%u,%u", cur.chn0[0].mask_en, cur.chn1[0].mask_en,
             cur.chn1[0].mask_width, cur.chn1[0].mask_height, cur.chn1[0].mask_pos_left, cur.chn1[0].mask_pos_top);
    return r;
}
#endif

#ifdef H_AUTOZOOM
/* ePTZ: crop 50 % mid of the sensor picture on ch1, scaled to 640x360 */
static int c_autozoom(int ph, int arg, char *info, size_t n)
{
    IMPISPAutoZoom z;
    int r;

    (void)arg;
    if (!HAVE(IMP_ISP_Tuning_SetAutoZoom)) return CASE_NA;
    memset(&z, 0, sizeof(z));
    z.chan = 1; z.scaler_enable = 1; z.scaler_outwidth = SUB_W; z.scaler_outheight = SUB_H;
    if (ph) { z.crop_enable = 0; IMP_ISP_Tuning_SetAutoZoom(&z); return 0; }
    z.crop_enable = 1; z.crop_left = (g_sw / 4) & ~1; z.crop_top = (g_sh / 4) & ~1;
    z.crop_width = (g_sw / 2) & ~1; z.crop_height = (g_sh / 2) & ~1;
    r = IMP_ISP_Tuning_SetAutoZoom(&z);
    snprintf(info, n, "crop %d,%d %dx%d (no readback; restore = crop off)", z.crop_left, z.crop_top, z.crop_width, z.crop_height);
    return r;
}
#endif

#ifdef H_SCALERLV
/* scaler quality level on ch1: arg = level 0..128 */
static int c_scalerlv(int ph, int arg, char *info, size_t n)
{
    IMPISPScalerLv s;
    int r;

    if (!HAVE(IMP_ISP_Tuning_SetScalerLv)) return CASE_NA;
    memset(&s, 0, sizeof(s));
    s.channel = 1; s.method = IMP_ISP_SCALER_METHOD_FIXED_WEIGHT; s.level = ph ? 128 : arg;
    if (ph) { s.method = IMP_ISP_SCALER_METHOD_FITTING_CURVE; IMP_ISP_Tuning_SetScalerLv(&s); return 0; }
    r = IMP_ISP_Tuning_SetScalerLv(&s);
    snprintf(info, n, "ch1 fixed-weight level %d (no readback; restore = fitting curve)", arg);
    return r;
}
#endif

static int c_ispbypass(int ph, int arg, char *info, size_t n)
{
    int r;

    (void)arg;
    if (!HAVE(IMP_ISP_Tuning_SetISPBypass)) return CASE_NA;
    if (ph) { IMP_ISP_Tuning_SetISPBypass(IMPISP_TUNING_OPS_MODE_DISABLE); return 0; }
    r = IMP_ISP_Tuning_SetISPBypass(IMPISP_TUNING_OPS_MODE_ENABLE);
    snprintf(info, n, "bypass on (restore = off)");
    return r;
}

#else /* ------------------------------------------------------------ T41 */

/* flips: arg bit 0 H, bit 1 V; arg bit 8 = ISP instead of sensor */
static int c_flip(int ph, int arg, char *info, size_t n)
{
    static IMPISPHVFLIPAttr old;
    IMPISPHVFLIPAttr a, cur;
    int r, k, m = arg & 3;

    if (!HAVE(IMP_ISP_Tuning_SetHVFLIP)) return CASE_NA;
    if (ph) { IMP_ISP_Tuning_SetHVFLIP(IMPVI_MAIN, &old); return 0; }
    memset(&old, 0, sizeof(old)); IMP_ISP_Tuning_GetHVFLIP(IMPVI_MAIN, &old);
    a = old;
    if (arg & 0x100) for (k = 0; k < 3; k++) a.isp_mode[k] = m; else a.sensor_mode = m;
    r = IMP_ISP_Tuning_SetHVFLIP(IMPVI_MAIN, &a);
    memset(&cur, 0, sizeof(cur)); IMP_ISP_Tuning_GetHVFLIP(IMPVI_MAIN, &cur);
    snprintf(info, n, "sensor %d isp %d/%d/%d (was %d %d)", cur.sensor_mode, cur.isp_mode[0], cur.isp_mode[1],
             cur.isp_mode[2], old.sensor_mode, old.isp_mode[0]);
    return r;
}

/* gamma: arg 0 steep, 1 linear */
static int c_gamma(int ph, int arg, char *info, size_t n)
{
    static IMPISPGammaAttr old;
    IMPISPGammaAttr g, cur;
    int k, r;

    if (!HAVE(IMP_ISP_Tuning_SetGammaAttr)) return CASE_NA;
    if (ph) { IMP_ISP_Tuning_SetGammaAttr(IMPVI_MAIN, &old); return 0; }
    memset(&old, 0, sizeof(old)); IMP_ISP_Tuning_GetGammaAttr(IMPVI_MAIN, &old);
    memset(&g, 0, sizeof(g));
    g.Curve_type = IMP_ISP_GAMMA_CURVE_USER;
    for (k = 0; k < 129; k++)
        g.gamma[k] = arg ? (uint16_t)(k * 4095 / 128) : (uint16_t)(pow(k / 128.0, 0.3) * 4095 + 0.5);
    r = IMP_ISP_Tuning_SetGammaAttr(IMPVI_MAIN, &g);
    memset(&cur, 0, sizeof(cur)); IMP_ISP_Tuning_GetGammaAttr(IMPVI_MAIN, &cur);
    snprintf(info, n, "type %d g[64]=%u (was type %d g[64]=%u)", cur.Curve_type, cur.gamma[64], old.Curve_type, old.gamma[64]);
    return r;
}

/* AE scene attr: arg 0/1 = target compensation low/high; 2 = HLC on, 3 = BLC on */
static int c_aescene(int ph, int arg, char *info, size_t n)
{
    static IMPISPAEScenceAttr old;
    IMPISPAEScenceAttr a, cur;
    int r;

    if (!HAVE(IMP_ISP_Tuning_SetAeScenceAttr)) return CASE_NA;
    if (ph) { IMP_ISP_Tuning_SetAeScenceAttr(IMPVI_MAIN, &old); return 0; }
    memset(&old, 0, sizeof(old)); IMP_ISP_Tuning_GetAeScenceAttr(IMPVI_MAIN, &old);
    a = old;
    if (arg < 2) { a.AeTargetCompEn = TISP_AE_SCENCE_GLOBAL_ENABLE; a.AeTargetComp = arg ? 200 : 60; }
    else if (arg == 2) { a.AeHLCEn = TISP_AE_SCENCE_GLOBAL_ENABLE; a.AeHLCStrength = 255; }
    else { a.AeBLCEn = TISP_AE_SCENCE_GLOBAL_ENABLE; a.AeBLCStrength = 255; }
    r = IMP_ISP_Tuning_SetAeScenceAttr(IMPVI_MAIN, &a);
    memset(&cur, 0, sizeof(cur)); IMP_ISP_Tuning_GetAeScenceAttr(IMPVI_MAIN, &cur);
    snprintf(info, n, "comp en%d=%u hlc en%d=%u blc en%d=%u luma %u (was comp %u)", cur.AeTargetCompEn, cur.AeTargetComp,
             cur.AeHLCEn, cur.AeHLCStrength, cur.AeBLCEn, cur.AeBLCStrength, cur.luma, old.AeTargetComp);
    return r;
}

/* AE expr info: arg 0 it short, 1 it long, 2 again low, 3 again high, 4 max again low, 5 max dgain low */
static int c_aeexpr(int ph, int arg, char *info, size_t n)
{
    static IMPISPAEExprInfo old;
    IMPISPAEExprInfo a, cur;
    int r;

    if (!HAVE(IMP_ISP_Tuning_SetAeExprInfo)) return CASE_NA;
    if (ph) { IMP_ISP_Tuning_SetAeExprInfo(IMPVI_MAIN, &old); return 0; }
    memset(&old, 0, sizeof(old)); IMP_ISP_Tuning_GetAeExprInfo(IMPVI_MAIN, &old);
    a = old;
    switch (arg) {
    case 0: a.AeIntegrationTimeUnit = ISP_CORE_EXPR_UNIT_US; a.AeMode = IMPISP_TUNING_OPS_TYPE_MANUAL;
            a.AeIntegrationTimeMode = IMPISP_TUNING_OPS_TYPE_MANUAL; a.AeIntegrationTime = 300; break;
    case 1: a.AeIntegrationTimeUnit = ISP_CORE_EXPR_UNIT_US; a.AeMode = IMPISP_TUNING_OPS_TYPE_MANUAL;
            a.AeIntegrationTimeMode = IMPISP_TUNING_OPS_TYPE_MANUAL; a.AeIntegrationTime = 30000; break;
    case 2: a.AeMode = IMPISP_TUNING_OPS_TYPE_MANUAL; a.AeAGainManualMode = IMPISP_TUNING_OPS_TYPE_MANUAL; a.AeAGain = 1024; break;
    case 3: a.AeMode = IMPISP_TUNING_OPS_TYPE_MANUAL; a.AeAGainManualMode = IMPISP_TUNING_OPS_TYPE_MANUAL; a.AeAGain = 16 * 1024; break;
    case 4: a.AeMaxAGainMode = IMPISP_TUNING_OPS_TYPE_MANUAL; a.AeMaxAGain = old.AeMinAGain ? old.AeMinAGain : 1024; break;
    case 6: a.AeIntegrationTimeUnit = ISP_CORE_EXPR_UNIT_US; a.AeMode = IMPISP_TUNING_OPS_TYPE_MANUAL;   /* IMGFX_PRE=dark: short time + 16x again */
            a.AeIntegrationTimeMode = IMPISP_TUNING_OPS_TYPE_MANUAL; a.AeIntegrationTime = 200;
            a.AeAGainManualMode = IMPISP_TUNING_OPS_TYPE_MANUAL; a.AeAGain = 16 * 1024; break;
    default: a.AeMaxDgainMode = IMPISP_TUNING_OPS_TYPE_MANUAL; a.AeMaxDgain = old.AeMinDgain ? old.AeMinDgain : 1024; break;
    }
    r = IMP_ISP_Tuning_SetAeExprInfo(IMPVI_MAIN, &a);
    memset(&cur, 0, sizeof(cur)); IMP_ISP_Tuning_GetAeExprInfo(IMPVI_MAIN, &cur);
    snprintf(info, n, "mode %d it=%u again=%u dgain=%u maxA=%u maxD=%u (was it=%u again=%u)", cur.AeMode, cur.AeIntegrationTime,
             cur.AeAGain, cur.AeDGain, cur.AeMaxAGain, cur.AeMaxDgain, old.AeIntegrationTime, old.AeAGain);
    return r;
}

/* AWB: arg 0 auto, 1 manual R high, 2 manual B high, 100+m preset */
static int c_awb(int ph, int arg, char *info, size_t n)
{
    static IMPISPWBAttr old;
    IMPISPWBAttr w, cur;
    int r;

    if (!HAVE(IMP_ISP_Tuning_SetAwbAttr)) return CASE_NA;
    if (ph) { IMP_ISP_Tuning_SetAwbAttr(IMPVI_MAIN, &old); return 0; }
    memset(&old, 0, sizeof(old)); IMP_ISP_Tuning_GetAwbAttr(IMPVI_MAIN, &old);
    w = old;
    if (arg == 0) w.mode = ISP_CORE_WB_MODE_AUTO;
    else if (arg < 100) {
        unsigned int rg = old.gain_val.rgain ? old.gain_val.rgain : 0x100, bg = old.gain_val.bgain ? old.gain_val.bgain : 0x100;
        w.mode = ISP_CORE_WB_MODE_MANUAL;
        if (arg == 1) rg *= 2; else bg *= 2;
        w.gain_val.rgain = rg; w.gain_val.bgain = bg;
    } else w.mode = arg - 100;
    r = IMP_ISP_Tuning_SetAwbAttr(IMPVI_MAIN, &w);
    memset(&cur, 0, sizeof(cur)); IMP_ISP_Tuning_GetAwbAttr(IMPVI_MAIN, &cur);
    snprintf(info, n, "mode %d r=%u b=%u ct=%u (was mode %d r=%u b=%u)", cur.mode, cur.gain_val.rgain, cur.gain_val.bgain,
             cur.ct, old.mode, old.gain_val.rgain, old.gain_val.bgain);
    return r;
}

static int c_ccm(int ph, int arg, char *info, size_t n)
{
    static IMPISPCCMAttr old;
    IMPISPCCMAttr a, cur;
    static const float swap[9] = { 0, 0, 1, 0, 1, 0, 1, 0, 0 };
    int r, k;

    if (!HAVE(IMP_ISP_Tuning_SetCCMAttr)) return CASE_NA;
    if (ph) { IMP_ISP_Tuning_SetCCMAttr(IMPVI_MAIN, &old); return 0; }
    memset(&old, 0, sizeof(old)); IMP_ISP_Tuning_GetCCMAttr(IMPVI_MAIN, &old);
    memset(&a, 0, sizeof(a));
    a.ManualEn = IMPISP_TUNING_OPS_MODE_ENABLE;
    for (k = 0; k < 9; k++) a.ColorMatrix[k] = arg ? 1.0f / 3 : swap[k];
    r = IMP_ISP_Tuning_SetCCMAttr(IMPVI_MAIN, &a);
    memset(&cur, 0, sizeof(cur)); IMP_ISP_Tuning_GetCCMAttr(IMPVI_MAIN, &cur);
    snprintf(info, n, "manual=%d m0=%.2f m4=%.2f m8=%.2f (was manual=%d)", cur.ManualEn, cur.ColorMatrix[0],
             cur.ColorMatrix[4], cur.ColorMatrix[8], old.ManualEn);
    return r;
}

/* CSC gamut 0..3, 4 = user matrix (BT601 full, U/V sign inverted) */
static int c_csc(int ph, int arg, char *info, size_t n)
{
    static IMPISPCSCAttr old;
    IMPISPCSCAttr a, cur;
    static const float user[9] = { 0.299f, 0.587f, 0.114f, 0.169f, 0.331f, -0.5f, -0.5f, 0.419f, 0.081f };
    int r;

    if (!HAVE(IMP_ISP_Tuning_SetISPCSCAttr)) return CASE_NA;
    if (ph) { IMP_ISP_Tuning_SetISPCSCAttr(IMPVI_MAIN, &old); return 0; }
    memset(&old, 0, sizeof(old)); IMP_ISP_Tuning_GetISPCSCAttr(IMPVI_MAIN, &old);
    memset(&a, 0, sizeof(a));
    a.ColorGamut = arg;
    if (arg == 4) {
        memcpy(a.Matrix.CscCoef, user, sizeof(user));
        a.Matrix.CscOffset[0] = 0x00; a.Matrix.CscOffset[1] = 0x80;
        a.Matrix.CscClip[0] = 0x00; a.Matrix.CscClip[1] = 0xff; a.Matrix.CscClip[2] = 0x00; a.Matrix.CscClip[3] = 0xff;
    }
    r = IMP_ISP_Tuning_SetISPCSCAttr(IMPVI_MAIN, &a);
    memset(&cur, 0, sizeof(cur)); IMP_ISP_Tuning_GetISPCSCAttr(IMPVI_MAIN, &cur);
    snprintf(info, n, "gamut %d (was %d) coef0 %.3f", cur.ColorGamut, old.ColorGamut, cur.Matrix.CscCoef[0]);
    return r;
}

/* module strengths: arg = module index | (en/value<<8) : value 0 or 255 */
static int c_ratio(int ph, int arg, char *info, size_t n)
{
    static IMPISPModuleRatioAttr old;
    IMPISPModuleRatioAttr a, cur;
    int r, idx = arg & 0xff, val = (arg >> 8) & 0xff;

    if (!HAVE(IMP_ISP_Tuning_SetModule_Ratio)) return CASE_NA;
    if (ph) { IMP_ISP_Tuning_SetModule_Ratio(IMPVI_MAIN, &old); return 0; }
    memset(&old, 0, sizeof(old)); IMP_ISP_Tuning_GetModule_Ratio(IMPVI_MAIN, &old);
    a = old;
    a.ratio_attr[idx].en = IMPISP_TUNING_OPS_MODE_ENABLE; a.ratio_attr[idx].ratio = val;
    r = IMP_ISP_Tuning_SetModule_Ratio(IMPVI_MAIN, &a);
    memset(&cur, 0, sizeof(cur)); IMP_ISP_Tuning_GetModule_Ratio(IMPVI_MAIN, &cur);
    snprintf(info, n, "[%d] en%d ratio %u (was en%d %u)", idx, cur.ratio_attr[idx].en, cur.ratio_attr[idx].ratio,
             old.ratio_attr[idx].en, old.ratio_attr[idx].ratio);
    return r;
}

#define MODBITS(X) X(ccm, bitBypassCCM) X(gamma, bitBypassGAMMA) X(defog, bitBypassDEFOG) X(lsc, bitBypassLSC) \
    X(dpc, bitBypassDPC) X(sdns, bitBypassSDNS) X(mdns, bitBypassMDNS) X(sharpen, bitBypassYSP) X(adr, bitBypassADR) \
    X(ydns, bitBypassYDNS) X(bcsh, bitBypassBCSH)
#define XE(nm, bit) MB_##nm,
enum { MODBITS(XE) MB_N };
#define XC(nm, bit) case MB_##nm: m.bit = 1; break;
#define XU(nm, bit) case MB_N + MB_##nm: m.bit = 0; break;   /* unbypass-<nm>: clear the bit */
static int c_mod(int ph, int arg, char *info, size_t n)
{
    static IMPISPModuleCtl old;
    IMPISPModuleCtl m, cur;
    int r;

    if (!HAVE(IMP_ISP_Tuning_SetModuleControl)) return CASE_NA;
    if (ph) { IMP_ISP_Tuning_SetModuleControl(IMPVI_MAIN, &old); return 0; }
    memset(&old, 0, sizeof(old)); IMP_ISP_Tuning_GetModuleControl(IMPVI_MAIN, &old);
    m = old;
    switch (arg) { MODBITS(XC) MODBITS(XU) default: break; }
    r = IMP_ISP_Tuning_SetModuleControl(IMPVI_MAIN, &m);
    memset(&cur, 0, sizeof(cur)); IMP_ISP_Tuning_GetModuleControl(IMPVI_MAIN, &cur);
    snprintf(info, n, "key 0x%08x -> 0x%08x (was 0x%08x)", m.key, cur.key, old.key);
    return r;
}

/* privacy mask block: solid red, 50 % of ch1 */
static int c_mask(int ph, int arg, char *info, size_t n)
{
    static IMPISPMaskBlockAttr old;
    IMPISPMaskBlockAttr a;
    int r;

    (void)arg;
    if (!HAVE(IMP_ISP_Tuning_SetMaskBlock)) return CASE_NA;
    memset(&a, 0, sizeof(a));
    a.chx = 1; a.pinum = 0;
    if (ph) { a.mask_en = 0; IMP_ISP_Tuning_SetMaskBlock(IMPVI_MAIN, &a); return 0; }
    memset(&old, 0, sizeof(old));
    a.mask_en = 1; a.mask_pos_left = SUB_W / 4; a.mask_pos_top = SUB_H / 4;
    a.mask_width = SUB_W / 2; a.mask_height = SUB_H / 2;
    a.mask_type = IMPISP_MASK_TYPE_RGB;
    a.mask_value.argb.r_value = 255;
    r = IMP_ISP_Tuning_SetMaskBlock(IMPVI_MAIN, &a);
    snprintf(info, n, "chx1 block0 %dx%d (no readback; restore = off)", a.mask_width, a.mask_height);
    return r;
}

/* ePTZ: ch1 crop 50 % mid via AutoZoom */
static int c_autozoom(int ph, int arg, char *info, size_t n)
{
    static IMPISPAutoZoom old;
    IMPISPAutoZoom z;
    int r;

    (void)arg;
    if (!HAVE(IMP_ISP_Tuning_SetAutoZoom)) return CASE_NA;
    if (ph) { IMP_ISP_Tuning_SetAutoZoom(IMPVI_MAIN, &old); return 0; }
    memset(&old, 0, sizeof(old));
    if (HAVE(IMP_ISP_Tuning_GetAutoZoom)) IMP_ISP_Tuning_GetAutoZoom(IMPVI_MAIN, &old);
    z = old;
    z.zoom_chx_en[1] = 1; z.zoom_left[1] = (g_sw / 4) & ~1; z.zoom_top[1] = (g_sh / 4) & ~1;
    z.zoom_width[1] = (g_sw / 2) & ~1; z.zoom_height[1] = (g_sh / 2) & ~1;
    r = IMP_ISP_Tuning_SetAutoZoom(IMPVI_MAIN, &z);
    snprintf(info, n, "ch1 zoom %d,%d %dx%d (was en %d)", z.zoom_left[1], z.zoom_top[1], z.zoom_width[1], z.zoom_height[1],
             old.zoom_chx_en[1]);
    return r;
}

/* scaler quality: arg level 0..128, fixed weight */
static int c_scalerlv(int ph, int arg, char *info, size_t n)
{
    IMPISPScalerLvAttr s;
    int r;

    if (!HAVE(IMP_ISP_Tuning_SetScalerLv)) return CASE_NA;
    memset(&s, 0, sizeof(s));
    s.chx = 1; s.mode = ph ? IMPISP_SCALER_FITTING_CRUVE : IMPISP_SCALER_FIXED_WEIGHT; s.level = ph ? 128 : arg;
    r = IMP_ISP_Tuning_SetScalerLv(IMPVI_MAIN, &s);
    if (ph) return 0;
    snprintf(info, n, "ch1 fixed-weight level %d (no readback; restore = fitting curve)", arg);
    return r;
}
#endif /* T41 */

/* --------------------------------------------------------------- table */
typedef struct { const char *name; casefn fn; int arg; int settle_ms; int risky; } Case;

#define SET 400    /* settle for ISP-pipeline-only changes */
#define AE 2500    /* AE/AWB re-convergence */

static const Case cases[] = {
    { "base", NULL, 0, 0, 0 },
#ifndef PLATFORM_T41
    { "isp-hflip", c_flip, 1, SET, 0 },
    { "isp-vflip", c_flip, 2, SET, 0 },
    { "isp-hvflip", c_flip, 3, SET, 0 },
# ifdef H_HVFLIP2
    { "isp-hvflip2-api", c_flip2, 3, SET, 0 },
# endif
# ifdef H_HVFLIPE
    { "hvflip-enum-h", c_flipe, 1, SET, 0 },
    { "hvflip-enum-v", c_flipe, 2, SET, 0 },
    { "hvflip-enum-hv", c_flipe, 3, SET, 0 },
# endif
# ifdef H_SENSFLIP
    { "sensor-hflip", c_sflip, 1, SET, 0 },
    { "sensor-vflip", c_sflip, 2, SET, 0 },
    { "sensor-hvflip", c_sflip, 3, SET, 0 },
# endif
#else
    { "sensor-hflip", c_flip, 1, SET, 0 },
    { "sensor-vflip", c_flip, 2, SET, 0 },
    { "sensor-hvflip", c_flip, 3, SET, 0 },
    { "isp-hflip", c_flip, 0x101, SET, 0 },
    { "isp-vflip", c_flip, 0x102, SET, 0 },
    { "isp-hvflip", c_flip, 0x103, SET, 0 },
#endif
#if defined(H_FCROP) || defined(H_EXT_FCROP)
    { "fcrop-mid50", c_fcrop, 0, SET, 0 },
    { "fcrop-topleft50", c_fcrop, 1, SET, 0 },
#endif
    { "fs1-crop-mid50", c_fs1crop, 0, SET, 0 },
    { "fs1-crop-topleft50", c_fs1crop, 1, SET, 0 },
#ifdef H_AUTOZOOM
    { "autozoom-ch1-mid50", c_autozoom, 0, SET, 0 },
#endif
#ifdef PLATFORM_T41
    { "autozoom-ch1-mid50", c_autozoom, 0, SET, 0 },
#endif
    { "brightness-low", c_bright, 30, SET, 0 },
    { "brightness-high", c_bright, 225, SET, 0 },
    { "contrast-low", c_contrast, 30, SET, 0 },
    { "contrast-high", c_contrast, 225, SET, 0 },
    { "saturation-0", c_sat, 0, SET, 0 },
    { "saturation-high", c_sat, 230, SET, 0 },
    { "sharpness-0", c_sharp, 0, SET, 0 },
    { "sharpness-high", c_sharp, 255, SET, 0 },
#if defined(H_BCSHHUE) || defined(PLATFORM_T41)
    { "hue-low", c_hue, 40, SET, 0 },
    { "hue-high", c_hue, 215, SET, 0 },
    { "bcsh-combo", c_bcsh, 0, SET, 0 },
#endif
    { "gamma-steep", c_gamma, 0, SET, 0 },
    { "gamma-linear", c_gamma, 1, SET, 0 },
#ifdef PLATFORM_T41
    { "aecomp-low", c_aescene, 0, AE, 0 },
    { "aecomp-high", c_aescene, 1, AE, 0 },
    { "ae-hlc", c_aescene, 2, AE, 0 },
    { "ae-blc", c_aescene, 3, AE, 0 },
    { "exp-short", c_aeexpr, 0, AE, 0 },
    { "exp-long", c_aeexpr, 1, AE, 0 },
    { "again-low", c_aeexpr, 2, AE, 0 },
    { "again-high", c_aeexpr, 3, AE, 0 },
    { "maxagain-low", c_aeexpr, 4, AE, 0 },
    { "maxdgain-low", c_aeexpr, 5, AE, 0 },
#else
# ifdef H_AECOMP
    { "aecomp-low", c_aecomp, 60, AE, 0 },
    { "aecomp-high", c_aecomp, 200, AE, 0 },
# endif
    { "exp-short", c_expr, 300, AE, 0 },
    { "exp-long", c_expr, 30000, AE, 0 },
# ifdef H_AEATTR
    { "aeattr-it-short", c_aeattr, 0, AE, 0 },
    { "aeattr-it-long", c_aeattr, 1, AE, 0 },
    { "aeattr-again-low", c_aeattr, 2, AE, 0 },
    { "aeattr-again-high", c_aeattr, 3, AE, 0 },
    { "aeattr-dgain-high", c_aeattr, 4, AE, 0 },
# endif
    { "maxagain-low", c_maxgain, 32, AE, 0 },
    { "maxdgain-low", c_maxgain, 0x100 | 32, AE, 0 },
#endif
    { "awb-auto", c_awb, 0, AE, 0 },
    { "awb-manual-rhigh", c_awb, 1, AE, 0 },
    { "awb-manual-bhigh", c_awb, 2, AE, 0 },
    { "awb-daylight", c_awb, 100 + ISP_CORE_WB_MODE_DAY_LIGHT, AE, 0 },
    { "awb-cloudy", c_awb, 100 + ISP_CORE_WB_MODE_CLOUDY, AE, 0 },
    { "awb-incandescent", c_awb, 100 + ISP_CORE_WB_MODE_INCANDESCENT, AE, 0 },
    { "awb-fluorescent", c_awb, 100 + ISP_CORE_WB_MODE_FLOURESCENT, AE, 0 },
    { "awb-twilight", c_awb, 100 + ISP_CORE_WB_MODE_TWILIGHT, AE, 0 },
    { "awb-shade", c_awb, 100 + ISP_CORE_WB_MODE_SHADE, AE, 0 },
    { "awb-warm-fluorescent", c_awb, 100 + ISP_CORE_WB_MODE_WARM_FLOURESCENT, AE, 0 },
#ifdef H_AWBCT
    { "awbct-2800", c_awbct, 2800, AE, 0 },
    { "awbct-7500", c_awbct, 7500, AE, 0 },
    { "wbalgo-grayworld", c_wbalgo, 1, AE, 0 },
    { "wbalgo-reweight", c_wbalgo, 2, AE, 0 },
#endif
#if defined(H_CCM) || defined(PLATFORM_T41)
    { "ccm-swap-rb", c_ccm, 0, SET, 0 },
    { "ccm-mono", c_ccm, 1, SET, 0 },
#endif
#if defined(H_CSC) || defined(H_EXT_CSC) || defined(PLATFORM_T41)
    { "csc-0", c_csc, 0, SET, 0 },
    { "csc-1", c_csc, 1, SET, 0 },
    { "csc-2", c_csc, 2, SET, 0 },
    { "csc-3", c_csc, 3, SET, 0 },
    { "csc-4-user", c_csc, 4, SET, 0 },
#endif
#ifdef PLATFORM_T41
    { "drc-ratio-0", c_ratio, 2 | (0 << 8), SET, 0 },
    { "drc-ratio-255", c_ratio, 2 | (255 << 8), SET, 0 },
    { "defog-ratio-255", c_ratio, 4 | (255 << 8), SET, 0 },
    { "sinter-ratio-0", c_ratio, 0 | (0 << 8), SET, 0 },
    { "sinter-ratio-255", c_ratio, 0 | (255 << 8), SET, 0 },
    { "temper-ratio-0", c_ratio, 1 | (0 << 8), SET, 0 },
    { "temper-ratio-255", c_ratio, 1 | (255 << 8), SET, 0 },
    { "dpc-ratio-0", c_ratio, 3 | (0 << 8), SET, 0 },
    { "dpc-ratio-255", c_ratio, 3 | (255 << 8), SET, 0 },
    { "mask-block", c_mask, 0, SET, 0 },
    { "scaler-lv-0", c_scalerlv, 0, SET, 0 },
    { "scaler-lv-128", c_scalerlv, 128, SET, 0 },
#else
# ifdef H_WDRATTR
    { "wdr-off", c_wdr, 0, SET, 0 },
    { "wdr-on", c_wdr, 1, SET, 0 },
# endif
# ifdef H_RAWDRC
    { "rawdrc-off", c_rawdrc, 0, SET, 0 },
    { "rawdrc-manual255", c_rawdrc, 1, SET, 0 },
    { "rawdrc-unlimit", c_rawdrc, 2, SET, 0 },
# endif
# ifdef H_DRCEN
    { "drc-off", c_drcen, 0, SET, 0 },
    { "drc-on", c_drcen, 1, SET, 0 },
# endif
# ifdef H_DRCSTR
    { "drc-strength-0", c_drcstr, 0, SET, 0 },
    { "drc-strength-255", c_drcstr, 255, SET, 0 },
# endif
# ifdef H_DEFOG
    { "defog-on", c_defogen, 1, SET, 0 },
    { "defog-strength-255", c_defogstr, 255, SET, 0 },
# endif
# ifdef H_ANTIFOG
    { "antifog-strong", c_antifog, 1, SET, 0 },
    { "antifog-weak", c_antifog, 3, SET, 0 },
# endif
    { "sinter-0", c_sinter, 0, SET, 0 },
    { "sinter-255", c_sinter, 255, SET, 0 },
    { "temper-0", c_temper, 0, SET, 0 },
    { "temper-255", c_temper, 255, SET, 0 },
# if defined(H_DPC) || defined(H_DPSTR)
    { "dpc-0", c_dpc, 0, SET, 0 },
    { "dpc-255", c_dpc, 255, SET, 0 },
# endif
    { "hilight-depress-0", c_hld, 0, SET, 0 },
    { "hilight-depress-255", c_hld, 255, SET, 0 },
# ifdef H_BLC
    { "backlight-comp-0", c_blc, 0, SET, 0 },
    { "backlight-comp-255", c_blc, 255, SET, 0 },
# endif
# ifdef H_MASK
    { "mask-block", c_mask, 0, SET, 0 },
# endif
# ifdef H_SCALERLV
    { "scaler-lv-0", c_scalerlv, 0, SET, 0 },
    { "scaler-lv-128", c_scalerlv, 128, SET, 0 },
# endif
# ifdef H_COLORFX
    { "colorfx-bw", c_colorfx, 1, SET, 0 },
    { "colorfx-sepia", c_colorfx, 2, SET, 0 },
    { "colorfx-negative", c_colorfx, 3, SET, 0 },
    { "colorfx-vivid", c_colorfx, 9, SET, 0 },
# endif
# ifdef H_SCENE
    { "scene-night", c_scene, 8, AE, 0 },
    { "scene-landscape", c_scene, 7, AE, 0 },
# endif
# ifdef H_MESH
    { "shading-low", c_mesh, 0, SET, 0 },
    { "shading-ultra", c_mesh, 3, SET, 0 },
# endif
#endif
#ifdef H_MODCTL
# define XT(nm, bit) { "bypass-" #nm, c_mod, MB_##nm, SET, 0 },
    MODBITS(XT)
    { "unbypass-ccm", c_mod, MB_N + MB_ccm, SET, 0 },
# ifndef PLATFORM_T21
    { "unbypass-bcsh", c_mod, MB_N + MB_bcsh, SET, 0 },
# endif
#endif
#ifdef PLATFORM_T41
# define XT(nm, bit) { "bypass-" #nm, c_mod, MB_##nm, SET, 0 },
    MODBITS(XT)
    { "unbypass-defog", c_mod, MB_N + MB_defog, SET, 0 },   /* #135: clear the calibrated defog bypass */
#endif
    { "aflicker-off", c_aflick, 0, AE, 0 },
    { "aflicker-50hz", c_aflick, 1, AE, 0 },
    { "aflicker-60hz", c_aflick, 2, AE, 0 },
    { "mode-day", c_runmode, 0, AE, 0 },
    { "mode-night", c_runmode, 1, AE, 0 },
#ifndef PLATFORM_T41
    { "isp-bypass", c_ispbypass, 0, SET, 1 },
#endif
    { "fps-15", c_fps, 15, AE, 1 },
    { "fps-10", c_fps, 10, AE, 1 },
    { "base-end", NULL, 0, 0, 0 },   /* noise / drift reference vs. base */
};
#define NCASES (sizeof(cases) / sizeof(cases[0]))

/* IMGFX_PRE=tok,tok...: put blocks into a state where their bypass bit matters, for the whole run (#106; restored at the end).
 *   dark   manual short exposure (the AE then drives the gain to the cap): noisy dark picture (denoise, DPC)
 *   defog  defog on at strength 255 (T21: SetAntiFogAttr strong; T41: module ratio 255)
 *   nr     spatial + temporal noise reduction strength 255 (T41: sinter/temper ratio 255)
 *   dpc    defect pixel correction strength 255 (T41: ratio 255)
 * The bypass-<block> cases then compare against a base taken with the block working hard. */
static void pre_apply(const char *list, int on)
{
    char buf[128], *t, *sv = NULL;
#ifdef PLATFORM_T41
    static IMPISPModuleRatioAttr oldratio; static int ratio_saved;
    IMPISPModuleRatioAttr ra;
#else
    char tmp[16];
#endif

    if (!list || !*list) return;
    snprintf(buf, sizeof(buf), "%s", list);
    for (t = strtok_r(buf, ",", &sv); t; t = strtok_r(NULL, ",", &sv)) {
        char info[160]; int r = 0;
        info[0] = 0;
        if (!strcmp(t, "dark")) {
#ifdef PLATFORM_T41
            r = on ? c_aeexpr(0, 6, info, sizeof(info)) : c_aeexpr(1, 6, info, sizeof(info));
#else
            /* manual short integration time, gain caps untouched: the AE raises the gain to hold the brightness */
            static IMPISPExpr oe; IMPISPExpr e;
            if (on) {
                memset(&oe, 0, sizeof(oe)); IMP_ISP_Tuning_GetExpr(&oe);
                memset(&e, 0, sizeof(e)); e.s_attr.mode = ISP_CORE_EXPR_MODE_MANUAL; e.s_attr.unit = ISP_CORE_EXPR_UNIT_US; e.s_attr.time = 200;
                r = IMP_ISP_Tuning_SetExpr(&e);
                memset(&e, 0, sizeof(e)); IMP_ISP_Tuning_GetExpr(&e);
                snprintf(info, sizeof(info), "manual time it=%u (was %u)", e.g_attr.integration_time, oe.g_attr.integration_time);
            } else {
                memset(&e, 0, sizeof(e)); e.s_attr.mode = ISP_CORE_EXPR_MODE_AUTO; e.s_attr.unit = ISP_CORE_EXPR_UNIT_US; e.s_attr.time = oe.g_attr.integration_time;
                IMP_ISP_Tuning_SetExpr(&e);
            }
#endif
            if (on) sleep(4);   /* AE settles at the cap */
        }
#ifdef PLATFORM_T41
        else if (!strcmp(t, "defog") || !strcmp(t, "nr") || !strcmp(t, "dpc")) {
            if (!HAVE(IMP_ISP_Tuning_SetModule_Ratio) || !on) continue;
            memset(&ra, 0, sizeof(ra)); IMP_ISP_Tuning_GetModule_Ratio(IMPVI_MAIN, &ra);
            if (!ratio_saved) { oldratio = ra; ratio_saved = 1; }
            if (!strcmp(t, "defog")) { ra.ratio_attr[4].en = IMPISP_TUNING_OPS_MODE_ENABLE; ra.ratio_attr[4].ratio = 255; }
            if (!strcmp(t, "nr")) { ra.ratio_attr[0].en = ra.ratio_attr[1].en = IMPISP_TUNING_OPS_MODE_ENABLE; ra.ratio_attr[0].ratio = ra.ratio_attr[1].ratio = 255; }
            if (!strcmp(t, "dpc")) { ra.ratio_attr[3].en = IMPISP_TUNING_OPS_MODE_ENABLE; ra.ratio_attr[3].ratio = 255; }
            r = IMP_ISP_Tuning_SetModule_Ratio(IMPVI_MAIN, &ra);
            IMP_ISP_Tuning_GetModule_Ratio(IMPVI_MAIN, &ra);
            snprintf(info, sizeof(info), "ratio sinter %u temper %u dpc %u defog %u", ra.ratio_attr[0].ratio, ra.ratio_attr[1].ratio, ra.ratio_attr[3].ratio, ra.ratio_attr[4].ratio);
        }
#else
# ifdef H_DEFOG
        else if (!strcmp(t, "defog")) {
            if (on) { r = c_defogen(0, 1, info, sizeof(info)); c_defogstr(0, 255, tmp, sizeof(tmp)); } else { c_defogstr(1, 0, tmp, sizeof(tmp)); c_defogen(1, 0, info, sizeof(info)); }
        }
# endif
# ifdef H_ANTIFOG
        else if (!strcmp(t, "defog")) {
            r = on ? c_antifog(0, 1, info, sizeof(info)) : c_antifog(1, 1, info, sizeof(info));
        }
# endif
        else if (!strcmp(t, "nr")) {
            if (on) { r = c_sinter(0, 255, info, sizeof(info)); c_temper(0, 255, tmp, sizeof(tmp)); } else { c_sinter(1, 0, info, sizeof(info)); c_temper(1, 0, tmp, sizeof(tmp)); }
        }
# if defined(H_DPC) || defined(H_DPSTR)
        else if (!strcmp(t, "dpc")) {
            r = on ? c_dpc(0, 255, info, sizeof(info)) : c_dpc(1, 0, info, sizeof(info));
        }
# endif
#endif
        else continue;
        if (on) say("[T] pre %s: ret %d %s\n", t, r, info);
    }
#ifdef PLATFORM_T41
    if (!on && ratio_saved && HAVE(IMP_ISP_Tuning_SetModule_Ratio)) IMP_ISP_Tuning_SetModule_Ratio(IMPVI_MAIN, &oldratio);
#endif
}

/* ------------------------------------------------------------- capture */
static int g_got_w, g_got_h;

/* IMGFX_STATS=1: no file; the picture is measured in memory (mean Y/U/V, detail energy, 8x8 luma grid) and the
 * numbers are printed on the [R] line (" ST ...").  No picture leaves the camera. */
static int g_stats;
static char g_st[1024];

static void measure(const IMPFrameInfo *fr)
{
    const unsigned char *y = (const unsigned char *)(uintptr_t)fr->virAddr;
    int w = fr->width, h = fr->height, i, j, n = 0;
    const unsigned char *uv = y + (size_t)w * h;
    double sy = 0, su = 0, sv = 0, lap = 0, gs[64] = {0}, gc[64] = {0};
    char *p = g_st;

    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) {
            int k = (j * 8 / h) * 8 + i * 8 / w;
            sy += y[(size_t)j * w + i]; gs[k] += y[(size_t)j * w + i]; gc[k]++;
            if (j > 0 && j < h - 1 && i > 0 && i < w - 1 && !(i & 1) && !(j & 1)) {
                int l = 4 * y[(size_t)j * w + i] - y[(size_t)j * w + i - 1] - y[(size_t)j * w + i + 1] - y[(size_t)(j - 1) * w + i] - y[(size_t)(j + 1) * w + i];
                lap += l < 0 ? -l : l; n++;
            }
        }
    for (j = 0; j < h / 2; j++)
        for (i = 0; i < w / 2; i++) { su += uv[(size_t)j * w + 2 * i]; sv += uv[(size_t)j * w + 2 * i + 1]; }
    p += snprintf(p, sizeof(g_st), "ST Y=%.2f U=%.2f V=%.2f H=%.3f G=", sy / ((double)w * h), su / ((double)w * h / 4), sv / ((double)w * h / 4), n ? lap / n : 0.0);
    for (i = 0; i < 64; i++) p += snprintf(p, sizeof(g_st) - (p - g_st), "%s%.1f", i ? "," : "", gc[i] ? gs[i] / gc[i] : 0.0);
}

/* drop 'skip' frames, then save the next one as raw NV12 (or measure it with IMGFX_STATS) */
static int g_pair;   /* IMGFX_PAIR=1: a second, consecutive frame per case (<name>-b.nv12) for temporal-noise measurements */
static int g_chn = 1, g_crop;   /* IMGFX_CH0CROP=N: frames from ch0, centred N x N window saved */

/* the centred g_crop x g_crop window of an NV12 frame (Y rows, then the UV rows) */
static void write_crop(FILE *f, const uint8_t *y, int w, int h)
{
    int n = g_crop, x0, y0, j;
    const uint8_t *uv = y + (size_t)w * h;

    if (n > w) n = w & ~1;
    if (n > h) n = h & ~1;
    x0 = ((w - n) / 2) & ~1; y0 = ((h - n) / 2) & ~1;
    for (j = 0; j < n; j++) fwrite(y + (size_t)(y0 + j) * w + x0, 1, n, f);
    for (j = 0; j < n / 2; j++) fwrite(uv + (size_t)(y0 / 2 + j) * w + x0, 1, n, f);
    g_got_w = g_got_h = n;
}
static int snap1(const char *path, int skip);
static int snap(const char *path, int skip)
{
    int r = snap1(path, skip);

    if (!r && g_pair && !g_stats) {
        char p2[300], *dot;
        char keep[sizeof(g_st)];

        snprintf(p2, sizeof(p2), "%s", path);
        dot = strrchr(p2, '.');
        if (dot) { memmove(dot + 2, dot, strlen(dot) + 1); dot[0] = '-'; dot[1] = 'b'; }
        memcpy(keep, g_st, sizeof(keep));
        snap1(p2, 0);
        memcpy(g_st, keep, sizeof(keep));
    }
    return r;
}
static int g_gf_ret;
static int snap1(const char *path, int skip)
{
    int got = 0, tries;
    FILE *f;

    g_st[0] = 0;
    for (tries = 0; tries < 30 && !got; tries++) {
        IMPFrameInfo *fr = NULL;

        if ((g_gf_ret = IMP_FrameSource_GetFrame(g_chn, &fr)) < 0 || !fr) {
            usleep(100000);
            continue;
        }
        if (skip > 0) { skip--; IMP_FrameSource_ReleaseFrame(g_chn, fr); continue; }
        if (g_stats) {
            measure(fr);
            g_got_w = fr->width; g_got_h = fr->height;
            got = 1;
            IMP_FrameSource_ReleaseFrame(g_chn, fr);
            continue;
        }
        f = fopen(path, "wb");
        if (f) {
            size_t sz = (size_t)fr->width * fr->height * 3 / 2;

            if (g_crop && (!fr->size || fr->size >= sz)) {
                write_crop(f, (const uint8_t *)(uintptr_t)fr->virAddr, fr->width, fr->height);
            } else {
                if (fr->size && fr->size < sz) sz = fr->size;
                if (save_guard_ok(sz)) fwrite((void *)(uintptr_t)fr->virAddr, 1, sz, f);
                g_got_w = fr->width; g_got_h = fr->height;
            }
            fclose(f);
            got = 1;
        }
        IMP_FrameSource_ReleaseFrame(g_chn, fr);
    }
    return got ? 0 : -1;
}

/* IMGFX_HOOK=cmd: run cmd after each case settled (IMGFX_CASE = case name), e.g. a register dump */
static void hook(const char *name)
{
    const char *h = getenv("IMGFX_HOOK");

    if (!h || !*h) return;
    setenv("IMGFX_CASE", name, 1);
    fflush(stdout);
    if (system(h) < 0) say("[E] hook failed\n");
}

static void settle(int ms)
{
    IMPFrameInfo *fr;
    int k;

    usleep((300 + ms + g_extra_ms) * 1000);
    for (k = 0; k < 3; k++) {         /* skip 3 frames (older than the change) */
        fr = NULL;
        if (IMP_FrameSource_GetFrame(g_chn, &fr) == 0 && fr) IMP_FrameSource_ReleaseFrame(g_chn, fr);
    }
}

/* watchdog: a case that wedges the pipeline (GetFrame blocks) ends the run */
static const char *g_cur = "init";
static void on_alarm(int sig)
{
    static const char m[] = "\n[E] watchdog: no progress for 90 s, case: ";
    (void)sig;
    if (write(1, m, sizeof(m) - 1) < 0) _exit(3);
    if (write(1, g_cur, strlen(g_cur)) < 0) _exit(3);
    if (write(1, "\n", 1) < 0) _exit(3);
    _exit(3);
}

static int match(const char *name, const char *filter)
{
    return !filter || strstr(name, filter) != NULL;
}

int picture_main(int argc, char **argv)
{
    IMPFSChnAttr fs0, fs1;
    const char *filter;
    char path[512], info[256];
    unsigned i, ran = 0, na = 0, nset = 0;
    int idx = 0;
    int rc = 0;

    if (argc == 2 && !strcmp(argv[1], "--list")) {   /* case names for this SoC build */
        for (i = 0; i < NCASES; i++) printf("%s%s\n", cases[i].name, cases[i].risky ? " (risky, runs last, IMGFX_SKIP_RISKY=1 skips)" : "");
        return 0;
    }
    if (argc < 6) {
        fprintf(stderr, "imgfx (%s) usage: %s sensor i2c_addr sensor_w sensor_h outdir [case-filter]   |   %s --list\n", SOC, argv[0], argv[0]);
        return 2;
    }
    g_sw = atoi(argv[3]); g_sh = atoi(argv[4]);
    outdir = argv[5];
    filter = argc > 6 ? argv[6] : NULL;
    mkdir(outdir, 0755);
    snprintf(path, sizeof(path), "%s/summary-%s.txt", outdir, SOC);
    sum = fopen(path, "a");   /* appended: filtered re-runs add to the same file */
    g_stats = getenv("IMGFX_STATS") && atoi(getenv("IMGFX_STATS"));
    g_pair = getenv("IMGFX_PAIR") && atoi(getenv("IMGFX_PAIR"));
    if (getenv("IMGFX_CH0CROP") && atoi(getenv("IMGFX_CH0CROP")) > 0) { g_crop = atoi(getenv("IMGFX_CH0CROP")) & ~1; g_chn = 0; }
    if (getenv("IMGFX_SETTLE_MS")) g_extra_ms = atoi(getenv("IMGFX_SETTLE_MS"));
    g_risky = !(getenv("IMGFX_SKIP_RISKY") && atoi(getenv("IMGFX_SKIP_RISKY")));
    g_night = (getenv("IMGFX_MODE") && !strcmp(getenv("IMGFX_MODE"), "night")) ||
              (getenv("CC_MODE") && !strcmp(getenv("CC_MODE"), "night"));

    memset(&g_sensor, 0, sizeof(g_sensor));
    strncpy(g_sensor.name, argv[1], sizeof(g_sensor.name) - 1);
    g_sensor.cbus_type = TX_SENSOR_CONTROL_INTERFACE_I2C;
    strncpy(g_sensor.i2c.type, argv[1], sizeof(g_sensor.i2c.type) - 1);
    g_sensor.i2c.addr = (int)strtol(argv[2], NULL, 0);
    g_sensor.rst_gpio = g_sensor.pwdn_gpio = g_sensor.power_gpio = -1;

#ifdef PLATFORM_T41
    if (IMP_ISP_Open() < 0 || IMP_ISP_AddSensor(IMPVI_MAIN, &g_sensor) < 0 || IMP_ISP_EnableSensor(IMPVI_MAIN, &g_sensor) < 0) {
        say("[E] ISP open/add/enable sensor failed\n"); return 1;
    }
#else
    if (IMP_ISP_Open() < 0 || IMP_ISP_AddSensor(&g_sensor) < 0 || IMP_ISP_EnableSensor() < 0) {
        say("[E] ISP open/add/enable sensor failed\n"); return 1;
    }
#endif
    if (IMP_System_Init() < 0 || IMP_ISP_EnableTuning() < 0) { say("[E] System_Init/EnableTuning failed\n"); return 1; }
    { char tmp[8]; c_runmode(1, 0, tmp, sizeof(tmp)); }   /* running mode = IMGFX_MODE (restore path) */
    say("[T] imgfx %s sensor %s %dx%d sub %dx%d mode %s\n", SOC, argv[1], g_sw, g_sh, SUB_W, SUB_H, g_night ? "night" : "day");

    memset(&fs0, 0, sizeof(fs0));
    fs0.picWidth = g_sw; fs0.picHeight = g_sh; fs0.pixFmt = PIX_FMT_NV12;
    fs0.outFrmRateNum = 15; fs0.outFrmRateDen = 1; fs0.nrVBs = 2;
    fs0.type = FS_PHY_CHANNEL;
    fs1 = fs0;
    fs1.picWidth = SUB_W; fs1.picHeight = SUB_H;
    fs1.scaler.enable = 1; fs1.scaler.outwidth = SUB_W; fs1.scaler.outheight = SUB_H;
    if (IMP_FrameSource_CreateChn(0, &fs0) < 0 || IMP_FrameSource_CreateChn(1, &fs1) < 0) { say("[E] FS_CreateChn failed\n"); return 1; }
    if (!getenv("IMGFX_DEPTH_LATE")) {
        IMP_FrameSource_SetFrameDepth(1, 1);
        if (g_chn == 0) IMP_FrameSource_SetFrameDepth(0, 1);
    }
    if (IMP_FrameSource_EnableChn(0) < 0 || IMP_FrameSource_EnableChn(1) < 0) { say("[E] FS_EnableChn failed\n"); return 1; }
    if (getenv("IMGFX_DEPTH_LATE")) {   /* vendor diag: frame depth after EnableChn */
        int d1 = IMP_FrameSource_SetFrameDepth(1, 1), d0 = g_chn == 0 ? IMP_FrameSource_SetFrameDepth(0, 1) : 0;
        say("[I] SetFrameDepth after EnableChn: ch1 %d ch0 %d\n", d1, d0);
    }
    sleep(3);   /* AE/AWB settle */
    if (getenv("IMGFX_SAT") && HAVE(IMP_ISP_Tuning_SetSaturation)) {   /* saturation for the whole run (colour cases) */
        int sr = SETU8(IMP_ISP_Tuning_SetSaturation, atoi(getenv("IMGFX_SAT")));
        say("[T] saturation %d for the run (ret %d)\n", atoi(getenv("IMGFX_SAT")), sr);
        usleep(600000);
    }

    pre_apply(getenv("IMGFX_PRE"), 1);
    signal(SIGALRM, on_alarm);
    for (i = 0; i < NCASES; i++) {
        const Case *c = &cases[i];
        int r, tmo;

        if (c->risky && !g_risky) continue;
        idx++;
        if (!match(c->name, filter) && strncmp(c->name, "base", 4)) continue;
        if (!c->fn) {
            g_cur = c->name; alarm(90);
            snprintf(path, sizeof(path), "%s/%02d-%s.nv12", outdir, idx, c->name);
            settle(0);
            hook(c->name);
            r = snap(path, 0);
            say("[R] %s set=0 get=%s %s\n", c->name, r ? "NO PICTURE" : "picture", g_st);
            if (r) say("[I] GetFrame(%d) last ret %d\n", g_chn, g_gf_ret);
            if (r) rc = 1;
            ran++;
            continue;
        }
        info[0] = 0;
        g_cur = c->name; alarm(90);
        r = c->fn(0, c->arg, info, sizeof(info));
        if (r == CASE_NA) {
            say("[R] %s N/A\n", c->name);
            na++;
            continue;
        }
        nset++;
        snprintf(path, sizeof(path), "%s/%02d-%s.nv12", outdir, idx, c->name);
        settle(c->settle_ms);
        hook(c->name);
        tmo = snap(path, 0);
        say("[R] %s set=%d get=%s%s | %s\n", c->name, r, info, tmo ? " [NO PICTURE]" : "", g_st);
        if (tmo) rc = 1;
        c->fn(1, c->arg, info, sizeof(info));
        usleep(c->settle_ms > 1000 ? 1500000 : 400000);
        ran++;
    }
    alarm(0);
    pre_apply(getenv("IMGFX_PRE"), 0);
    if (getenv("IMGFX_SAT") && HAVE(IMP_ISP_Tuning_SetSaturation)) SETU8(IMP_ISP_Tuning_SetSaturation, 128);
    say("[T] done: %u pictures, %u cases N/A, %u functions exercised (picture size %dx%d)\n", ran, na, nset, g_got_w, g_got_h);

    IMP_FrameSource_DisableChn(1);
    IMP_FrameSource_DisableChn(0);
    IMP_FrameSource_DestroyChn(1);
    IMP_FrameSource_DestroyChn(0);
    IMP_ISP_DisableTuning();
    IMP_System_Exit();
#ifdef PLATFORM_T41
    IMP_ISP_DisableSensor(IMPVI_MAIN);
    IMP_ISP_DelSensor(IMPVI_MAIN, &g_sensor);
#else
    IMP_ISP_DisableSensor();
    IMP_ISP_DelSensor(&g_sensor);
#endif
    IMP_ISP_Close();
    if (sum) fclose(sum);
    return rc;
}

#endif /* APITEST_T40 */
