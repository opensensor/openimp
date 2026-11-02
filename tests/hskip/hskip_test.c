/* Host test of src/hskip against the OEM decisions of tests/hskip/hskip_vectors.txt
 * (tools/hskip_oracle.py: i264e_init_skip_header / i264e_decide_slice_type_and_rd of libimp 3.12.0). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hskip/hskip.h"

int main(int argc, char **argv)
{
    FILE *f = fopen(argc > 1 ? argv[1] : "tests/hskip/hskip_vectors.txt", "r");
    char line[256];
    HSkip s;
    unsigned gop = 0, typ = 0, m = 0, n = 0, same = 0, a, b, c;
    int pics = 0, bad = 0, scen = 0, left = 0;

    if (!f) {
        perror("vectors");
        return 2;
    }
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == 'S') {
            int w;

            if (sscanf(line, "S %u %u %u %u %u : %u %u %u", &gop, &typ, &m, &n, &same, &a, &b, &c) != 8)
                return 2;
            hskip_init(&s, gop, typ, m, n, same);
            if (s.a != a || s.b != b || s.buffers != c) {
                printf("FAIL init gop %u type %u m %u n %u: %u %u %u, OEM %u %u %u\n", gop, typ, m, n, s.a, s.b, s.buffers, a, b, c);
                bad++;
            }
            w = 0;
            (void)w;
            scen++;
            left = 240;
        } else if (left > 0) {
            unsigned fn, idr, ft, ltr, reo, mm;
            int ref, rec, idcval;
            HSkipPic p;

            if (sscanf(line, "%u %u %u %d %d %u %u %d %u", &fn, &idr, &ft, &ref, &rec, &ltr, &reo, &idcval, &mm) != 9)
                return 2;
            hskip_decide(&s, 0, 0, &p);
            pics++;
            left--;
            if (p.counter != fn || p.idr != (int)idr || p.ftype != ft || p.ref != ref || p.recon != rec ||
                p.ltr_flag != (int)ltr || p.reorder != (int)reo || (p.mmco ? 2u : 0u) != mm ||
                (p.reorder && idcval != 2 * 16 + 0)) {
                if (bad < 20)
                    printf("FAIL gop %u type %u m %u n %u same %u pic %d: fn %u/%u idr %d/%u ft %u/%u ref %d/%d rec %d/%d ltr %d/%u reo %d/%u mmco %d/%u\n",
                           gop, typ, m, n, same, 240 - left - 1, p.counter, fn, p.idr, idr, p.ftype, ft, p.ref, ref, p.recon, rec,
                           p.ltr_flag, ltr, p.reorder, reo, p.mmco ? 2 : 0, mm);
                bad++;
            }
        }
    }
    printf("hskip: %d scenarios, %d pictures, %d differences\n", scen, pics, bad);
    return bad != 0;
}
