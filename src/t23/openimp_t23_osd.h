#ifndef OPENIMP_T23_OSD_H
#define OPENIMP_T23_OSD_H

/* Draw OSD group `group` into a FrameSource frame (IMPFrameInfo) on its way
 * to the encoder; the T23 counterpart of openimp_t31_osd_apply(). */
void openimp_t23_osd_apply(int group, void *frame);

#endif
