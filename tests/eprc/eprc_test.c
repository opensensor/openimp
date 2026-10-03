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

/* The Helix activity-class counts (0x40094..0x400a0) of picture `frame`
 * (tools/eprc_oracle.py sas_histogram) */
static void sas_histogram(uint32_t frame, uint32_t mbs, uint32_t scene,
                          uint32_t out[4])
{
    uint32_t s = 0x6a09e667u ^ (frame * 0x27d4eb2du) ^ (scene * 0x165667b1u);
    uint32_t dom = (frame / 13u + scene) % 7u, w[7], c[7], total = 0, sum = 0;
    int i;

    for (i = 0; i < 7; i++) {
        s = xorshift(s);
        w[i] = 1u + (s & 0x3fu) + ((uint32_t)i == dom ? 0x200u + (s >> 20) : 0u);
        total += w[i];
    }
    for (i = 0; i < 7; i++) {
        c[i] = (uint32_t)((uint64_t)mbs * w[i] / total);
        sum += c[i];
    }
    c[dom] += mbs - sum;
    for (i = 0; i < 7; i++)
        if (c[i] > 0xffffu)
            c[i] = 0xffffu;
    out[0] = c[0] | c[1] << 16;
    out[1] = c[2] | c[3] << 16;
    out[2] = c[4] | c[5] << 16;
    out[3] = c[6];
}

/* slice +752..+925 without the pointers +820, +864, +868 */
static uint32_t mbrc_slice_hash(const uint8_t *slice)
{
    uint8_t b[174];

    memcpy(b, slice + 752, sizeof(b));
    memset(b + 68, 0, 4);
    memset(b + 112, 0, 8);
    return fnv1a(b, sizeof(b));
}

/* 'C' line: one h264_get_mb_qp call (tools/eprc_oracle.py --mbrc-calls) */
static int mbrc_call(const char *line, int t21)
{
    static uint8_t A[400], S[7000], slice[EPRC_SLICE_SIZE];
    long long v[20];
    int d = t21 ? -40 : 0, n = 0, i;
    const char *p = line + 1;
    char *e;

    while (n < 20) {
        v[n] = strtoll(p, &e, 10);
        if (e == p)
            break;
        n++;
        p = e;
    }
    if (n != 20)
        return -1;
    memset(A, 0, sizeof(A));
    memset(S, 0, sizeof(S));
    memset(slice, 0, sizeof(slice));
    memcpy(A + 40, &(uint32_t){(uint32_t)v[0]}, 4);
    memcpy(A + 44, &(uint32_t){(uint32_t)v[1]}, 4);
    memcpy(A + 52, &(int32_t){(int32_t)v[2]}, 4);
    memcpy(A + 208, &(uint32_t){0x30u}, 4);
    memcpy(A + 212, &(uint32_t){0x30u}, 4);
    memcpy(S + 0, &(uint32_t){(uint32_t)v[3]}, 4);
    memcpy(S + 28, &(uint32_t){(uint32_t)v[4]}, 4);
    S[68] = (uint8_t)v[5];
    memcpy(S + 88, &(uint32_t){(uint32_t)v[6]}, 4);
    for (i = 0; i < 4; i++)
        memcpy(S + 4488 + d + 4 * i, &(uint32_t){(uint32_t)v[7 + i]}, 4);
    slice[859] = (uint8_t)v[11];
    EPRC_MbQp(A, S, slice, t21);
    for (i = 0; i < 7; i++)
        if ((int8_t)S[5336 + d + i] != v[12 + i])
            return 1;
    return mbrc_slice_hash(slice) != (uint32_t)v[19] ? 1 : 0;
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
    int i10;
    uint32_t zcrc = 0;
    int32_t (*picture_class)(const Eprc *) = t21 ? EPRC21_PictureClass : EPRC_PictureClass;
    long checked = 0, failed = 0;
    uint64_t pixels = 0;
    uint32_t mbs = 0, mline[10];
    int have_mline = 0;

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
            mbs = ((p.width + 15u) / 16u) * ((p.height + 15u) / 16u);
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
            if (flags & 4) {
                /* the macroblock rate control of the picture */
                const uint8_t *S = rc.p + (t21 ? 336 : 352);
                int d = t21 ? -40 : 0;
                uint8_t st[168];

                mline[0] = pic.mbrc.qp_flags;
                for (i = 0; i < 7; i++)
                    mline[1 + i] = pic.mbrc.reg[i];
                mline[8] = mbrc_slice_hash(slice);
                memcpy(st, S + 368 + d, 8);
                memcpy(st + 8, S + 5184 + d, 160);
                mline[9] = fnv1a(st, sizeof(st));
                have_mline = 1;
            }
            if (pic.type != want[0] || pic.qp != want[1]) {
                if (failed++ < 10)
                    fprintf(stderr, "scenario %d frame %d: type/qp %d/%d, OEM %d/%d\n",
                            scenario, frame, pic.type, pic.qp, want[0], want[1]);
            }
            bytes = picture_bytes((uint32_t)frame, pic.qp, in.frames_since_idr == 0,
                                  pixels, scenario % 2);
            statistics((uint32_t)frame, regs);
            if (flags & 4)
                sas_histogram((uint32_t)frame, mbs, (uint32_t)scenario, regs + 16);
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
        } else if (line[0] == 'M' && active) {
            uint32_t want[10];
            char *s = line + 1;

            for (i10 = 0; i10 < 10; i10++)
                want[i10] = (uint32_t)strtoul(s, &s, 10);
            checked++;
            if ((!have_mline || memcmp(want, mline, sizeof(want))) && failed++ < 10)
                fprintf(stderr, "scenario %d frame %d: macroblock rate control "
                        "%x %x %x %x %08x %08x, OEM %x %x %x %x %08x %08x\n",
                        scenario, frame - 1, mline[0], mline[1], mline[6], mline[7],
                        mline[8], mline[9], want[0], want[1], want[6], want[7],
                        want[8], want[9]);
            have_mline = 0;
        } else if (line[0] == 'C') {
            int r = mbrc_call(line, t21);

            checked++;
            if (r < 0)
                return 2;
            if (r && failed++ < 10)
                fprintf(stderr, "h264_get_mb_qp call differs: %s", line);
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
