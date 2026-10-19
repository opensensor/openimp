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
        else
            snprintf(rep, rn, "error: parameter out of range");
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

    if (mode < 0 || mode > 3)
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
    slot[1 - __atomic_load_n(&slot_idx, __ATOMIC_ACQUIRE)] = n;
    __atomic_store_n(&slot_idx, 1 - slot_idx, __ATOMIC_RELEASE);
    __atomic_store_n(&frames, 0, __ATOMIC_RELAXED);
    stats.objects_shown = 0;
    stats.objects_active = 0;
    __atomic_add_fetch(&epoch, 1, __ATOMIC_RELEASE);
    __atomic_store_n(&openimp_tp_mode_v, mode, __ATOMIC_RELEASE);
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
    f.mode = __atomic_load_n(&openimp_tp_mode_v, __ATOMIC_ACQUIRE);
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
