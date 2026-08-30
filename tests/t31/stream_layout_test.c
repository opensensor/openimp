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

    if (failed)
        return 1;
    puts("T31 stream-layout tests passed");
    return 0;
}
