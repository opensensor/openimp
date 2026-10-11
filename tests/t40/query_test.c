/* T40 IMP_Encoder_Query status block (src/t40/p2_query.h): vendor 1.3.1
 * field semantics on the OpenIMP on-demand encoder.  Host test, built
 * without PLATFORM_T40 (the 64-bit host fails the T40 stream ABI assert;
 * the status block has the same 6-word layout on every SoC). */
#include <assert.h>
#include <stdio.h>

#include <imp/imp_encoder.h>
#include "t40/p2_query.h"

int main(void)
{
    IMPEncoderCHNStat st;

    /* idle, registered, nothing encoded */
    openimp_t40_query_fill(&st, 1, 0, 0, 0);
    assert(st.registered && st.leftPics == 0 && st.leftStreamFrames == 0);
    assert(st.leftStreamBytes == 0 && st.curPacks == 0 && st.work_done == 1);
    /* PollingStream encoded a picture, GetStream not called yet */
    openimp_t40_query_fill(&st, 1, 1, 0, 0);
    assert(st.leftStreamFrames == 1 && st.curPacks == 0 && st.leftPics == 0);
    /* stream handed out by GetStream (until ReleaseStream) */
    openimp_t40_query_fill(&st, 1, 1, 1, 0);
    assert(st.leftStreamFrames == 0);
    /* encoding in progress */
    openimp_t40_query_fill(&st, 1, 0, 0, 1);
    assert(st.work_done == 0);
    /* not registered */
    openimp_t40_query_fill(&st, 0, 0, 0, 0);
    assert(!st.registered);
    puts("T40 encoder query: all checks passed");
    return 0;
}
