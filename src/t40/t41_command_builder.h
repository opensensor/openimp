#ifndef OPENIMP_T41_COMMAND_BUILDER_H
#define OPENIMP_T41_COMMAND_BUILDER_H

#include <stddef.h>
#include <stdint.h>

#include "t41_command_layout.h"

/*
 * T41 command words are common across resolutions, while buffer ownership
 * remains an encoder-adapter concern. The adapter supplies physical
 * addresses and the two rotating per-picture offsets; this unit owns the
 * proven command packing and rate-control arithmetic.
 */
typedef struct OpenIMPT41CommandParams {
    uint32_t width;
    uint32_t height;
    uint32_t bitrate;
    uint32_t fps_num;
    uint32_t fps_den;
    uint32_t min_qp;
    uint32_t picture_qp;
    uint32_t max_qp;
    uint32_t rate_control_qp;
    uint32_t picture_number;
    int is_idr;

    uint32_t source_y;
    uint32_t source_uv;

    uint32_t reference_y;
    uint32_t reference_uv;
    uint32_t reference_map_luma;
    uint32_t reference_map_chroma;
    uint32_t reference_luma_offset;
    uint32_t reference_chroma_offset;

    uint32_t reconstruction_y;
    uint32_t reconstruction_uv;
    uint32_t reconstruction_map_luma;
    uint32_t reconstruction_map_chroma;
    uint32_t reconstruction_luma_offset;
    uint32_t reconstruction_chroma_offset;

    uint32_t stream_buffer;
    uint32_t stream_part_offset;

    uint32_t ep2;
    uint32_t ep1;
    uint32_t mv_previous;
    uint32_t mv_current;
    uint32_t ep3;
} OpenIMPT41CommandParams;

#define OPENIMP_T41_STREAM_PAYLOAD_OFFSET 0x0220u

uint32_t openimp_t41_reconstruction_pitch(uint32_t width);
uint32_t openimp_t41_reconstruction_luma_size(uint32_t width,
                                              uint32_t height);
uint32_t openimp_t41_reconstruction_chroma_size(uint32_t width,
                                                uint32_t height);
uint32_t openimp_t41_reconstruction_map_luma_size(uint32_t width,
                                                  uint32_t height);
uint32_t openimp_t41_reconstruction_map_chroma_size(uint32_t width,
                                                    uint32_t height);
uint32_t openimp_t41_reconstruction_map_slot_size(uint32_t width,
                                                  uint32_t height);
uint32_t openimp_t41_motion_vector_slot_size(uint32_t width,
                                             uint32_t height);
uint32_t openimp_t41_reconstruction_manager_size(uint32_t width,
                                                 uint32_t height);
uint32_t openimp_t41_hwrc_grid(uint32_t width, uint32_t height);
uint32_t openimp_t41_next_rate_control_qp(uint32_t current_qp,
                                          uint32_t min_qp,
                                          uint32_t max_qp,
                                          uint32_t payload_bytes,
                                          uint32_t bitrate,
                                          uint32_t fps_num,
                                          uint32_t fps_den);

int openimp_t41_build_command(void *slot, size_t slot_size,
                              const OpenIMPT41CommandParams *params);

#endif
