#ifndef OPENIMP_T21_H264_DESCRIPTOR_H
#define OPENIMP_T21_H264_DESCRIPTOR_H

#include <stddef.h>
#include <stdint.h>

/* OpenIMP-owned inputs to the T21 Helix command-list builder.  This is not
 * the private SDK structure recovered from libimp; every hardware address and
 * codec property used below is named and supplied explicitly. */
typedef struct {
    uint8_t slice_type; /* 0: I/IDR, 1: P */
    uint8_t mb_width;
    uint8_t mb_height;
    uint8_t first_mby;
    uint8_t last_mby;
    uint8_t qp;
    uint8_t raw_format;
    uint16_t width;
    uint16_t height;
    const uint8_t *cabac_state;
    uint32_t raw[3];
    uint32_t stride[2];
    uint32_t reference_y;
    uint32_t reference_c;
    uint32_t output_y;
    uint32_t output_c;
    uint32_t bitstream;
    uint32_t scratch_base;
    /* Offsets of the EMC per-macroblock buffers 0x3004c, 0x30050, 0x30054
     * and 0x30058 from scratch_base (0x30018 is at scratch_base), and the
     * bitstream window in KiB (0x30040).  T23_HelixScratchLayout() fills
     * them for a picture size; zero offsets select the T21 1080p layout. */
    uint32_t scratch_offset[4];
    uint32_t bitstream_kib;
    /* T23: 0x30000 bit 19, set by the OEM builder in its ISP-direct mode
     * (slice field +449): the core raises BSFULL and pauses at the window
     * end instead of writing past it.  Only with a kernel that handles a
     * BSFULL-only stop (thingino patches 0096/0098). */
    uint8_t bsf_stop;
    uint32_t *descriptor;
    size_t descriptor_words;
} T21H264SliceConfig;

/* EMC scratch layout for an mb_width x mb_height picture (T21, T23): fills
 * config->scratch_offset and returns the scratch size in bytes. */
uint32_t T23_HelixScratchLayout(uint32_t mb_width, uint32_t mb_height,
                                uint32_t offsets[4]);
/* The T21 layout: the stock library's 1 MiB EMC buffer, per macroblock. */
uint32_t T21_HelixScratchLayout(uint32_t mb_width, uint32_t mb_height,
                                uint32_t offsets[4]);

int T21_H264_BuildDescriptor(const T21H264SliceConfig *config,
                             size_t *pair_count);

#endif
