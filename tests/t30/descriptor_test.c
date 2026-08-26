#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cabac.h"
#include "set.h"
#include "t30_h264_descriptor.h"

#define VDMA_VALID 0x80000000u
#define VDMA_TERM  0x40000000u

static uint32_t descriptor[4096];

typedef struct {
    const uint8_t *data;
    size_t bit_count;
    size_t offset;
} BitReader;

static uint32_t read_bits(BitReader *reader, unsigned int count)
{
    uint32_t value = 0;
    unsigned int i;

    assert(count <= 32u);
    assert(reader->offset + count <= reader->bit_count);
    for (i = 0; i < count; i++) {
        value <<= 1;
        value |= (reader->data[reader->offset / 8u] >>
                  (7u - reader->offset % 8u)) & 1u;
        reader->offset++;
    }
    return value;
}

static uint32_t read_ue(BitReader *reader)
{
    unsigned int leading_zeros = 0;

    while (read_bits(reader, 1) == 0u) {
        leading_zeros++;
        assert(leading_zeros < 32u);
    }
    return ((1u << leading_zeros) - 1u) +
           read_bits(reader, leading_zeros);
}

static uint32_t pair_register(size_t index)
{
    return descriptor[index * 2u + 1u] & 0xffffcu;
}

static uint32_t pair_value(size_t index)
{
    return descriptor[index * 2u];
}

static T30H264SliceConfig make_config(h264_cabac_t *cabac)
{
    T30H264SliceConfig config;

    memset(&config, 0, sizeof(config));
    config.mb_width = 40;
    config.mb_height = 23;
    config.last_mby = 22;
    config.qp = 28;
    config.raw_format = 8;
    config.dcs_oth = 1;
    config.width = 640;
    config.height = 360;
    config.cabac_state = cabac->state;
    config.raw[0] = 0x06000000u;
    config.raw[1] = 0x06039800u;
    config.stride[0] = 640;
    config.stride[1] = 640;
    config.reference_y = 0x06100000u;
    config.reference_c = 0x06139800u;
    config.output_y = 0x06200000u;
    config.output_c = 0x06239800u;
    config.bitstream = 0x06300100u;
    config.descriptor = descriptor;
    config.descriptor_words = sizeof(descriptor) / sizeof(descriptor[0]);
    return config;
}

static void test_idr_descriptor(void)
{
    h264_cabac_t cabac;
    T30H264SliceConfig config;
    size_t count = 0;

    memset(&cabac, 0, sizeof(cabac));
    h264_cabac_context_init(&cabac, SLICE_TYPE_I, 28, 0);
    config = make_config(&cabac);
    memset(descriptor, 0, sizeof(descriptor));
    assert(T30_H264_BuildDescriptor(&config, &count) == 0);
    assert(count == 611u);
    assert(pair_register(53) == 0x80040u);
    assert(pair_register(62) == 0x80030u);
    assert(pair_value(62) == 0x16270001u);
    assert(pair_register(126) == 0x70068u);
    assert(pair_value(126) == 1u);
    assert(pair_register(135) == 0x90018u);
    assert(pair_value(135) == 0x00001c11u);
    assert(pair_register(count - 1u) == 0x40000u);
    assert(pair_value(count - 1u) == 0xc0001c2bu);
    assert((descriptor[(count - 1u) * 2u + 1u] &
            (VDMA_VALID | VDMA_TERM)) == (VDMA_VALID | VDMA_TERM));
}

static void test_p_descriptor(void)
{
    h264_cabac_t cabac;
    T30H264SliceConfig config;
    size_t count = 0;

    memset(&cabac, 0, sizeof(cabac));
    h264_cabac_context_init(&cabac, SLICE_TYPE_P, 28, 0);
    config = make_config(&cabac);
    config.slice_type = 1;
    memset(descriptor, 0, sizeof(descriptor));
    assert(T30_H264_BuildDescriptor(&config, &count) == 0);
    assert(count == 785u);
    assert(pair_register(53) == 0x5010cu);
    assert(pair_register(62) == 0x50800u);
    assert(pair_value(62) == config.reference_y);
    assert(pair_register(63) == 0x50804u);
    assert(pair_value(63) == config.reference_c);
    assert(pair_register(226) == 0x50000u);
    assert(pair_value(226) == 0x11u);
    assert(pair_register(236) == 0x80030u);
    assert(pair_value(236) == 0x16270002u);
    assert(pair_register(246) == 0x8002cu);
    assert(pair_value(246) == cabac.state[14]);
    assert(pair_register(300) == 0x70068u);
    assert(pair_value(300) == 9u);
    assert(pair_register(309) == 0x90018u);
    assert(pair_value(309) == 0x00001c12u);
    assert(pair_register(count - 3u) == 0x00060u);
    assert(pair_value(count - 3u) == 0x0c0c0404u);
    assert(pair_register(count - 2u) == 0x00064u);
    assert(pair_value(count - 2u) == 0x97850fcfu);
    assert(pair_register(count - 1u) == 0x40000u);
    assert(pair_value(count - 1u) == 0xc0001c3bu);

    config.reference_y = 0;
    errno = 0;
    assert(T30_H264_BuildDescriptor(&config, NULL) == -1);
    assert(errno == EINVAL);
}

static void test_high_profile_sps(void)
{
    uint32_t storage[32] = {0};
    h264_sps_t sps;
    bs_t bits;
    BitReader reader;

    memset(&sps, 0, sizeof(sps));
    sps.i_profile_idc = PROFILE_HIGH;
    sps.i_level_idc = 40;
    sps.i_chroma_format_idc = CHROMA_420;
    sps.i_log2_max_frame_num = 10;
    sps.i_poc_type = 2;
    sps.i_num_ref_frames = 1;
    sps.i_mb_width = 120;
    sps.i_mb_height = 68;
    sps.b_frame_mbs_only = 1;
    sps.b_direct8x8_inference = 1;
    sps.b_crop = 1;
    sps.crop.i_bottom = 8;

    bs_init(&bits, storage, sizeof(storage));
    h264e_sps_write(&bits, &sps);
    reader.data = (const uint8_t *)storage;
    reader.bit_count = (size_t)bs_pos(&bits);
    reader.offset = 0;

    assert(read_bits(&reader, 8) == PROFILE_HIGH);
    assert(read_bits(&reader, 8) == 0u);
    assert(read_bits(&reader, 8) == 40u);
    assert(read_ue(&reader) == 0u);   /* seq_parameter_set_id */
    assert(read_ue(&reader) == 1u);   /* chroma_format_idc */
    assert(read_ue(&reader) == 0u);   /* bit_depth_luma_minus8 */
    assert(read_ue(&reader) == 0u);   /* bit_depth_chroma_minus8 */
    assert(read_bits(&reader, 1) == 0u); /* qpprime bypass */
    assert(read_bits(&reader, 1) == 0u); /* scaling matrix */
    assert(read_ue(&reader) == 6u);   /* log2_max_frame_num_minus4 */
    assert(read_ue(&reader) == 2u);   /* pic_order_cnt_type */
    assert(read_ue(&reader) == 1u);   /* max_num_ref_frames */
    assert(read_bits(&reader, 1) == 0u); /* frame number gaps */
    assert(read_ue(&reader) == 119u); /* pic_width_in_mbs_minus1 */
    assert(read_ue(&reader) == 67u);  /* pic_height_in_map_units_minus1 */
    assert(read_bits(&reader, 1) == 1u); /* frame_mbs_only_flag */
    assert(read_bits(&reader, 1) == 1u); /* direct_8x8_inference_flag */
    assert(read_bits(&reader, 1) == 1u); /* frame_cropping_flag */
    assert(read_ue(&reader) == 0u);
    assert(read_ue(&reader) == 0u);
    assert(read_ue(&reader) == 0u);
    assert(read_ue(&reader) == 4u);   /* frame_crop_bottom_offset */
    assert(read_bits(&reader, 1) == 0u); /* vui_parameters_present_flag */
}

int main(void)
{
    h264_cabac_init();
    test_idr_descriptor();
    test_p_descriptor();
    test_high_profile_sps();
    puts("T30 encoder tests passed");
    return 0;
}
