/* apitest - common definitions: SoC flags, report line, guard arenas.
 *
 * Every vendor IMP_/SU_ function is declared weak (apitest_weak.h, generated
 * from the ingenic-headers of the SoC), so a function the linked libimp does
 * not export is NULL at run time and prints N/A.  Functions the vendor headers
 * of the SoC do not declare at all are not part of that SoC's test (#if HAS_x).
 */
#ifndef APITEST_H
#define APITEST_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <time.h>
#include <dlfcn.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "imp/imp_common.h"
#include "imp/imp_system.h"
#include "imp/imp_isp.h"
#include "imp/imp_framesource.h"
#if defined(__has_include)
# if __has_include("imp/imp_emu_framesource.h")
#  include "imp/imp_emu_framesource.h"
# endif
#endif
#include "imp/imp_encoder.h"
#include "imp/imp_osd.h"
#if defined(__has_include)
# if __has_include("imp/imp_decoder.h")
#  include "imp/imp_decoder.h"
# endif
#endif
#include "imp/imp_ivs.h"
#include "imp/imp_ivs_move.h"
#include "imp/imp_ivs_base_move.h"
#include "imp/imp_audio.h"
#include "sysutils/su_base.h"
#include "sysutils/su_misc.h"
#include "sysutils/su_adc.h"
#if defined(__has_include)
# if __has_include("sysutils/su_battery.h")
#  include "sysutils/su_battery.h"
# endif
#endif
#include "sysutils/su_cipher.h"
#if defined(__has_include)
# if __has_include("imp/imp_dmic.h")
#  include "imp/imp_dmic.h"
# endif
#endif

#include "apitest_have.h"
#include "apitest_weak.h"

/* ------------------------------------------------------------------ SoC */
/* T40 = T41 API family: build with -DPLATFORM_T41 -DAPITEST_T40 (T40 1.3.1 headers) */
#if defined(APITEST_T40)
# define SOC "T40"
#elif defined(PLATFORM_T10)
# define SOC "T10"
#elif defined(PLATFORM_T20)
# define SOC "T20"
#elif defined(PLATFORM_T21)
# define SOC "T21"
#elif defined(PLATFORM_T23)
# define SOC "T23"
#elif defined(PLATFORM_T30)
# define SOC "T30"
#elif defined(PLATFORM_T31)
# define SOC "T31"
#elif defined(PLATFORM_T41)
# define SOC "T41"
#else
# error "define PLATFORM_T10|T20|T21|T23|T30|T31|T41"
#endif

/* encoder API family: T10/T20/T21/T23 IMPEncoderCHNAttr (per-codec rc unions),
 * T31/T41 IMPEncoderChnAttr (SetDefaultParam, profile based) */
#if defined(PLATFORM_T31) || defined(PLATFORM_T41)
# define ENC_NEW 1
#else
# define ENC_OLD 1
#endif
/* H.265 in the vendor API of the SoC */
#if defined(PLATFORM_T21) || defined(PLATFORM_T23) || defined(PLATFORM_T31) || defined(PLATFORM_T41)
# define HAVE_H265 1
#endif
#ifdef PLATFORM_T41
# define V0 IMPVI_MAIN,
#else
# define V0
#endif

/* ---------------------------------------------------------------- report */
enum { V_PASS, V_FAIL, V_NA, V_SKIP };
#define CASE_NA (-0x7fff0000)

/* the function name goes through FN(): it is compile-checked against the
 * vendor header and recorded in section "apifn" for --list / counting */
#define FN(f) ({ static const char *t_ __attribute__((section("apifn"), used)) = #f; (void)(void *)&f; t_; })
/* names that are not plain functions of the vendor API (labels) */
#define LBL(s) ({ static const char *t_ __attribute__((section("apifn"), used)) = s; t_; })

void rep(const char *fn, long ret, int verdict, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
void rep_na(const char *fn);
void rep_area(const char *name);
extern int g_pass, g_fail, g_na, g_skip;

/* run the block only if the (weak) function is exported, else print N/A */
#define NEED(f) if (!(f)) { rep_na(FN(f)); } else
#define RET0(f, r, ...) rep(FN(f), (long)(r), (r) == 0 ? V_PASS : V_FAIL, __VA_ARGS__)
/* condition-based */
#define CHECK(f, r, ok, ...) rep(FN(f), (long)(r), ((r) == 0 && (ok)) ? V_PASS : V_FAIL, __VA_ARGS__)

/* ---------------------------------------------------------------- guards */
/* gnew(): zero-filled out buffer with a 64 byte guard (0xA5) on both sides.
 * A guard hit sets the pending-overwrite flag; the next rep() prints FAIL
 * OVERWRITE for that line. */
void *gnew(size_t sz);
int gchk(void *p);          /* 1 = guards intact; hit -> flag */
void gfree(void *p);
int gtouched(void *p);      /* user area differs from the 0 fill */
#define G(T, n) T *n = (T *)gnew(sizeof(T))
#define GN(T, n, cnt) T *n = (T *)gnew(sizeof(T) * (cnt))
extern int g_overwrites;

/* ----------------------------------------------------------- environment */
extern int g_fps;                 /* sensor rate in effect */
extern int g_sw, g_sh;               /* sensor size = FS ch0 */
#define SUB_W 640
#define SUB_H 360
#define CH_MAIN 0
#define CH_SUB 1
extern IMPSensorInfo g_sensor;
extern int g_mode_night;

int64_t now_us(void);
void wdog(const char *what, int sec);   /* (re)arm the watchdog, 0 = off */

/* sections (one file each) */
void t_system(void);
void t_fs_pre(void);       /* FS create + pre-enable settings + enable */
void t_fs(void);
void t_fs_post(void);      /* FS disable + destroy */
void t_enc(void);
void t_osd(void);
void t_ivs(void);
void t_audio(void);
void t_isp(void);
void t_cases(void);
void t_dmic(void);        /* IMP_DMIC_* round trips (at_cases.c); area "dmic" runs it without the ISP, case "dmic" inside area cases */
void t_binpath_pre(void);  /* IMP_ISP_Set/GetDefaultBinPath, between ISP_Open and AddSensor (T41 family) */
void cases_list(void);     /* names of the cases of area "cases" */
int picture_main(int argc, char **argv);   /* former imgfx (at_picture.c) */
int t40dev_main(int argc, char **argv);    /* former t40_devtest (at_t40dev.c, T40 only) */        /* targeted cases for functions without a case (at_cases.c) */
void t_isp_rt(void);       /* generated Get/Set same-value round trips (gen_rt.py) */
void t_su(void);
void t_system_exit(int ret);

/* encoder chain (H.264 chn 0 in group 0, optionally with an OSD group in between) for the OSD test */
int enc_chain_up(void);
void enc_chain_down(void);
int enc_pull_h264(int n);

/* fetch one NV12 frame from FS ch1 (for IVS/encoder-less users) */
int fs_peek(int ch, int *w, int *h, int64_t *ts);

#endif
