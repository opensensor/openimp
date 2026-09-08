/* Link to the real T41 backend: no mock JPEG encoder and no camera access.
 * The padded NV12 image has red/blue halves. An ordinary decoder must find
 * those colors; the old inherited constant-gray placeholder fails. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "codec.h"
#include "hw_encoder.h"
#include <imp/imp_encoder.h>

typedef struct {
    uint32_t index, pool_index, width, height, pixel_format, size;
    uint32_t physical_address, virtual_address, direct_address, pool;
    int64_t timestamp;
} CapturedFrame;

int main(int argc, char **argv)
{
    uint32_t params[0x794 / 4];
    unsigned char pixels[64 * 32 * 3 / 2];
    CapturedFrame frame = {
        .width = 64, .height = 24, .pixel_format = 10,
        .size = sizeof(pixels), .virtual_address = (uintptr_t)pixels,
    };
    void *codec = NULL, *user, *stream;
    unsigned int x, y, iteration;
    FILE *output;

    assert(argc == 2);
    assert(!AL_Codec_Encode_SetDefaultParam(params));
    ((uint16_t *)params)[4] = ((uint16_t *)params)[6] = frame.width;
    ((uint16_t *)params)[5] = ((uint16_t *)params)[7] = frame.height;
    params[0x20 / 4] = IMP_ENC_PROFILE_JPEG;
    assert(!AL_Codec_Encode_Create(&codec, params));
    memset(pixels, 0, sizeof(pixels));
    for (y = 0; y < frame.height; ++y)
        for (x = 0; x < frame.width; ++x)
            pixels[y * 64 + x] = x < 32 ? 76 : 29;
    for (y = 0; y < frame.height / 2; ++y)
        for (x = 0; x < frame.width; x += 2) {
            pixels[64 * 32 + y * 64 + x] = x < 32 ? 85 : 255;
            pixels[64 * 32 + y * 64 + x + 1] = x < 32 ? 255 : 107;
        }
    output = fopen(argv[1], "wb");
    assert(output);
    /* Exceed the four-entry metadata FIFO and preserve timestamp zero. */
    for (iteration = 0; iteration < 8; ++iteration) {
        HWStreamBuffer *encoded;
        frame.timestamp = iteration * 40022;
        assert(!AL_Codec_Encode_Process(codec, &frame, &frame));
        assert(!AL_Codec_Encode_GetStream(codec, &stream, &user));
        assert(user == &frame);
        encoded = stream;
        assert(encoded->timestamp == (uint64_t)frame.timestamp);
        assert(encoded->length > 600);
        if (iteration == 7)
            assert(fwrite((void *)(uintptr_t)encoded->virt_addr, 1,
                          encoded->length, output) == encoded->length);
        assert(!AL_Codec_Encode_ReleaseStream(codec, stream, user));
    }
    assert(!fclose(output));
    assert(!AL_Codec_Encode_Destroy(codec));
    puts("T41 real JPEG backend: eight padded color frames and timestamps passed");
    return 0;
}
