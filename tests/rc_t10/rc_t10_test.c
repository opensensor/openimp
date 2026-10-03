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
    return failed ? 1 : 0;
}
