/*
 * su_sysutils.c - the vendor libsysutils functions of the T40 backlog (#70) inside libimp:
 * RTC time / alarm, reboot, suspend, ADC, key events, LED.  Ported from the vendor libsysutils
 * 1.3.1 disassembly (sysutils/base/base.c, misc/adc.c, misc/key.c, misc/led.c); the open stack
 * ships no libsysutils.so, so a program that links the SU_* names finds them here.
 *
 *   SU_Base_GetTime/SetTime       /dev/rtc0 RTC_RD_TIME / RTC_SET_TIME, month 1..12, year as is
 *   SU_Base_GetAlarm/SetAlarm     RTC_ALM_READ / RTC_ALM_SET
 *   SU_Base_EnableAlarm           refuses (-1) unless the alarm lies after the current time,
 *                                 then RTC_AIE_ON; SU_Base_DisableAlarm RTC_AIE_OFF
 *   SU_Base_PollingAlarm(ms)      select() on the RTC fd; the vendor returns 0 also on timeout
 *   SU_Base_Reboot                sync(), SIGTERM to init (busybox: reboot), returns 0
 *   SU_Base_Suspend               writes "mem" to /sys/power/state
 *   SU_ADC_*                      six channels /dev/ingenic_adc_aux_0..5: ioctl 1 enable, 2 disable, read(4) value
 *   SU_Key_*                      the input device named "gpio_keys"; Enable/DisableEvent edit the gpio_keys
 *                                 sysfs lists (keys, disabled_keys)
 *   SU_LED_Command(n, cmd)        sysfs GPIO n: export, active_low 0, direction out, value, unexport for cmd 2
 *   SU_CIPHER_*                   not provided by the open stack: the vendor functions need the Ingenic AES/DES
 *                                 kernel drivers (/dev/aes, /dev/jz-des) which the open stack does not ship; they
 *                                 answer -1 with errno ENOTSUP (the vendor answers -1 on a kernel without them)
 *
 * Differences to the vendor library (documented): a NULL argument is -1 instead of a fault; the
 * key lookup returns -1 when no gpio_keys device exists (the vendor opens an uninitialised path);
 * every handle the vendor leaks on an error path is closed; no imp_log output (stderr "openimp-su").
 * Host tests replace the device nodes and the ioctl through the OPENIMP_SU_* macros.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef OPENIMP_SU_IOCTL
#define OPENIMP_SU_IOCTL ioctl
#endif
#ifndef OPENIMP_SU_KILL
#define OPENIMP_SU_KILL kill
#endif
#ifndef OPENIMP_SU_SYNC
#define OPENIMP_SU_SYNC sync
#endif
#ifndef OPENIMP_SU_RTC
#define OPENIMP_SU_RTC "/dev/rtc0"
#endif
#ifndef OPENIMP_SU_POWER_STATE
#define OPENIMP_SU_POWER_STATE "/sys/power/state"
#endif
#ifndef OPENIMP_SU_ADC_PREFIX
#define OPENIMP_SU_ADC_PREFIX "/dev/ingenic_adc_aux_"
#endif
#ifndef OPENIMP_SU_INPUT_DIR
#define OPENIMP_SU_INPUT_DIR "/dev/input"
#endif
#ifndef OPENIMP_SU_KEYS
#define OPENIMP_SU_KEYS "/sys/devices/platform/gpio_keys/keys"
#endif
#ifndef OPENIMP_SU_DISABLED_KEYS
#define OPENIMP_SU_DISABLED_KEYS "/sys/devices/platform/gpio_keys/disabled_keys"
#endif
#ifndef OPENIMP_SU_GPIO
#define OPENIMP_SU_GPIO "/sys/class/gpio/"
#endif

#define SU_ERR(...) do { fprintf(stderr, "openimp-su: " __VA_ARGS__); fputc('\n', stderr); } while (0)

/* SUTime of sysutils/su_base.h: sec, min, hour, mday, mon (1..12), year (>1900) */
typedef struct { int sec, min, hour, mday, mon, year; } su_time;

/* struct rtc_time of linux/rtc.h (36 bytes) and the ioctl numbers of /dev/rtc */
struct su_rtc_time { int sec, min, hour, mday, mon, year, wday, yday, isdst; };
#define SU_RTC_AIE_ON 0x20007001U
#define SU_RTC_AIE_OFF 0x20007002U
#define SU_RTC_ALM_SET 0x80247007U
#define SU_RTC_ALM_READ 0x40247008U
#define SU_RTC_RD_TIME 0x40247009U
#define SU_RTC_SET_TIME 0x8024700aU

static int rtc_read(unsigned int cmd, su_time *t, const char *what)
{
    struct su_rtc_time r;
    int fd, rc;

    if (!t)
        return -1;
    fd = open(OPENIMP_SU_RTC, O_RDONLY);
    if (fd < 0) {
        SU_ERR("%s: open %s: %s", what, OPENIMP_SU_RTC, strerror(errno));
        return -1;
    }
    memset(&r, 0, sizeof(r));
    rc = OPENIMP_SU_IOCTL(fd, cmd, &r);
    close(fd);
    if (rc < 0) {
        SU_ERR("%s: ioctl failed: %s", what, strerror(errno));
        return -1;
    }
    t->sec = r.sec; t->min = r.min; t->hour = r.hour; t->mday = r.mday;
    t->mon = r.mon + 1; t->year = r.year + 1900;
    return 0;
}

static int rtc_write(unsigned int cmd, const su_time *t, const char *what)
{
    struct su_rtc_time r;
    int fd, rc;

    if (!t)
        return -1;
    fd = open(OPENIMP_SU_RTC, O_RDONLY);
    if (fd < 0) {
        SU_ERR("%s: open %s: %s", what, OPENIMP_SU_RTC, strerror(errno));
        return -1;
    }
    memset(&r, 0, sizeof(r));
    r.sec = t->sec; r.min = t->min; r.hour = t->hour; r.mday = t->mday;
    r.mon = t->mon - 1; r.year = t->year - 1900;
    rc = OPENIMP_SU_IOCTL(fd, cmd, &r);
    if (rc == -1)
        SU_ERR("%s: ioctl failed: %s", what, strerror(errno));
    close(fd);
    return rc;
}

int SU_Base_GetTime(su_time *t)
{
    return rtc_read(SU_RTC_RD_TIME, t, "SU_Base_GetTime");
}

int SU_Base_SetTime(su_time *t)
{
    return rtc_write(SU_RTC_SET_TIME, t, "SU_Base_SetTime");
}

int SU_Base_GetAlarm(su_time *t)
{
    return rtc_read(SU_RTC_ALM_READ, t, "SU_Base_GetAlarm");
}

int SU_Base_SetAlarm(su_time *t)
{
    return rtc_write(SU_RTC_ALM_SET, t, "SU_Base_SetAlarm");
}

static int su_time_after(const su_time *a, const su_time *b)     /* a later than b */
{
    if (a->year != b->year) return a->year > b->year;
    if (a->mon != b->mon) return a->mon > b->mon;
    if (a->mday != b->mday) return a->mday > b->mday;
    if (a->hour != b->hour) return a->hour > b->hour;
    if (a->min != b->min) return a->min > b->min;
    return a->sec > b->sec;
}

static int rtc_ctl(unsigned int cmd, const char *what)
{
    int fd = open(OPENIMP_SU_RTC, O_RDONLY), rc;

    if (fd < 0) {
        SU_ERR("%s: open %s: %s", what, OPENIMP_SU_RTC, strerror(errno));
        return -1;
    }
    rc = OPENIMP_SU_IOCTL(fd, cmd, 0);
    if (rc == -1)
        SU_ERR("%s: ioctl failed: %s", what, strerror(errno));
    close(fd);
    return rc;
}

int SU_Base_EnableAlarm(void)
{
    su_time now, alarm;

    if (SU_Base_GetTime(&now) < 0 || SU_Base_GetAlarm(&alarm) < 0)
        return -1;
    if (!su_time_after(&alarm, &now)) {
        SU_ERR("SU_Base_EnableAlarm: the alarm time is not after the current time");
        return -1;
    }
    return rtc_ctl(SU_RTC_AIE_ON, "SU_Base_EnableAlarm");
}

int SU_Base_DisableAlarm(void)
{
    return rtc_ctl(SU_RTC_AIE_OFF, "SU_Base_DisableAlarm");
}

int SU_Base_PollingAlarm(uint32_t timeoutMsec)
{
    fd_set set;
    struct timeval tv;
    int fd = open(OPENIMP_SU_RTC, O_RDONLY), rc;

    if (fd < 0) {
        SU_ERR("SU_Base_PollingAlarm: open %s: %s", OPENIMP_SU_RTC, strerror(errno));
        return -1;
    }
    FD_ZERO(&set);
    FD_SET(fd, &set);
    tv.tv_sec = timeoutMsec / 1000;
    tv.tv_usec = (timeoutMsec % 1000) * 1000;
    rc = select(fd + 1, &set, NULL, NULL, &tv);
    if (rc < 0)
        SU_ERR("SU_Base_PollingAlarm: select error: %s", strerror(errno));
    else if (rc == 0)
        SU_ERR("SU_Base_PollingAlarm: select timeout");
    close(fd);
    return 0;               /* the vendor returns 0 also after an error or a timeout */
}

int SU_Base_Reboot(void)
{
    OPENIMP_SU_SYNC();
    OPENIMP_SU_KILL(1, SIGTERM);
    return 0;
}

static pthread_mutex_t su_suspend_lock = PTHREAD_MUTEX_INITIALIZER;

int SU_Base_Suspend(void)
{
    int fd, rc = 0;

    pthread_mutex_lock(&su_suspend_lock);
    fd = open(OPENIMP_SU_POWER_STATE, O_RDWR);
    if (fd < 0) {
        SU_ERR("SU_Base_Suspend: open %s: %s", OPENIMP_SU_POWER_STATE, strerror(errno));
        rc = -1;
    } else {
        if (write(fd, "mem", 3) != 3) {
            SU_ERR("SU_Base_Suspend: write failed: %s", strerror(errno));
            rc = -1;
        }
        close(fd);
    }
    pthread_mutex_unlock(&su_suspend_lock);
    return rc;
}

/* ---- ADC ---- */
#define SU_ADC_CHANNELS 6
static int su_adc_fd[SU_ADC_CHANNELS];
static int su_adc_ready;

int SU_ADC_Init(void)
{
    int fds[SU_ADC_CHANNELS], i;
    char path[256];

    for (i = 0; i < SU_ADC_CHANNELS; i++) {
        snprintf(path, sizeof(path), "%s%d", OPENIMP_SU_ADC_PREFIX, i);
        fds[i] = open(path, O_RDONLY);
        if (fds[i] < 0) {
            SU_ERR("SU_ADC_Init: open path %s error: %s", path, strerror(errno));
            while (i--)
                close(fds[i]);
            return -1;
        }
    }
    memcpy(su_adc_fd, fds, sizeof(fds));
    su_adc_ready = 1;
    return 0;
}

int SU_ADC_Exit(void)
{
    int i;

    if (su_adc_ready)
        for (i = 0; i < SU_ADC_CHANNELS; i++)
            close(su_adc_fd[i]);
    su_adc_ready = 0;
    return 0;
}

static int su_adc_ctl(uint32_t chn, int cmd, const char *what)
{
    if (!su_adc_ready) {
        SU_ERR("%s: adc is NULL !", what);
        return -1;
    }
    if (chn >= SU_ADC_CHANNELS) {
        SU_ERR("%s: chn_num error !", what);
        return -1;
    }
    if (OPENIMP_SU_IOCTL(su_adc_fd[chn], cmd, 0) < 0) {
        SU_ERR("%s: chn_num=%u, ioctl(%d, %d): %s", what, chn, su_adc_fd[chn], cmd, strerror(errno));
        return -1;
    }
    return 0;
}

int SU_ADC_EnableChn(uint32_t chn)
{
    return su_adc_ctl(chn, 1, "SU_ADC_EnableChn");
}

int SU_ADC_DisableChn(uint32_t chn)
{
    return su_adc_ctl(chn, 2, "SU_ADC_DisableChn");
}

int SU_ADC_GetChnValue(uint32_t chn, int *value)
{
    if (!su_adc_ready) {
        SU_ERR("SU_ADC_GetChnValue: adc is NULL !");
        return -1;
    }
    if (chn >= SU_ADC_CHANNELS) {
        SU_ERR("SU_ADC_GetChnValue: chn_num error !");
        return -1;
    }
    if (!value) {
        SU_ERR("SU_ADC_GetChnValue: value error !");
        return -1;
    }
    if (read(su_adc_fd[chn], value, 4) < 0) {
        SU_ERR("SU_ADC_GetChnValue: chn_num=%u, read(%d) failed: %s", chn, su_adc_fd[chn], strerror(errno));
        return -1;
    }
    return 0;
}

/* ---- keys ---- */
#define SU_KEY_MAX 32
#define SU_EVIOCGNAME79 0x404f4506U

struct su_key { uint16_t code, disabled; };
static struct su_key su_keys[SU_KEY_MAX];
static int su_nkeys;
static int su_keys_loaded;

int SU_Key_OpenEvent(void)
{
    DIR *d = opendir(OPENIMP_SU_INPUT_DIR);
    struct dirent *e;
    char path[512];
    int found = 0;

    if (!d)
        return -1;
    while ((e = readdir(d)) != NULL) {
        char name[80];
        int fd = openat(dirfd(d), e->d_name, O_RDONLY);

        if (fd < 0)
            continue;
        memset(name, 0, sizeof(name));
        if (OPENIMP_SU_IOCTL(fd, SU_EVIOCGNAME79, name) <= 0) {
            close(fd);
            continue;
        }
        close(fd);
        if (!strcmp(name, "gpio_keys")) {
            snprintf(path, sizeof(path), "%s/%s", OPENIMP_SU_INPUT_DIR, e->d_name);
            found = 1;
            break;
        }
    }
    closedir(d);
    if (!found)
        return -1;
    return open(path, O_RDONLY);
}

int SU_Key_CloseEvent(int fd)
{
    return close(fd);
}

int SU_Key_ReadEvent(int fd, int *keyCode, int *event)
{
    struct { struct timeval tv; uint16_t type, code; int32_t value; } ev;      /* struct input_event, 16 bytes on MIPS32 */

    if (!keyCode || !event)
        return -1;
    for (;;) {
        ssize_t n = read(fd, &ev, sizeof(ev));

        if (n < 1) {
            SU_ERR("SU_Key_ReadEvent: read key event error: %s", strerror(errno));
            return -1;
        }
        if (n == (ssize_t)sizeof(ev) && ev.type == 1)       /* EV_KEY */
            break;
    }
    *event = ev.value != 0 ? 1 : 0;                         /* KEY_RELEASED 0, KEY_PRESSED 1 */
    *keyCode = ev.code & 0xff;
    return 0;
}

static int su_read_trim(int fd, char *buf, size_t size)
{
    char *p;
    ssize_t n = read(fd, buf, size - 1);

    if (n < 0)
        return -1;
    buf[n] = 0;
    if ((p = strrchr(buf, '\n')) != NULL) *p = 0;
    if ((p = strrchr(buf, '\r')) != NULL) *p = 0;
    return 0;
}

static int su_load_keys(void)
{
    char buf[128], *save = NULL, *tok;
    int fd = open(OPENIMP_SU_KEYS, O_RDONLY), n = 0;

    if (fd < 0) {
        SU_ERR("open keys error: %s", strerror(errno));
        return -1;
    }
    memset(buf, 0, sizeof(buf));
    if (su_read_trim(fd, buf, sizeof(buf)) < 0) {
        SU_ERR("read keys error: %s", strerror(errno));
        close(fd);
        return -1;
    }
    close(fd);
    for (tok = strtok_r(buf, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
        if (n == SU_KEY_MAX) {
            SU_ERR("Too many keys");
            return -1;
        }
        su_keys[n].code = (uint16_t)atoi(tok);
        su_keys[n].disabled = 0;
        n++;
    }
    su_nkeys = n;
    su_keys_loaded = 1;
    return 0;
}

/* one pass of the vendor update_enabled_keys / update_disabled_keys: `want` is the comma list of the keys
 * whose state changed; enabling removes them from the disabled_keys file content, disabling appends the
 * ones that are not in it.  The vendor writes strlen(want) + 1 bytes of the new content. */
static int su_update_keys(int disable)
{
    char want[128], cur[128], tmp[16];
    char *tok, *comma, *p;
    int fd, i, changed = 0;

    memset(want, 0, sizeof(want));
    memset(cur, 0, sizeof(cur));
    for (i = 0; i < su_nkeys; i++) {
        if ((su_keys[i].disabled != 0) == (disable != 0)) {
            snprintf(tmp, sizeof(tmp), "%d,", su_keys[i].code);
            if (strlen(want) + strlen(tmp) < sizeof(want))
                strcat(want, tmp);
        }
    }
    if (!want[0])
        return 0;
    if ((p = strrchr(want, ',')) != NULL)
        *p = 0;
    fd = open(OPENIMP_SU_DISABLED_KEYS, O_RDWR);
    if (fd < 0) {
        SU_ERR("open %s error: %s", OPENIMP_SU_DISABLED_KEYS, strerror(errno));
        return -1;
    }
    if (su_read_trim(fd, cur, sizeof(cur)) < 0) {
        SU_ERR("read disabled_keys error: %s", strerror(errno));
        close(fd);
        return -1;
    }
    tok = want;
    if (!disable) {
        if (!cur[0]) {
            close(fd);
            return 0;
        }
        do {
            char *hit;

            comma = strchr(tok, ',');
            if (comma && *comma)
                *comma = 0;
            hit = strstr(cur, tok);
            if (hit) {
                char *next = strchr(hit, ',');

                *hit = 0;
                if (next)
                    strcat(cur, next + 1);
                changed++;
            }
            tok = comma ? comma + 1 : NULL;
        } while (tok);
        if (!changed) {
            close(fd);
            return 0;
        }
    } else if (!cur[0]) {
        strcat(cur, want);
    } else {
        do {
            comma = strchr(tok, ',');
            if (comma && *comma)
                *comma = 0;
            if (!strstr(cur, tok)) {
                strcat(cur, ",");
                changed++;
                strcat(cur, tok);
            }
            tok = comma ? comma + 1 : NULL;
        } while (tok);
        if (!changed) {
            close(fd);
            return 0;
        }
    }
    if (write(fd, cur, strlen(want) + 1) < 0) {
        SU_ERR("write disabled_keys error: %s", strerror(errno));
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

static int su_key_set(int keyCode, int disable)
{
    int i;

    if (!su_keys_loaded && su_load_keys() < 0)
        return -1;
    for (i = 0; i < su_nkeys; i++)
        if (su_keys[i].code == (uint16_t)keyCode) {
            su_keys[i].disabled = (uint16_t)disable;
            break;
        }
    return su_update_keys(disable);
}

int SU_Key_EnableEvent(int keyCode)
{
    return su_key_set(keyCode, 0);
}

int SU_Key_DisableEvent(int keyCode)
{
    return su_key_set(keyCode, 1);
}

/* ---- LED (sysfs GPIO) ---- */
static int su_sysfs_write(const char *path, const char *value, const char *what)
{
    char buf[64];
    int fd = open(path, O_WRONLY);

    if (fd < 0) {
        SU_ERR("SU_LED_Command: open() %s error ! (%s)", what, path);
        return -1;
    }
    memset(buf, 0, sizeof(buf));
    snprintf(buf, sizeof(buf), "%s", value);
    (void)!write(fd, buf, sizeof(buf));         /* the vendor does not look at the result either */
    close(fd);
    return 0;
}

int SU_LED_Command(int ledNum, int cmd)
{
    char path[512], val[16];

    snprintf(val, sizeof(val), "%d", ledNum);
    snprintf(path, sizeof(path), "%sexport", OPENIMP_SU_GPIO);
    if (su_sysfs_write(path, val, "export") < 0)
        return -1;
    snprintf(path, sizeof(path), "%sgpio%d/active_low", OPENIMP_SU_GPIO, ledNum);
    if (su_sysfs_write(path, "0", "active_low") < 0)
        return -1;
    snprintf(path, sizeof(path), "%sgpio%d/direction", OPENIMP_SU_GPIO, ledNum);
    if (su_sysfs_write(path, "out", "direction") < 0)
        return -1;
    snprintf(path, sizeof(path), "%sgpio%d/value", OPENIMP_SU_GPIO, ledNum);
    snprintf(val, sizeof(val), "%d", cmd > 1 ? (cmd != 2) : cmd);
    if (su_sysfs_write(path, val, "value") < 0)
        return -1;
    if (cmd != 2)
        return 0;
    snprintf(path, sizeof(path), "%sunexport", OPENIMP_SU_GPIO);
    snprintf(val, sizeof(val), "%d", ledNum);
    return su_sysfs_write(path, val, "unexport");
}

/* ---- cipher: no AES/DES kernel driver in the open stack ---- */
static int su_cipher_unsupported(void)
{
    errno = ENOTSUP;
    return -1;
}

int SU_CIPHER_Init(void) { return su_cipher_unsupported(); }
int SU_CIPHER_Exit(void) { return su_cipher_unsupported(); }
int SU_CIPHER_DES_Init(void) { return su_cipher_unsupported(); }
int SU_CIPHER_DES_Exit(void) { return su_cipher_unsupported(); }
int SU_CIPHER_DES_Test(void) { return su_cipher_unsupported(); }
int SU_CIPHER_CreateHandle(void) { return su_cipher_unsupported(); }
int SU_CIPHER_DestroyHandle(int fd) { (void)fd; return su_cipher_unsupported(); }
int SU_CIPHER_ConfigHandle(int h, void *ctrl) { (void)h; (void)ctrl; return su_cipher_unsupported(); }
int SU_CIPHER_Encrypt(int h, unsigned int *src, unsigned int *dst, unsigned int len)
{
    (void)h; (void)src; (void)dst; (void)len;
    return su_cipher_unsupported();
}
int SU_CIPHER_Decrypt(int h, unsigned int *src, unsigned int *dst, unsigned int len)
{
    (void)h; (void)src; (void)dst; (void)len;
    return su_cipher_unsupported();
}
