/* Built-in audio codecs of the AENC/ADEC modules (T31 and T23).
 *
 * These match the vendor libimp 1.1.6 codecs bit for bit: G.711 A/u-law
 * (Sun g711.c with 13/14-bit segment search), IMA ADPCM (Jansen's
 * adpcm.c, high nibble first) and G.726 at 16 kbit/s (Sun g72x reference
 * plus the 2-bit tables, MSB-first bit packing). The T23 OEM libimp links
 * the same codec objects but opens G.726 at 32 kbit/s (G.721 tables), so
 * that rate is here too. The vendor keeps one static
 * state per codec and direction; here the state belongs to the caller so
 * channels do not share predictor history.
 *
 * The module has no IMP dependencies so the host tests can link it.
 */
#ifndef OPENIMP_AUDIO_CODEC_H
#define OPENIMP_AUDIO_CODEC_H

#include <stdint.h>

typedef struct {
    int16_t valprev;
    int8_t index;
} OpenIMPAdpcmState;

typedef struct {
    int32_t yl;          /* locked quantizer scale factor */
    int16_t yu;          /* unlocked quantizer scale factor */
    int16_t dms;         /* short-term average of F[I] */
    int16_t dml;         /* long-term average of F[I] */
    int16_t ap;          /* speed control */
    int16_t a[2];        /* pole predictor coefficients */
    int16_t b[6];        /* zero predictor coefficients */
    int16_t pk[2];       /* signs of previous partial reconstructions */
    int16_t dq[6];       /* previous quantized differences, float format */
    int16_t sr[2];       /* previous reconstructed signal, float format */
    int8_t td;           /* tone detect */
    uint32_t bit_buffer; /* packer/unpacker */
    int bit_count;
    int bits;            /* code word size: 2 (16 kbit/s) or 4 (32 kbit/s) */
} OpenIMPG726State;

uint8_t openimp_linear2alaw(int16_t pcm);
uint8_t openimp_linear2ulaw(int16_t pcm);
int16_t openimp_alaw2linear(uint8_t code);
int16_t openimp_ulaw2linear(uint8_t code);

/* return bytes written */
int openimp_g711a_encode(uint8_t *out, const int16_t *in, int samples);
int openimp_g711u_encode(uint8_t *out, const int16_t *in, int samples);
/* return bytes written (2 per input byte) */
int openimp_g711a_decode(int16_t *out, const uint8_t *in, int bytes);
int openimp_g711u_decode(int16_t *out, const uint8_t *in, int bytes);

void openimp_adpcm_init(OpenIMPAdpcmState *state);
/* return bytes written: (samples + 1) / 2 */
int openimp_adpcm_encode(OpenIMPAdpcmState *state, uint8_t *out,
                         const int16_t *in, int samples);
/* decodes 2 samples per input byte; returns bytes written (4 per byte) */
int openimp_adpcm_decode(OpenIMPAdpcmState *state, int16_t *out,
                         const uint8_t *in, int bytes);

/* G.726 at 16 kbit/s (2-bit codes, what T31 libimp 1.1.6 uses for PT_G726)
 * or 32 kbit/s (4-bit codes, G.721, what the T23 OEM libimp uses). The init
 * call picks the rate; encode/decode follow the state. */
void openimp_g726_16_init(OpenIMPG726State *state);
void openimp_g726_32_init(OpenIMPG726State *state);
/* return bytes written; leftover bits stay in the state like the vendor */
int openimp_g726_encode(OpenIMPG726State *state, uint8_t *out,
                        const int16_t *in, int samples);
/* return samples written (8 / bits per input byte) */
int openimp_g726_decode(OpenIMPG726State *state, int16_t *out,
                        const uint8_t *in, int bytes);
/* the same, kept for the 16 kbit/s callers */
int openimp_g726_16_encode(OpenIMPG726State *state, uint8_t *out,
                           const int16_t *in, int samples);
int openimp_g726_16_decode(OpenIMPG726State *state, int16_t *out,
                           const uint8_t *in, int bytes);

#endif
