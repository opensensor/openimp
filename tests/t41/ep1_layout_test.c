#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "t40/t40_ep1.h"

/* The layout test exercises only the lambda prefix.  Initialization's
 * scaling-list calls are linked here as harmless host stubs. */
int32_t AL_AVC_GenerateHwScalingList(void *scaling, int32_t scratch)
{
    (void)scaling;
    (void)scratch;
    return 0;
}

int32_t AL_AVC_WriteEncHwScalingList(void *scaling, void *scratch,
                                     uint32_t *output)
{
    (void)scaling;
    (void)scratch;
    (void)output;
    return 0;
}

int32_t AL_AVC_WriteEncHwScalingListT41(void *scaling, void *scratch,
                                        uint32_t *output)
{
    (void)scaling;
    (void)scratch;
    (void)output;
    return 0;
}

int main(void)
{
    uint8_t ep1[0x6400u];
    uint8_t intra[52u * 4u];
    unsigned int changed = 0u;
    unsigned int qp;

    assert(openimp_t41_init_ep1(ep1, sizeof(ep1)) == 0);
    memcpy(intra, ep1, sizeof(intra));
    for (qp = 0u; qp < 52u; ++qp) {
        assert(ep1[qp * 4u] == 0u);
        assert(ep1[qp * 4u + 2u] == 0u);
    }

    assert(openimp_t41_update_ep1_lambda(ep1, sizeof(ep1), 1u) == 0);
    for (qp = 0u; qp < 52u; ++qp) {
        assert(ep1[qp * 4u] == 0u);
        assert(ep1[qp * 4u + 2u] == 0u);
        assert(ep1[qp * 4u + 3u] == intra[qp * 4u + 3u]);
        changed += memcmp(ep1 + qp * 4u, intra + qp * 4u, 4u) != 0;
    }
    /* This is the exact first-P delta observed in the OEM EP1 trace. */
    assert(changed == 26u);

    assert(openimp_t41_update_ep1_lambda(ep1, sizeof(ep1), 2u) == 0);
    assert(memcmp(ep1, intra, sizeof(intra)) == 0);
    assert(openimp_t41_update_ep1_lambda(NULL, sizeof(ep1), 1u) == -1);
    assert(openimp_t41_update_ep1_lambda(ep1, 0xcfu, 1u) == -1);
    assert(openimp_t41_update_ep1_lambda(ep1, sizeof(ep1), 3u) == -1);

    puts("T41 EP1 lambda layout: OK");
    return 0;
}
