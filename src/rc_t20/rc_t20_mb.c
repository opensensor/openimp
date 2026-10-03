/*
 * Macroblock rate control of the OEM T20 libimp 3.12.0 (H264_SMA_CalMBFlag,
 * H264_SMA_CalMBQP, JZM_QPTabConv).  Work in progress.
 */
#include "rc_t20.h"
#include "rc_t20_internal.h"

void RCT20_CalMBFlag(uint8_t *E, const uint8_t *luma)
{
    (void)E;
    (void)luma;
}

void RCT20_CalMBQP(uint8_t *E)
{
    (void)E;
}

void RCT20_MBQpReencode(uint8_t *E)
{
    (void)E;
}

int32_t RCT20_QPTabConv(const uint8_t *qp, int32_t n, uint8_t *tab)
{
    (void)qp;
    (void)n;
    (void)tab;
    return 0;
}
