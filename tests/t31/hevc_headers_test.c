/*
 * Host test for the T31 HEVC header writer: every NAL is parsed back with
 * an independent H.265 syntax reader (7.3.2.1-7.3.2.3, 7.3.6.1) and the
 * values the AVPU command list depends on are checked.  The HEVC access-unit
 * check and NAL split of t31_stream_layout are exercised on the same bytes.
 *
 * With OPENIMP_HEVC_HEADERS_DUMP=<file> the test also writes a small Annex B
 * stream (IDR + P headers with a dummy payload byte) for
 * "ffmpeg -i <file> -c copy -bsf:v trace_headers -f null -".
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "t40/t31_hevc_headers.h"
#include "t40/t31_stream_layout.h"

static int failures;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,         \
                    __LINE__, #cond);                                      \
            failures++;                                                    \
        }                                                                  \
    } while (0)

#define CHECK_EQ(actual, expected)                                         \
    do {                                                                   \
        long long a_ = (long long)(actual);                                \
        long long e_ = (long long)(expected);                              \
        if (a_ != e_) {                                                    \
            fprintf(stderr, "%s:%d: %s = %lld, expected %lld\n", __FILE__, \
                    __LINE__, #actual, a_, e_);                            \
            failures++;                                                    \
        }                                                                  \
    } while (0)

typedef struct {
    uint8_t rbsp[512];
    uint32_t size;
    uint32_t pos;
    int error;
    int nal_type;
    int layer_id;
    int tid_plus1;
} Reader;

/* Strip start code, NAL header and emulation prevention bytes. */
static int reader_open(Reader *r, const uint8_t *nal, int length)
{
    int index;
    int zeros = 0;

    memset(r, 0, sizeof(*r));
    if (length < 6 || nal[0] || nal[1] || nal[2] || nal[3] != 1)
        return -1;
    if (nal[4] & 0x80)
        return -1;
    r->nal_type = (nal[4] >> 1) & 0x3f;
    r->layer_id = ((nal[4] & 1) << 5) | (nal[5] >> 3);
    r->tid_plus1 = nal[5] & 7;
    for (index = 6; index < length; index++) {
        if (zeros >= 2 && nal[index] == 3) {
            /* An emulation byte must be followed by 0..3 or end. */
            if (index + 1 < length && nal[index + 1] > 3)
                return -1;
            zeros = 0;
            continue;
        }
        if (zeros >= 2 && nal[index] <= 2)
            return -1; /* unescaped start code / forbidden sequence */
        if (r->size >= sizeof(r->rbsp))
            return -1;
        r->rbsp[r->size++] = nal[index];
        zeros = nal[index] == 0 ? zeros + 1 : 0;
    }
    return 0;
}

static uint32_t u(Reader *r, int bits)
{
    uint32_t value = 0;

    while (bits--) {
        if (r->pos >= r->size * 8u) {
            r->error = 1;
            return 0;
        }
        value = (value << 1) |
                ((r->rbsp[r->pos >> 3] >> (7 - (r->pos & 7))) & 1u);
        r->pos++;
    }
    return value;
}

static uint32_t ue(Reader *r)
{
    int zeros = 0;

    while (!r->error && u(r, 1) == 0)
        if (++zeros > 31) {
            r->error = 1;
            return 0;
        }
    return ((1u << zeros) - 1u) + u(r, zeros);
}

static int32_t se(Reader *r)
{
    uint32_t code = ue(r);

    return (code & 1u) ? (int32_t)((code + 1u) / 2u)
                       : -(int32_t)(code / 2u);
}

static void check_trailing(Reader *r)
{
    CHECK_EQ(u(r, 1), 1);
    while (r->pos & 7u)
        CHECK_EQ(u(r, 1), 0);
    CHECK_EQ(r->pos, r->size * 8u);
    CHECK(!r->error);
}

static void check_ptl(Reader *r, int level_idc)
{
    int flag;

    CHECK_EQ(u(r, 2), 0);        /* profile space */
    CHECK_EQ(u(r, 1), 0);        /* tier */
    CHECK_EQ(u(r, 5), 1);        /* Main */
    for (flag = 0; flag < 32; flag++)
        CHECK_EQ(u(r, 1), flag == 1 || flag == 2);
    CHECK_EQ(u(r, 1), 1);        /* progressive */
    CHECK_EQ(u(r, 1), 0);
    CHECK_EQ(u(r, 1), 0);
    CHECK_EQ(u(r, 1), 1);        /* frame only */
    CHECK_EQ(u(r, 32), 0);
    CHECK_EQ(u(r, 11), 0);
    CHECK_EQ(u(r, 1), 0);
    CHECK_EQ(u(r, 8), level_idc);
}

static void check_vps(const uint8_t *nal, int length,
                      const OpenIMPT31HevcConfig *config)
{
    Reader r;

    CHECK_EQ(reader_open(&r, nal, length), 0);
    CHECK_EQ(r.nal_type, 32);
    CHECK_EQ(r.layer_id, 0);
    CHECK_EQ(r.tid_plus1, 1);
    CHECK_EQ(u(&r, 4), 0);
    CHECK_EQ(u(&r, 1), 1);
    CHECK_EQ(u(&r, 1), 1);
    CHECK_EQ(u(&r, 6), 0);
    CHECK_EQ(u(&r, 3), 0);
    CHECK_EQ(u(&r, 1), 1);
    CHECK_EQ(u(&r, 16), 0xffff);
    check_ptl(&r, config->level_idc);
    CHECK_EQ(u(&r, 1), 0);       /* sub layer ordering info */
    CHECK_EQ(ue(&r), 1);         /* max dec pic buffering - 1 */
    CHECK_EQ(ue(&r), 0);
    CHECK_EQ(ue(&r), 0);
    CHECK_EQ(u(&r, 6), 0);
    CHECK_EQ(ue(&r), 0);
    CHECK_EQ(u(&r, 1), 0);
    CHECK_EQ(u(&r, 1), 0);
    check_trailing(&r);
}

static void check_sps(const uint8_t *nal, int length,
                      const OpenIMPT31HevcConfig *config,
                      uint32_t expect_w, uint32_t expect_h,
                      uint32_t crop_right, uint32_t crop_bottom,
                      uint32_t tick, uint32_t scale)
{
    Reader r;
    uint32_t cropped;

    CHECK_EQ(reader_open(&r, nal, length), 0);
    CHECK_EQ(r.nal_type, 33);
    CHECK_EQ(u(&r, 4), 0);
    CHECK_EQ(u(&r, 3), 0);
    CHECK_EQ(u(&r, 1), 1);
    check_ptl(&r, config->level_idc);
    CHECK_EQ(ue(&r), 0);         /* sps id */
    CHECK_EQ(ue(&r), 1);         /* 4:2:0 */
    CHECK_EQ(ue(&r), expect_w);
    CHECK_EQ(ue(&r), expect_h);
    cropped = u(&r, 1);
    CHECK_EQ(cropped, crop_right || crop_bottom);
    if (cropped) {
        CHECK_EQ(ue(&r), 0);
        CHECK_EQ(ue(&r), crop_right);
        CHECK_EQ(ue(&r), 0);
        CHECK_EQ(ue(&r), crop_bottom);
    }
    CHECK_EQ(ue(&r), 0);
    CHECK_EQ(ue(&r), 0);
    CHECK_EQ(ue(&r), config->log2_max_poc_lsb - 4);
    CHECK_EQ(u(&r, 1), 1);
    CHECK_EQ(ue(&r), 1);
    CHECK_EQ(ue(&r), 0);
    CHECK_EQ(ue(&r), 0);
    CHECK_EQ(ue(&r), 0);         /* min CB 8 */
    CHECK_EQ(ue(&r), 2);         /* CTB 32 */
    CHECK_EQ(ue(&r), 0);         /* min TB 4 */
    CHECK_EQ(ue(&r), 3);         /* max TB 32 */
    CHECK_EQ(ue(&r), 1);         /* depth inter */
    CHECK_EQ(ue(&r), 1);         /* depth intra */
    CHECK_EQ(u(&r, 1), 0);       /* scaling lists */
    CHECK_EQ(u(&r, 1), 0);       /* AMP */
    CHECK_EQ(u(&r, 1), 0);       /* SAO */
    CHECK_EQ(u(&r, 1), 0);       /* PCM */
    CHECK_EQ(ue(&r), 0);         /* no SPS RPS */
    CHECK_EQ(u(&r, 1), 0);       /* long term */
    CHECK_EQ(u(&r, 1), config->tmvp_enabled);
    CHECK_EQ(u(&r, 1), 1);       /* strong intra smoothing */
    CHECK_EQ(u(&r, 1), 1);       /* VUI */
    CHECK_EQ(u(&r, 1), 0);
    CHECK_EQ(u(&r, 1), 0);
    CHECK_EQ(u(&r, 1), 1);
    CHECK_EQ(u(&r, 3), 5);
    CHECK_EQ(u(&r, 1), 0);
    CHECK_EQ(u(&r, 1), 1);
    CHECK_EQ(u(&r, 8), 1);
    CHECK_EQ(u(&r, 8), 1);
    CHECK_EQ(u(&r, 8), 1);
    CHECK_EQ(u(&r, 1), 0);
    CHECK_EQ(u(&r, 1), 0);
    CHECK_EQ(u(&r, 1), 0);
    CHECK_EQ(u(&r, 1), 0);
    CHECK_EQ(u(&r, 1), 0);
    CHECK_EQ(u(&r, 1), 1);       /* timing */
    CHECK_EQ(u(&r, 32), tick);
    CHECK_EQ(u(&r, 32), scale);
    CHECK_EQ(u(&r, 1), 0);
    CHECK_EQ(u(&r, 1), 0);
    CHECK_EQ(u(&r, 1), 0);       /* bitstream restriction */
    CHECK_EQ(u(&r, 1), 0);       /* extensions */
    check_trailing(&r);
}

static void check_pps(const uint8_t *nal, int length,
                      const OpenIMPT31HevcConfig *config)
{
    Reader r;

    CHECK_EQ(reader_open(&r, nal, length), 0);
    CHECK_EQ(r.nal_type, 34);
    CHECK_EQ(ue(&r), 0);
    CHECK_EQ(ue(&r), 0);
    CHECK_EQ(u(&r, 1), 0);
    CHECK_EQ(u(&r, 1), 0);
    CHECK_EQ(u(&r, 3), 0);
    CHECK_EQ(u(&r, 1), 0);                     /* sign data hiding */
    CHECK_EQ(u(&r, 1), config->cabac_init_present);
    CHECK_EQ(ue(&r), 0);
    CHECK_EQ(ue(&r), 0);
    CHECK_EQ(se(&r), 0);                       /* init_qp 26 */
    CHECK_EQ(u(&r, 1), 0);
    CHECK_EQ(u(&r, 1), 0);
    CHECK_EQ(u(&r, 1), config->cu_qp_delta_enabled);
    if (config->cu_qp_delta_enabled)
        CHECK_EQ(ue(&r), config->diff_cu_qp_delta_depth);
    CHECK_EQ(se(&r), 0);
    CHECK_EQ(se(&r), 0);
    CHECK_EQ(u(&r, 1), 0);
    CHECK_EQ(u(&r, 1), 0);
    CHECK_EQ(u(&r, 1), 0);
    CHECK_EQ(u(&r, 1), 0);
    CHECK_EQ(u(&r, 1), 0);                     /* tiles */
    CHECK_EQ(u(&r, 1), 0);                     /* WPP */
    CHECK_EQ(u(&r, 1), 1);                     /* LF across slices */
    CHECK_EQ(u(&r, 1), 1);                     /* deblocking control */
    CHECK_EQ(u(&r, 1), 0);
    CHECK_EQ(u(&r, 1), 0);
    CHECK_EQ(se(&r), -1);                      /* matches cmd[4] 0x1f */
    CHECK_EQ(se(&r), -1);                      /* matches cmd[4] 0x3f */
    CHECK_EQ(u(&r, 1), 0);
    CHECK_EQ(u(&r, 1), 0);
    CHECK_EQ(ue(&r), 0);
    CHECK_EQ(u(&r, 1), 0);
    CHECK_EQ(u(&r, 1), 0);
    check_trailing(&r);
}

static void check_slice(const uint8_t *nal, int length,
                        const OpenIMPT31HevcConfig *config,
                        const OpenIMPT31HevcSlice *slice)
{
    Reader r;

    CHECK_EQ(reader_open(&r, nal, length), 0);
    CHECK_EQ(r.nal_type, slice->is_idr ? 19 : 1);
    CHECK_EQ(r.tid_plus1, 1);
    CHECK_EQ(u(&r, 1), 1);                     /* first slice */
    if (slice->is_idr)
        CHECK_EQ(u(&r, 1), 0);                 /* no_output_of_prior_pics */
    CHECK_EQ(ue(&r), 0);                       /* PPS id */
    CHECK_EQ(ue(&r), slice->is_idr ? 2 : 1);   /* I / P */
    if (!slice->is_idr) {
        CHECK_EQ(u(&r, config->log2_max_poc_lsb),
                 slice->poc & ((1u << config->log2_max_poc_lsb) - 1u));
        CHECK_EQ(u(&r, 1), 0);                 /* RPS in slice header */
        CHECK_EQ(ue(&r), 1);                   /* one negative picture */
        CHECK_EQ(ue(&r), 0);
        CHECK_EQ(ue(&r), slice->ref_poc_delta - 1u);
        CHECK_EQ(u(&r, 1), 1);
        if (config->tmvp_enabled)
            CHECK_EQ(u(&r, 1), 1);
        CHECK_EQ(u(&r, 1), 0);                 /* no override */
        if (config->cabac_init_present)
            CHECK_EQ(u(&r, 1), slice->cabac_init_flag);
        CHECK_EQ(ue(&r), 5u - config->max_merge_cand);
    }
    CHECK_EQ(se(&r), slice->qp - 26);
    CHECK_EQ(u(&r, 1), 1);                     /* LF across slices */
    check_trailing(&r);                        /* byte_alignment() */
}

static void test_headers(uint32_t width, uint32_t height, uint32_t fps_num,
                         uint32_t fps_den, uint8_t level_idc,
                         uint32_t coded_w, uint32_t coded_h,
                         uint32_t tick, uint32_t scale)
{
    OpenIMPT31HevcConfig config;
    uint8_t buffer[1024];
    int vps;
    int sps;
    int pps;

    openimp_t31_hevc_default_config(&config, width, height, fps_num,
                                    fps_den);
    CHECK_EQ(config.level_idc, level_idc);
    config.cu_qp_delta_enabled = 1;

    vps = openimp_t31_hevc_write_vps(buffer, sizeof(buffer), &config);
    CHECK(vps > 0);
    check_vps(buffer, vps, &config);
    sps = openimp_t31_hevc_write_sps(buffer, sizeof(buffer), &config);
    CHECK(sps > 0);
    check_sps(buffer, sps, &config, coded_w, coded_h,
              (coded_w - width) / 2u, (coded_h - height) / 2u, tick, scale);
    pps = openimp_t31_hevc_write_pps(buffer, sizeof(buffer), &config);
    CHECK(pps > 0);
    check_pps(buffer, pps, &config);

    config.cu_qp_delta_enabled = 0;
    config.cabac_init_present = 1;
    pps = openimp_t31_hevc_write_pps(buffer, sizeof(buffer), &config);
    CHECK(pps > 0);
    check_pps(buffer, pps, &config);
}

static void test_slices(void)
{
    OpenIMPT31HevcConfig config;
    OpenIMPT31HevcSlice slice;
    uint8_t buffer[256];
    int length;
    int qp;

    openimp_t31_hevc_default_config(&config, 1920, 1080, 25, 1);
    memset(&slice, 0, sizeof(slice));
    for (qp = 0; qp <= 51; qp++) {
        slice.is_idr = 1;
        slice.qp = qp;
        length = openimp_t31_hevc_write_slice_header(buffer, sizeof(buffer),
                                                     &config, &slice);
        CHECK(length > 6);
        check_slice(buffer, length, &config, &slice);
        /* The header must end on a non-zero byte so the hardware payload
         * cannot complete a start code across the boundary. */
        CHECK(buffer[length - 1] != 0u);

        slice.is_idr = 0;
        slice.poc = (uint32_t)qp * 13u + 1u;
        slice.ref_poc_delta = 1u;
        length = openimp_t31_hevc_write_slice_header(buffer, sizeof(buffer),
                                                     &config, &slice);
        CHECK(length > 6);
        check_slice(buffer, length, &config, &slice);
        CHECK(buffer[length - 1] != 0u);
    }

    config.cabac_init_present = 1;
    config.tmvp_enabled = 0;
    slice.cabac_init_flag = 1;
    slice.poc = 300u;            /* wraps the 8-bit LSB */
    slice.qp = 30;
    length = openimp_t31_hevc_write_slice_header(buffer, sizeof(buffer),
                                                 &config, &slice);
    CHECK(length > 6);
    check_slice(buffer, length, &config, &slice);

    slice.ref_poc_delta = 0u;    /* a P slice needs a reference */
    CHECK_EQ(openimp_t31_hevc_write_slice_header(buffer, sizeof(buffer),
                                                 &config, &slice), -1);
    slice.ref_poc_delta = 1u;
    slice.qp = 52;
    CHECK_EQ(openimp_t31_hevc_write_slice_header(buffer, sizeof(buffer),
                                                 &config, &slice), -1);
    slice.qp = 30;
    CHECK_EQ(openimp_t31_hevc_write_slice_header(buffer, 8, &config,
                                                 &slice), -1);
}

static void test_access_units(void)
{
    OpenIMPT31HevcConfig config;
    OpenIMPT31HevcSlice slice;
    OpenIMPT31AvcAuCheck report;
    OpenIMPT31AnnexBNAL nals[8];
    uint8_t au[512];
    int header;
    int length;

    openimp_t31_hevc_default_config(&config, 640, 360, 25, 1);
    memset(&slice, 0, sizeof(slice));
    slice.is_idr = 1;
    slice.qp = 30;
    header = openimp_t31_hevc_write_parameter_sets(au, sizeof(au), &config);
    CHECK(header > 0);
    length = openimp_t31_hevc_write_slice_header(
        au + header, sizeof(au) - (size_t)header, &config, &slice);
    CHECK(length > 0);
    length += header;
    /* A few payload bytes like the AVPU would append. */
    au[length++] = 0xaf;
    au[length++] = 0x17;
    au[length++] = 0x80;

    CHECK_EQ(openimp_t31_hevc_au_check(au, (uint32_t)length, 1, &report), 0);
    CHECK_EQ(report.nal_count, 4);
    CHECK_EQ(report.vcl_count, 1);
    CHECK_EQ(openimp_t31_hevc_au_check(au, (uint32_t)length, -1, &report), 0);
    /* Read as AVC the same bytes are not a valid AU. */
    CHECK(openimp_t31_avc_au_check(au, (uint32_t)length, 1, &report) != 0);
    /* An IDR AU is not a P AU. */
    CHECK(openimp_t31_hevc_au_check(au, (uint32_t)length, 0, &report) &
          OPENIMP_T31_AU_BAD_ORDER);

    CHECK_EQ(openimp_t31_hevc_annexb_nals(au, (uint32_t)length, nals, 8), 4);
    CHECK_EQ(nals[0].nal_type, 32);
    CHECK_EQ(nals[1].nal_type, 33);
    CHECK_EQ(nals[2].nal_type, 34);
    CHECK_EQ(nals[3].nal_type, 19);
    CHECK_EQ(nals[0].offset, 0);
    CHECK_EQ(nals[3].offset + nals[3].length, (uint32_t)length);

    slice.is_idr = 0;
    slice.poc = 1;
    slice.ref_poc_delta = 1;
    length = openimp_t31_hevc_write_slice_header(au, sizeof(au), &config,
                                                 &slice);
    CHECK(length > 0);
    au[length++] = 0x12;
    CHECK_EQ(openimp_t31_hevc_au_check(au, (uint32_t)length, 0, &report), 0);
    CHECK_EQ(openimp_t31_hevc_annexb_nals(au, (uint32_t)length, nals, 8), 1);
    CHECK_EQ(nals[0].nal_type, 1);

    /* Unescaped start code in the payload: a second NAL. */
    au[length++] = 0x00;
    au[length++] = 0x00;
    au[length++] = 0x01;
    au[length++] = 0x02;
    au[length++] = 0x01;
    CHECK(openimp_t31_hevc_au_check(au, (uint32_t)length, 0, &report) &
          OPENIMP_T31_AU_SHORT_START);

    /* A temporal id other than 0 is not something this encoder emits. */
    length = openimp_t31_hevc_write_slice_header(au, sizeof(au), &config,
                                                 &slice);
    au[5] = 0x02;
    CHECK(openimp_t31_hevc_au_check(au, (uint32_t)length, 0, &report) &
          OPENIMP_T31_AU_BAD_TYPE);
}

static void test_emulation_prevention(void)
{
    OpenIMPT31HevcConfig config;
    uint8_t buffer[1024];
    Reader r;
    int length;

    /* 1/1 fps with a time_scale of 1 produces 00 00 00 01 in the VUI
     * timing words, which must be escaped. */
    openimp_t31_hevc_default_config(&config, 1920, 1080, 1, 1);
    length = openimp_t31_hevc_write_sps(buffer, sizeof(buffer), &config);
    CHECK(length > 0);
    CHECK_EQ(reader_open(&r, buffer, length), 0);
    check_sps(buffer, length, &config, 1920, 1080, 0, 0, 1, 1);
    {
        int index;
        int escapes = 0;

        for (index = 6; index + 2 < length; index++)
            if (!buffer[index] && !buffer[index + 1] &&
                buffer[index + 2] == 3)
                escapes++;
        CHECK(escapes > 0);
    }
}

static void test_levels(void)
{
    CHECK_EQ(openimp_t31_hevc_level_idc(640, 360, 25, 1), 63);
    CHECK_EQ(openimp_t31_hevc_level_idc(1280, 720, 30, 1), 93);
    CHECK_EQ(openimp_t31_hevc_level_idc(1920, 1080, 25, 1), 120);
    CHECK_EQ(openimp_t31_hevc_level_idc(1920, 1080, 60, 1), 123);
    CHECK_EQ(openimp_t31_hevc_level_idc(2560, 1440, 25, 1), 150);
    CHECK_EQ(openimp_t31_hevc_level_idc(2880, 1620, 20, 1), 150);
    CHECK_EQ(openimp_t31_hevc_level_idc(3840, 2160, 60, 1), 153);
}

static void dump_stream(const char *path)
{
    OpenIMPT31HevcConfig config;
    OpenIMPT31HevcSlice slice;
    uint8_t buffer[1024];
    FILE *file = fopen(path, "wb");
    int length;
    uint32_t index;

    if (!file) {
        perror(path);
        failures++;
        return;
    }
    openimp_t31_hevc_default_config(&config, 1920, 1080, 25, 1);
    config.cu_qp_delta_enabled = 1;
    memset(&slice, 0, sizeof(slice));
    for (index = 0u; index < 3u; index++) {
        slice.is_idr = index == 0u;
        slice.poc = index;
        slice.ref_poc_delta = 1u;
        slice.qp = 30;
        if (slice.is_idr) {
            length = openimp_t31_hevc_write_parameter_sets(
                buffer, sizeof(buffer), &config);
            fwrite(buffer, 1, (size_t)length, file);
        }
        length = openimp_t31_hevc_write_slice_header(buffer, sizeof(buffer),
                                                     &config, &slice);
        fwrite(buffer, 1, (size_t)length, file);
        fputc(0xaa, file); /* placeholder for slice_segment_data() */
        fputc(0x80, file);
    }
    fclose(file);
}

int main(void)
{
    const char *dump = getenv("OPENIMP_HEVC_HEADERS_DUMP");

    test_headers(1920, 1080, 25, 1, 120, 1920, 1080, 1, 25);
    test_headers(640, 360, 25, 1, 63, 640, 360, 1, 25);
    test_headers(2560, 1440, 30000, 1001, 150, 2560, 1440, 1001, 30000);
    test_headers(1278, 718, 20, 2, 93, 1280, 720, 1, 10);
    test_slices();
    test_access_units();
    test_emulation_prevention();
    test_levels();
    if (dump && *dump)
        dump_stream(dump);

    if (failures) {
        fprintf(stderr, "T31 HEVC header tests: %d failure(s)\n", failures);
        return 1;
    }
    printf("T31 HEVC header tests passed\n");
    return 0;
}
