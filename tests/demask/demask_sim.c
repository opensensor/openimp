/*
 * Host simulator of the demask channel loop (device evidence helper for
 * tools/demask_check.py): reads the per-picture QP sequence of a camera run
 * and prints the ISP sharpness calls src/demask makes for it.
 *
 *   demask-sim frm_used < events
 *   events: 'C en auto cnt thr measure' (the CreateChn attribute, copied into the zeroed record; measure = the
 *   encoder's QP parameter at channel_i264e_encoder_init: the FixQP value or the i264e default 30),
 *   'A en auto cnt thr' (IMP_Encoder_SetChnDemask) or 'P qp' (one picture)
 *   output: per 'A' line 'A restore' (1: SetSharpness_internal(0x80, 0)); per
 *   'P' line 'P mode value' (-1 -1: no call, enable off)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "demask/demask.h"

int main(int argc, char **argv)
{
    DemaskState s;
    char line[128];
    int frm_used = argc > 1 ? atoi(argv[1]) : 0;

    memset(&s, 0, sizeof(s));
    while (fgets(line, sizeof(line), stdin)) {
        if (line[0] == 'C') {
            int en, au, cnt, thr, measure = DEMASK_QP_DEFAULT;

            if (sscanf(line, "C %d %d %d %d %d", &en, &au, &cnt, &thr, &measure) < 4)
                return 2;
            memset(&s, 0, sizeof(s));
            s.enable = en != 0;
            s.auto_mode = au != 0;
            s.cnt = cnt;
            s.thr = thr;
            s.measure = measure;
        } else if (line[0] == 'A') {
            int en, au, cnt, thr;

            if (sscanf(line, "A %d %d %d %d", &en, &au, &cnt, &thr) != 4)
                return 2;
            printf("A %d\n", demask_set(&s, en, au, cnt, thr));
        } else if (line[0] == 'P') {
            int qp, mode = -1;
            uint32_t value = (uint32_t)-1;

            if (sscanf(line, "P %d", &qp) != 1)
                return 2;
            if (!frm_used)
                (void)demask_strategy(&s);
            if (s.enable)
                (void)demask_after_picture(&s, &value, &mode);
            demask_picture_qp(&s, qp);
            printf("P %d %d\n", mode, (int)value);
        }
    }
    return 0;
}
