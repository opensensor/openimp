/*
 * Native T30 Helix H.264 encoder.
 *
 * The descriptor generator and AVC syntax helpers are derived from Ingenic's
 * GPL-2.0 Helix sources.  This wrapper deliberately uses the legacy T30
 * /dev/soc_vpu ABI directly; no V4L2 codec node or proprietary libimp is
 * involved.
 */

#include "t30_helix_encoder.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "dma_alloc.h"
#include "imp_log_int.h"
#include "t30/h264enc/common.h"
#include "t30/t30_h264_descriptor.h"

#define T30_CHANNEL_REQUEST 0xc0386300u
#define T30_CHANNEL_RELEASE 0xc0386301u
#define T30_CHANNEL_RUN     0xc0386302u

#define T30_HELIX_H264_CORE 0x02000001u
#define T30_H264_ENCODE      0u
#define T30_CHANNEL_OPEN     0u
#define T30_CHANNEL_CLOSE    2u
#define T30_CHANNEL_DELAY_MS 20000u
#define T30_DESCRIPTOR_WINDOW (1u << 20)
#define T30_EMC_SIZE          (1u << 21)
#define T30_DBLK_SIZE         (1u << 20)
#define T30_RECON_SIZE        (1u << 18)
#define T30_MV_SIZE           (1u << 11)
#define T30_SE_SIZE           (1u << 11)
#define T30_QPT_SIZE          (1u << 14)
#define T30_RC_SIZE           (1u << 15)
#define T30_CPX_SIZE          (1u << 17)
#define T30_MOD_SIZE          (1u << 15)
#define T30_SAD_SIZE          (1u << 17)
#define T30_SLICE_OFFSET      256u
#define T30_HEADER_CAPACITY   4096u
#define T30_NV12_MODE         8u

typedef struct {
    uint32_t clist;
    uint32_t vlist;
    uint32_t mdelay;
    uint32_t channel_id;
    int32_t vpu_id;
    uint32_t codecdir;
    uint32_t workphase;
    uint32_t status;
    uint32_t output_len;
    uint32_t dma_addr;
    int32_t thread_id;
    uint32_t cmpx;
    uint32_t n_flag;
    uint32_t ncu_addr;
} T30ChannelNode;

_Static_assert(sizeof(T30ChannelNode) == 56,
               "T30 soc_vpu channel ABI mismatch");

typedef struct {
    IMPDMABufferInfo dma;
    uint32_t y;
    uint32_t c;
} T30ReferenceFrame;

struct T30HelixEncoder {
    int fd;
    T30ChannelNode channel;
    HWEncoderParams params;
    IMPDMABufferInfo descriptor;
    IMPDMABufferInfo emc;
    IMPDMABufferInfo temporary;
    T30ReferenceFrame reference[3];
    T30H264IDRConfig slice;
    h264_sps_t sps;
    h264_pps_t pps;
    h264_slice_header_t slice_header;
    h264_cabac_t cabac;
    uint8_t headers[T30_HEADER_CAPACITY];
    uint32_t headers_size;
    uint32_t frame_number;
    uint32_t idr_pic_id;
    int force_idr;
};

static void t30_dma_release(IMPDMABufferInfo *dma)
{
    if (dma && dma->phys_addr) {
        DMA_FreePhys(dma->phys_addr);
        memset(dma, 0, sizeof(*dma));
    }
}

static int t30_dma_allocate(IMPDMABufferInfo *dma, uint32_t size,
                            const char *tag)
{
    if (size > INT32_MAX || DMA_AllocDescriptor(dma, (int)size, tag) != 0 ||
        !dma->phys_addr || !dma->virt_addr) {
        LOG_CODEC("T30 Helix: DMA allocation failed tag=%s size=%u", tag,
                  size);
        return -1;
    }
    memset((void *)(uintptr_t)dma->virt_addr, 0, size);
    return 0;
}

static uint32_t t30_level_for_size(uint32_t width, uint32_t height)
{
    uint64_t macroblocks = ((uint64_t)width + 15u) / 16u;

    macroblocks *= ((uint64_t)height + 15u) / 16u;
    return macroblocks > 3600u ? 40u : 31u;
}

static void t30_init_parameter_sets(T30HelixEncoder *encoder)
{
    h264_sps_t *sps = &encoder->sps;
    h264_pps_t *pps = &encoder->pps;
    uint32_t aligned_width = (encoder->params.width + 15u) & ~15u;
    uint32_t aligned_height = (encoder->params.height + 15u) & ~15u;

    memset(sps, 0, sizeof(*sps));
    sps->i_id = 0;
    sps->i_profile_idc = PROFILE_MAIN;
    sps->i_level_idc = (int)t30_level_for_size(encoder->params.width,
                                                encoder->params.height);
    sps->b_constraint_set1 = 1;
    sps->i_log2_max_frame_num = 10;
    sps->i_poc_type = 2;
    sps->i_num_ref_frames = 1;
    sps->i_mb_width = (int)(aligned_width / 16u);
    sps->i_mb_height = (int)(aligned_height / 16u);
    sps->b_frame_mbs_only = 1;
    sps->b_direct8x8_inference = 1;
    sps->i_chroma_format_idc = CHROMA_420;
    if (aligned_width != encoder->params.width ||
        aligned_height != encoder->params.height) {
        sps->b_crop = 1;
        sps->crop.i_right = (int)(aligned_width - encoder->params.width);
        sps->crop.i_bottom = (int)(aligned_height - encoder->params.height);
    }

    memset(pps, 0, sizeof(*pps));
    pps->i_id = 0;
    pps->i_sps_id = 0;
    pps->b_cabac = 1;
    pps->i_num_slice_groups = 1;
    pps->i_num_ref_idx_l0_default_active = 1;
    pps->i_num_ref_idx_l1_default_active = 1;
    pps->i_pic_init_qp = 26;
    pps->i_pic_init_qs = 26;
    pps->b_deblocking_filter_control = 1;
}

static int t30_annexb_nal(bs_t *bits, uint8_t *destination,
                          uint32_t capacity, int type, int priority)
{
    uint8_t *source = bits->p_start;
    uint8_t *end = source + (uint32_t)bs_pos(bits) / 8u;
    uint8_t *output = destination;

    if (capacity < 5u)
        return -1;
    *output++ = 0;
    *output++ = 0;
    *output++ = 0;
    *output++ = 1;
    *output++ = (uint8_t)((priority << 5) | type);
    while (source < end) {
        if ((uint32_t)(output - destination) >= capacity)
            return -1;
        if (source[0] <= 3u && output - destination >= 2 &&
            output[-2] == 0u && output[-1] == 0u) {
            if ((uint32_t)(output - destination) >= capacity)
                return -1;
            *output++ = 3u;
        }
        *output++ = *source++;
    }
    return (int)(output - destination);
}

static int t30_generate_headers(T30HelixEncoder *encoder)
{
    uint8_t temporary[512];
    bs_t bits;
    int length;

    memset(temporary, 0, sizeof(temporary));
    encoder->headers_size = 0;
    bs_init(&bits, temporary, sizeof(temporary));
    h264e_sps_write(&bits, &encoder->sps);
    length = t30_annexb_nal(&bits, encoder->headers,
                            sizeof(encoder->headers), NAL_SPS,
                            NAL_PRIORITY_HIGHEST);
    if (length < 0)
        return -1;
    encoder->headers_size = (uint32_t)length;

    memset(temporary, 0, sizeof(temporary));
    bs_init(&bits, temporary, sizeof(temporary));
    h264e_pps_write(&bits, &encoder->sps, &encoder->pps);
    length = t30_annexb_nal(&bits,
                            encoder->headers + encoder->headers_size,
                            sizeof(encoder->headers) - encoder->headers_size,
                            NAL_PPS, NAL_PRIORITY_HIGHEST);
    if (length < 0)
        return -1;
    encoder->headers_size += (uint32_t)length;
    return 0;
}

static void t30_fill_slice(T30HelixEncoder *encoder,
                           const IMPFrameInfo *frame, uint32_t qp)
{
    T30H264IDRConfig *slice = &encoder->slice;

    memset(slice, 0, sizeof(*slice));
    slice->mb_width = (uint8_t)encoder->sps.i_mb_width;
    slice->mb_height = (uint8_t)encoder->sps.i_mb_height;
    slice->first_mby = 0;
    slice->last_mby = (uint8_t)(slice->mb_height - 1u);
    slice->qp = (uint8_t)qp;
    slice->cabac_state = encoder->cabac.state;
    slice->raw_format = T30_NV12_MODE;
    slice->stride[0] = (int)encoder->params.width;
    slice->stride[1] = (int)encoder->params.width;
    slice->raw[0] = frame->phyAddr;
    /* T30 FrameSource lays NV12 chroma after the macroblock-aligned luma
     * plane, not immediately after the visible-height plane. */
    slice->raw[1] = frame->phyAddr +
                    (uint32_t)encoder->sps.i_mb_width * 16u *
                    (uint32_t)encoder->sps.i_mb_height * 16u;
    slice->raw[2] = 0;
    slice->output_y = encoder->reference[0].y;
    slice->output_c = encoder->reference[0].c;
    slice->bitstream = encoder->temporary.phys_addr + T30_SLICE_OFFSET;
    slice->descriptor = (uint32_t *)(uintptr_t)encoder->descriptor.virt_addr;
    slice->descriptor_words = encoder->descriptor.size / sizeof(uint32_t);
    /* SDK 1.0.5 selects the alternate DCS threshold for its substream. */
    slice->dcs_oth = encoder->params.width <= 640u ? 1u : 0u;
}

int OpenIMP_T30_HelixCreate(T30HelixEncoder **encoder_out,
                            const HWEncoderParams *params)
{
    T30HelixEncoder *encoder;
    uint64_t frame_size;
    unsigned int i;

    if (!encoder_out || !params || !params->width || !params->height)
        return -1;
    frame_size = (uint64_t)params->width * params->height;
    if (frame_size > UINT32_MAX / 2u)
        return -1;
    encoder = calloc(1, sizeof(*encoder));
    if (!encoder)
        return -1;
    encoder->fd = -1;
    encoder->params = *params;
    if (!encoder->params.gop_length)
        encoder->params.gop_length = 25;
    if (!encoder->params.qp || encoder->params.qp > 51u)
        encoder->params.qp = 28;
    if (!encoder->params.min_qp || encoder->params.min_qp > 51u)
        encoder->params.min_qp = 18;
    if (!encoder->params.max_qp || encoder->params.max_qp > 51u)
        encoder->params.max_qp = 45;

    encoder->fd = open("/dev/soc_vpu", O_RDWR | O_CLOEXEC);
    if (encoder->fd < 0)
        goto fail;
    memset(&encoder->channel, 0, sizeof(encoder->channel));
    encoder->channel.mdelay = T30_CHANNEL_DELAY_MS;
    encoder->channel.thread_id = -1;
    if (ioctl(encoder->fd, T30_CHANNEL_REQUEST, &encoder->channel) != 0)
        goto fail;
    if (t30_dma_allocate(&encoder->descriptor, T30_DESCRIPTOR_WINDOW,
                         "t30-helix-desc") != 0 ||
        t30_dma_allocate(&encoder->emc, T30_EMC_SIZE,
                         "t30-helix-emc") != 0 ||
        t30_dma_allocate(&encoder->temporary, (uint32_t)frame_size * 2u,
                         "t30-helix-bs") != 0)
        goto fail;
    for (i = 0; i < 3u; i++) {
        if (t30_dma_allocate(&encoder->reference[i].dma,
                             (uint32_t)(frame_size + frame_size / 2u),
                             "t30-helix-ref") != 0)
            goto fail;
        encoder->reference[i].y = encoder->reference[i].dma.phys_addr;
        encoder->reference[i].c = encoder->reference[i].y +
                                  (uint32_t)frame_size;
    }
    t30_init_parameter_sets(encoder);
    h264_cabac_init();
    if (t30_generate_headers(encoder) != 0)
        goto fail;
    encoder->force_idr = 1;
    *encoder_out = encoder;
    LOG_CODEC("T30 Helix: native encoder ready channel=%u %ux%u desc=0x%08x",
              encoder->channel.channel_id, params->width, params->height,
              encoder->descriptor.phys_addr);
    return 0;

fail:
    LOG_CODEC("T30 Helix: create failed: %s", strerror(errno));
    OpenIMP_T30_HelixDestroy(encoder);
    return -1;
}

int OpenIMP_T30_HelixEncode(T30HelixEncoder *encoder,
                            const IMPFrameInfo *frame,
                            HWStreamBuffer **stream_out)
{
    uint8_t *temporary;
    uint8_t *output;
    HWStreamBuffer *stream;
    bs_t bits;
    uint32_t capacity;
    uint32_t offset = 0;
    uint32_t qp;
    int idr;
    int nal_length;
    size_t descriptor_pairs;

    if (!encoder || !frame || !stream_out || !frame->phyAddr)
        return -1;
    /* The source-only T30 bring-up currently emits the exact IDR program.
     * P-frame motion-estimation programming is the next incremental step. */
    idr = 1;
    encoder->force_idr = 0;
    qp = encoder->params.qp;

    h264e_slice_header_init(&encoder->slice_header, &encoder->sps,
                            &encoder->pps,
                            idr ? (int)(encoder->idr_pic_id++ & 1u) : -1,
                            idr ? 0 : (int)(encoder->frame_number & 1023u),
                            (int)qp);
    encoder->slice_header.i_type = idr ? SLICE_TYPE_I : SLICE_TYPE_P;
    encoder->slice_header.i_disable_deblocking_filter_idc = 0;
    temporary = (uint8_t *)(uintptr_t)encoder->temporary.virt_addr;
    memset(temporary, 0, T30_SLICE_OFFSET);
    bs_init(&bits, temporary, T30_SLICE_OFFSET);
    h264e_slice_header_write(&bits, &encoder->slice_header,
                             idr ? NAL_PRIORITY_HIGHEST : NAL_PRIORITY_HIGH);
    bs_align_1(&bits);
    h264_cabac_context_init(&encoder->cabac, encoder->slice_header.i_type,
                            (int)qp,
                            encoder->slice_header.i_cabac_init_idc);

    t30_fill_slice(encoder, frame, qp);
    if (T30_H264_BuildIDRDescriptor(&encoder->slice,
                                    &descriptor_pairs) != 0) {
        LOG_CODEC("T30 Helix: descriptor build failed: %s",
                  strerror(errno));
        return -1;
    }
    encoder->channel.vpu_id = (int32_t)T30_HELIX_H264_CORE;
    encoder->channel.codecdir = T30_H264_ENCODE;
    encoder->channel.dma_addr = encoder->descriptor.phys_addr;
    encoder->channel.thread_id = -1;
    if (ioctl(encoder->fd, T30_CHANNEL_RUN, &encoder->channel) != 0 ||
        !encoder->channel.output_len ||
        encoder->channel.output_len > encoder->temporary.size - T30_SLICE_OFFSET) {
        LOG_CODEC("T30 Helix: run failed frame=%u errno=%d status=0x%08x len=%u",
                  encoder->frame_number, errno, encoder->channel.status,
                  encoder->channel.output_len);
        return -1;
    }
    DMA_RmemFlushCache(temporary + T30_SLICE_OFFSET,
                       encoder->channel.output_len, 2);
    if (bits.i_left != 32)
        return -1;
    memcpy(bits.p, temporary + T30_SLICE_OFFSET,
           encoder->channel.output_len);
    bits.p += encoder->channel.output_len;

    capacity = encoder->temporary.size * 2u + T30_HEADER_CAPACITY;
    output = malloc(capacity);
    stream = calloc(1, sizeof(*stream));
    if (!output || !stream) {
        free(output);
        free(stream);
        return -1;
    }
    if (idr) {
        memcpy(output, encoder->headers, encoder->headers_size);
        offset = encoder->headers_size;
    }
    nal_length = t30_annexb_nal(&bits, output + offset, capacity - offset,
                                idr ? NAL_SLICE_IDR : NAL_SLICE,
                                idr ? NAL_PRIORITY_HIGHEST :
                                      NAL_PRIORITY_HIGH);
    if (nal_length < 0) {
        free(output);
        free(stream);
        return -1;
    }
    stream->virt_addr = (uint32_t)(uintptr_t)output;
    stream->length = offset + (uint32_t)nal_length;
    stream->timestamp = frame->timeStamp;
    stream->frame_type = idr ? HW_FRAME_TYPE_I : HW_FRAME_TYPE_P;
    stream->slice_type = stream->frame_type;
    *stream_out = stream;
    encoder->frame_number++;
    if (encoder->frame_number <= 4u ||
        (encoder->frame_number % 100u) == 0u)
        LOG_CODEC("T30 Helix: frame=%u %s bytes=%u hw=%u status=0x%08x desc=%u",
                  encoder->frame_number, idr ? "IDR" : "P",
                  stream->length, encoder->channel.output_len,
                  encoder->channel.status, (unsigned int)descriptor_pairs);
    return 0;
}

int OpenIMP_T30_HelixRequestIDR(T30HelixEncoder *encoder)
{
    if (!encoder)
        return -1;
    encoder->force_idr = 1;
    return 0;
}

void OpenIMP_T30_HelixDestroy(T30HelixEncoder *encoder)
{
    unsigned int i;

    if (!encoder)
        return;
    if (encoder->fd >= 0 && encoder->channel.clist) {
        encoder->channel.workphase = T30_CHANNEL_CLOSE;
        (void)ioctl(encoder->fd, T30_CHANNEL_RELEASE, &encoder->channel);
    }
    if (encoder->fd >= 0)
        close(encoder->fd);
    for (i = 0; i < 3u; i++)
        t30_dma_release(&encoder->reference[i].dma);
    t30_dma_release(&encoder->temporary);
    t30_dma_release(&encoder->emc);
    t30_dma_release(&encoder->descriptor);
    free(encoder);
}
