/*
 * Replay the OEM T31 rate-controller traces (tests/t31/traces (.trc files), recorded
 * from libimp 1.1.6 under emulation by tools/t31_rc_emu/cq_trace.py) against
 * the port in src/t40/t31_al_rc.c.  After every call the 328-byte state must
 * be identical to the OEM state; the picture QP and the filler value too.
 */
#include "t40/t31_al_rc.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef TRACE_DIR
#define TRACE_DIR "traces"
#endif

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int unhex(const char *s, uint8_t *out, size_t n)
{
    size_t i;

    for (i = 0; i < n; ++i) {
        int hi = hexval(s[2 * i]), lo = hexval(s[2 * i + 1]);

        if (hi < 0 || lo < 0)
            return -1;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return 0;
}

static const char *field_name(size_t off)
{
    static const struct { size_t off; const char *name; } names[] = {
        {0, "needs_init"}, {4, "fps_1000"}, {8, "clk"}, {24, "min_qp"}, {26, "max_qp"},
        {28, "p_qp"}, {30, "i_qp_ref"}, {32, "qp"}, {36, "qp_sync"}, {72, "hrd.cpb_bits"},
        {76, "hrd.init_delay"}, {96, "hrd.arrival"}, {100, "hrd.arrival_rem"},
        {104, "hrd.removal"}, {108, "hrd.removal_rem"}, {112, "hrd.seconds"},
        {120, "hrd.total_lo"}, {124, "hrd.total_hi"}, {128, "hrd.pictures"},
        {132, "hrd.idle"}, {136, "init_level"}, {140, "target_frame"}, {148, "max_frame"},
        {164, "ip_delta"}, {168, "pb_delta"}, {176, "ratio_i"}, {180, "ratio_b"},
        {184, "ratio_3"}, {192, "last_type"}, {196, "prev_type"}, {200, "model_size[0]"},
        {204, "model_size[1]"}, {208, "model_size[2]"}, {212, "model_size[3]"},
        {216, "model_qp[0]"}, {220, "model_qp[1]"}, {224, "model_qp[2]"}, {228, "model_qp[3]"},
        {232, "f232"}, {236, "f236"}, {240, "p20_ref"}, {244, "last_size"},
        {248, "step_ratio[0]"}, {252, "step_ratio[1]"}, {256, "step_ratio[2]"},
        {260, "step_ratio[3]"}, {264, "gop_pictures"}, {268, "type_count[0]"},
        {272, "type_count[1]"}, {276, "type_count[2]"}, {280, "opt_bit0.."},
        {284, "f284"}, {302, "auto_ip"}, {304, "step"},
    };
    size_t i, best = 0;
    const char *name = "?";

    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (names[i].off <= off && names[i].off >= best) {
            best = names[i].off;
            name = names[i].name;
        }
    return name;
}

static int compare_state(const T31AlRc *rc, const uint8_t *want, const char *file,
                         int line, const char *what)
{
    const uint8_t *have = (const uint8_t *)&rc->st;
    size_t i;
    int bad = 0;

    for (i = 0; i < sizeof(rc->st); ++i) {
        if (have[i] != want[i]) {
            size_t w = i & ~3u;
            uint32_t h32, w32;

            memcpy(&h32, have + w, 4);
            memcpy(&w32, want + w, 4);
            if (!bad)
                printf("%s:%d: %s: state differs\n", file, line, what);
            printf("    +%zu (%s): have 0x%08x (%d) want 0x%08x (%d)\n", w,
                   field_name(w), h32, (int32_t)h32, w32, (int32_t)w32);
            bad = 1;
            i = w + 3;
        }
    }
    return bad;
}

static int parse_params(const char *s, T31AlRcParam *rcp, T31AlGopParam *gop,
                        uint8_t *state, const char **rest)
{
    uint8_t rb[64], gb[28];
    const char *p = s;

    if (unhex(p, rb, sizeof(rb)) != 0)
        return -1;
    p += 128;
    while (*p == ' ') ++p;
    if (unhex(p, gb, sizeof(gb)) != 0)
        return -1;
    p += 56;
    while (*p == ' ') ++p;
    if (unhex(p, state, 328) != 0)
        return -1;
    p += 656;
    memcpy(rcp, rb, sizeof(*rcp));
    memcpy(gop, gb, sizeof(*gop));
    if (rest)
        *rest = p;
    return 0;
}

static int run_file(const char *path)
{
    FILE *f = fopen(path, "r");
    static char line[4096];
    T31AlRc rc;
    T31AlRcParam rcp;
    T31AlGopParam gop;
    uint8_t want[328];
    int lineno = 0, failures = 0, pictures = 0, inited = 0;

    if (!f) {
        printf("cannot open %s\n", path);
        return 1;
    }
    memset(&rc, 0, sizeof(rc));
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);

        ++lineno;
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        if (line[0] == 'E')
            break;
        if (line[0] == 'I') {
            unsigned int mode;
            int n = 0;

            if (sscanf(line, "I %u %n", &mode, &n) != 1 ||
                parse_params(line + n, &rcp, &gop, want, NULL) != 0) {
                printf("%s:%d: bad I line\n", path, lineno);
                return 1;
            }
            if (t31_al_rc_init(&rc, mode, &rcp, &gop) != 0) {
                printf("%s:%d: init failed\n", path, lineno);
                return 1;
            }
            inited = 1;
            failures += compare_state(&rc, want, path, lineno, "init");
        } else if (line[0] == 'S') {
            if (parse_params(line + 2, &rcp, &gop, want, NULL) != 0) {
                printf("%s:%d: bad S line\n", path, lineno);
                return 1;
            }
            t31_al_rc_set_params(&rc, &rcp, &gop);
            failures += compare_state(&rc, want, path, lineno, "set_params");
        } else if (line[0] == 'R') {
            if (unhex(line + 2, want, 328) != 0) {
                printf("%s:%d: bad R line\n", path, lineno);
                return 1;
            }
            t31_al_rc_reset(&rc);
            failures += compare_state(&rc, want, path, lineno, "reset");
        } else if (line[0] == 'Q') {
            T31AlRcPicture pic;
            unsigned int type, flags;
            int qpoff, fixed, forced, want_qp, got;

            if (sscanf(line, "Q %u %u %d %d %d -> %d", &type, &flags, &qpoff, &fixed,
                       &forced, &want_qp) != 6) {
                printf("%s:%d: bad Q line\n", path, lineno);
                return 1;
            }
            memset(&pic, 0, sizeof(pic));
            pic.type = type;
            pic.flags = flags;
            pic.qp_offset = (int8_t)qpoff;
            pic.fixed_qp = (uint8_t)fixed;
            pic.forced_qp = (int8_t)forced;
            got = t31_al_rc_picture_qp(&rc, &pic);
            if (got != want_qp) {
                printf("%s:%d: picture qp: have %d want %d\n", path, lineno, got, want_qp);
                ++failures;
            }
        } else if (line[0] == 'P') {
            T31AlRcPicture pic;
            T31AlRcStatus st;
            unsigned int type, flags, size, ovf, extra, s20, s24, s28, s32, s36, s40,
                s44, s48, sse_lo, sse_hi;
            int qpoff, fixed, forced, qp, want_filler, filler, n = 0;

            if (sscanf(line, "P %u %u %d %d %d %u %u %u %u %u %u %u %u %u %u %u %d %u %u -> %d %n",
                       &type, &flags, &qpoff, &fixed, &forced, &size, &ovf, &extra,
                       &s20, &s24, &s28, &s32, &s36, &s40, &s44, &s48, &qp, &sse_lo,
                       &sse_hi, &want_filler, &n) != 20 ||
                unhex(line + n, want, 328) != 0) {
                printf("%s:%d: bad P line\n", path, lineno);
                return 1;
            }
            memset(&pic, 0, sizeof(pic));
            pic.type = type;
            pic.flags = flags;
            pic.qp_offset = (int8_t)qpoff;
            pic.fixed_qp = (uint8_t)fixed;
            pic.forced_qp = (int8_t)forced;
            memset(&st, 0, sizeof(st));
            st.bits = size;
            st.stat20 = s20; st.stat24 = s24; st.stat28 = s28; st.stat32 = s32;
            st.stat36 = s36; st.stat40 = s40; st.stat44 = s44; st.stat48 = s48;
            st.qp = (int16_t)qp;
            st.sse_lo = sse_lo; st.sse_hi = sse_hi;
            filler = t31_al_rc_picture_start(&rc, &pic, &st, size);
            if (filler != want_filler) {
                printf("%s:%d: filler: have %d want %d\n", path, lineno, filler, want_filler);
                ++failures;
            }
            t31_al_rc_update(&rc, &pic, &st, size, (uint8_t)ovf, extra);
            failures += compare_state(&rc, want, path, lineno, "update");
            ++pictures;
        }
        if (failures >= 3) {
            printf("%s: stopping after %d failures\n", path, failures);
            break;
        }
    }
    fclose(f);
    if (!inited) {
        printf("%s: no init line\n", path);
        return 1;
    }
    printf("%s: %d pictures, %s\n", path, pictures, failures ? "FAIL" : "ok");
    return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : TRACE_DIR;
    DIR *d = opendir(dir);
    struct dirent *e;
    int rc = 0, files = 0;

    if (!d) {
        printf("cannot open trace directory %s\n", dir);
        return 1;
    }
    while ((e = readdir(d)) != NULL) {
        size_t n = strlen(e->d_name);
        char path[1024];

        if (n < 5 || strcmp(e->d_name + n - 4, ".trc") != 0)
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        rc |= run_file(path);
        ++files;
    }
    closedir(d);
    if (!files) {
        printf("no traces in %s\n", dir);
        return 1;
    }
    return rc;
}
