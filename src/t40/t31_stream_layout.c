#include "t31_stream_layout.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

int openimp_t31_completion_payload_size(const void *status,
                                        size_t status_size,
                                        uint32_t *payload_size)
{
    if (!status || !payload_size ||
        status_size < OPENIMP_T31_COMPLETION_PAYLOAD_SIZE_OFFSET +
                          sizeof(*payload_size))
        return -1;

    memcpy(payload_size,
           (const uint8_t *)status +
               OPENIMP_T31_COMPLETION_PAYLOAD_SIZE_OFFSET,
           sizeof(*payload_size));
    return *payload_size ? 0 : -1;
}

int openimp_t31_stream_layout(uint32_t capacity, uint32_t payload_offset,
                              uint32_t header_size, uint32_t payload_size,
                              OpenIMPT31StreamLayout *layout)
{
    if (!layout || payload_size == 0u || payload_offset > capacity ||
        header_size > payload_offset ||
        payload_size > capacity - payload_offset)
        return -1;

    layout->payload_end = payload_offset + payload_size;
    layout->access_unit_size = header_size + payload_size;
    return 0;
}

static uint32_t find_annexb_start4(const uint8_t *data, uint32_t offset,
                                   uint32_t length)
{
    while (offset + 4u <= length) {
        if (data[offset] == 0u && data[offset + 1u] == 0u &&
            data[offset + 2u] == 0u && data[offset + 3u] == 1u)
            return offset;
        offset++;
    }
    return length;
}

static int annexb_nals(const uint8_t *data, uint32_t length,
                       OpenIMPT31AnnexBNAL *nals, uint32_t capacity, int hevc)
{
    uint32_t begin;
    uint32_t count = 0u;

    if (!data || !nals || !length || !capacity)
        return -1;

    begin = find_annexb_start4(data, 0u, length);
    if (begin != 0u)
        return 0;

    while (begin < length) {
        uint32_t nal_header = begin + 4u;
        uint32_t next;

        if (nal_header + (hevc ? 1u : 0u) >= length || count >= capacity)
            return -1;
        next = find_annexb_start4(data, nal_header, length);
        if (next <= nal_header)
            return -1;
        nals[count].offset = begin;
        nals[count].length = next - begin;
        nals[count].nal_type = hevc
            ? (uint8_t)((data[nal_header] >> 1) & 0x3fu)
            : (uint8_t)(data[nal_header] & 0x1fu);
        count++;
        begin = next;
    }

    return (int)count;
}

int openimp_t31_annexb_nals(const uint8_t *data, uint32_t length,
                            OpenIMPT31AnnexBNAL *nals, uint32_t capacity)
{
    return annexb_nals(data, length, nals, capacity, 0);
}

int openimp_t31_hevc_annexb_nals(const uint8_t *data, uint32_t length,
                                 OpenIMPT31AnnexBNAL *nals, uint32_t capacity)
{
    return annexb_nals(data, length, nals, capacity, 1);
}

/* Offset of the next 00 00 01 at or after offset, or length.  Skips three
 * bytes whenever the third byte cannot be part of a start code, so the scan
 * touches about a third of a CABAC payload. */
static uint32_t find_annexb_start3(const uint8_t *data, uint32_t offset,
                                   uint32_t length)
{
    while (offset + 3u <= length) {
        uint8_t third = data[offset + 2u];

        if (third > 1u) {
            offset += 3u;
        } else if (third == 1u) {
            if (data[offset] == 0u && data[offset + 1u] == 0u)
                return offset;
            offset += 3u;
        } else {
            offset++;
        }
    }
    return length;
}

static uint32_t au_check(const uint8_t *data, uint32_t length, int is_idr,
                         OpenIMPT31AvcAuCheck *report, int hevc)
{
    static const uint8_t avc_idr_sequence[3] = { 7u, 8u, 5u };
    static const uint8_t hevc_idr_sequence[4] = { 32u, 33u, 34u, 19u };
    const uint8_t *idr_sequence = hevc ? hevc_idr_sequence : avc_idr_sequence;
    const uint32_t idr_count = hevc ? 4u : 3u;
    const uint8_t idr_type = hevc ? 19u : 5u;
    uint32_t start;
    uint32_t index = 0u;
    uint32_t order_bad = 0u;

    if (!report)
        return 0u;
    memset(report, 0, sizeof(*report));
    report->first_bad_offset = UINT32_MAX;
    if (!data || length < 4u ||
        data[0] != 0u || data[1] != 0u || data[2] != 0u || data[3] != 1u)
        report->flags |= OPENIMP_T31_AU_NO_START_CODE;
    if (!data || !length)
        return report->flags;

    start = find_annexb_start3(data, 0u, length);
    while (start < length) {
        uint32_t begin = start > 0u && data[start - 1u] == 0u
            ? start - 1u : start;
        uint32_t header = start + 3u;
        uint32_t next = find_annexb_start3(data, header, length);
        uint32_t end = next;
        uint32_t unexpected = 0u;
        uint8_t type = 0u;

        if (next < length && next > header && data[next - 1u] == 0u)
            end = next - 1u;
        if (begin == start) {
            report->flags |= OPENIMP_T31_AU_SHORT_START;
            unexpected = 1u;
        }
        if (header >= end || (hevc && header + 1u >= end)) {
            report->flags |= OPENIMP_T31_AU_EMPTY_NAL;
            unexpected = 1u;
        } else {
            int known;

            if (hevc) {
                type = (data[header] >> 1) & 0x3fu;
                /* nuh_layer_id 0 and nuh_temporal_id_plus1 1 */
                known = (type == 1u || type == 19u || type == 32u ||
                         type == 33u || type == 34u) &&
                        (data[header] & 1u) == 0u &&
                        data[header + 1u] == 1u;
            } else {
                type = data[header] & 0x1fu;
                known = type == 1u || type == 5u || type == 7u ||
                        type == 8u;
            }
            if (data[header] & 0x80u) {
                report->flags |= OPENIMP_T31_AU_FORBIDDEN_BIT;
                unexpected = 1u;
            }
            if (!known) {
                report->flags |= OPENIMP_T31_AU_BAD_TYPE;
                unexpected = 1u;
            }
            if (type == 1u || type == idr_type) {
                report->vcl_count++;
                if (is_idr < 0)
                    is_idr = type == idr_type;
            }
        }
        if (is_idr > 0 && (index >= idr_count || type != idr_sequence[index]))
            order_bad = unexpected = 1u;
        else if (is_idr == 0 && (index != 0u || type != 1u))
            order_bad = unexpected = 1u;
        if (unexpected && report->first_bad_offset == UINT32_MAX)
            report->first_bad_offset = begin;

        if (report->recorded < OPENIMP_T31_AU_MAX_NALS) {
            OpenIMPT31AnnexBNAL *nal = &report->nals[report->recorded++];

            nal->offset = begin;
            nal->length = end - begin;
            nal->nal_type = type;
        } else {
            report->flags |= OPENIMP_T31_AU_TOO_MANY_NALS;
        }
        index++;
        start = next;
    }

    report->nal_count = index;
    if (report->vcl_count > 1u)
        report->flags |= OPENIMP_T31_AU_MULTI_VCL;
    if (!report->vcl_count)
        report->flags |= OPENIMP_T31_AU_NO_VCL;
    if (order_bad || (is_idr > 0 && index != idr_count) ||
        (is_idr == 0 && index != 1u))
        report->flags |= OPENIMP_T31_AU_BAD_ORDER;
    return report->flags;
}

uint32_t openimp_t31_avc_au_check(const uint8_t *data, uint32_t length,
                                  int is_idr, OpenIMPT31AvcAuCheck *report)
{
    return au_check(data, length, is_idr, report, 0);
}

uint32_t openimp_t31_hevc_au_check(const uint8_t *data, uint32_t length,
                                   int is_idr, OpenIMPT31AvcAuCheck *report)
{
    return au_check(data, length, is_idr, report, 1);
}

static size_t append_text(char *out, size_t out_size, size_t used,
                          const char *fmt, ...)
{
    va_list args;
    int n;

    if (used >= out_size)
        return used;
    va_start(args, fmt);
    n = vsnprintf(out + used, out_size - used, fmt, args);
    va_end(args);
    if (n < 0)
        return out_size;
    used += (size_t)n;
    return used < out_size ? used : out_size;
}

void openimp_t31_avc_au_describe(const uint8_t *data, uint32_t length,
                                 const OpenIMPT31AvcAuCheck *report,
                                 char *out, size_t out_size)
{
    static const struct {
        uint32_t flag;
        const char *name;
    } names[] = {
        { OPENIMP_T31_AU_NO_START_CODE, "nostart" },
        { OPENIMP_T31_AU_SHORT_START, "sc3" },
        { OPENIMP_T31_AU_MULTI_VCL, "multivcl" },
        { OPENIMP_T31_AU_NO_VCL, "novcl" },
        { OPENIMP_T31_AU_BAD_TYPE, "type" },
        { OPENIMP_T31_AU_FORBIDDEN_BIT, "fzb" },
        { OPENIMP_T31_AU_BAD_ORDER, "order" },
        { OPENIMP_T31_AU_EMPTY_NAL, "empty" },
        { OPENIMP_T31_AU_TOO_MANY_NALS, "more" },
    };
    size_t used = 0u;
    uint32_t index;

    if (!out || !out_size)
        return;
    out[0] = '\0';
    if (!report)
        return;

    used = append_text(out, out_size, used, "flags=0x%03x",
                       (unsigned int)report->flags);
    for (index = 0u; index < sizeof(names) / sizeof(names[0]); index++)
        if (report->flags & names[index].flag)
            used = append_text(out, out_size, used, " %s",
                               names[index].name);
    used = append_text(out, out_size, used, " len=%u nals=%u vcl=%u [",
                       (unsigned int)length,
                       (unsigned int)report->nal_count,
                       (unsigned int)report->vcl_count);
    for (index = 0u; index < report->recorded; index++)
        used = append_text(out, out_size, used, "%s%u@%u+%u",
                           index ? " " : "",
                           (unsigned int)report->nals[index].nal_type,
                           (unsigned int)report->nals[index].offset,
                           (unsigned int)report->nals[index].length);
    used = append_text(out, out_size, used, "]");

    if (report->first_bad_offset != UINT32_MAX && data &&
        report->first_bad_offset < length) {
        uint32_t bad = report->first_bad_offset;
        uint32_t from = bad >= 8u ? bad - 8u : 0u;
        uint32_t to = length - bad > 8u ? bad + 8u : length;

        /* Shows whether the extra NAL is a bare start code, an escape the
         * payload pass missed, or a copied header. */
        used = append_text(out, out_size, used, " bad@%u [", bad);
        for (index = from; index < to; index++)
            used = append_text(out, out_size, used, "%s%02x",
                               index == from ? "" :
                                   (index == bad ? "|" : " "),
                               data[index]);
        used = append_text(out, out_size, used, "]");
    }
    (void)used;
}
