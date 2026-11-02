/* BlackEnhance slice data against the bytes of the OEM i264e_icodec_black (tools/hskip_oracle.py --black) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "t30/h264enc/cabac.h"
#include "t30/h264enc/slice.h"
#include "hskip/hskip.h"

int main(int argc, char **argv)
{
    FILE *f = fopen(argc > 1 ? argv[1] : "tests/hskip/black_vectors.txt", "r");
    char line[512], hex[400];
    unsigned mbs, len;
    int bad = 0, n = 0, qp;
    h264_cabac_t *cb = calloc(1, sizeof(*cb));

    if (!f)
        return 2;
    for (qp = 0; qp <= 0; qp++) {
        rewind(f);
        h264_cabac_context_init(cb, SLICE_TYPE_P, qp, 0);
        while (fgets(line, sizeof(line), f)) {
            uint8_t out[300];
            char got[600];
            size_t i, k;

            if (sscanf(line, "%*u %*u %u %u %399s", &mbs, &len, hex) != 3)
                continue;
            k = hskip_black_slice_data(out, sizeof(out), mbs, cb->state[11]);
            for (i = 0; i < k; i++)
                sprintf(got + 2 * i, "%02x", out[i]);
            got[2 * k] = 0;
            n++;
            if (k != len || strcmp(got, hex)) {
                printf("FAIL %u macroblocks: %s (%zu), OEM %s (%u)\n", mbs, got, k, hex, len);
                bad++;
            }
        }
    }
    printf("hskip black: %d slices, %d differences\n", n, bad);
    free(cb);
    return bad != 0;
}
