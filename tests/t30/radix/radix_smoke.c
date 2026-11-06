/*
 * Desk / QEMU tool: encode synthetic NV12 pictures with the T30 Radix
 * encoder object on the real /dev/soc_vpu and rmem, without the ISP:
 *   radix_smoke <width> <height> <frames> <out.h265> [rc: fixqp|cbr|vbr] [kbit/s]
 * The pictures are a gradient with a moving box (and moving stripes in
 * the chroma); the stream goes to the file as the encoder returns it.
 * For the QEMU T30 machine (Radix model) and as a first T30X step.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dma_alloc.h"
#include "t30/radix/t30_radix_encoder.h"

static void draw(uint8_t *y, uint8_t *c, uint32_t w, uint32_t h, uint32_t h16, uint32_t f)
{
    uint32_t i, j, bx = (f * 8u) % (w > 64u ? w - 64u : 1u), by = (f * 4u) % (h > 64u ? h - 64u : 1u);

    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            y[j * w + i] = (uint8_t)((i + j + f) & 0xffu);
    for (j = by; j < by + 64u && j < h; j++)
        for (i = bx; i < bx + 64u && i < w; i++)
            y[j * w + i] = (uint8_t)(((i - bx) ^ (j - by)) & 8u ? 235u : 16u);
    for (j = 0; j < h / 2u; j++)
        for (i = 0; i < w; i += 2u) {
            c[j * w + i] = (uint8_t)(128u + ((i / 2u + f) & 32u ? 40u : 0u));
            c[j * w + i + 1u] = (uint8_t)(128u - ((j + f) & 16u ? 40u : 0u));
        }
    (void)h16;
}

int main(int argc, char **argv)
{
    uint32_t w, h, w16, h16, n, f, total = 0, idrs = 0;
    HWEncoderParams hp;
    IMPDMABufferInfo buf;
    T30RadixEncoder *e;
    IMPFrameInfo frame;
    FILE *out;

    if (argc < 5) {
        fprintf(stderr, "usage: %s w h frames out.h265 [fixqp|cbr|vbr] [kbit/s]\n", argv[0]);
        return 2;
    }
    w = (uint32_t)atoi(argv[1]);
    h = (uint32_t)atoi(argv[2]);
    n = (uint32_t)atoi(argv[3]);
    w16 = (w + 15u) & ~15u;
    h16 = (h + 15u) & ~15u;
    memset(&hp, 0, sizeof(hp));
    hp.width = w;
    hp.height = h;
    hp.fps_num = 25;
    hp.fps_den = 1;
    hp.gop_length = 25;
    hp.rc_mode = argc > 5 && !strcmp(argv[5], "fixqp") ? HW_RC_MODE_FIXQP
               : argc > 5 && !strcmp(argv[5], "vbr") ? HW_RC_MODE_VBR : HW_RC_MODE_CBR;
    hp.bitrate = (argc > 6 ? (uint32_t)atoi(argv[6]) : 1000u) * 1000u;
    hp.qp = 30;
    hp.max_qp = 45;
    hp.min_qp = 15;
    if (DMA_AllocDescriptor(&buf, (int)(w16 * h16 * 3u / 2u), "radix-smoke") != 0) {
        fprintf(stderr, "rmem allocation failed\n");
        return 1;
    }
    if (OpenIMP_T30_RadixCreate(&e, &hp) != 0) {
        fprintf(stderr, "RadixCreate failed\n");
        return 1;
    }
    out = fopen(argv[4], "wb");
    if (!out)
        return 1;
    memset(&frame, 0, sizeof(frame));
    frame.phyAddr = buf.phys_addr;
    frame.virAddr = buf.virt_addr;
    frame.size = w16 * h16 * 3u / 2u;
    frame.width = w;
    frame.height = h;
    for (f = 0; f < n; f++) {
        HWStreamBuffer *s = NULL;
        uint8_t *y = (uint8_t *)(uintptr_t)buf.virt_addr;

        draw(y, y + w16 * h16, w16, h, h16, f);
        frame.timeStamp = f * 40000u;
        if (OpenIMP_T30_RadixEncode(e, &frame, &s) != 0) {
            fprintf(stderr, "frame %u: encode failed (failures %u)\n", f,
                    OpenIMP_T30_RadixFailures(e));
            continue;
        }
        fwrite((const void *)(uintptr_t)s->virt_addr, 1, s->length, out);
        total += s->length;
        idrs += s->frame_type == HW_FRAME_TYPE_I;
        printf("frame %u: %s %u bytes\n", f, s->frame_type == HW_FRAME_TYPE_I ? "IDR" : "P", s->length);
        free((void *)(uintptr_t)s->virt_addr);
        free(s);
    }
    fclose(out);
    OpenIMP_T30_RadixDestroy(e);
    DMA_FreePhys(buf.phys_addr);
    printf("radix_smoke: %u frames, %u IDR, %u bytes\n", n, idrs, total);
    return 0;
}
