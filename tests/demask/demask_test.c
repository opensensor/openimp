/*
 * src/demask against the vendor reference vectors of tools/demask_oracle.py
 * (the OEM T10/T20 3.12.0 functions run under unicorn).
 *
 *   demask-test demask_vectors.txt
 *
 * Every 'P' line is one H.264 picture of the vendor channel loop: the
 * pre-picture strategy call (made while SetChnFrmUsedMode is off), the
 * post-picture block (strategy, sharpness step, ISP call) and the QP the
 * encoder reported for the picture.  'A' lines are IMP_Encoder_SetChnDemask.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "demask/demask.h"

int main(int argc, char **argv)
{
    FILE *f;
    char line[256];
    DemaskState s;
    int frm_used = 0;
    char name[64] = "";
    unsigned pictures = 0, sets = 0, scenarios = 0, errors = 0;

    if (argc < 2) {
        fprintf(stderr, "usage: %s vectors\n", argv[0]);
        return 2;
    }
    f = fopen(argv[1], "r");
    if (!f) {
        perror(argv[1]);
        return 2;
    }
    memset(&s, 0, sizeof(s));
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == 'S') {
            if (sscanf(line, "S %63s %d", name, &frm_used) != 2)
                return 2;
            memset(&s, 0, sizeof(s));
            scenarios++;
        } else if (line[0] == 'A') {
            int en, au, cnt, thr, ret, restore, r;

            if (sscanf(line, "A %d %d %d %d : %d %d", &en, &au, &cnt, &thr, &ret, &restore) != 6)
                return 2;
            r = demask_set(&s, en, au, cnt, thr);
            if (ret != 0 || r != restore) {
                printf("%s: set %d %d %d %d: restore %d, expected %d\n", name, en, au, cnt, thr, r, restore);
                errors++;
            }
            sets++;
        } else if (line[0] == 'P') {
            int qp, pre, post, sharp, mode, value;
            int gpre = -1, gpost = -1, gmode = -1;
            uint32_t gvalue = (uint32_t)-1;

            if (sscanf(line, "P %d : %d %d %d %d %d", &qp, &pre, &post, &sharp, &mode, &value) != 6)
                return 2;
            if (!frm_used)
                gpre = demask_strategy(&s);
            if (s.enable)
                gpost = demask_after_picture(&s, &gvalue, &gmode);
            demask_picture_qp(&s, qp);
            if (gpre != pre || gpost != post || s.sharp != sharp || gmode != mode ||
                (int)gvalue != value) {
                printf("%s: picture %u qp %d: pre %d post %d sharp %d mode %d value %d, "
                       "expected %d %d %d %d %d\n", name, pictures, qp, gpre, gpost, s.sharp,
                       gmode, (int)gvalue, pre, post, sharp, mode, value);
                errors++;
            }
            pictures++;
        }
    }
    fclose(f);
    printf("demask: %u scenarios, %u SetChnDemask calls, %u pictures, %u differences\n",
           scenarios, sets, pictures, errors);
    return errors ? 1 : 0;
}
