/*
 * Print OpenIMP's T30 Helix H.264 command list (src/t30/t30_h264_descriptor.c)
 * as "register[T]=value" lines, one pair per line, for
 * tools/t30_oem_slice_order.py.  Host build:
 *   cc -O2 -std=gnu11 -Isrc/t30 -Isrc/t30/h264enc -o t30_descriptor_dump \
 *      tools/t30_descriptor_dump.c src/t30/t30_h264_descriptor.c \
 *      src/t30/h264enc/cabac.c src/t30/h264enc/common.c src/t30/h264enc/set.c
 *   t30_descriptor_dump SLICE_TYPE(0=I,1=P) MB_WIDTH MB_HEIGHT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "t30_h264_descriptor.h"

int main(int argc, char **argv)
{
    static uint32_t descriptor[1u << 14];
    static uint8_t cabac[1024];
    T30H264SliceConfig config;
    size_t count = 0, i;
    int mb_width, mb_height;

    if (argc != 4) {
        fprintf(stderr, "usage: %s 0|1 MB_WIDTH MB_HEIGHT\n", argv[0]);
        return 2;
    }
    mb_width = atoi(argv[2]);
    mb_height = atoi(argv[3]);
    for (i = 0; i < sizeof(cabac); i++)
        cabac[i] = (uint8_t)((i * 37) % 126 + 1);
    memset(&config, 0, sizeof(config));
    config.slice_type = (uint8_t)atoi(argv[1]);
    config.mb_width = (uint8_t)mb_width;
    config.mb_height = (uint8_t)mb_height;
    config.last_mby = (uint8_t)(mb_height - 1);
    config.qp = 30;
    config.raw_format = 8;
    config.dcs_oth = 1;
    config.width = (uint16_t)(mb_width * 16);
    config.height = (uint16_t)(mb_height * 16);
    config.cabac_state = cabac;
    config.raw[0] = 0x06000000u;
    config.raw[1] = 0x06100000u;
    config.stride[0] = config.stride[1] = (uint32_t)(mb_width * 16);
    config.reference_y = 0x06200000u;
    config.reference_c = 0x06300000u;
    config.output_y = 0x06400000u;
    config.output_c = 0x06500000u;
    config.bitstream = 0x06600100u;
    config.descriptor = descriptor;
    config.descriptor_words = sizeof(descriptor) / sizeof(descriptor[0]);
    if (T30_H264_BuildDescriptor(&config, &count) != 0) {
        fprintf(stderr, "T30_H264_BuildDescriptor failed\n");
        return 1;
    }
    for (i = 0; i < count; i++) {
        uint32_t value = descriptor[2 * i], command = descriptor[2 * i + 1];
        printf("%05x%s=%08x\n", command & 0xffffcu,
               (command & 0x40000000u) ? "T" : "", value);
    }
    return 0;
}
