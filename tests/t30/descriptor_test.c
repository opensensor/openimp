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

/* Command lists the stock T10/T20 libimp (H264E_T10_SliceInit) built on a
 * T10L (jxh42, 1280x720 NV12), captured from VDMA TASKRG with the encoder
 * stopped: fixtures/t10_stock_{I,P}.txt, one "vdma_word=value" per pair. */
static size_t load_stock(const char *path, uint32_t *words, size_t capacity)
{
    FILE *file = fopen(path, "r");
    unsigned int address;
    unsigned int value;
    size_t pairs = 0;

    assert(file);
    while (fscanf(file, "%x=%x", &address, &value) == 2) {
        assert(pairs * 2u + 2u <= capacity);
        words[pairs * 2u] = value;
        words[pairs * 2u + 1u] = address;
        pairs++;
    }
    fclose(file);
    return pairs;
}

static void test_t10_matches_stock(const char *path, int p_slice)
{
    static uint32_t stock[4096];
    h264_cabac_t cabac;
    T30H264SliceConfig config;
    size_t stock_pairs;
    size_t count = 0;
    size_t i;
    int model;
    int matched_model = -1;

    stock_pairs = load_stock(path, stock, sizeof(stock) / sizeof(stock[0]));
    assert(stock_pairs == 635u);
    for (model = 0; model < (p_slice ? 3 : 1); model++) {
        size_t mismatches = 0;

        memset(&cabac, 0, sizeof(cabac));
        h264_cabac_context_init(&cabac, p_slice ? SLICE_TYPE_P : SLICE_TYPE_I,
                                p_slice ? 27 : 26, model);
        memset(&config, 0, sizeof(config));
        config.slice_type = p_slice ? 1u : 0u;
        config.mb_width = 80;
        config.mb_height = 45;
        config.last_mby = 44;
        config.qp = p_slice ? 27u : 26u;
        config.raw_format = 8;
        config.width = 1280;
        config.height = 720;
        config.cabac_state = cabac.state;
        config.raw[0] = p_slice ? 0x037ee000u : 0x0393f800u;
        config.raw[1] = p_slice ? 0x038cf000u : 0x03a20800u;
        config.stride[0] = 1280;
        config.stride[1] = 1280;
        if (p_slice) {
            config.reference_y = 0x035a1e00u;
            config.reference_c = 0x03690680u;
        }
        config.output_y = 0x0342b300u;
        config.output_c = 0x0351c500u;
        config.bitstream = 0x032fa000u;
        config.descriptor = descriptor;
        config.descriptor_words = sizeof(descriptor) / sizeof(descriptor[0]);
        memset(descriptor, 0, sizeof(descriptor));
        assert(T10_H264_BuildDescriptor(&config, &count) == 0);
        assert(count == stock_pairs);
        for (i = 0; i < count; i++) {
            uint32_t reg = descriptor[i * 2u + 1u];

            /* every VDMA word (register, valid/terminal flags) and order */
            assert(reg == stock[i * 2u + 1u]);
            /* ROI max QP: the stock list carries the rate-control max QP */
            if ((reg & 0xffffcu) == 0x40040u)
                continue;
            /* the stock I slice programs MCE with a stale (zero) base;
             * OpenIMP points it at the picture's own reconstruction */
            if (!p_slice && ((reg & 0xffffcu) == 0x50304u ||
                             (reg & 0xffffcu) == 0x50b04u)) {
                assert(descriptor[i * 2u] ==
                       ((reg & 0xffffcu) == 0x50304u ? config.output_y
                                                     : config.output_c));
                assert(stock[i * 2u] ==
                       T10_H264_ReferenceOffset(80, (reg & 0xffffcu) ==
                                                        0x50b04u));
                continue;
            }
            if (descriptor[i * 2u] != stock[i * 2u])
                mismatches++;
        }
        if (mismatches == 0u) {
            matched_model = model;
            break;
        }
    }
    assert(matched_model == 0);
    assert(descriptor[(count - 1u) * 2u + 1u] == 0xc0040000u);
    assert(T10_H264_ReferenceOffset(80, 0) == 0x5300u);
    assert(T10_H264_ReferenceOffset(80, 1) == 0x2980u);
    assert(T10_H264_ReferencePlaneSize(80, 45, 0) == 82u * 47u * 256u);
}

int main(void)
{
    h264_cabac_init();
    test_idr_descriptor();
    test_p_descriptor();
    test_high_profile_sps();
    test_t10_matches_stock("fixtures/t10_stock_I.txt", 0);
    test_t10_matches_stock("fixtures/t10_stock_P.txt", 1);
    puts("T30 encoder tests passed (incl. T10 stock command lists)");
    return 0;
}
