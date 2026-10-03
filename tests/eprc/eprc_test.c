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

/* OpenIMP extra qp_down_max: off is the OEM controller (vectors above);
 * on, a P picture after a P picture never falls more than qp_down_max
 * below the last coded QP, and the slice fields follow the QP.  Static
 * scene with a noise cliff (the case that makes the OEM QP saw-tooth). */
static uint32_t cliff_bytes(uint32_t frame, int32_t qp, int32_t ref, int idr)
{
    double b;

    if (idr) {
        b = 700000.0;
        for (int32_t i = 30; i < qp; i++)
            b *= 0.906;
    } else {
        b = 30000.0 / (1.0 + (double)(1u << (qp > 28 ? (qp - 28 > 20 ? 20 : qp - 28) : 0)));
        if (qp < 28)
            b = 30000.0;
        for (int32_t d = ref - qp; d > 0; d--)
            b *= 1.7;
        for (int32_t d = ref - qp; d < 0; d++)
            b /= 1.7;
        b += 60.0 + (double)(frame % 5u) * 10.0;
    }
    return (uint32_t)b;
}

static int qp_down_check(void)
{
    static uint8_t slice[EPRC_SLICE_SIZE];
    uint32_t regs[EPRC_STAT_REGS];
    int on, big[2] = { 0, 0 }, diff = 0, bad = 0;
    uint8_t seq[2][300];

    memset(regs, 0, sizeof(regs));
    regs[15] = 2000000u;
    for (on = 0; on < 2; on++) {
        EprcParams p;
        Eprc rc;
        EprcFrameIn in;
        EprcPicture pic;
        int32_t prev = -1, prev_type = -1, ref = 35;
        uint32_t n;

        memset(&p, 0, sizeof(p));
        p.width = 1920; p.height = 1080; p.rc_mode = EPRC_MODE_CBR;
        p.gop = 50; p.fps_num = 15; p.fps_den = 1; p.min_qp = 15;
        p.max_qp = 45; p.bitrate = 1500; p.max_bitrate = 1500;
        p.frm_qp_step = 3; p.gop_qp_step = 15; p.static_time = 2;
        p.change_pos = 80; p.quality = 4; p.init_qp = -1;
        p.qp_down_max = on ? 1u : 0u;
        memset(slice, 0, sizeof(slice));
        if (EPRC_Init(&rc, &p, slice) != 0)
            return 1;
        for (n = 0; n < 300; n++) {
            uint32_t over;

            memset(&in, 0, sizeof(in));
            in.frames_since_idr = n % 50u;
            if (EPRC_FrameStart(&rc, &in, &pic) != 0)
                return 1;
            over = pic.qp > 33 ? pic.qp - 33u : 0u;
            if (slice[448] != pic.qp || slice[808] != pic.qp ||
                slice[809] != (pic.qp + 13 > 51 ? 51 : pic.qp + 13) ||
                *(uint16_t *)(slice + 1058) != 384u + 48u * over ||
                *(uint16_t *)(slice + 1060) != 96u + 12u * over)
                bad++;
            if (pic.type == 0 && prev_type == 0 && pic.qp < prev - 1)
                big[on]++;
            seq[on][n] = pic.qp;
            EPRC_FrameEndEx(&rc, cliff_bytes(n, pic.qp, ref, pic.type == 2),
                            regs, &pic, 0);
            prev = pic.qp;
            prev_type = pic.type;
            ref = pic.qp;
        }
        EPRC_Free(&rc);
    }
    for (int i = 0; i < 300; i++)
        diff += seq[0][i] != seq[1][i];
    printf("eprc qp_down_max: QP falls > 1 after P: off %d, on %d; %d of 300 "
           "pictures differ; slice field errors %d\n", big[0], big[1], diff, bad);
    return !(big[0] > 0 && big[1] == 0 && diff > 0 && bad == 0);
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
    int scenario = -1, frame = 0, idr = 1, active = 0;
    long checked = 0, failed = 0;
    uint64_t pixels = 0;

    if (!f) {
        perror(path);
        return 2;
    }
    memset(&rc, 0, sizeof(rc));
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == 'S') {
            int v[18];
            if (sscanf(line, "S %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
                       &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8],
                       &v[9], &v[10], &v[11], &v[12], &v[13], &v[14], &v[15], &v[16],
                       &v[17]) != 18)
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
            memset(&in, 0, sizeof(in));
            in.frames_since_idr = (uint32_t)(frame % idr);
            if ((int)in.frames_since_idr != since || n < 2)
                return 2;
            if (start(&rc, &in, &pic) != 0) {
                fprintf(stderr, "scenario %d frame %d: FrameStart failed\n", scenario, frame);
                return 1;
            }
            checked++;
            if (pic.type != want[0] || pic.qp != want[1]) {
                if (failed++ < 10)
                    fprintf(stderr, "scenario %d frame %d: type/qp %d/%d, OEM %d/%d\n",
                            scenario, frame, pic.type, pic.qp, want[0], want[1]);
            }
            bytes = picture_bytes((uint32_t)frame, pic.qp, since == 0, pixels, scenario % 2);
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
        }
    }
    fclose(f);
    if (active)
        release(&rc);
    printf("eprc%s: %ld pictures checked against the OEM controller, %ld mismatches\n",
           t21 ? " (T21)" : "", checked, failed);
    if (qp_down_check()) {
        fprintf(stderr, "eprc qp_down_max check failed\n");
        return 1;
    }
    return failed ? 1 : 0;
}
