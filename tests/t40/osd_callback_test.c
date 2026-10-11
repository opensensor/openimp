/* T40 IMP_OSD_SetGroupCallback (src/t23/openimp_t23_osd.c, vendor 1.3.1
 * 0xbda40 / osd_update): group < 6 and created, the callback runs with
 * (group, frame) for every frame the OSD cell handles, NULL clears it.
 * Host test (no IPU: no region is registered, only the callback path). */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <imp/imp_osd.h>
#include "t23/openimp_t23_osd.h"

int IMP_OSD_SetGroupCallback(int grpNum, int (*callback)(int group_index, IMPFrameInfo *frame));

static int calls, last_group;
static IMPFrameInfo *last_frame;

static int cb(int group, IMPFrameInfo *frame)
{
    calls++;
    last_group = group;
    last_frame = frame;
    return 0;
}

int main(void)
{
    IMPFrameInfo f;

    memset(&f, 0, sizeof(f));
    f.width = 640;
    f.height = 360;
    assert(IMP_OSD_SetGroupCallback(0, cb) == -1);      /* not created */
    assert(IMP_OSD_SetGroupCallback(6, cb) == -1);
    assert(IMP_OSD_SetGroupCallback(-1, cb) == -1);
    assert(IMP_OSD_CreateGroup(2) == 0);
    assert(IMP_OSD_SetGroupCallback(2, cb) == 0);
    openimp_t23_osd_apply(2, &f);                       /* not started: still called */
    assert(calls == 1 && last_group == 2 && last_frame == &f);
    openimp_t23_osd_apply(1, &f);                       /* other group */
    assert(calls == 1);
    assert(IMP_OSD_Start(2) == 0);
    openimp_t23_osd_apply(2, &f);
    assert(calls == 2);
    assert(IMP_OSD_SetGroupCallback(2, NULL) == 0);
    openimp_t23_osd_apply(2, &f);
    assert(calls == 2);
    assert(IMP_OSD_SetGroupCallback(2, cb) == 0);
    assert(IMP_OSD_DestroyGroup(2) == 0);               /* destroy clears it */
    assert(IMP_OSD_CreateGroup(2) == 0);
    openimp_t23_osd_apply(2, &f);
    assert(calls == 2);
    puts("T40 OSD group callback: all checks passed");
    return 0;
}
