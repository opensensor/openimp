/* On-device bring-up test for the native T23 Helix H.264 encoder.
 *
 * Encodes a few pictures of a synthetic NV12 frame held in reserved memory
 * with exactly the code libimp uses for OPENIMP_T23_ENCODER=native
 * (src/t30/t30_helix_encoder.c, T21-family command list for T23) and
 * writes an Annex-B stream to decode on a PC.  It uses nothing but
 * /dev/soc_vpu and /dev/rmem: stop the streamer (timps/RVD and anything
 * else using libimp) first, since this program allocates from the same
 * reserved memory.
 *
 *   openimp-t23-helix-selftest [-c] [-w W] [-h H] [-n FRAMES] [-g GOP]
 *       [-q QP] [-m] [-t TIMEOUT_MS] [-o OUT.h264]
 *
 *   -c   only open /dev/soc_vpu, request and release a channel and
 *        allocate the buffers; submit nothing to the VPU
 *   -m   move a square between pictures (default: identical pictures)
 *
 * Every step is also written to /dev/kmsg ("t23-helix-selftest: ...") so
 * a console or netconsole capture shows the last step before a hang. */
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dma_alloc.h"
#include "t30/t30_helix_encoder.h"

static int kmsg_fd = -1;

static void note(const char *fmt, ...)
{
    char line[256];
    va_list args;
    int n;

    va_start(args, fmt);
    n = vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    if (n < 0)
        return;
    if ((size_t)n >= sizeof(line))
        n = (int)sizeof(line) - 1;
    fprintf(stderr, "%s\n", line);
    if (kmsg_fd >= 0) {
        char kline[300];
        int k = snprintf(kline, sizeof(kline), "t23-helix-selftest: %s\n",
                         line);

        if (k > 0)
            (void)!write(kmsg_fd, kline, (size_t)k);
    }
}

static void draw(uint8_t *nv12, uint32_t width, uint32_t aligned_height,
                 uint32_t height, uint32_t frame, int moving)
{
    static const uint8_t bars[8][3] = {
        {235, 128, 128}, {210, 16, 146}, {170, 166, 16}, {145, 54, 34},
        {106, 202, 222}, {81, 90, 240}, {41, 240, 110}, {16, 128, 128},
    };
    uint8_t *chroma = nv12 + width * aligned_height;
    uint32_t square = moving ? (frame * 8u) % (width > 64u ? width - 64u : 1u)
                             : width / 3u;
    uint32_t x, y;

    for (y = 0; y < aligned_height; y++)
        for (x = 0; x < width; x++) {
            const uint8_t *bar = bars[(x * 8u) / width];
            uint8_t luma = y < height * 3u / 4u ? bar[0]
                                                : (uint8_t)(x * 255u / width);

            if (x >= square && x < square + 64u && y >= height / 3u &&
                y < height / 3u + 64u)
                luma = 255u - luma;
            nv12[y * width + x] = luma;
        }
    for (y = 0; y < aligned_height / 2u; y++)
        for (x = 0; x < width / 2u; x++) {
            const uint8_t *bar = bars[(x * 16u) / width];
            int gray = y * 2u >= height * 3u / 4u;

            chroma[y * width + 2u * x] = gray ? 128u : bar[1];
            chroma[y * width + 2u * x + 1u] = gray ? 128u : bar[2];
        }
}

static void usage(void)
{
    fprintf(stderr, "usage: openimp-t23-helix-selftest [-c] [-w W] [-h H] "
            "[-n FRAMES] [-g GOP] [-q QP] [-m] [-t TIMEOUT_MS] "
            "[-o OUT.h264]\n");
}

int main(int argc, char **argv)
{
    HWEncoderParams params;
    T30HelixEncoder *encoder = NULL;
    IMPDMABufferInfo frame_buffer;
    const char *output = "/tmp/t23_helix_selftest.h264";
    uint32_t width = 640, height = 360, frames = 10, gop = 5, qp = 30;
    uint32_t aligned_height;
    uint32_t frame_size;
    uint32_t i;
    uint64_t total = 0;
    int check_only = 0;
    int moving = 0;
    int opt;
    FILE *out;
    char timeout[16];

    while ((opt = getopt(argc, argv, "cw:h:n:g:q:mt:o:")) != -1) {
        switch (opt) {
        case 'c': check_only = 1; break;
        case 'w': width = (uint32_t)strtoul(optarg, NULL, 0); break;
        case 'h': height = (uint32_t)strtoul(optarg, NULL, 0); break;
        case 'n': frames = (uint32_t)strtoul(optarg, NULL, 0); break;
        case 'g': gop = (uint32_t)strtoul(optarg, NULL, 0); break;
        case 'q': qp = (uint32_t)strtoul(optarg, NULL, 0); break;
        case 'm': moving = 1; break;
        case 't':
            snprintf(timeout, sizeof(timeout), "%s", optarg);
            setenv("OPENIMP_T23_HELIX_TIMEOUT_MS", timeout, 1);
            break;
        case 'o': output = optarg; break;
        default: usage(); return 2;
        }
    }
    if (width < 64u || width > 2048u || (width & 15u) || height < 64u ||
        height > 2048u || !frames || frames > 1000u || !gop ||
        qp < 10u || qp > 51u) {
        usage();
        return 2;
    }
    kmsg_fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
    aligned_height = (height + 15u) & ~15u;
    frame_size = width * aligned_height * 3u / 2u;

    memset(&params, 0, sizeof(params));
    params.width = width;
    params.height = height;
    params.fps_num = 15;
    params.fps_den = 1;
    params.gop_length = gop;
    params.rc_mode = HW_RC_MODE_FIXQP;
    params.qp = qp;
    params.min_qp = qp;
    params.max_qp = qp;

    note("start %ux%u frames=%u gop=%u qp=%u%s%s", width, height, frames,
         gop, qp, check_only ? " check-only" : "", moving ? " moving" : "");
    if (DMA_AllocDescriptor(&frame_buffer, (int)frame_size,
                            "t23-selftest-frame") != 0 ||
        !frame_buffer.phys_addr ||
        DMA_VirtToPhys((void *)(uintptr_t)frame_buffer.virt_addr) !=
            frame_buffer.phys_addr) {
        note("cannot allocate the %u-byte frame in reserved memory "
             "(is /dev/rmem available and the streamer stopped?)",
             frame_size);
        return 1;
    }
    note("frame phys=0x%08x size=%u", frame_buffer.phys_addr, frame_size);
    draw((uint8_t *)(uintptr_t)frame_buffer.virt_addr, width,
         aligned_height, height, 0, moving);

    note("creating encoder (channel request, buffer allocation)");
    if (OpenIMP_T30_HelixCreate(&encoder, &params) != 0) {
        note("encoder create failed: %s", strerror(errno));
        DMA_FreePhys(frame_buffer.phys_addr);
        return 1;
    }
    if (check_only) {
        OpenIMP_T30_HelixDestroy(encoder);
        DMA_FreePhys(frame_buffer.phys_addr);
        note("check-only: channel requested and released, nothing "
             "submitted");
        return 0;
    }
    out = fopen(output, "wb");
    if (!out) {
        note("cannot open %s: %s", output, strerror(errno));
        OpenIMP_T30_HelixDestroy(encoder);
        return 1;
    }
    for (i = 0; i < frames; i++) {
        IMPFrameInfo info;
        HWStreamBuffer *stream = NULL;
        int ret;

        if (moving && i)
            draw((uint8_t *)(uintptr_t)frame_buffer.virt_addr, width,
                 aligned_height, height, i, moving);
        memset(&info, 0, sizeof(info));
        info.width = width;
        info.height = height;
        info.pixfmt = 0x3231564eu;     /* NV12 */
        info.size = frame_size;
        info.phyAddr = frame_buffer.phys_addr;
        info.virAddr = frame_buffer.virt_addr;
        info.timeStamp = (int64_t)i * 66667;
        note("frame %u: submit", i);
        ret = OpenIMP_T30_HelixEncode(encoder, &info, &stream);
        if (ret != 0) {
            note("frame %u: encode failed (consecutive failures %u)", i,
                 OpenIMP_T30_HelixFailures(encoder));
            if (OpenIMP_T30_HelixFailures(encoder) >= 3u)
                break;
            continue;
        }
        note("frame %u: %s %u bytes", i,
             stream->frame_type == HW_FRAME_TYPE_I ? "IDR" : "P",
             stream->length);
        total += stream->length;
        if (fwrite((const void *)(uintptr_t)stream->virt_addr, 1,
                   stream->length, out) != stream->length)
            note("write to %s failed", output);
        fflush(out);
        free((void *)(uintptr_t)stream->virt_addr);
        free(stream);
    }
    fclose(out);
    OpenIMP_T30_HelixDestroy(encoder);
    DMA_FreePhys(frame_buffer.phys_addr);
    note("done: %llu bytes in %s", (unsigned long long)total, output);
    if (kmsg_fd >= 0)
        close(kmsg_fd);
    return total ? 0 : 1;
}
