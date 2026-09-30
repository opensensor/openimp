#ifndef OPENIMP_T31_STREAM_LAYOUT_H
#define OPENIMP_T31_STREAM_LAYOUT_H

#include <stddef.h>
#include <stdint.h>

#define OPENIMP_T31_COMPLETION_PAYLOAD_SIZE_OFFSET 0x104u

typedef struct {
    uint32_t payload_end;
    uint32_t access_unit_size;
} OpenIMPT31StreamLayout;

typedef struct {
    uint32_t offset;
    uint32_t length;
    uint8_t nal_type;
} OpenIMPT31AnnexBNAL;

/*
 * Structural check of one published H.264 access unit, the way a decoder
 * sees it: every 00 00 01 is a NAL boundary, whether or not the pack
 * splitter (4-byte start codes only) cut there.
 */
#define OPENIMP_T31_AU_MAX_NALS 8u

#define OPENIMP_T31_AU_NO_START_CODE  0x001u /* AU does not begin 00 00 00 01 */
#define OPENIMP_T31_AU_SHORT_START    0x002u /* 3-byte start code inside the AU */
#define OPENIMP_T31_AU_MULTI_VCL      0x004u /* more than one slice NAL (1/5) */
#define OPENIMP_T31_AU_NO_VCL         0x008u /* no slice NAL at all */
#define OPENIMP_T31_AU_BAD_TYPE       0x010u /* NAL type not in {1,5,7,8} */
#define OPENIMP_T31_AU_FORBIDDEN_BIT  0x020u /* forbidden_zero_bit set */
#define OPENIMP_T31_AU_BAD_ORDER      0x040u /* IDR != SPS,PPS,IDR or P != slice */
#define OPENIMP_T31_AU_EMPTY_NAL      0x080u /* start code without NAL header */
#define OPENIMP_T31_AU_TOO_MANY_NALS  0x100u /* more NALs than recorded */

typedef struct {
    uint32_t flags;
    uint32_t nal_count;        /* all NALs, including unrecorded ones */
    uint32_t vcl_count;
    uint32_t first_bad_offset; /* start code of the first unexpected NAL,
                                  UINT32_MAX when none */
    uint32_t recorded;         /* entries valid in nals[] */
    OpenIMPT31AnnexBNAL nals[OPENIMP_T31_AU_MAX_NALS];
} OpenIMPT31AvcAuCheck;

int openimp_t31_completion_payload_size(const void *status,
                                        size_t status_size,
                                        uint32_t *payload_size);
int openimp_t31_stream_layout(uint32_t capacity, uint32_t payload_offset,
                              uint32_t header_size, uint32_t payload_size,
                              OpenIMPT31StreamLayout *layout);
int openimp_t31_annexb_nals(const uint8_t *data, uint32_t length,
                            OpenIMPT31AnnexBNAL *nals, uint32_t capacity);
/* is_idr: 1 = expect SPS,PPS,IDR; 0 = expect one P slice; -1 = take the
 * expectation from the first slice NAL.  Returns report->flags (0 = clean). */
uint32_t openimp_t31_avc_au_check(const uint8_t *data, uint32_t length,
                                  int is_idr, OpenIMPT31AvcAuCheck *report);
/* One-line summary of a check result for logs: flag names, NAL list
 * (type@offset+length) and the bytes around the first unexpected NAL. */
void openimp_t31_avc_au_describe(const uint8_t *data, uint32_t length,
                                 const OpenIMPT31AvcAuCheck *report,
                                 char *out, size_t out_size);

#endif
