#include <assert.h>
#include <stdio.h>

#include "t30_h264_level.h"

int main(void)
{
    /* Unchanged floor and the classic 1080p30 case. */
    assert(t30_h264_level(640, 360, 25, 1, 500000, 1) == 31u);
    assert(t30_h264_level(1280, 720, 30, 1, 2000000, 1) == 31u);
    assert(t30_h264_level(1920, 1080, 25, 1, 2000000, 1) == 40u);
    assert(t30_h264_level(1920, 1080, 30, 1, 4000000, 2) == 40u);

    /* Previously reported as 4.0 although they exceed MaxFS 8192. */
    assert(t30_h264_level(2304, 1296, 20, 1, 3000000, 1) == 50u);
    assert(t30_h264_level(2560, 1440, 25, 1, 4000000, 1) == 50u);
    assert(t30_h264_level(2592, 1944, 20, 1, 4000000, 1) == 50u);

    /* Macroblock rate and bitrate also select the level. */
    assert(t30_h264_level(1280, 720, 60, 1, 4000000, 1) == 32u);
    assert(t30_h264_level(1920, 1080, 30, 1, 30000000, 1) == 41u);

    /* Missing frame rate falls back to 25 fps. */
    assert(t30_h264_level(1920, 1080, 0, 0, 2000000, 1) == 40u);
    puts("T30 H.264 level tests passed");
    return 0;
}
