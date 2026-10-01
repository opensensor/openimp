#ifndef OPENIMP_T31_OSD_H
#define OPENIMP_T31_OSD_H

/* Blend the visible PIC/COVER regions of OSD group `group` into an NV12
 * frame (FrameSource frame info: +0x08 width, +0x0c height, +0x18 phys,
 * +0x1c virt) with the IPU. No-op if OPENIMP_T31_OSD=0. */
void openimp_t31_osd_apply(int group, void *frame);

#endif
