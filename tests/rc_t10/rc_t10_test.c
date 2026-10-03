/* src/rc_t10 against the OEM T10 rate controller: rc_t10_vectors.txt was
 * produced by tools/rc_t20_oracle.py --t10, which runs the T10 branch of the
 * OEM T20 3.12.0 libimp (i264e_ratecontrol_init/_start/_is_reenc with
 * param[0] = 1) on synthetic picture sizes and statistics; this test replays
 * the same inputs through RCT10_Init/Start/End and compares every picture
 * QP, picture type and re-encode decision. */
#include "rc_t10/rc_t10.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* OpenIMP extra superfrm_bits: off (the default) must keep the OEM
 * thresholds (bits / 1024: a T10 VBR picture above 13714 bits is coded
 * again), on must compare in bits (no re-encode below 14 Mbit, still one
 * above).  The OEM vectors above run with it off. */
static int superfrm_check(void)
{
    RcT10Params p;
    RcT10 rc;
    RcT10Picture pic;
    RcT10Stats st;
    int on, i, again[2] = { 0, 0 }, big[2] = { 0, 0 };

    for (on = 0; on < 2; on++) {
        RCT10_DefaultParams(&p);
        p.method = 2;           /* VBR */
        p.width = 1920;
        p.height = 1080;
        p.gop = 25;
        p.max_bitrate = 2000;
        p.superfrm_bits = (uint32_t)on;
        if (RCT10_Init(&rc, &p) != 0)
            return 1;
        if (*(const int32_t *)(rc.e + 88) != (on ? 19660800 : 19200) ||
            *(const int32_t *)(rc.e + 92) != (on ? 14043429 : 13714))
            return 1;
        for (i = 0; i < 100; i++) {
            RCT10_Start(&rc, i % 25 == 0, &pic);
            st.cmpx = 400000u + 1000u * (uint32_t)(i % 7);
            st.bits = i % 25 == 0 ? 600000u : 80000u;
            if (i == 60)
                st.bits = 20000000u;    /* a real super frame */
            if (RCT10_End(&rc, &st, &pic) == 1) {
                again[on]++;
                if (i == 60)
                    big[on] = 1;
                st.bits /= 2;
                RCT10_End(&rc, &st, &pic);
            }
        }
    }
    printf("rc_t10 superfrm: off %d re-encodes, on %d (super frame %d)\n",
           again[0], again[1], big[1]);
    return !(again[0] >= 90 && again[1] == 1 && big[1] == 1);
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "rc_t10_vectors.txt";
    FILE *f = fopen(path, "r");
    char line[512];
    RcT10 rc;
    RcT10Picture pic;
    int active = 0, scenario = -1, frame = 0;
    long checked = 0, failed = 0;

    if (!f) {
        perror(path);
        return 2;
    }
    memset(&rc, 0, sizeof(rc));
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == 'S') {
            RcT10Params p;
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
            RCT10_DefaultParams(&p);
            p.method = m; p.width = w; p.height = h; p.gop = gop;
            p.fps_num = fn; p.fps_den = fd; p.qp = qp; p.min_qp = mn;
            p.max_qp = mx; p.bitrate = br; p.i_bias = bias; p.frm_qp_step = frm;
            p.gop_qp_step = gs; p.gop_relation = rel; p.static_time = st;
            p.max_bitrate = mbr; p.change_pos = chg; p.quality = q;
            p.new_max_qp_trig = trig; p.new_max_qp = nmq; p.mb_rc2 = 1;
            p.super_i_bits = si; p.super_p_bits = sp;
            if (RCT10_Init(&rc, &p) != 0) {
                fprintf(stderr, "init failed\n");
                return 1;
            }
            active = 1;
            scenario++;
        } else if (line[0] == 'F' && active) {
            unsigned idr, qp;

            if (sscanf(line + 1, "%d %u %u", &frame, &idr, &qp) != 3)
                return 2;
            RCT10_Start(&rc, (int)idr, &pic);
            checked++;
            if (pic.qp != qp || pic.idr != (int)idr) {
                if (failed++ < 10)
                    fprintf(stderr, "scenario %d frame %d: qp %u idr %d, "
                            "expected %u %u\n", scenario, frame, pic.qp,
                            pic.idr, qp, idr);
            }
        } else if (line[0] == 'E' && active) {
            RcT10Stats st;
            unsigned r0, r1, r2, re, qp;
            int r;

            if (sscanf(line + 1, "%u %u %u %u %u %u %u", &st.cmpx, &st.bits,
                       &r0, &r1, &r2, &re, &qp) != 7)
                return 2;
            r = RCT10_End(&rc, &st, &pic);
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
    printf("rc_t10: %ld decisions of %d scenarios checked, %ld differ\n",
           checked, scenario + 1, failed);
    if (superfrm_check()) {
        fprintf(stderr, "rc_t10 superfrm check failed\n");
        return 1;
    }
    return failed ? 1 : 0;
}
