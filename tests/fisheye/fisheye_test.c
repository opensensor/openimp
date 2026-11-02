/*
 * src/fisheye against the vendor reference vectors of tools/fisheye_oracle.py
 * (the OEM T10/T20 3.12.0 i264e_icodec_enc run under unicorn).
 *
 *   fisheye-test fisheye_vectors.txt
 *
 * 'C flag stype counter fps_num fps_den qp : table': the ROI table the vendor
 * function leaves for the hardware, with the table pre-filled with 0xa5 + i.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fisheye/fisheye.h"

int main(int argc, char **argv)
{
    FILE *f;
    char line[512];
    unsigned cases = 0, errors = 0, regions = 0;

    if (argc < 2) {
        fprintf(stderr, "usage: %s vectors\n", argv[0]);
        return 2;
    }
    f = fopen(argv[1], "r");
    if (!f) {
        perror(argv[1]);
        return 2;
    }
    while (fgets(line, sizeof(line), f)) {
        unsigned flag, stype, counter, fnum, fden;
        int qp, n = 0;
        uint8_t roi[8][7], want[56];
        char *hex;
        unsigned i;

        if (line[0] != 'C')
            continue;
        if (sscanf(line, "C %u %u %u %u %u %d : %n", &flag, &stype, &counter,
                   &fnum, &fden, &qp, &n) < 6 || n == 0)
            return 2;
        hex = line + n;
        for (i = 0; i < 56; i++) {
            unsigned v;
            if (sscanf(hex + 2 * i, "%2x", &v) != 1)
                return 2;
            want[i] = (uint8_t)v;
        }
        for (i = 0; i < 56; i++)
            ((uint8_t *)roi)[i] = (uint8_t)(0xa5 + i);
        fisheye_roi((int)flag, counter, stype == 2, fnum, fden, qp, roi);
        cases++;
        if (flag && want[0])
            regions++;
        if (memcmp(roi, want, 56)) {
            if (errors++ < 10)
                fprintf(stderr, "DIFF: %s", line);
        }
    }
    fclose(f);
    printf("fisheye: %u cases (%u with region 0), %u differences\n", cases,
           regions, errors);
    if (!cases || !regions)
        return 2;
    return errors ? 1 : 0;
}
