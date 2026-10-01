#ifndef OPENIMP_T30_ANNEXB_H
#define OPENIMP_T30_ANNEXB_H

/*
 * Annex-B NAL writer for the native Helix encoders.
 *
 * The RBSP can be appended in several pieces (CPU-written slice header,
 * then the VPU's CABAC payload) while the emulation-prevention state carries
 * across the seam, so neither piece has to be copied next to the other
 * first.  Runs of non-zero bytes go through memchr/memcpy; only zero bytes
 * take the per-byte path.
 */

#include <stdint.h>
#include <string.h>

typedef struct {
    uint8_t *output;
    uint8_t *end;
    unsigned int zeros;
} T30AnnexBWriter;

/* Worst case: one emulation-prevention byte per two payload bytes. */
static inline uint32_t t30_annexb_bound(uint32_t rbsp_bytes)
{
    return 5u + rbsp_bytes + rbsp_bytes / 2u + 1u;
}

static inline int t30_annexb_begin(T30AnnexBWriter *writer,
                                   uint8_t *destination, uint32_t capacity,
                                   int type, int priority)
{
    if (capacity < 5u)
        return -1;
    destination[0] = 0;
    destination[1] = 0;
    destination[2] = 0;
    destination[3] = 1;
    destination[4] = (uint8_t)((priority << 5) | type);
    writer->output = destination + 5;
    writer->end = destination + capacity;
    writer->zeros = 0;
    return 0;
}

static inline int t30_annexb_append(T30AnnexBWriter *writer,
                                    const uint8_t *source, uint32_t length)
{
    while (length) {
        uint8_t byte;

        if (writer->zeros < 2u) {
            const uint8_t *zero = memchr(source, 0, length);
            uint32_t run = zero ? (uint32_t)(zero - source) : length;

            if (run) {
                if (run > (uint32_t)(writer->end - writer->output))
                    return -1;
                memcpy(writer->output, source, run);
                writer->output += run;
                writer->zeros = 0;
                source += run;
                length -= run;
                continue;
            }
        }
        byte = *source++;
        length--;
        if (writer->zeros >= 2u && byte <= 3u) {
            if (writer->output >= writer->end)
                return -1;
            *writer->output++ = 3u;
            writer->zeros = 0;
        }
        if (writer->output >= writer->end)
            return -1;
        *writer->output++ = byte;
        writer->zeros = byte ? 0u : writer->zeros + 1u;
    }
    return 0;
}

#endif
