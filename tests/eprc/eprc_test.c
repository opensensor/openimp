/* src/eprc against the OEM rate controller: eprc_vectors.txt was produced
 * by tools/eprc_oracle.py, which runs the OEM T23 1.3.0 code on the same
 * synthetic picture sizes and statistics as below; eprc_t21_vectors.txt the
 * same from the T21 1.0.33 library, checked against the T21 revision
 * (eprc_t21.c) with "eprc-test eprc_t21_vectors.txt t21". */
#include "eprc/eprc.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t xorshift(uint32_t s)
{
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

static void statistics(uint32_t frame, uint32_t regs[EPRC_STAT_REGS])
{
    uint32_t s = 0x9e3779b9u ^ (frame * 0x85ebca6bu);
    int i;

    for (i = 0; i < EPRC_STAT_REGS; i++) {
        s = xorshift(s);
        regs[i] = s >> (s & 15u);
    }
}

static uint32_t picture_bytes(uint32_t frame, uint32_t qp, int idr,
                              uint64_t pixels, int pattern)
{
    uint64_t cplx = pattern ? ((frame / 40u) % 2u ? 5u : 1u) : 1u + frame % 7u;
    uint64_t size = pixels * cplx / 40u;
    uint32_t i;

    for (i = 0; i < qp; i++)
        size = size * 89u / 100u;
    if (idr)
        size *= 4u;
    if (frame % 37u == 36u)
        size *= 120u;
    return size < 20u ? 20u : (uint32_t)size;
}

static void ae_zones(uint32_t frame, uint32_t z[EPRC_AE_ZONES])
{
    uint32_t s = 0x2545f491u ^ (frame * 0x9e3779b1u);
    unsigned int i;

    for (i = 0; i < EPRC_AE_ZONES; i++) {
        s = xorshift(s);
        z[i] = s & 0xfffffu;
    }
}

static uint32_t fnv1a(const uint8_t *p, size_t n)
{
    uint32_t h = 0x811c9dc5u;

    while (n--)
        h = (h ^ *p++) * 0x01000193u;
    return h;
}

/* i264e_decide_slice_type_and_rd (T23 0x34d78, T21 the same rule) for the
 * skip type N1X with maxSameSceneCnt n: frames since the IDR of this
 * picture from those of the previous one (-1: none yet), the GOP and the
 * picture class the controller reported last. */
static int32_t decide_since(int32_t prev, uint32_t gop, uint32_t n,
                            uint32_t mode, int32_t cls)
{
    uint32_t next;

    if (prev < 0)
        return 0;
    next = (uint32_t)prev + 1u;
    if (n == 0u)
        return (int32_t)(next % gop);
    if (next % gop == 0u && mode != 0u && cls == 5 && (uint32_t)prev >= gop)
        return 0;
    return (int32_t)(next % (n * gop));
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "eprc_vectors.txt";
    int t21 = argc > 2 && strcmp(argv[2], "t21") == 0;
    int (*init)(Eprc *, const EprcParams *, uint8_t *) = t21 ? EPRC21_Init : EPRC_Init;
    void (*release)(Eprc *) = t21 ? EPRC21_Free : EPRC_Free;
    int (*start)(Eprc *, const EprcFrameIn *, EprcPicture *) =
        t21 ? EPRC21_FrameStart : EPRC_FrameStart;
    int (*end)(Eprc *, uint32_t, const uint32_t *, EprcPicture *) =
        t21 ? EPRC21_FrameEnd : EPRC_FrameEnd;
    FILE *f = fopen(path, "r");
    char line[512];
    Eprc rc;
    static uint8_t slice[EPRC_SLICE_SIZE];
    EprcParams p;
    int scenario = -1, frame = 0, idr = 1, active = 0, flags = 0;
    int32_t prev_since = -1;
    uint32_t zcrc = 0;
    int32_t (*picture_class)(const Eprc *) = t21 ? EPRC21_PictureClass : EPRC_PictureClass;
    long checked = 0, failed = 0;
    uint64_t pixels = 0;

    if (!f) {
        perror(path);
        return 2;
    }
    memset(&rc, 0, sizeof(rc));
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == 'S') {
            int v[21] = {0}, nv;
            nv = sscanf(line, "S %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
                        &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8],
                        &v[9], &v[10], &v[11], &v[12], &v[13], &v[14], &v[15], &v[16],
                        &v[17], &v[18], &v[19], &v[20]);
            if (nv != 18 && nv != 21)
                return 2;
            if (active)
                release(&rc);
            memset(&p, 0, sizeof(p));
            p.rc_mode = (uint32_t)v[0];
            p.width = (uint32_t)v[1];
            p.height = (uint32_t)v[2];
            p.gop = (uint32_t)v[3];
            p.fps_num = (uint32_t)v[4];
            p.fps_den = 1;
            p.min_qp = (uint32_t)v[5];
            p.max_qp = (uint32_t)v[6];
            p.bitrate = (uint32_t)v[7];
            p.max_bitrate = (uint32_t)v[8];
            p.frm_qp_step = (uint32_t)v[9];
            p.gop_qp_step = (uint32_t)v[10];
            p.i_bias = v[11];
            p.static_time = (uint32_t)v[12];
            p.change_pos = (uint32_t)v[13];
            p.quality = (uint32_t)v[14];
            p.bg_interval_gops = (uint32_t)v[15];
            p.init_qp = v[16];
            idr = v[17];
            p.field52 = (uint32_t)v[18];
            p.cqp = (uint32_t)v[19];
            flags = v[20];
            prev_since = -1;
            pixels = (uint64_t)p.width * p.height;
            memset(slice, 0, sizeof(slice));
            if (init(&rc, &p, slice) != 0)
                return 2;
            active = 1;
            scenario++;
            frame = 0;
        } else if (line[0] == 'F' && active) {
            int want[16], n = 0, since, i;
            char *s = line + 1;
            EprcFrameIn in;
            EprcPicture pic;
            uint32_t regs[EPRC_STAT_REGS], bytes;

            since = (int)strtol(s, &s, 10);
            while (n < 16) {
                char *e;
                long x = strtol(s, &e, 10);
                if (e == s)
                    break;
                want[n++] = (int)x;
                s = e;
            }
            uint32_t zones[EPRC_AE_ZONES];

            memset(&in, 0, sizeof(in));
            if (flags & 2) {
                prev_since = decide_since(prev_since, p.gop, p.bg_interval_gops,
                                          p.rc_mode, picture_class(&rc));
                in.frames_since_idr = (uint32_t)prev_since;
                if (prev_since != since && failed++ < 10)
                    fprintf(stderr, "scenario %d frame %d: frames since IDR %d, OEM %d\n",
                            scenario, frame, prev_since, since);
            } else {
                in.frames_since_idr = (uint32_t)(frame % idr);
                if ((int)in.frames_since_idr != since)
                    return 2;
            }
            if (n < 2)
                return 2;
            if (flags & 1) {
                ae_zones((uint32_t)frame, zones);
                in.ae_zone = zones;
            }
            if (start(&rc, &in, &pic) != 0) {
                fprintf(stderr, "scenario %d frame %d: FrameStart failed\n", scenario, frame);
                return 1;
            }
            checked++;
            if (flags & 1)
                zcrc = fnv1a(rc.a7148, 3648);
            if (pic.type != want[0] || pic.qp != want[1]) {
                if (failed++ < 10)
                    fprintf(stderr, "scenario %d frame %d: type/qp %d/%d, OEM %d/%d\n",
                            scenario, frame, pic.type, pic.qp, want[0], want[1]);
            }
            bytes = picture_bytes((uint32_t)frame, pic.qp, in.frames_since_idr == 0,
                                  pixels, scenario % 2);
            statistics((uint32_t)frame, regs);
            for (i = 2; ; i++) {
                int r = end(&rc, bytes, regs, &pic);
                if (r != 1) {
                    if (i != n && failed++ < 10)
                        fprintf(stderr, "scenario %d frame %d: %d re-encodes, OEM %d\n",
                                scenario, frame, i - 2, n - 2);
                    break;
                }
                checked++;
                if (i >= n || pic.qp != want[i]) {
                    if (failed++ < 10)
                        fprintf(stderr, "scenario %d frame %d: re-encode QP %d, OEM %d\n",
                                scenario, frame, pic.qp, i < n ? want[i] : -1);
                    break;
                }
                bytes = bytes / 2u < 20u ? 20u : bytes / 2u;
            }
            frame++;
        } else if (line[0] == 'Z' && active) {
            uint32_t want = (uint32_t)strtoul(line + 1, NULL, 10);

            checked++;
            if (want != zcrc && failed++ < 10)
                fprintf(stderr, "scenario %d frame %d: AE zone state %08x, OEM %08x\n",
                        scenario, frame - 1, zcrc, want);
        }
    }
    fclose(f);
    if (active)
        release(&rc);
    printf("eprc%s: %ld pictures checked against the OEM controller, %ld mismatches\n",
           t21 ? " (T21)" : "", checked, failed);
    return failed ? 1 : 0;
}
