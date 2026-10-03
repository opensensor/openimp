/* hevc_policy_test - H.265 CreateChn policy per SoC (src/t40/p2_hevc_policy.h).
 * Built three ways: -DPLATFORM_T23, -DPLATFORM_T21 (and T20 = T21 + T20) must
 * reject, a T31/T41-style build must not use the no-hardware message. */
#include <stdio.h>
#include <string.h>
#include "t40/p2_hevc_policy.h"

int main(void)
{
#ifdef EXPECT_REJECT
    if (!P2_HEVC_NO_HARDWARE) { fprintf(stderr, "FAIL: not rejected\n"); return 1; }
    if (strcmp(P2_HEVC_NO_HW_MSG, "H.265 not supported by the hardware on this SoC "
               "(Helix encoder is H.264/JPEG only); use H.264")) {
        fprintf(stderr, "FAIL: message\n"); return 1;
    }
#else
    if (P2_HEVC_NO_HARDWARE) { fprintf(stderr, "FAIL: wrongly rejected\n"); return 1; }
#endif
    puts("hevc policy ok");
    return 0;
}
