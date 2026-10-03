/* src/rc_t20 against the OEM T20 rate controller: rc_t20_vectors.txt was
 * produced by tools/rc_t20_oracle.py, which runs the OEM T20 3.12.0 code
 * (i264e_ratecontrol_init/_start/_is_reenc) on synthetic picture sizes,
 * statistics and (macroblock rate control) luma pictures; this test replays
 * the same inputs through RCT20_Init/Start/End and compares every picture
 * QP, picture type, re-encode decision and macroblock QP table. */
#include "rc_t20/rc_t20.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the synthetic luma of tools/rc_t20_oracle.py picture() */
static void picture(uint8_t *out, uint32_t frame, uint32_t stride,
                    uint32_t lines)
{
    uint32_t x, y;

    for (y = 0; y < lines; y++)
        for (x = 0; x < stride; x++) {
            uint32_t b = (x >> 4) + (y >> 4) * 3u + frame, v;

            if (b % 4u == 0u)
                v = 30u + (frame * 7u) % 60u;
            else if (b % 4u == 1u)
                v = 140u + ((x + y) & 3u);
            else if (b % 4u == 2u)
                v = (x * 13u + y * 29u + frame * 5u) & 0xffu;
            else
                v = ((x * x + y * 7u + frame) * 2654435761u >> 13) & 0xffu;
            out[y * stride + x] = (uint8_t)v;
        }
}

/* "T words sum mean" against the controller's table */
static int table_differs(const RcT20 *rc, const char *line)
{
    unsigned words, sum, mean;
    const uint32_t *t = NULL;
    uint32_t n, s = 0, i;

    if (sscanf(line + 1, "%u %u %u", &words, &sum, &mean) != 3)
        return 1;
    n = RCT20_QpTable(rc, &t);
    for (i = 0; i < n; i++)
        s += t[i];
    if (n != words || s != sum)
        return 1;
    return n && *(const uint32_t *)(const void *)(rc->e + RCT20_RX_BASE + 352) != mean;
}

/* OpenIMP extra iaware: off is the OEM controller (vectors above); on a
 * static scene with I pictures ~70 x the P pictures (as recorded at
 * 1080p), the OEM overshoots the CBR bit rate by about the I pictures,
 * the I-aware P budget brings it back near the target. */
static double iaware_rate(uint32_t method, uint32_t on)
{
    RcT20Params p;
    RcT20 rc;
    RcT20Picture pic;
    RcT20Stats st;
    double total = 0.0;
    uint32_t n;

    RCT20_DefaultParams(&p);
    p.method = method;
    p.width = 1920;
    p.height = 1080;
    p.gop = 50;
    p.fps_num = 15;
    p.min_qp = 15;
    p.max_qp = 45;
    p.bitrate = 1500;
    p.max_bitrate = 1500;
    p.mb_rc = 0;
    p.iaware = on;
    memset(&rc, 0, sizeof(rc));
    if (RCT20_Init(&rc, &p) != 0)
        return -1.0;
    for (n = 0; n < 450; n++) {
        int idr = n % 50u == 0;
        double b;
        int q;

        RCT20_Start(&rc, idr, NULL, 0, &pic);
        b = idr ? 4200000.0 : 60000.0;
        for (q = 30; q < pic.qp; q++)
            b *= idr ? 0.906 : 0.891;
        for (q = pic.qp; q < 30; q++)
            b /= idr ? 0.906 : 0.891;
        memset(&st, 0, sizeof(st));
        st.cmpx = idr ? 8100000u : 2073600u + 5000u * (n % 7u);
        st.bits = (uint32_t)b;
        st.reg[0] = 80u | 80u << 16;
        st.reg[1] = 240u;
        st.reg[2] = 240u;
        total += b;
        for (q = 0; q < 4 && RCT20_End(&rc, &st, &pic) == 1; q++)
            ;
    }
    RCT20_Free(&rc);
    return total / (450.0 / 15.0) / 1000.0 / 1500.0;
}

static int iaware_check(void)
{
    double off = iaware_rate(1, 0), on = iaware_rate(1, 1);
    double voff = iaware_rate(2, 0), von = iaware_rate(2, 1);

    printf("rc_t20 iaware: CBR rate %.2f -> %.2f, VBR %.2f -> %.2f "
           "(x configured bit rate)\n", off, on, voff, von);
    return !(off > 1.5 && on < 0.75 * off && on > 0.6 && on < 1.4 &&
             von < 0.8 * voff);
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "rc_t20_vectors.txt";
    FILE *f = fopen(path, "r");
    char line[512];
    RcT20 rc;
    RcT20Picture pic;
    int active = 0, scenario = -1, frame = 0;
    uint8_t *luma = NULL;
    uint32_t stride = 0, lines = 0;
    long checked = 0, failed = 0;

    if (!f) {
        perror(path);
        return 2;
    }
    memset(&rc, 0, sizeof(rc));
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == 'S') {
            RcT20Params p;
            unsigned m, w, h, gop, fn, fd, qp, mn, mx, br, frm, gs, rel, st, mbr,
                chg, q, nmq, mbrc;
            int bias, si, sp;
            float trig;

            if (sscanf(line + 1, "%u %u %u %u %u %u %u %u %u %u %d %u %u %u %u %u "
                       "%u %u %f %u %d %d %u", &m, &w, &h, &gop, &fn, &fd, &qp,
                       &mn, &mx, &br, &bias, &frm, &gs, &rel, &st, &mbr, &chg,
                       &q, &trig, &nmq, &si, &sp, &mbrc) != 23) {
                fprintf(stderr, "bad line: %s", line);
                return 2;
            }
            RCT20_DefaultParams(&p);
            p.method = m; p.width = w; p.height = h; p.gop = gop;
            p.fps_num = fn; p.fps_den = fd; p.qp = qp; p.min_qp = mn;
            p.max_qp = mx; p.bitrate = br; p.i_bias = bias; p.frm_qp_step = frm;
            p.gop_qp_step = gs; p.gop_relation = rel; p.static_time = st;
            p.max_bitrate = mbr; p.change_pos = chg; p.quality = q;
            p.new_max_qp_trig = trig; p.new_max_qp = nmq; p.mb_rc = mbrc;
            p.mb_rc2 = 1; p.super_i_bits = si; p.super_p_bits = sp;
            if (RCT20_Init(&rc, &p) != 0) {
                fprintf(stderr, "init failed\n");
                return 1;
            }
            active = 1;
            scenario++;
            stride = (w + 15u) & ~15u;
            lines = ((h + 15u) & ~15u) + 16u;
            free(luma);
            luma = mbrc ? malloc(stride * lines + 64u) : NULL;
        } else if (line[0] == 'F' && active) {
            unsigned idr, qp;

            if (sscanf(line + 1, "%d %u %u", &frame, &idr, &qp) != 3)
                return 2;
            if (luma)
                picture(luma, (uint32_t)frame, stride, lines);
            RCT20_Start(&rc, (int)idr, luma, stride, &pic);
            checked++;
            if (pic.qp != qp || pic.idr != (int)idr) {
                if (failed++ < 10)
                    fprintf(stderr, "scenario %d frame %d: qp %u idr %d, "
                            "expected %u %u\n", scenario, frame, pic.qp,
                            pic.idr, qp, idr);
            }
        } else if (line[0] == 'T' && active) {
            checked++;
            if (table_differs(&rc, line)) {
                if (failed++ < 10)
                    fprintf(stderr, "scenario %d frame %d: QP table differs\n",
                            scenario, frame);
            }
        } else if (line[0] == 'E' && active) {
            RcT20Stats st;
            unsigned re, qp;
            int r;

            if (sscanf(line + 1, "%u %u %u %u %u %u %u", &st.cmpx, &st.bits,
                       &st.reg[0], &st.reg[1], &st.reg[2], &re, &qp) != 7)
                return 2;
            r = RCT20_End(&rc, &st, &pic);
            checked++;
            if (r != (int)re || (re && pic.qp != qp)) {
                if (failed++ < 10)
                    fprintf(stderr, "scenario %d frame %d: re-encode %d qp %u, "
                            "expected %u %u\n", scenario, frame, r, pic.qp,
                            re, qp);
            }
        }
    }
    fclose(f);
    free(luma);
    RCT20_Free(&rc);
    printf("rc_t20: %ld decisions of %d scenarios checked, %ld differ\n",
           checked, scenario + 1, failed);
    if (iaware_check()) {
        fprintf(stderr, "rc_t20 iaware check failed\n");
        return 1;
    }
    return failed ? 1 : 0;
}
