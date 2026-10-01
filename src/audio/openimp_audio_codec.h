/* Built-in speech codecs of the Ingenic IMP audio encoder/decoder (AENC/ADEC):
 * G.711 A-law/u-law, G.726 at 32 kbit/s and IMA ADPCM.  Platform neutral;
 * shared by every OpenIMP target that provides IMP_AENC/IMP_ADEC. */
#ifndef OPENIMP_AUDIO_CODEC_H
#define OPENIMP_AUDIO_CODEC_H

#include <stdint.h>

/* G.711: one byte per 16-bit sample; return bytes written. */
int openimp_g711a_encode(uint8_t *out, const int16_t *in, int samples);
int openimp_g711u_encode(uint8_t *out, const int16_t *in, int samples);
int openimp_g711a_decode(int16_t *out, const uint8_t *in, int bytes);
int openimp_g711u_decode(int16_t *out, const uint8_t *in, int bytes);

/* G.726-32 (ITU G.721 algorithm, CCITT reference implementation) with the
 * OEM bit packing: 4-bit code words, most significant first; a partial
 * byte carries over to the next call, as in the OEM codec. */
typedef struct {
    int32_t yl;
    int16_t yu, dms, dml, ap;
    int16_t a[2], b[6], pk[2], dq[6], sr[2];
    int8_t td;
    uint32_t bitbuf;
    int nbits;
} OpenIMPG726State;

void openimp_g726_init(OpenIMPG726State *state);
/* returns bytes written */
int openimp_g726_encode(OpenIMPG726State *state, uint8_t *out,
                        const int16_t *in, int samples);
/* returns samples written */
int openimp_g726_decode(OpenIMPG726State *state, int16_t *out,
                        const uint8_t *in, int bytes);

/* IMA/DVI ADPCM, 4 bits per sample, first sample in the high nibble. */
typedef struct {
    int16_t valprev;
    int8_t index;
} OpenIMPAdpcmState;

/* returns bytes written */
int openimp_adpcm_encode(OpenIMPAdpcmState *state, uint8_t *out,
                         const int16_t *in, int samples);
/* decodes `nibbles` code words (2 per byte); returns samples written */
int openimp_adpcm_decode(OpenIMPAdpcmState *state, int16_t *out,
                         const uint8_t *in, int nibbles);

#endif
