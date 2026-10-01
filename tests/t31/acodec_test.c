/* Host tests for the AENC/ADEC built-in codecs and channel API
 * (src/audio, shared by T31 and T23). Built once as is (T31 behaviour:
 * G.726 at 16 kbit/s, frame time stamps) and once with -DPLATFORM_T23
 * (G.726 at 32 kbit/s, wall-clock time stamps) by tests/t23. */

#define _GNU_SOURCE
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include <imp/imp_audio.h>

#include "audio/openimp_audio_codec.h"

#define CHECK(condition)                                                      \
    do {                                                                      \
        if (!(condition)) {                                                   \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,  \
                    #condition);                                              \
            exit(1);                                                          \
        }                                                                     \
    } while (0)

/* The AENC/ADEC module sizes its nodes from the AI/AO device attributes. */
static int stub_numperfrm = 160;

int IMP_AI_GetPubAttr(int device, IMPAudioIOAttr *attr)
{
    (void)device;
    memset(attr, 0, sizeof(*attr));
    attr->samplerate = AUDIO_SAMPLE_RATE_8000;
    attr->bitwidth = AUDIO_BIT_WIDTH_16;
    attr->soundmode = AUDIO_SOUND_MODE_MONO;
    attr->numPerFrm = stub_numperfrm;
    return 0;
}

int IMP_AO_GetPubAttr(int device, IMPAudioIOAttr *attr)
{
    return IMP_AI_GetPubAttr(device, attr);
}

/* Deterministic, multiple-of-4 test signal (the G.726 reference below was
 * produced from it with an independent encoder, ffmpeg's adpcm_g726). */
static void make_signal(int16_t *out, int count)
{
    uint32_t lcg = 12345u;
    int n;

    for (n = 0; n < count; n++) {
        int32_t tri = (n * 97) % 2000;
        int32_t v;

        lcg = lcg * 1103515245u + 12345u;
        tri = tri < 1000 ? tri : 2000 - tri;
        v = (tri - 500) * 24 + (int32_t)((lcg >> 16) & 0x7ff) - 1024;
        out[n] = (int16_t)(v & ~3);
    }
}

static double snr_db(const int16_t *ref, const int16_t *test, int count,
                     int skip)
{
    double signal = 0.0;
    double noise = 0.0;
    int i;

    for (i = skip; i < count; i++) {
        double d = (double)ref[i] - (double)test[i];

        signal += (double)ref[i] * (double)ref[i];
        noise += d * d;
    }
    if (noise == 0.0)
        return 200.0;
    return 10.0 * log10(signal / noise);
}

static void sine(int16_t *out, int count, double amplitude, double freq)
{
    int i;

    for (i = 0; i < count; i++)
        out[i] = (int16_t)lrint(amplitude * sin(2.0 * M_PI * freq * i / 8000.0));
}

static void test_g711(void)
{
    int code;
    int16_t pcm[1600];
    int16_t back[1600];
    uint8_t coded[1600];

    /* Sun g711.c reference points */
    CHECK(openimp_linear2alaw(0) == 0xd5);
    CHECK(openimp_linear2alaw(-1) == 0x55);
    CHECK(openimp_linear2alaw(32767) == 0xaa);
    CHECK(openimp_linear2alaw(-32768) == 0x2a);
    CHECK(openimp_linear2ulaw(0) == 0xff);
    CHECK(openimp_linear2ulaw(32767) == 0x80);
    CHECK(openimp_linear2ulaw(-32768) == 0x00);
    CHECK(openimp_alaw2linear(0xd5) == 8);
    CHECK(openimp_alaw2linear(0xaa) == 32256);
    CHECK(openimp_alaw2linear(0x2a) == -32256);
    CHECK(openimp_ulaw2linear(0xff) == 0);
    CHECK(openimp_ulaw2linear(0x80) == 32124);
    CHECK(openimp_ulaw2linear(0x00) == -32124);

    /* every code word survives decode + encode */
    for (code = 0; code < 256; code++) {
        CHECK(openimp_linear2alaw(openimp_alaw2linear((uint8_t)code)) ==
              code);
        if (code != 0x7f) /* u-law negative zero encodes as +0 */
            CHECK(openimp_linear2ulaw(openimp_ulaw2linear((uint8_t)code)) ==
                  code);
    }

    sine(pcm, 1600, 12000.0, 1000.0);
    CHECK(openimp_g711a_encode(coded, pcm, 1600) == 1600);
    CHECK(openimp_g711a_decode(back, coded, 1600) == 3200);
    CHECK(snr_db(pcm, back, 1600, 0) > 30.0);
    CHECK(openimp_g711u_encode(coded, pcm, 1600) == 1600);
    CHECK(openimp_g711u_decode(back, coded, 1600) == 3200);
    CHECK(snr_db(pcm, back, 1600, 0) > 30.0);
}

static void test_adpcm(void)
{
    OpenIMPAdpcmState enc;
    OpenIMPAdpcmState dec;
    int16_t pcm[1600];
    int16_t back[1600];
    uint8_t coded[800];
    int i;

    sine(pcm, 1600, 8000.0, 440.0);
    openimp_adpcm_init(&enc);
    openimp_adpcm_init(&dec);
    /* two calls to check the state carries over between frames */
    CHECK(openimp_adpcm_encode(&enc, coded, pcm, 800) == 400);
    CHECK(openimp_adpcm_encode(&enc, coded + 400, pcm + 800, 800) == 400);
    CHECK(openimp_adpcm_decode(&dec, back, coded, 400) == 1600);
    CHECK(openimp_adpcm_decode(&dec, back + 800, coded + 400, 400) == 1600);
    CHECK(snr_db(pcm, back, 1600, 160) > 20.0);

    /* an odd sample count flushes the last high nibble */
    openimp_adpcm_init(&enc);
    CHECK(openimp_adpcm_encode(&enc, coded, pcm, 3) == 2);
    CHECK((coded[1] & 0x0f) == 0);

    /* silence stays silent */
    memset(pcm, 0, sizeof(pcm));
    openimp_adpcm_init(&enc);
    CHECK(openimp_adpcm_encode(&enc, coded, pcm, 64) == 32);
    for (i = 0; i < 32; i++)
        CHECK(coded[i] == 0);
}

static const uint8_t g726_ffmpeg_prefix[83] = {
    0xaa, 0x95, 0x55, 0x54, 0xea, 0xef, 0xf1, 0x14, 0x03, 0x3a, 0xef, 0xf1,
    0x11, 0x00, 0xfb, 0xaf, 0xc8, 0x11, 0x00, 0xcb, 0xaf, 0xf3, 0x14, 0x40,
    0xde, 0xfb, 0xfc, 0x35, 0x43, 0x4f, 0x8b, 0x8f, 0x31, 0x17, 0x37, 0xfb,
    0xe3, 0xcd, 0x74, 0xdf, 0x3b, 0xe2, 0x37, 0x04, 0x31, 0xfb, 0xec, 0x83,
    0x74, 0x03, 0x3b, 0xeb, 0x33, 0x17, 0x4c, 0x3e, 0x28, 0xc8, 0xd0, 0x4c,
    0x33, 0xbb, 0x3c, 0xc4, 0x1c, 0x32, 0x2f, 0x86, 0x01, 0xd3, 0xcc, 0xbf,
    0xb7, 0x30, 0x41, 0xb0, 0x8e, 0xe7, 0x31, 0x01, 0x20, 0x88, 0xe8,
};

/* ffmpeg's adpcm_g726 at 32 kbit/s agrees with the Sun G.721 arithmetic
 * on the first 93 bytes (186 samples) of make_signal(); the old T23
 * src/audio G.721 coder, written independently from the CCITT reference,
 * matched this one bit for bit over 64000 samples. */
static const uint8_t g726_32_ffmpeg_prefix[93] = {
    0x88, 0x88, 0x87, 0x77, 0x77, 0x65, 0x32, 0x21, 0xec, 0xb9, 0xaa, 0xcd,
    0xde, 0x14, 0x55, 0x65, 0x32, 0x2e, 0xfe, 0xaa, 0xa9, 0xcd, 0xee, 0x22,
    0x36, 0x55, 0x5f, 0x31, 0xfe, 0xab, 0x9a, 0xae, 0xef, 0xc4, 0x16, 0x27,
    0x12, 0xff, 0x1e, 0xed, 0xba, 0xad, 0xc1, 0xee, 0x35, 0x46, 0x53, 0x21,
    0xf3, 0xdb, 0xa9, 0xd9, 0xdf, 0xc2, 0x1f, 0x55, 0x64, 0x12, 0x14, 0xde,
    0x91, 0x8e, 0xd1, 0xde, 0x1f, 0x15, 0x17, 0x1e, 0x21, 0xff, 0xcf, 0xbd,
    0x9e, 0xfe, 0xc4, 0xb6, 0x4f, 0x54, 0xd5, 0xdf, 0xfc, 0xac, 0xab, 0x1c,
    0xed, 0x5b, 0x7f, 0x53, 0xc3, 0x4e, 0xee, 0x9e, 0xab,

};

static void test_g726_32(void)
{
    OpenIMPG726State enc;
    OpenIMPG726State dec;
    int16_t pcm[1600];
    int16_t back[1600];
    uint8_t coded[800];
    int written;

    make_signal(pcm, 1600);
    openimp_g726_32_init(&enc);
    /* an odd sample count keeps its nibble for the next call */
    written = openimp_g726_encode(&enc, coded, pcm, 1);
    CHECK(written == 0);
    written += openimp_g726_encode(&enc, coded, pcm + 1, 1598);
    CHECK(written == 799);
    written += openimp_g726_encode(&enc, coded + 799, pcm + 1599, 1);
    CHECK(written == 800);
    CHECK(memcmp(coded, g726_32_ffmpeg_prefix,
                 sizeof(g726_32_ffmpeg_prefix)) == 0);

    openimp_g726_32_init(&dec);
    CHECK(openimp_g726_decode(&dec, back, coded, 800) == 1600);
    CHECK(snr_db(pcm, back, 1600, 80) > 12.0);

    sine(pcm, 1600, 6000.0, 500.0);
    openimp_g726_32_init(&enc);
    openimp_g726_32_init(&dec);
    CHECK(openimp_g726_encode(&enc, coded, pcm, 1600) == 800);
    CHECK(openimp_g726_decode(&dec, back, coded, 800) == 1600);
    CHECK(snr_db(pcm, back, 1600, 80) > 20.0);
}

static int64_t wall_us(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

static void test_g726(void)
{
    OpenIMPG726State enc;
    OpenIMPG726State dec;
    int16_t pcm[1600];
    int16_t back[1600];
    uint8_t coded[400];
    int written;

    make_signal(pcm, 1600);
    openimp_g726_16_init(&enc);
    /* frames that are not a multiple of 4 samples keep their bits */
    written = openimp_g726_16_encode(&enc, coded, pcm, 3);
    CHECK(written == 0);
    written += openimp_g726_16_encode(&enc, coded, pcm + 3, 1596);
    CHECK(written == 399);
    written += openimp_g726_16_encode(&enc, coded + 399, pcm + 1599, 1);
    CHECK(written == 400);
    CHECK(memcmp(coded, g726_ffmpeg_prefix, sizeof(g726_ffmpeg_prefix)) == 0);
    openimp_g726_16_init(&enc);
    CHECK(openimp_g726_16_encode(&enc, coded, pcm, 1600) == 400);
    /* the independent ffmpeg encoder agrees on the opening 332 samples; it
     * departs from the Sun/libimp arithmetic later (rounding of the
     * predictor), so only the prefix is a cross-check */
    CHECK(memcmp(coded, g726_ffmpeg_prefix, sizeof(g726_ffmpeg_prefix)) == 0);

    openimp_g726_16_init(&dec);
    CHECK(openimp_g726_16_decode(&dec, back, coded, 400) == 1600);
    CHECK(snr_db(pcm, back, 1600, 80) > 6.0);

    sine(pcm, 1600, 6000.0, 500.0);
    openimp_g726_16_init(&enc);
    openimp_g726_16_init(&dec);
    CHECK(openimp_g726_16_encode(&enc, coded, pcm, 1600) == 400);
    CHECK(openimp_g726_16_decode(&dec, back, coded, 400) == 1600);
    CHECK(snr_db(pcm, back, 1600, 80) > 6.0);
}

static int user_open_calls;
static int user_close_calls;

static int user_open(void *attr, void *encoder)
{
    CHECK(attr == NULL && encoder == NULL);
    user_open_calls++;
    return 0;
}

/* "encoder" that copies every other byte */
static int user_encode(void *encoder, IMPAudioFrame *frame,
                       unsigned char *out, int *out_len)
{
    const uint8_t *in = (const uint8_t *)(const void *)frame->virAddr;
    int i;

    CHECK(encoder == NULL);
    CHECK(*out_len >= 8192);
    for (i = 0; i < frame->len / 2; i++)
        out[i] = in[i * 2];
    *out_len = frame->len / 2;
    return 0;
}

static int user_close(void *encoder)
{
    CHECK(encoder == NULL);
    user_close_calls++;
    return 0;
}

static int user_decode(void *decoder, unsigned char *in, int in_len,
                       unsigned short *out, int *out_len, int *chns)
{
    CHECK(decoder == NULL && chns == NULL);
    memcpy(out, in, (size_t)in_len);
    *out_len = in_len;
    return 0;
}

static void test_api(void)
{
    IMPAudioEncChnAttr enc_attr;
    IMPAudioDecChnAttr dec_attr;
    IMPAudioEncEncoder user_encoder;
    IMPAudioDecDecoder user_decoder;
    IMPAudioFrame frame;
    IMPAudioStream stream;
    IMPAudioStream decoded;
    int16_t pcm[160];
    uint8_t expect[160];
    int handles[6];
    int handle;
    int i;

    sine(pcm, 160, 9000.0, 700.0);
    memset(&frame, 0, sizeof(frame));
    frame.bitwidth = AUDIO_BIT_WIDTH_16;
    frame.soundmode = AUDIO_SOUND_MODE_MONO;
    frame.virAddr = (uint32_t *)(void *)pcm;
    frame.len = (int)sizeof(pcm);
    frame.timeStamp = 123456789;
    frame.seq = 7;

    /* invalid arguments */
    memset(&enc_attr, 0, sizeof(enc_attr));
    enc_attr.type = PT_PCM;
    enc_attr.bufSize = 4;
    CHECK(IMP_AENC_CreateChn(0, &enc_attr) == -1);  /* PCM is not built in */
    enc_attr.type = PT_AEC;
    CHECK(IMP_AENC_CreateChn(0, &enc_attr) == -1);
    enc_attr.type = PT_G711A;
    CHECK(IMP_AENC_CreateChn(6, &enc_attr) == -1);
    CHECK(IMP_AENC_CreateChn(-1, &enc_attr) == -1);
    enc_attr.type = (IMPAudioPalyloadType)6;         /* unregistered slot */
    CHECK(IMP_AENC_CreateChn(0, &enc_attr) == -1);
    CHECK(IMP_AENC_SendFrame(0, &frame) == -1);       /* not created */

    /* G.711A encode through a channel */
    enc_attr.type = PT_G711A;
    CHECK(IMP_AENC_CreateChn(0, &enc_attr) == 0);
    CHECK(IMP_AENC_CreateChn(0, &enc_attr) == -1);    /* already created */
    CHECK(IMP_AENC_PollingStream(0, 10) == -1);       /* empty: timeout */
    CHECK(IMP_AENC_GetStream(0, &stream, NOBLOCK) == -1);
    CHECK(IMP_AENC_SendFrame(0, &frame) == 0);
    CHECK(IMP_AENC_PollingStream(0, 10) == 0);
    {
        int64_t before = wall_us();

        CHECK(IMP_AENC_GetStream(0, &stream, BLOCK) == 0);
#if defined(PLATFORM_T23)
        /* T23 OEM: GetStream stamps the stream with gettimeofday() */
        CHECK(stream.timeStamp >= before && stream.timeStamp <= wall_us());
#else
        (void)before;
        CHECK(stream.timeStamp == 123456789);
#endif
    }
    CHECK(stream.len == 160);
    CHECK(stream.seq == 7);
    openimp_g711a_encode(expect, pcm, 160);
    CHECK(memcmp(stream.stream, expect, 160) == 0);

    /* ADEC G.711A back to PCM */
    memset(&dec_attr, 0, sizeof(dec_attr));
    dec_attr.type = PT_G711A;
    dec_attr.bufSize = 4;
    dec_attr.mode = ADEC_MODE_PACK;
    CHECK(IMP_ADEC_CreateChn(1, &dec_attr) == 0);
    CHECK(IMP_ADEC_SendStream(1, &stream, BLOCK) == 0);
    CHECK(IMP_AENC_ReleaseStream(0, &stream) == 0);
    CHECK(IMP_ADEC_PollingStream(1, 10) == 0);
    CHECK(IMP_ADEC_GetStream(1, &decoded, BLOCK) == 0);
    CHECK(decoded.len == 320);
    CHECK(snr_db(pcm, (const int16_t *)(const void *)decoded.stream, 160, 0) >
          30.0);
    CHECK(IMP_ADEC_ReleaseStream(1, &decoded) == 0);
    stream.stream = expect;                            /* foreign pointer */
    CHECK(IMP_ADEC_ReleaseStream(1, &stream) == -1);

    /* ClearChnBuf drops queued output */
    stream.stream = expect;
    stream.len = 160;
    CHECK(IMP_ADEC_SendStream(1, &stream, BLOCK) == 0);
    CHECK(IMP_ADEC_ClearChnBuf(1) == 0);
    CHECK(IMP_ADEC_GetStream(1, &decoded, NOBLOCK) == -1);
    CHECK(IMP_ADEC_DestroyChn(1) == 0);
    CHECK(IMP_AENC_DestroyChn(0) == 0);
    CHECK(IMP_AENC_DestroyChn(0) == 0);

    /* every built-in type creates and produces the expected sizes */
    {
        static const struct {
            IMPAudioPalyloadType type;
            int enc_len;
            int dec_len;
        } cases[] = {
            { PT_G711U, 160, 320 },
            { PT_ADPCM, 80, 320 },
#if defined(PLATFORM_T23)
            { PT_G726, 80, 320 },   /* 32 kbit/s */
#else
            { PT_G726, 40, 320 },   /* 16 kbit/s */
#endif
        };
        unsigned int c;

        for (c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
            enc_attr.type = cases[c].type;
            dec_attr.type = cases[c].type;
            CHECK(IMP_AENC_CreateChn(2, &enc_attr) == 0);
            CHECK(IMP_ADEC_CreateChn(2, &dec_attr) == 0);
            CHECK(IMP_AENC_SendFrame(2, &frame) == 0);
            CHECK(IMP_AENC_GetStream(2, &stream, BLOCK) == 0);
            CHECK(stream.len == cases[c].enc_len);
            CHECK(IMP_ADEC_SendStream(2, &stream, BLOCK) == 0);
            CHECK(IMP_ADEC_GetStream(2, &decoded, BLOCK) == 0);
            CHECK(decoded.len == cases[c].dec_len);
            CHECK(IMP_ADEC_ReleaseStream(2, &decoded) == 0);
            CHECK(IMP_AENC_ReleaseStream(2, &stream) == 0);
            CHECK(IMP_ADEC_DestroyChn(2) == 0);
            CHECK(IMP_AENC_DestroyChn(2) == 0);
        }
    }

    /* user encoder registration: handles 6..10, then full */
    memset(&user_encoder, 0, sizeof(user_encoder));
    user_encoder.type = (IMPAudioPalyloadType)6;
    strcpy(user_encoder.name, "test");
    user_encoder.openEncoder = user_open;
    user_encoder.encoderFrm = user_encode;
    user_encoder.closeEncoder = user_close;
    for (i = 0; i < 5; i++) {
        CHECK(IMP_AENC_RegisterEncoder(&handles[i], &user_encoder) == 0);
        CHECK(handles[i] == 6 + i);
    }
    CHECK(IMP_AENC_RegisterEncoder(&handles[5], &user_encoder) == -1);
    for (i = 1; i < 5; i++)
        CHECK(IMP_AENC_UnRegisterEncoder(&handles[i]) == 0);
    handle = 3;
    CHECK(IMP_AENC_UnRegisterEncoder(&handle) == -1);

    enc_attr.type = (IMPAudioPalyloadType)handles[0];
    enc_attr.bufSize = 2;
    CHECK(IMP_AENC_CreateChn(3, &enc_attr) == 0);
    CHECK(user_open_calls == 1);
    CHECK(IMP_AENC_SendFrame(3, &frame) == 0);
    CHECK(IMP_AENC_SendFrame(3, &frame) == 0);
    CHECK(IMP_AENC_GetStream(3, &stream, BLOCK) == 0);
    CHECK(stream.len == 160);
    CHECK(stream.stream[1] == ((const uint8_t *)(const void *)pcm)[2]);
    CHECK(IMP_AENC_ReleaseStream(3, &stream) == 0);
    CHECK(IMP_AENC_DestroyChn(3) == 0);
    CHECK(user_close_calls == 1);
    CHECK(IMP_AENC_UnRegisterEncoder(&handles[0]) == 0);

    memset(&user_decoder, 0, sizeof(user_decoder));
    strcpy(user_decoder.name, "copy");
    user_decoder.decodeFrm = user_decode;
    CHECK(IMP_ADEC_RegisterDecoder(&handle, &user_decoder) == 0);
    CHECK(handle == 6);
    dec_attr.type = (IMPAudioPalyloadType)handle;
    CHECK(IMP_ADEC_CreateChn(4, &dec_attr) == 0);
    stream.stream = expect;
    stream.len = 100;
    CHECK(IMP_ADEC_SendStream(4, &stream, BLOCK) == 0);
    CHECK(IMP_ADEC_GetStream(4, &decoded, BLOCK) == 0);
    CHECK(decoded.len == 100 && memcmp(decoded.stream, expect, 100) == 0);
    CHECK(IMP_ADEC_ReleaseStream(4, &decoded) == 0);
    CHECK(IMP_ADEC_DestroyChn(4) == 0);
    CHECK(IMP_ADEC_UnRegisterDecoder(&handle) == 0);

    /* without AI attributes the node falls back to 800 bytes */
    stub_numperfrm = 0;
    enc_attr.type = PT_G711U;
    CHECK(IMP_AENC_CreateChn(5, &enc_attr) == 0);
    frame.len = 2000;   /* 1000 samples: clamped to the 800-byte node */
    {
        static int16_t big[1000];

        frame.virAddr = (uint32_t *)(void *)big;
        CHECK(IMP_AENC_SendFrame(5, &frame) == 0);
    }
    CHECK(IMP_AENC_GetStream(5, &stream, BLOCK) == 0);
    CHECK(stream.len == 800);
    CHECK(IMP_AENC_ReleaseStream(5, &stream) == 0);
    CHECK(IMP_AENC_DestroyChn(5) == 0);
    stub_numperfrm = 160;
}

int main(void)
{
    test_g711();
    test_adpcm();
    test_g726();
    test_g726_32();
    test_api();
#if defined(PLATFORM_T23)
    printf("T23 audio codec tests passed\n");
#else
    printf("T31 audio codec tests passed\n");
#endif
    return 0;
}
