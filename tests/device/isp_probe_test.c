/*
 * Host test of isp_probe_preload.c: a fake libimp in the executable
 * (-rdynamic, so dlsym(RTLD_DEFAULT) finds it) records what the probe
 * passes; commands go through the real named pipe and the log is checked.
 * One function (SetTemperDnsCtl) is left out to check "not available".
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %d: ", __LINE__); fprintf(stderr, __VA_ARGS__); \
    fputc('\n', stderr); } } while (0)

static unsigned char brightness = 128;
static int32_t colorfx, scene, runmode;
static struct { uint32_t mode; uint16_t r, b; } wb;
static struct { uint32_t type; uint8_t strength; } temper;
static struct { uint32_t enable, type; uint8_t strength; } sinter;
static struct { uint32_t mode; uint8_t strength, smax, smin; uint16_t bl, wl; } drc;

int IMP_ISP_Tuning_SetBrightness(unsigned char v) { brightness = v; return 0; }
int IMP_ISP_Tuning_GetBrightness(unsigned char *v) { *v = brightness; return 0; }
int IMP_ISP_Tuning_SetColorfxMode(int32_t v) { colorfx = v; return 0; }
int IMP_ISP_Tuning_GetColorfxMode(int32_t *v) { *v = colorfx; return 0; }
int IMP_ISP_Tuning_SetSceneMode(int32_t v) { scene = v; return v == 99 ? -1 : 0; }
int IMP_ISP_Tuning_GetSceneMode(int32_t *v) { *v = scene; return 0; }
int IMP_ISP_Tuning_SetISPRunningMode(int32_t v) { runmode = v; return 0; }
int IMP_ISP_Tuning_GetISPRunningMode(int32_t *v) { *v = runmode; return 0; }
int IMP_ISP_Tuning_SetWB(void *p) { memcpy(&wb, p, 8); return 0; }
int IMP_ISP_Tuning_GetWB(void *p) { memcpy(p, &wb, 8); return 0; }
int IMP_ISP_Tuning_SetTemperDnsAttr(void *p) { memcpy(&temper, p, 8); return 0; }
int IMP_ISP_Tuning_GetTemperDnsAttr(void *p) { memcpy(p, &temper, 8); return 0; }
int IMP_ISP_Tuning_SetSinterDnsAttr(void *p) { memcpy(&sinter, p, 12); return 0; }
int IMP_ISP_Tuning_GetSinterDnsAttr(void *p) { memcpy(p, &sinter, 12); return 0; }
int IMP_ISP_Tuning_SetRawDRC(void *p) { memcpy(&drc, p, 12); return 0; }
int IMP_ISP_Tuning_GetRawDRC(void *p) { memcpy(p, &drc, 12); return 0; }

static int log_has(const char *path, const char *text)
{
    char line[512];
    FILE *f = fopen(path, "r");
    int found = 0;

    if (!f)
        return 0;
    while (fgets(line, sizeof(line), f))
        if (strstr(line, text))
            found = 1;
    fclose(f);
    return found;
}

int main(void)
{
    const char *fifo = getenv("ISPPROBE_FIFO");
    const char *log = getenv("ISPPROBE_LOG");
    const char *commands =
        "brightness 200\ncolorfx 2\nscene 99\nrunningmode night\n"
        "wb 4\ntemperattr manual 255\nsinterattr manual 7\nrawdrc 0 33\n"
        "temperctl auto\nbrightness 300\nget all\nbogus 1\n";
    FILE *out;
    int i;

    if (!fifo || !log)
        return 2;
    for (i = 0; i < 200 && access(fifo, F_OK) != 0; i++)
        usleep(10000);
    out = fopen(fifo, "w");
    if (!out)
        return 2;
    fputs(commands, out);
    fclose(out);
    for (i = 0; i < 200 && !log_has(log, "unknown command bogus"); i++)
        usleep(10000);

    CHECK(brightness == 200, "brightness %u", brightness);
    CHECK(colorfx == 2 && log_has(log, "get colorfx: ret=0 value=2"), "colorfx");
    CHECK(log_has(log, "scene 99: ret=-1"), "scene return value not logged");
    CHECK(runmode == 1, "runningmode night -> %d", runmode);
    CHECK(wb.mode == 4 && log_has(log, "get wb: ret=0 mode=4"), "wb");
    CHECK(temper.type == 2 && temper.strength == 255, "temperattr");
    CHECK(sinter.enable == 1 && sinter.type == 1 && sinter.strength == 7,
          "sinterattr");
    CHECK(drc.mode == 0 && drc.strength == 33 && drc.wl == 0xfff, "rawdrc");
    CHECK(log_has(log, "temperctl: IMP_ISP_Tuning_SetTemperDnsCtl not available"),
          "missing function not reported");
    CHECK(log_has(log, "brightness: bad arguments") && brightness == 200,
          "out-of-range brightness accepted");
    CHECK(log_has(log, "get maxagain: IMP_ISP_Tuning_GetMaxAgain not available"),
          "get all");
    if (failures) {
        fprintf(stderr, "isp probe: %d check(s) failed\n", failures);
        return 1;
    }
    printf("isp probe tests passed\n");
    return 0;
}
