/* Test pattern runtime control: OpenIMP_TestPattern_Set/Get (include/openimp_testpattern.h) and the
 * render entry point of the frame hooks.  See src/openimp_testpattern_gen.h for the pictures. */
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <string.h>
#include "openimp_testpattern_gen.h"
#include "openimp_testpattern_rt.h"

#define VIS __attribute__((visibility("default")))

VIS const int OpenIMP_Cap_TestPattern = 1;

int openimp_tp_mode_v;
static OpenIMPTestPatternParams slot[2];
static int slot_idx;
static uint32_t epoch;
static uint64_t frames;
static struct openimp_tp_stats stats;
static pthread_mutex_t set_lock = PTHREAD_MUTEX_INITIALIZER;   /* Set/Get only, never the frame path */

/* ---- mode 4: sensor built-in test pattern.  The frame hooks keep running in mode 0 (openimp_tp_mode_v
 * stays 0), only Get reports 4.  Register access goes through the platform hook below (stock ABI sensor
 * register ioctl of the ISP driver -> sensor s_register, same I2C path and lock as the driver itself);
 * builds without it (host tests may define one) report ENODEV.  The table matches the registered sensor
 * name by prefix; entries are SoC independent (the bit lives in the sensor).  Evidence:
 * audit/ISP_TESTINPUT_1019.md (rdwrapper scratch). */
/* set 1 = write *val, 0 = read into *val, -1 = only fill name; name always gets the sensor name (32 bytes)
 * when a sensor is registered.  Returns 0 or -1. */
int openimp_tp_sensor_reg(int set, uint32_t reg, uint32_t *val, char name[32]) __attribute__((weak));

struct tp_sensor_entry {
    const char *name;                   /* registered sensor name, exact */
    uint16_t id_hi, id_lo;              /* chip id registers: read first, proves the register path */
    uint16_t id;
    uint16_t reg;                       /* test pattern register and bit */
    uint8_t mask;
    const char *desc;
};

/* SmartSens SC*: 0x4501 bit3 = test pattern (incremental ramp), checked on sc2336 (T23), sc4336p (T31),
 * sc5336 (T41, 0xc4 -> 0xcc), sc830ai (T40, 0xc4 -> 0xcc) by I2C; sc1a4t same family, not checked.
 * SOI JX-*: 0x0c bit0 = test mode (Ingenic SDK jxh42.c: "0x41 test mode, 0x40 DVP output").  Device T20 .107
 * jxf23: vertical colour columns, but the T20/T21 jxf23 drivers rewrite 0x0c in set_analog_gain, so the
 * pattern ends at the next AE gain step (hold AE / fixed gain, or Set(4) again).
 * GalaxyCore gc5603: no source for the register yet, no entry. */
static const struct tp_sensor_entry tp_sensor_tab[] = {
    { "sc2336p", 0x3107, 0x3108, 0x9b3a, 0x4501, 0x08, "ramp" },
    { "sc2336",  0x3107, 0x3108, 0xcb3a, 0x4501, 0x08, "ramp" },
    { "sc4336p", 0x3107, 0x3108, 0x9c42, 0x4501, 0x08, "ramp" },
    { "sc5336",  0x3107, 0x3108, 0xce50, 0x4501, 0x08, "ramp" },
    { "sc830ai", 0x3107, 0x3108, 0xc143, 0x4501, 0x08, "ramp" },
    { "sc1a4t",  0x3107, 0x3108, 0xda4d, 0x4501, 0x08, "ramp (unverified)" },
    { "jxf23",   0x000a, 0x000b, 0x0f23, 0x000c, 0x01, "colour columns (AE gain step clears it)" },
    { "jxf22",   0x000a, 0x000b, 0x0f22, 0x000c, 0x01, "test mode (unverified)" },
    { "jxh42",   0x000a, 0x000b, 0xa042, 0x000c, 0x01, "test mode (unverified)" },
};

static int tp_sensor_active;            /* Get reports mode 4 */
static const struct tp_sensor_entry *tp_sensor_cur;

static const struct tp_sensor_entry *tp_sensor_lookup(char name[32])
{
    size_t i;

    if (!openimp_tp_sensor_reg) {
        errno = ENODEV;
        return NULL;
    }
    name[0] = 0;
    if (openimp_tp_sensor_reg(-1, 0, NULL, name) != 0 || !name[0]) {
        errno = ENODEV;
        return NULL;
    }
    name[31] = 0;
    for (i = 0; i < sizeof tp_sensor_tab / sizeof tp_sensor_tab[0]; i++)
        if (!strcmp(name, tp_sensor_tab[i].name))
            return &tp_sensor_tab[i];
    errno = ENOTSUP;
    return NULL;
}

/* chip id check (a driver whose register ioctl is a no-op returns no id), then read-modify-write and
 * read back.  on sets, off clears the bit (also right after the driver reloaded its init table). */
static int tp_sensor_bit(const struct tp_sensor_entry *e, int on)
{
    char name[32];
    uint32_t hi = 0, lo = 0, v = 0, nv, rb = 0;
    int r1 = openimp_tp_sensor_reg(0, e->id_hi, &hi, name);
    int r2 = openimp_tp_sensor_reg(0, e->id_lo, &lo, name);

    if (r1 != 0 || r2 != 0 || (((hi & 0xffu) << 8) | (lo & 0xffu)) != e->id ||
        openimp_tp_sensor_reg(0, e->reg, &v, name) != 0) {
        fprintf(stderr, "[TP] sensor %s: id read %d/%d 0x%02x%02x (want 0x%04x), no register access\n",
                e->name, r1, r2, hi & 0xffu, lo & 0xffu, e->id);
        goto eio;
    }
    nv = on ? (v | e->mask) : (v & ~(uint32_t)e->mask);
    if (nv == v)
        return 0;
    if (openimp_tp_sensor_reg(1, e->reg, &nv, name) != 0 ||
        openimp_tp_sensor_reg(0, e->reg, &rb, name) != 0 || (rb & 0xffu) != (nv & 0xffu)) {
        fprintf(stderr, "[TP] sensor %s: 0x%04x 0x%02x -> 0x%02x, read back 0x%02x\n",
                e->name, e->reg, v & 0xffu, nv & 0xffu, rb & 0xffu);
        goto eio;
    }
    fprintf(stderr, "[TP] sensor %s: 0x%04x 0x%02x -> 0x%02x (%s)\n", e->name, e->reg, v & 0xffu,
            nv & 0xffu, on ? e->desc : "off");
    return 0;
eio:
    errno = EIO;
    return -1;
}

VIS int OpenIMP_TestPattern_SensorInfo(char name[32], uint32_t *reg, uint32_t *mask, const char **desc)
{
    char n[32];
    const struct tp_sensor_entry *e = tp_sensor_lookup(n);

    if (name)
        memcpy(name, n, sizeof n);
    if (!e)
        return -1;
    if (reg) *reg = e->reg;
    if (mask) *mask = e->mask;
    if (desc) *desc = e->desc;
    return 0;
}

/* called with set_lock held; mode 4 on (want != 0) or off */
static int tp_sensor_switch(int want)
{
    if (want) {
        char name[32];
        const struct tp_sensor_entry *e = tp_sensor_lookup(name);

        if (!e || tp_sensor_bit(e, 1) != 0)
            return -1;
        tp_sensor_cur = e;
        tp_sensor_active = 1;
        return 0;
    }
    if (tp_sensor_active) {
        tp_sensor_active = 0;            /* even on error: a stopped sensor reloads its init table anyway */
        return tp_sensor_bit(tp_sensor_cur, 0);
    }
    return 0;
}

/* ---- optional control socket (OPENIMP_TEST_PATTERN_CTL=<path>): lets tools/testpattern/tpctl drive the
 * pattern of a running process (timps, roitest) from outside.  One datagram per command:
 *   get
 *   set <mode> [step=N] [logo=0..1] [noise=N] [obj_size=N] [obj_contrast=N] [obj_speed=N]
 *              [obj_cell=X,Y] [obj_on_ms=N] [obj_period_ms=N] [script=PATH]
 * The reply (datagram to the sender) is "ok ..." or "error ...".  Started only when the variable is set. */
static void tp_state_text(char *out, size_t n)
{
    OpenIMPTestPatternState s;

    s.size = (uint32_t)sizeof s;
    OpenIMP_TestPattern_Get(&s);
    snprintf(out, n, "ok mode=%d frames=%llu objects_shown=%llu objects_active=%u step=%u logo=%u noise=%d "
             "obj_size=%u obj_contrast=%d obj_speed=%u obj_cell=%u,%u obj_on_ms=%u obj_period_ms=%u script=%s",
             s.mode, (unsigned long long)s.frames, (unsigned long long)s.objects_shown, s.objects_active,
             s.params.step, s.params.logo, s.params.noise, s.params.obj_size, s.params.obj_contrast,
             s.params.obj_speed, s.params.obj_cell_x, s.params.obj_cell_y, s.params.obj_on_ms,
             s.params.obj_period_ms, s.params.script);
}

static void tp_ctl_line(char *line, char *rep, size_t rn)
{
    char *save = NULL, *tok = strtok_r(line, " \t\r\n", &save);

    if (tok && !strcmp(tok, "get")) {
        tp_state_text(rep, rn);
    } else if (tok && !strcmp(tok, "set") && (tok = strtok_r(NULL, " \t\r\n", &save))) {
        OpenIMPTestPatternState s;
        int mode = atoi(tok);
        unsigned a, b;

        s.size = (uint32_t)sizeof s;
        OpenIMP_TestPattern_Get(&s);
        while ((tok = strtok_r(NULL, " \t\r\n", &save))) {
            OpenIMPTestPatternParams *p = &s.params;
            char *v = strchr(tok, '=');

            if (!v)
                goto bad;
            *v++ = 0;
            if (!strcmp(tok, "step")) p->step = (uint32_t)atoi(v);
            else if (!strcmp(tok, "logo")) p->logo = (uint32_t)atoi(v);
            else if (!strcmp(tok, "noise")) p->noise = atoi(v);
            else if (!strcmp(tok, "obj_size")) p->obj_size = (uint32_t)atoi(v);
            else if (!strcmp(tok, "obj_contrast")) p->obj_contrast = atoi(v);
            else if (!strcmp(tok, "obj_speed")) p->obj_speed = (uint32_t)atoi(v);
            else if (!strcmp(tok, "obj_on_ms")) p->obj_on_ms = (uint32_t)atoi(v);
            else if (!strcmp(tok, "obj_period_ms")) p->obj_period_ms = (uint32_t)atoi(v);
            else if (!strcmp(tok, "obj_cell") && sscanf(v, "%u,%u", &a, &b) == 2) { p->obj_cell_x = a; p->obj_cell_y = b; }
            else if (!strcmp(tok, "script")) snprintf(p->script, sizeof p->script, "%s", v);
            else goto bad;
        }
        if (OpenIMP_TestPattern_Set(mode, &s.params) == 0)
            tp_state_text(rep, rn);
        else if (errno == EINVAL)
            snprintf(rep, rn, "error: parameter out of range");
        else
            snprintf(rep, rn, "error: sensor pattern: %s", strerror(errno));
    } else {
bad:
        snprintf(rep, rn, "error: usage: get | set <mode> [key=value ...]");
    }
}

static void *tp_ctl_thread(void *arg)
{
    int fd = (int)(intptr_t)arg;

    for (;;) {
        char line[512], rep[640];
        struct sockaddr_un from;
        socklen_t fl = sizeof from;
        ssize_t n = recvfrom(fd, line, sizeof line - 1, 0, (struct sockaddr *)&from, &fl);

        if (n < 0)
            continue;
        line[n] = 0;
        tp_ctl_line(line, rep, sizeof rep);
        if (fl > sizeof(sa_family_t))
            (void)sendto(fd, rep, strlen(rep), 0, (struct sockaddr *)&from, fl);
    }
    return NULL;
}

static void tp_ctl_start(const char *path)
{
    struct sockaddr_un a;
    pthread_t th;
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);

    if (fd < 0)
        return;
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    snprintf(a.sun_path, sizeof a.sun_path, "%s", path);
    unlink(path);
    if (bind(fd, (struct sockaddr *)&a, sizeof a) < 0 || pthread_create(&th, NULL, tp_ctl_thread, (void *)(intptr_t)fd)) {
        close(fd);
        return;
    }
    pthread_detach(th);
}

__attribute__((constructor)) static void tp_init(void)
{
    const char *e;
    OpenIMPTestPatternParams *p = &slot[0];

    OpenIMP_TestPattern_ParamsInit(p);
    if ((e = getenv("OPENIMP_TEST_PATTERN_STEP"))) {
        int v = atoi(e);
        p->step = (uint32_t)(v < 2 ? 2 : v) & ~1u;
    }
    if ((e = getenv("OPENIMP_TEST_PATTERN_NOISE")))
        p->noise = atoi(e);
    if ((e = getenv("OPENIMP_TP_LOGO")) && e[0] >= '0' && e[0] <= '1')
        p->logo = (uint32_t)(e[0] - '0');
    if ((e = getenv("OPENIMP_TEST_PATTERN_EVENTS")))
        snprintf(p->script, sizeof p->script, "%s", e);
    e = getenv("OPENIMP_TEST_PATTERN");
    if (e && e[0] >= '1' && e[0] <= '3')
        __atomic_store_n(&openimp_tp_mode_v, e[0] - '0', __ATOMIC_RELEASE);
    if ((e = getenv("OPENIMP_TEST_PATTERN_CTL")) && e[0])
        tp_ctl_start(e);
}

VIS int OpenIMP_TestPattern_Set(int mode, const OpenIMPTestPatternParams *p)
{
    OpenIMPTestPatternParams n;

    if (mode < 0 || mode > 4)
        goto inval;
    pthread_mutex_lock(&set_lock);
    n = slot[__atomic_load_n(&slot_idx, __ATOMIC_ACQUIRE)];
    if (p) {
        size_t cp = p->size;

        if (cp < 4u) {
            pthread_mutex_unlock(&set_lock);
            goto inval;
        }
        if (cp > sizeof n)
            cp = sizeof n;
        OpenIMP_TestPattern_ParamsInit(&n);
        memcpy(&n, p, cp);
        n.size = (uint32_t)sizeof n;
        n.script[sizeof n.script - 1] = 0;
        if (n.step == 0)
            n.step = 8;
        if (n.step < 2 || n.step > 4096 || n.logo > 1 || n.noise < 0 || n.noise > 16 ||
            n.obj_size > 640 || n.obj_contrast < -200 || n.obj_contrast > 200 ||
            n.obj_cell_x >= OPENIMP_TP_GX || n.obj_cell_y >= OPENIMP_TP_GY) {
            pthread_mutex_unlock(&set_lock);
            goto inval;
        }
        n.step &= ~1u;
    }
    if (mode == 4) {                     /* also re-asserts the bit after a sensor restart */
        int err;

        if (tp_sensor_switch(1) != 0) {
            err = errno;
            pthread_mutex_unlock(&set_lock);
            errno = err;
            return -1;
        }
    } else if (mode != 4 && tp_sensor_active) {
        (void)tp_sensor_switch(0);       /* the new frame mode applies regardless */
    }
    slot[1 - __atomic_load_n(&slot_idx, __ATOMIC_ACQUIRE)] = n;
    __atomic_store_n(&slot_idx, 1 - slot_idx, __ATOMIC_RELEASE);
    __atomic_store_n(&frames, 0, __ATOMIC_RELAXED);
    stats.objects_shown = 0;
    stats.objects_active = 0;
    __atomic_add_fetch(&epoch, 1, __ATOMIC_RELEASE);
    __atomic_store_n(&openimp_tp_mode_v, mode == 4 ? 0 : mode, __ATOMIC_RELEASE);
    pthread_mutex_unlock(&set_lock);
    return 0;
inval:
    errno = EINVAL;
    return -1;
}

VIS int OpenIMP_TestPattern_Get(OpenIMPTestPatternState *s)
{
    OpenIMPTestPatternState f;
    uint32_t sz;

    if (!s || s->size < 4u) {
        errno = EINVAL;
        return -1;
    }
    sz = s->size;
    memset(&f, 0, sizeof f);
    f.size = (uint32_t)sizeof f;
    f.abi = OPENIMP_TESTPATTERN_ABI;
    f.mode = tp_sensor_active ? 4 : __atomic_load_n(&openimp_tp_mode_v, __ATOMIC_ACQUIRE);
    f.params = slot[__atomic_load_n(&slot_idx, __ATOMIC_ACQUIRE)];
    f.frames = __atomic_load_n(&frames, __ATOMIC_RELAXED);
    f.objects_shown = stats.objects_shown;
    f.objects_active = stats.objects_active;
    memcpy(s, &f, sz < sizeof f ? sz : sizeof f);
    s->size = sz;
    return 0;
}

int openimp_tp_render(struct openimp_tp_chan *c, uint8_t *dst, uint32_t w, uint32_t h,
                      uint32_t pitch, uint32_t lines, int want3)
{
    int mode = __atomic_load_n(&openimp_tp_mode_v, __ATOMIC_ACQUIRE);
    uint32_t ep = __atomic_load_n(&epoch, __ATOMIC_ACQUIRE);
    OpenIMPTestPatternParams p = slot[__atomic_load_n(&slot_idx, __ATOMIC_ACQUIRE)];
    size_t need = (size_t)pitch * lines + (size_t)pitch * (h / 2u);

    if (mode == 0 || (mode == 3) != (want3 != 0))
        return 0;
    if (c->epoch != ep) {            /* a Set happened: counter restarts */
        c->epoch = ep;
        c->counter = 0;
    }
    if (mode == 3) {
        openimp_testpattern_motion3_p(dst, dst + (size_t)pitch * lines, w, h, pitch, c->counter++,
                                      &p, ep, &stats);
    } else {
        if (!c->cache || c->w != w || c->h != h || c->pitch != pitch || c->lines != lines ||
            c->logo != p.logo) {
            free(c->cache);
            c->cache = calloc(1, need);
            if (!c->cache)
                return 0;
            openimp_testpattern_nv12_logo(c->cache, c->cache + (size_t)pitch * lines, w, h, pitch, p.logo);
            c->w = w; c->h = h; c->pitch = pitch; c->lines = lines; c->logo = p.logo;
        }
        memcpy(dst, c->cache, need);
        if (mode == 2)
            openimp_testpattern_overlay(dst, dst + (size_t)pitch * lines, w, h, pitch, c->counter++, p.step);
    }
    __atomic_add_fetch(&frames, 1, __ATOMIC_RELAXED);
    return 1;
}
