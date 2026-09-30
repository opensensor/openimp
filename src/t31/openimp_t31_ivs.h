/* T31 IVS framework: capture hook called by the FrameSource path. */
#ifndef OPENIMP_T31_IVS_H
#define OPENIMP_T31_IVS_H

/* Called once for every frame FrameSource channel fs_chn dequeues from the
 * kernel, before the frame is handed to the encoder. frame points to the
 * VBM frame record, whose first 0x28 bytes have the vendor IMPFrameInfo
 * layout. Cheap when no IVS channel is receiving. */
void openimp_t31_ivs_capture(int fs_chn, const void *frame);

#endif
