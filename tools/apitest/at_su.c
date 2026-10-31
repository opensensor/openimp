#define _GNU_SOURCE 1
/* apitest: sysutils (SU_*) functions.  They live in libsysutils.so, resolved with dlsym
 * (type-checked against the vendor headers). Never called: SU_Base_Shutdown/Reboot/Suspend. */
#include <unistd.h>
#include "apitest.h"

static void *g_su;
#define SUSYM(name) ((__typeof__(&name))dlsym(g_su, #name))
/* resolve into 'f'; N/A line when missing */
#define SUNEED(name) __typeof__(&name) f_##name = g_su ? SUSYM(name) : NULL; if (!f_##name) { rep_na(FN(name)); } else

void t_su(void)
{
    int r;

    rep_area("su");
    g_su = dlopen("libsysutils.so", RTLD_NOW);
    if (!g_su) {
        rep(LBL("libsysutils.so"), -1, V_NA, "dlopen failed: %s", dlerror());
    }

    SUNEED(SU_Base_GetModelNumber) {
        G(SUModelNum, m);
        r = f_SU_Base_GetModelNumber(m); gchk(m);
        CHECK(SU_Base_GetModelNumber, r, m->chr[0] != 0, "\"%.16s\"", m->chr);
        gfree(m);
    }
    SUNEED(SU_Base_GetVersion) {
        G(SUVersion, v);
        r = f_SU_Base_GetVersion(v); gchk(v);
        CHECK(SU_Base_GetVersion, r, v->chr[0] != 0, "\"%.60s\"", v->chr);
        gfree(v);
    }
    SUNEED(SU_Base_GetDevID) {
        G(SUDevID, d);
        r = f_SU_Base_GetDevID(d); gchk(d);
        CHECK(SU_Base_GetDevID, r, gtouched(d), "%02x%02x%02x%02x%02x%02x%02x%02x...", d->hex[0], d->hex[1], d->hex[2], d->hex[3], d->hex[4], d->hex[5], d->hex[6], d->hex[7]);
        gfree(d);
    }
    /* The RTC (/dev/rtc0) and the ADC (/dev/ingenic_adc_aux_*, /dev/jz_adc_aux_*) are optional
     * hardware: the vendor libsysutils opens the same nodes and fails the same way when the
     * kernel has no such driver (checked on the T20 jxf23 board: no rtc class, no ADC node). */
    int has_rtc = access("/dev/rtc0", F_OK) == 0;
    const char *no_rtc = "no /dev/rtc0 (no RTC driver in this kernel/board; the vendor libsysutils opens the same node)";
    /* the time conversions are pure arithmetic (no device): fixed time 2001-09-09 01:46:40 UTC = 1000000000 */
    SUNEED(SU_Base_SUTime2Raw) {
        G(SUTime, ft); G(uint32_t, raw); G(SUTime, back);
        ft->year = 2001; ft->mon = 9; ft->mday = 9; ft->hour = 1; ft->min = 46; ft->sec = 40;
        int r2 = f_SU_Base_SUTime2Raw(ft, raw); gchk(raw);
        CHECK(SU_Base_SUTime2Raw, r2, *raw == 1000000000u, "2001-09-09 01:46:40 -> %u (expected 1000000000)", *raw);
        SUNEED(SU_Base_Raw2SUTime) {
            int r3 = f_SU_Base_Raw2SUTime(raw, back); gchk(back);
            CHECK(SU_Base_Raw2SUTime, r3, back->year == 2001 && back->mon == 9 && back->mday == 9 && back->hour == 1 && back->min == 46 && back->sec == 40,
                  "%u -> %04d-%02d-%02d %02d:%02d:%02d (round trip)", *raw, back->year, back->mon, back->mday, back->hour, back->min, back->sec);
        }
        gfree(ft); gfree(raw); gfree(back);
    }
    if (!has_rtc) {
        rep(FN(SU_Base_GetTime), -1, V_NA, "%s", no_rtc);
        rep(FN(SU_Base_SetTime), -1, V_NA, "%s", no_rtc);
        rep(FN(SU_Base_SetAlarm), -1, V_NA, "%s", no_rtc);
        rep(FN(SU_Base_GetAlarm), -1, V_NA, "%s", no_rtc);
        rep(FN(SU_Base_DisableAlarm), -1, V_NA, "%s", no_rtc);
    } else {
    SUNEED(SU_Base_GetTime) {
        G(SUTime, t); G(SUTime, t2);
        int r1 = f_SU_Base_GetTime(t); gchk(t);
        CHECK(SU_Base_GetTime, r1, t->year >= 1970 && t->mon >= 1 && t->mon <= 12 && t->mday >= 1 && t->mday <= 31 && t->hour < 24 && t->min < 60 && t->sec < 62,
              "%04d-%02d-%02d %02d:%02d:%02d", t->year, t->mon, t->mday, t->hour, t->min, t->sec);
        SUNEED(SU_Base_SetTime) {
            int r2 = f_SU_Base_SetTime(t);       /* writes the time just read back: no change */
            f_SU_Base_GetTime(t2); gchk(t2);
            CHECK(SU_Base_SetTime, r2, t2->year == t->year && t2->mon == t->mon && t2->mday == t->mday, "same time written back, now %04d-%02d-%02d %02d:%02d:%02d", t2->year, t2->mon, t2->mday, t2->hour, t2->min, t2->sec);
        }
        gfree(t); gfree(t2);
    }
    SUNEED(SU_Base_SetAlarm) {
        G(SUTime, a); G(SUTime, g);
        int r2;

        a->year = 2030; a->mon = 1; a->mday = 1; a->hour = 12; a->min = 0; a->sec = 0;
        r2 = f_SU_Base_SetAlarm(a);
        RET0(SU_Base_SetAlarm, r2, "2030-01-01 12:00:00");
        SUNEED(SU_Base_GetAlarm) {
            int r3 = f_SU_Base_GetAlarm(g); gchk(g);
            CHECK(SU_Base_GetAlarm, r3, g->year == 2030 && g->mon == 1 && g->mday == 1 && g->hour == 12, "%04d-%02d-%02d %02d:%02d:%02d", g->year, g->mon, g->mday, g->hour, g->min, g->sec);
        }
        SUNEED(SU_Base_DisableAlarm) { int r3 = f_SU_Base_DisableAlarm(); RET0(SU_Base_DisableAlarm, r3, ""); }
        gfree(a); gfree(g);
    }
    }
    rep(FN(SU_Base_EnableAlarm), 0, V_SKIP, "an enabled RTC alarm can wake/power the camera");
    rep(FN(SU_Base_PollingAlarm), 0, V_SKIP, "blocks until the alarm fires");
    rep(FN(SU_Base_Shutdown), 0, V_SKIP, "powers the camera off");
    rep(FN(SU_Base_Reboot), 0, V_SKIP, "reboots the camera");
    rep(FN(SU_Base_Suspend), 0, V_SKIP, "suspends the camera");
#if HAS_SU_Base_SetWkupMode
    /* modes 1 and 2 arm an RTC wake-up: never sent.  A mode outside 1..2 must be refused without touching the hardware; the symbol found first
     * (OpenIMP's libimp when it exports one, else the board's libsysutils) answers, the detail names the library. */
    {
        __typeof__(&SU_Base_SetWkupMode) fw = (__typeof__(&SU_Base_SetWkupMode))dlsym(RTLD_DEFAULT, "SU_Base_SetWkupMode");
        Dl_info di;

        if (!fw && g_su) fw = SUSYM(SU_Base_SetWkupMode);
        if (!fw) rep_na(FN(SU_Base_SetWkupMode));
        else {
            const char *lib = dladdr((void *)fw, &di) && di.dli_fname ? strrchr(di.dli_fname, '/') : NULL;
            r = fw(0);
            rep(FN(SU_Base_SetWkupMode), r, r == -1 ? V_PASS : V_FAIL, "mode 0 refused (%d) by %s; modes 1/2 not sent (arm an RTC wake-up)", r, lib ? lib + 1 : "?");
        }
    }
#endif

    /* misc / ADC / battery / cipher / key / LED */
#if HAS_SU_ADC_Init
    int has_adc = access("/dev/ingenic_adc_aux_0", F_OK) == 0 || access("/dev/jz_adc_aux_0", F_OK) == 0;
    if (!has_adc) rep(FN(SU_ADC_Init), -1, V_NA, "no /dev/ingenic_adc_aux_0 or /dev/jz_adc_aux_0 (no ADC driver; the vendor libsysutils opens the same nodes)");
    else {
    SUNEED(SU_ADC_Init) {
        r = f_SU_ADC_Init(); RET0(SU_ADC_Init, r, "");
        if (r == 0) {
            SUNEED(SU_ADC_EnableChn) {
                r = f_SU_ADC_EnableChn(0); RET0(SU_ADC_EnableChn, r, "chn 0");
                SUNEED(SU_ADC_GetChnValue) {
                    G(int, v);
                    int r2 = f_SU_ADC_GetChnValue(0, v); gchk(v);
                    RET0(SU_ADC_GetChnValue, r2, "chn 0 = %d", *v);
                    gfree(v);
                }
                SUNEED(SU_ADC_DisableChn) { r = f_SU_ADC_DisableChn(0); RET0(SU_ADC_DisableChn, r, "chn 0"); }
            }
            SUNEED(SU_ADC_Exit) { r = f_SU_ADC_Exit(); RET0(SU_ADC_Exit, r, ""); }
        }
    }
    }
#endif
#if HAS_SU_Battery_GetCapacity
    SUNEED(SU_Battery_GetCapacity) { r = f_SU_Battery_GetCapacity(); rep(FN(SU_Battery_GetCapacity), r, (r >= 0 && r <= 100) ? V_PASS : V_FAIL, "%d %% (negative = no battery)", r); }
    SUNEED(SU_Battery_GetVoltageUV) { r = f_SU_Battery_GetVoltageUV(); rep(FN(SU_Battery_GetVoltageUV), r, r >= 0 ? V_PASS : V_FAIL, "%d uV", r); }
    SUNEED(SU_Battery_GetStatus) { G(SUBatStatus, v); r = f_SU_Battery_GetStatus(v); gchk(v); RET0(SU_Battery_GetStatus, r, "status %d", (int)*v); gfree(v); }
    rep(FN(SU_Battery_GetEvent), 0, V_SKIP, "blocks until a battery event");
#endif
    SUNEED(SU_Key_OpenEvent) {
        int fd = f_SU_Key_OpenEvent();

        int nodev = fd < 0 && access("/dev/input/event0", F_OK) != 0;   /* no key / input device on this board */

        rep(FN(SU_Key_OpenEvent), fd, fd >= 0 ? V_PASS : nodev ? V_NA : V_FAIL, "event fd %d%s", fd, nodev ? " (no /dev/input/event* on this board)" : "");
        SUNEED(SU_Key_CloseEvent) {
            if (nodev) rep(FN(SU_Key_CloseEvent), 0, V_NA, "no input device, nothing to close");
            else { r = fd >= 0 ? f_SU_Key_CloseEvent(fd) : -1; RET0(SU_Key_CloseEvent, r, "fd %d", fd); }
        }
    }
    rep(FN(SU_Key_ReadEvent), 0, V_SKIP, "blocks until a key is pressed");
    rep(FN(SU_Key_EnableEvent), 0, V_SKIP, "masks a hardware key in the input driver");
    rep(FN(SU_Key_DisableEvent), 0, V_SKIP, "masks a hardware key in the input driver");
    rep(FN(SU_LED_Command), 0, V_SKIP, "drives the board LED");
    rep(FN(SU_CIPHER_Init), 0, V_SKIP, "hardware AES/DES DMA engine, not part of the base/misc set");
    rep(FN(SU_CIPHER_Exit), 0, V_SKIP, "see CIPHER_Init");
    rep(FN(SU_CIPHER_CreateHandle), 0, V_SKIP, "see CIPHER_Init");
    rep(FN(SU_CIPHER_DestroyHandle), 0, V_SKIP, "see CIPHER_Init");
    rep(FN(SU_CIPHER_ConfigHandle), 0, V_SKIP, "see CIPHER_Init");
    rep(FN(SU_CIPHER_Encrypt), 0, V_SKIP, "see CIPHER_Init");
    rep(FN(SU_CIPHER_Decrypt), 0, V_SKIP, "see CIPHER_Init");
#if HAS_SU_CIPHER_DES_Init
    rep(FN(SU_CIPHER_DES_Init), 0, V_SKIP, "see CIPHER_Init");
    rep(FN(SU_CIPHER_DES_Exit), 0, V_SKIP, "see CIPHER_Init");
    rep(FN(SU_CIPHER_DES_Test), 0, V_SKIP, "see CIPHER_Init");
#endif
    if (g_su) dlclose(g_su);
}
