#ifndef OPENIMP_T31_OSD_H
#define OPENIMP_T31_OSD_H

/* Draw the visible LINE/RECT/BITMAP regions (CPU) and blend the PIC/COVER
 * regions (IPU) of OSD group `group` into an NV12
 * frame (FrameSource frame info: +0x08 width, +0x0c height, +0x18 phys,
 * +0x1c virt) with the IPU. Used on T31 and on the Helix path (T20, T21,
 * T30). No-op if OPENIMP_T31_OSD=0. */
void openimp_t31_osd_apply(int group, void *frame);

#endif
