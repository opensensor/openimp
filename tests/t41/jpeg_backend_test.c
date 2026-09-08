/* Link to the real T41 backend: no mock JPEG encoder and no camera access.
 * The padded NV12 image has red/blue halves. An ordinary decoder must find
 * those colors; the old inherited constant-gray placeholder fails. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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
    unsigned char *pixels;
    CapturedFrame frame = {
        .width = 64, .height = 24, .pixel_format = 10,
    };
    void *codec = NULL, *user, *stream;
    unsigned int x, y, iteration;
    FILE *output;
    struct timespec start, end;
    unsigned int padded_height;

    assert(argc == 2 || argc == 4);
    if (argc == 4) {
        frame.width = (uint32_t)atoi(argv[2]);
        frame.height = (uint32_t)atoi(argv[3]);
        assert(frame.width >= 32 && frame.width <= 4096 && !(frame.width & 1));
        assert(frame.height >= 16 && frame.height <= 2160 && !(frame.height & 1));
    }
    padded_height = (frame.height + 15) & ~15u;
    frame.size = frame.width * padded_height * 3 / 2;
    pixels = calloc(1, frame.size);
    assert(pixels);
    frame.virtual_address = (uintptr_t)pixels;
    assert(!AL_Codec_Encode_SetDefaultParam(params));
    ((uint16_t *)params)[4] = ((uint16_t *)params)[6] = frame.width;
    ((uint16_t *)params)[5] = ((uint16_t *)params)[7] = frame.height;
    params[0x20 / 4] = IMP_ENC_PROFILE_JPEG;
    assert(!AL_Codec_Encode_Create(&codec, params));
    for (y = 0; y < frame.height; ++y)
        for (x = 0; x < frame.width; ++x)
            pixels[y * frame.width + x] = x < frame.width / 2 ? 76 : 29;
    for (y = 0; y < frame.height / 2; ++y)
        for (x = 0; x < frame.width; x += 2) {
            pixels[frame.width * padded_height + y * frame.width + x] =
                x < frame.width / 2 ? 85 : 255;
            pixels[frame.width * padded_height + y * frame.width + x + 1] =
                x < frame.width / 2 ? 255 : 107;
        }
    output = fopen(argv[1], "wb");
    assert(output);
    clock_gettime(CLOCK_MONOTONIC, &start);
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
    clock_gettime(CLOCK_MONOTONIC, &end);
    printf("JPEG %ux%u: %.3f ms/frame (8 frames)\n", frame.width, frame.height,
           ((end.tv_sec - start.tv_sec) * 1000.0 +
            (end.tv_nsec - start.tv_nsec) / 1000000.0) / 8.0);
    assert(!AL_Codec_Encode_Destroy(codec));
    free(pixels);
    puts("T41 real JPEG backend: eight padded color frames and timestamps passed");
    return 0;
}
