#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "t40/t31_stream_layout.h"

static int expect_trace(uint32_t capacity, uint32_t status_payload,
                        uint32_t header_size, uint32_t expected_payload_end,
                        uint32_t expected_access_unit_size)
{
    uint8_t status[0x168] = {0};
    uint32_t payload_size = 0;
    OpenIMPT31StreamLayout layout = {0};

    memcpy(status + OPENIMP_T31_COMPLETION_PAYLOAD_SIZE_OFFSET,
           &status_payload, sizeof(status_payload));
    if (openimp_t31_completion_payload_size(status, sizeof(status),
                                             &payload_size) != 0 ||
        payload_size != status_payload ||
        openimp_t31_stream_layout(capacity, 0x220u, header_size,
                                  payload_size, &layout) != 0 ||
        layout.payload_end != expected_payload_end ||
        layout.access_unit_size != expected_access_unit_size) {
        fprintf(stderr,
                "trace mismatch: cap=%u payload=%u hdr=%u got=%u/%u expected=%u/%u\n",
                capacity, status_payload, header_size, layout.payload_end,
                layout.access_unit_size, expected_payload_end,
                expected_access_unit_size);
        return 1;
    }
    return 0;
}

int main(void)
{
    static const uint8_t idr_access_unit[] = {
        0x00, 0x00, 0x00, 0x01, 0x67, 0xaa, 0xbb,
        0x00, 0x00, 0x00, 0x01, 0x68, 0xcc,
        0x00, 0x00, 0x00, 0x01, 0x65, 0xdd, 0xee, 0xff,
    };
    static const uint8_t p_access_unit[] = {
        0x00, 0x00, 0x00, 0x01, 0x41, 0x12, 0x34,
    };
    uint8_t status[0x168] = {0};
    uint32_t payload_size = 0;
    OpenIMPT31StreamLayout layout = {0};
    OpenIMPT31AnnexBNAL nals[3] = {{0}};
    int failed = 0;

    /* Archived T31 status/stream pairs: raw_end = 0x220 + status[0x104]. */
    failed |= expect_trace(0x30a00u, 0x6151u, 60u, 25457u, 24973u);
    failed |= expect_trace(0x30a00u, 0x2650u, 10u, 10352u, 9818u);
    failed |= expect_trace(0xe7680u, 0x1f296u, 62u, 128182u, 127700u);
    failed |= expect_trace(0x30a00u, 0x73eeu, 60u, 30222u, 29738u);
    failed |= expect_trace(0x30a00u, 0x1a93u, 10u, 7347u, 6813u);
    failed |= expect_trace(0xe7680u, 0x160bfu, 10u, 90847u, 90313u);
    failed |= expect_trace(0xe7680u, 0x10601u, 10u, 67617u, 67083u);

    if (openimp_t31_completion_payload_size(
            status, OPENIMP_T31_COMPLETION_PAYLOAD_SIZE_OFFSET + 3u,
            &payload_size) == 0 ||
        openimp_t31_completion_payload_size(status, sizeof(status),
                                             &payload_size) == 0 ||
        openimp_t31_stream_layout(0x1000u, 0x220u, 10u, 0u,
                                  &layout) == 0 ||
        openimp_t31_stream_layout(0x1000u, 0x220u, 0x221u, 1u,
                                  &layout) == 0 ||
        openimp_t31_stream_layout(0x1000u, 0x220u, 10u, 0xde1u,
                                  &layout) == 0) {
        fprintf(stderr, "invalid T31 completion layout accepted\n");
        failed = 1;
    }

    if (openimp_t31_annexb_nals(idr_access_unit,
                                sizeof(idr_access_unit), nals, 3u) != 3 ||
        nals[0].offset != 0u || nals[0].length != 7u ||
        nals[0].nal_type != 7u || nals[1].offset != 7u ||
        nals[1].length != 6u || nals[1].nal_type != 8u ||
        nals[2].offset != 13u || nals[2].length != 8u ||
        nals[2].nal_type != 5u ||
        openimp_t31_annexb_nals(p_access_unit, sizeof(p_access_unit),
                                nals, 3u) != 1 ||
        nals[0].offset != 0u || nals[0].length != sizeof(p_access_unit) ||
        nals[0].nal_type != 1u ||
        openimp_t31_annexb_nals(idr_access_unit,
                                sizeof(idr_access_unit), nals, 2u) >= 0 ||
        openimp_t31_annexb_nals(idr_access_unit + 1u,
                                sizeof(idr_access_unit) - 1u,
                                nals, 3u) != 0) {
        fprintf(stderr, "invalid T31 Annex-B pack layout\n");
        failed = 1;
    }

    {
        static const uint8_t p_embedded[] = {
            0x00, 0x00, 0x00, 0x01, 0x21, 0x9a, 0x00, 0x00,
            0x01, 0x21, 0x88, 0x12, 0x34,
        };
        static const uint8_t idr_mirrored[] = {
            0x00, 0x00, 0x00, 0x01, 0x27, 0xaa,
            0x00, 0x00, 0x00, 0x01, 0x28, 0xcc,
            0x00, 0x00, 0x00, 0x01, 0x25, 0xb8,
            0x00, 0x00, 0x00, 0x01, 0x27, 0xaa,
            0x00, 0x00, 0x00, 0x01, 0x28, 0xcc,
            0x00, 0x00, 0x00, 0x01, 0x25, 0xb8, 0x40,
        };
        static const uint8_t p_escaped[] = {
            0x00, 0x00, 0x00, 0x01, 0x21, 0x9a, 0x00, 0x00,
            0x03, 0x01, 0x00, 0x00, 0x03, 0x00, 0x7f,
        };
        static const uint8_t p_forbidden[] = {
            0x00, 0x00, 0x00, 0x01, 0xa1, 0x9a,
        };
        static const uint8_t p_sei[] = {
            0x00, 0x00, 0x00, 0x01, 0x06, 0x05, 0x80,
            0x00, 0x00, 0x00, 0x01, 0x21, 0x9a,
        };
        static const uint8_t p_garbage[] = {
            0x12, 0x00, 0x00, 0x00, 0x01, 0x21, 0x9a,
        };
        OpenIMPT31AvcAuCheck au;

        if (openimp_t31_avc_au_check(idr_access_unit,
                                     sizeof(idr_access_unit), 1, &au) ||
            au.nal_count != 3u || au.vcl_count != 1u ||
            au.first_bad_offset != UINT32_MAX ||
            openimp_t31_avc_au_check(idr_access_unit,
                                     sizeof(idr_access_unit), -1, &au) ||
            openimp_t31_avc_au_check(p_access_unit, sizeof(p_access_unit),
                                     0, &au) ||
            openimp_t31_avc_au_check(p_escaped, sizeof(p_escaped),
                                     0, &au) ||
            au.nals[0].length != sizeof(p_escaped)) {
            fprintf(stderr, "clean T31 access unit rejected\n");
            failed = 1;
        }
        if (openimp_t31_avc_au_check(p_embedded, sizeof(p_embedded),
                                     0, &au) !=
                (OPENIMP_T31_AU_SHORT_START | OPENIMP_T31_AU_MULTI_VCL |
                 OPENIMP_T31_AU_BAD_ORDER) ||
            au.first_bad_offset != 6u || au.nal_count != 2u ||
            au.nals[0].length != 6u || au.nals[1].offset != 6u ||
            au.nals[1].length != 7u) {
            fprintf(stderr, "embedded start code not reported (0x%x)\n",
                    au.flags);
            failed = 1;
        }
        {
            char text[256];

            openimp_t31_avc_au_describe(p_embedded, sizeof(p_embedded),
                                        &au, text, sizeof(text));
            if (strcmp(text,
                       "flags=0x046 sc3 multivcl order len=13 nals=2 vcl=2 "
                       "[1@0+6 1@6+7] bad@6 [00 00 00 01 21 9a|00 00 01 21 "
                       "88 12 34]") != 0) {
                fprintf(stderr, "unexpected AU summary: %s\n", text);
                failed = 1;
            }
            openimp_t31_avc_au_describe(p_embedded, sizeof(p_embedded),
                                        &au, text, 12u);
            if (strlen(text) != 11u) {
                fprintf(stderr, "AU summary overflowed\n");
                failed = 1;
            }
        }
        if (openimp_t31_avc_au_check(idr_mirrored, sizeof(idr_mirrored),
                                     1, &au) !=
                (OPENIMP_T31_AU_MULTI_VCL | OPENIMP_T31_AU_BAD_ORDER) ||
            au.first_bad_offset != 18u || au.nal_count != 6u) {
            fprintf(stderr, "mirrored prefix not reported (0x%x)\n",
                    au.flags);
            failed = 1;
        }
        if (openimp_t31_avc_au_check(p_forbidden, sizeof(p_forbidden),
                                     0, &au) !=
                OPENIMP_T31_AU_FORBIDDEN_BIT ||
            openimp_t31_avc_au_check(p_sei, sizeof(p_sei), 0, &au) !=
                (OPENIMP_T31_AU_BAD_TYPE | OPENIMP_T31_AU_BAD_ORDER) ||
            au.first_bad_offset != 0u ||
            openimp_t31_avc_au_check(p_garbage, sizeof(p_garbage), 0,
                                     &au) != OPENIMP_T31_AU_NO_START_CODE ||
            au.nals[0].offset != 1u ||
            openimp_t31_avc_au_check(p_access_unit, sizeof(p_access_unit),
                                     1, &au) !=
                OPENIMP_T31_AU_BAD_ORDER) {
            fprintf(stderr, "malformed T31 access unit accepted (0x%x)\n",
                    au.flags);
            failed = 1;
        }
    }

    if (failed)
        return 1;
    puts("T31 stream-layout tests passed");
    return 0;
}
