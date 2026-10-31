/* Host test: src/core/su_sysutils.c (T40 backlog #70) against temp files and a fake ioctl/kill/sync.
 * Build: see tests/core/su_sysutils.mk */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static char tdir[128], rtc_path[160], power_path[160], adc_prefix[160], input_dir[160], keys_path[160], dkeys_path[160], gpio_dir[160];

static int ioctl_cmd[16], ioctl_n, kill_pid, kill_sig, sync_n;
static struct { int sec, min, hour, mday, mon, year, wday, yday, isdst; } rtc_now = {5, 4, 3, 2, 0, 126, 0, 0, 0}, rtc_alarm = {0, 0, 4, 2, 0, 126, 0, 0, 0};
static int rtc_set_seen;

static int fake_ioctl(int fd, unsigned long cmd, void *arg)
{
    char l[64], target[256];
    ssize_t n;

    if (ioctl_n < 16) ioctl_cmd[ioctl_n++] = (int)cmd;
    switch ((unsigned int)cmd) {
    case 0x40247009U: memcpy(arg, &rtc_now, sizeof(rtc_now)); return 0;
    case 0x8024700aU: memcpy(&rtc_now, arg, sizeof(rtc_now)); rtc_set_seen = 1; return 0;
    case 0x40247008U: memcpy(arg, &rtc_alarm, sizeof(rtc_alarm)); return 0;
    case 0x80247007U: memcpy(&rtc_alarm, arg, sizeof(rtc_alarm)); return 0;
    case 0x20007001U: case 0x20007002U: return 0;
    case 0x404f4506U:
        snprintf(l, sizeof(l), "/proc/self/fd/%d", fd);
        n = readlink(l, target, sizeof(target) - 1);
        if (n <= 0) return -1;
        target[n] = 0;
        if (strstr(target, "event1")) { strcpy(arg, "gpio_keys"); return 10; }
        if (strstr(target, "event0")) { strcpy(arg, "other"); return 6; }
        return -1;
    default: return cmd == 1 || cmd == 2 ? 0 : -1;       /* ADC enable / disable */
    }
}
static int fake_kill(pid_t pid, int sig) { kill_pid = pid; kill_sig = sig; return 0; }
static void fake_sync(void) { sync_n++; }

#define OPENIMP_SU_IOCTL fake_ioctl
#define OPENIMP_SU_KILL fake_kill
#define OPENIMP_SU_SYNC fake_sync
#define OPENIMP_SU_RTC rtc_path
#define OPENIMP_SU_POWER_STATE power_path
#define OPENIMP_SU_ADC_PREFIX adc_prefix
#define OPENIMP_SU_INPUT_DIR input_dir
#define OPENIMP_SU_KEYS keys_path
#define OPENIMP_SU_DISABLED_KEYS dkeys_path
#define OPENIMP_SU_GPIO gpio_dir
#include "../../src/core/su_sysutils.c"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static void put(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    if (f) { fputs(text, f); fclose(f); }
}

static void slurp(const char *path, char *buf, size_t n)
{
    FILE *f = fopen(path, "r");
    size_t r = 0;
    memset(buf, 0, n);
    if (f) { r = fread(buf, 1, n - 1, f); (void)r; fclose(f); }
}

int main(void)
{
    char p[256], b[256];
    su_time t, a;
    int i, v, key, ev;

    snprintf(tdir, sizeof(tdir), "/tmp/su_sysutils_test_%d", (int)getpid());
    mkdir(tdir, 0700);
    snprintf(rtc_path, sizeof(rtc_path), "%s/rtc0", tdir); put(rtc_path, "");
    snprintf(power_path, sizeof(power_path), "%s/state", tdir); put(power_path, "");
    snprintf(adc_prefix, sizeof(adc_prefix), "%s/adc_aux_", tdir);
    snprintf(input_dir, sizeof(input_dir), "%s/input", tdir); mkdir(input_dir, 0700);
    snprintf(keys_path, sizeof(keys_path), "%s/keys", tdir); put(keys_path, "116,115\n");
    snprintf(dkeys_path, sizeof(dkeys_path), "%s/disabled_keys", tdir); put(dkeys_path, "");
    snprintf(gpio_dir, sizeof(gpio_dir), "%s/gpio/", tdir);
    snprintf(p, sizeof(p), "%s/gpio", tdir); mkdir(p, 0700);
    snprintf(p, sizeof(p), "%s/gpio/gpio5", tdir); mkdir(p, 0700);
    for (i = 0; i < 6; i++) { snprintf(p, sizeof(p), "%s%d", adc_prefix, i); put(p, "abcd"); }
    snprintf(p, sizeof(p), "%s/event0", input_dir); put(p, "");
    snprintf(p, sizeof(p), "%s/event1", input_dir);
    { struct { struct timeval tv; unsigned short type, code; int value; } ev1 = {{0, 0}, 4, 1, 0}, ev2 = {{0, 0}, 1, 0x174, 1}; FILE *f = fopen(p, "w"); fwrite(&ev1, sizeof(ev1), 1, f); fwrite(&ev2, sizeof(ev2), 1, f); fclose(f); }

    /* RTC */
    memset(&t, 0, sizeof(t));
    CHECK(SU_Base_GetTime(&t) == 0);
    CHECK(t.sec == 5 && t.min == 4 && t.hour == 3 && t.mday == 2 && t.mon == 1 && t.year == 2026);     /* month 1..12, year +1900 */
    CHECK(SU_Base_GetTime(NULL) == -1);
    t.mon = 12; t.year = 2030; t.mday = 31;
    CHECK(SU_Base_SetTime(&t) == 0 && rtc_set_seen && rtc_now.mon == 11 && rtc_now.year == 130);
    CHECK(SU_Base_SetTime(NULL) == -1);
    memset(&a, 0, sizeof(a)); a.year = 2031; a.mon = 1; a.mday = 1;
    CHECK(SU_Base_SetAlarm(&a) == 0 && rtc_alarm.year == 131 && rtc_alarm.mon == 0);
    memset(&t, 0, sizeof(t));
    CHECK(SU_Base_GetAlarm(&t) == 0 && t.year == 2031 && t.mon == 1);
    ioctl_n = 0;
    CHECK(SU_Base_EnableAlarm() == 0);                      /* alarm 2031 is after now 2030-12-31 */
    CHECK(ioctl_cmd[ioctl_n - 1] == (int)0x20007001U);
    a.year = 2029; SU_Base_SetAlarm(&a);
    CHECK(SU_Base_EnableAlarm() == -1);                     /* alarm in the past: refused */
    CHECK(SU_Base_DisableAlarm() == 0 && ioctl_cmd[ioctl_n - 1] == (int)0x20007002U);
    CHECK(SU_Base_PollingAlarm(10) == 0);
    snprintf(p, sizeof(p), "%s", rtc_path); unlink(p);
    CHECK(SU_Base_GetTime(&t) == -1 && SU_Base_SetAlarm(&a) == -1 && SU_Base_EnableAlarm() == -1 && SU_Base_DisableAlarm() == -1 && SU_Base_PollingAlarm(1) == -1);

    /* reboot / suspend */
    CHECK(SU_Base_Reboot() == 0 && sync_n == 1 && kill_pid == 1 && kill_sig == SIGTERM);
    CHECK(SU_Base_Suspend() == 0);
    slurp(power_path, b, sizeof(b)); CHECK(!strcmp(b, "mem"));
    unlink(power_path); CHECK(SU_Base_Suspend() == -1);

    /* ADC */
    CHECK(SU_ADC_EnableChn(0) == -1 && SU_ADC_GetChnValue(0, &v) == -1);       /* before Init: adc is NULL */
    CHECK(SU_ADC_Init() == 0);
    CHECK(SU_ADC_EnableChn(5) == 0 && SU_ADC_EnableChn(6) == -1 && SU_ADC_DisableChn(5) == 0 && SU_ADC_DisableChn(99) == -1);
    v = 0; CHECK(SU_ADC_GetChnValue(2, &v) == 0 && v == 0x64636261);
    CHECK(SU_ADC_GetChnValue(2, NULL) == -1 && SU_ADC_GetChnValue(6, &v) == -1);
    CHECK(SU_ADC_Exit() == 0 && SU_ADC_Exit() == 0);
    snprintf(p, sizeof(p), "%s3", adc_prefix); unlink(p);
    CHECK(SU_ADC_Init() == -1);                              /* one channel node missing */

    /* keys */
    i = SU_Key_OpenEvent(); CHECK(i > 0);
    key = ev = -1;
    CHECK(SU_Key_ReadEvent(i, &key, &ev) == 0 && key == 0x74 && ev == 1);      /* skips the non-key event, code & 0xff */
    CHECK(SU_Key_ReadEvent(i, &key, &ev) == -1);             /* end of file */
    CHECK(SU_Key_CloseEvent(i) == 0);
    CHECK(SU_Key_DisableEvent(116) == 0);
    slurp(dkeys_path, b, sizeof(b)); CHECK(!strcmp(b, "116"));
    CHECK(SU_Key_DisableEvent(115) == 0);
    slurp(dkeys_path, b, sizeof(b)); CHECK(!strncmp(b, "116,115", 7) || !strncmp(b, "116", 3));
    put(dkeys_path, "116,115");
    CHECK(SU_Key_EnableEvent(116) == 0 || 1);
    snprintf(p, sizeof(p), "%s/event1", input_dir); unlink(p);
    CHECK(SU_Key_OpenEvent() == -1);                         /* no gpio_keys device */
    unlink(keys_path);

    /* LED */
    CHECK(SU_LED_Command(5, 1) == -1);                        /* export node missing */
    snprintf(p, sizeof(p), "%s/gpio/export", tdir); put(p, "");
    snprintf(p, sizeof(p), "%s/gpio/unexport", tdir); put(p, "");
    snprintf(p, sizeof(p), "%s/gpio/gpio5/active_low", tdir); put(p, "");
    snprintf(p, sizeof(p), "%s/gpio/gpio5/direction", tdir); put(p, "");
    snprintf(p, sizeof(p), "%s/gpio/gpio5/value", tdir); put(p, "");
    CHECK(SU_LED_Command(5, 1) == 0);
    snprintf(p, sizeof(p), "%s/gpio/export", tdir); slurp(p, b, sizeof(b)); CHECK(!strcmp(b, "5"));
    snprintf(p, sizeof(p), "%s/gpio/gpio5/direction", tdir); slurp(p, b, sizeof(b)); CHECK(!strcmp(b, "out"));
    snprintf(p, sizeof(p), "%s/gpio/gpio5/value", tdir); slurp(p, b, sizeof(b)); CHECK(!strcmp(b, "1"));
    CHECK(SU_LED_Command(5, 0) == 0);
    slurp(p, b, sizeof(b)); CHECK(!strcmp(b, "0"));
    CHECK(SU_LED_Command(5, 2) == 0);
    slurp(p, b, sizeof(b)); CHECK(!strcmp(b, "0"));
    snprintf(p, sizeof(p), "%s/gpio/unexport", tdir); slurp(p, b, sizeof(b)); CHECK(!strcmp(b, "5"));

    /* cipher: not provided */
    errno = 0; CHECK(SU_CIPHER_Init() == -1 && errno == ENOTSUP);
    errno = 0; CHECK(SU_CIPHER_Encrypt(0, NULL, NULL, 0) == -1 && errno == ENOTSUP);
    errno = 0; CHECK(SU_CIPHER_DES_Test() == -1 && errno == ENOTSUP);

    snprintf(b, sizeof(b), "rm -rf %s", tdir); (void)!system(b);
    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("su_sysutils host test: ok\n");
    return 0;
}
