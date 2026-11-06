/*
 * Host test for the T30 Radix (H.265) encoder object.
 *
 * /dev/soc_vpu and the rmem allocator are fakes (as in helix_encoder_test):
 * RUN checks the channel node and the command list (source, bitstream,
 * reconstruction and reference addresses, slice type and QP in SDE_CFG0)
 * and writes a synthetic slice payload; the statistics reads return
 * pseudo-random values.  The test checks what the encoder object owns:
 * the access units (VPS/SPS/PPS before every IDR, NAL types, emulation
 * prevention over the header/payload seam), the slice QP against the list,
 * the GOP, the reference swap and the failure path.  Linked without PIE,
 * buffers in a low window: the encoder keeps addresses in 32-bit fields.
 */
#define _GNU_SOURCE
#include <assert.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "dma_alloc.h"
#include "t30/radix/t30_radix_encoder.h"

#define FAKE_FD 78

typedef struct {
    uint32_t clist, vlist, mdelay, channel_id;
    int32_t vpu_id;
    uint32_t codecdir, workphase, status, output_len, dma_addr;
    int32_t thread_id;
    uint32_t cmpx, n_flag, ncu_addr;
} FakeChannel;

#define WINDOW_BASE 0x58000000u
#define WINDOW_SIZE (128u << 20)
static uint8_t *window;
static size_t window_used;
static unsigned int live_buffers;

int DMA_AllocDescriptorTop(IMPDMABufferInfo *info, int size, const char *tag)
{
    (void)tag;
    if (!window) {
        window = mmap((void *)(uintptr_t)WINDOW_BASE, WINDOW_SIZE,
                      PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        assert(window == (uint8_t *)(uintptr_t)WINDOW_BASE);
    }
    window_used = (window_used + 4095u) & ~(size_t)4095u;
    assert(window_used + (size_t)size <= WINDOW_SIZE);
    memset(info, 0, sizeof(*info));
    info->virt_addr = info->phys_addr = (uint32_t)(uintptr_t)(window + window_used);
    info->size = (uint32_t)size;
    window_used += (size_t)size;
    live_buffers++;
    return 0;
}

int DMA_FreePhys(uint32_t phys_addr)
{
    assert(phys_addr);
    live_buffers--;
    return 0;
}

int DMA_RmemFlushCache(void *virt_addr, uint32_t size, int dir)
{
    (void)virt_addr;
    (void)size;
    (void)dir;
    return 0;
}

int __real_open(const char *path, int flags, ...);
int __wrap_open(const char *path, int flags, ...)
{
    if (!strcmp(path, "/dev/soc_vpu"))
        return FAKE_FD;
    return __real_open(path, flags, 0);
}

int __real_close(int fd);
int __wrap_close(int fd)
{
    return fd == FAKE_FD ? 0 : __real_close(fd);
}

/* what the last RUN's command list programmed */
static uint32_t list_sde_cfg0, list_bs, list_rawy, list_rawc, list_recon_y,
                list_ref_y, list_dst_stride, list_ref_stride;
static int list_has_ref_stride;
static int run_fail;
static unsigned int runs, reg_reads;
static uint32_t frame_phys;

static uint32_t list_value(const uint32_t *list, uint32_t reg, int *found)
{
    uint32_t v = 0;
    unsigned int i;

    *found = 0;
    for (i = 0; i < 4096u; i += 2u) {
        if ((list[i + 1] & 0x7fffcu) == reg && (list[i + 1] >> 31)) {
            v = list[i];
            *found = 1;
        }
        if (list[i + 1] == 0xc0008008u)
            break;
    }
    return v;
}

int __wrap_ioctl(int fd, unsigned long request, ...)
{
    va_list ap;
    void *arg;

    va_start(ap, request);
    arg = va_arg(ap, void *);
    va_end(ap);
    assert(fd == FAKE_FD);
    if (request == 0xc0386300u) {
        FakeChannel *c = arg;

        assert(c->codecdir == 0x10u && c->thread_id == -1 && c->mdelay == 20000u);
        c->clist = 1;
        c->channel_id = 5;
        return 0;
    }
    if (request == 0xc0386301u)
        return 0;
    if (request == 0xc0386307u) {
        uint32_t *reg = arg;

        assert(reg[0] >= 0x13600000u && reg[0] < 0x13680000u && !(reg[0] & 3u));
        reg[1] = (reg[0] * 2654435761u >> 9) & 0x0fffffffu;
        reg_reads++;
        return 0;
    }
    if (request == 0xc0386302u) {
        FakeChannel *c = arg;
        const uint32_t *list = (const uint32_t *)(uintptr_t)c->dma_addr;
        uint8_t *bs;
        int f1, f2, f3, f4, f5, f6, f7;
        unsigned int i, n;

        runs++;
        assert(c->vpu_id == 0x04000001 && c->codecdir == 0x10u);
        list_sde_cfg0 = list_value(list, 0x68000u, &f1);
        list_bs = list_value(list, 0x68008u, &f2);
        list_rawy = list_value(list, 0x2000cu, &f3);
        list_rawc = list_value(list, 0x20010u, &f4);
        list_recon_y = list_value(list, 0x10014u, &f5);
        list_ref_y = list_value(list, 0x30800u, &f6);
        list_dst_stride = list_value(list, 0x1001cu, &f7);
        list_ref_stride = list_value(list, 0x30064u, &list_has_ref_stride);
        assert(f7);
        assert(f1 && f2 && f3 && f4 && f5);
        assert(list_rawy == frame_phys);
        if (run_fail)
            return -1;
        /* slice data: an RBSP with zero runs that need emulation
         * prevention, ending with the stop bit */
        bs = (uint8_t *)(uintptr_t)list_bs;
        n = 300u + runs * 7u;
        for (i = 0; i < n; i++)
            bs[i] = (uint8_t)(i % 5u == 0u ? 0u : i * 13u + runs);
        bs[0] = bs[1] = 0;
        bs[2] = 1;
        bs[n - 1] = 0x80;
        c->output_len = n;
        return 0;
    }
    assert(!"unexpected ioctl");
    return -1;
}

/* the NAL types of an Annex B access unit; returns the count */
static unsigned int nal_types(const uint8_t *p, uint32_t len, unsigned int *types,
                              const uint8_t **slice, uint32_t *slice_len)
{
    unsigned int n = 0;
    uint32_t i = 0;

    while (i + 3u < len) {
        uint32_t start;

        if (!(p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 1)) {
            i++;
            continue;
        }
        start = i + 3u;
        types[n++] = (p[start] >> 1) & 0x3fu;
        for (i = start; i + 3u <= len && !(p[i] == 0 && p[i + 1] == 0 &&
                                           (p[i + 2] == 1 || (p[i + 2] == 0 && i + 3u < len && p[i + 3] == 1)));
             i++)
            ;
        if (types[n - 1] < 32u) {
            *slice = p + start;
            *slice_len = (i + 3u <= len ? i : len) - start;
        }
    }
    return n;
}

/* the payload of the first NAL of a type (up to the next start code) */
static const uint8_t *nal_find(const uint8_t *p, uint32_t len, unsigned int type,
                               uint32_t *out_len)
{
    uint32_t i, j;

    for (i = 0; i + 3u < len; i++) {
        if (!(p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 1) ||
            ((p[i + 3] >> 1) & 0x3fu) != type)
            continue;
        for (j = i + 3u; j + 3u <= len && !(p[j] == 0 && p[j + 1] == 0 && p[j + 2] <= 1u); j++)
            ;
        *out_len = (j + 3u <= len ? j : len) - (i + 3u);
        return p + i + 3u;
    }
    return NULL;
}

/* no 00 00 0x (x <= 3) left unescaped inside a NAL */
static int escaped(const uint8_t *p, uint32_t len)
{
    uint32_t i;

    for (i = 2; i < len; i++)
        if (p[i - 2] == 0 && p[i - 1] == 0 && p[i] < 3u)
            return 0;
    return 1;
}

int main(void)
{
    static uint8_t frame_buf[640 * 368 * 3 / 2] __attribute__((aligned(64)));
    HWEncoderParams hp;
    T30RadixEncoder *e;
    IMPFrameInfo frame;
    unsigned int f, idrs = 0;
    static uint8_t hdr0[512];
    uint32_t hdr0_len = 0;

    mallopt(M_MMAP_MAX, 0);     /* stream buffers stay in the low heap */
    memset(&hp, 0, sizeof(hp));
    hp.width = 640;
    hp.height = 360;
    hp.fps_num = 25;
    hp.fps_den = 1;
    hp.gop_length = 10;
    hp.rc_mode = HW_RC_MODE_CBR;
    hp.bitrate = 500000;
    hp.max_qp = 45;
    hp.min_qp = 15;
    assert(OpenIMP_T30_RadixCreate(&e, &hp) == 0);
    assert(live_buffers == 4u);   /* command list, bitstream, 2 pictures */

    memset(&frame, 0, sizeof(frame));
    frame.phyAddr = (uint32_t)(uintptr_t)frame_buf;
    frame_phys = frame.phyAddr;
    frame.size = sizeof(frame_buf);
    for (f = 0; f < 25u; f++) {
        HWStreamBuffer *s = NULL;
        unsigned int types[8], n;
        const uint8_t *slice = NULL;
        uint32_t slice_len = 0, prev_recon = list_recon_y;
        const uint8_t *au;
        int idr = f % 10u == 0u;

        frame.timeStamp = f;
        assert(OpenIMP_T30_RadixEncode(e, &frame, &s) == 0 && s);
        au = (const uint8_t *)(uintptr_t)s->virt_addr;
        n = nal_types(au, s->length, types, &slice, &slice_len);
        if (idr) {
            assert(n == 4u && types[0] == 32u && types[1] == 33u &&
                   types[2] == 34u && types[3] == 19u);
            assert(s->frame_type == HW_FRAME_TYPE_I);
            if (!idrs) {
                /* the SPS (the VPS carries random extension bits) */
                const uint8_t *sps = nal_find(au, s->length, 33u, &hdr0_len);

                assert(sps && hdr0_len <= sizeof(hdr0));
                memcpy(hdr0, sps, hdr0_len);
            }
            idrs++;
        } else {
            assert(n == 1u && types[0] == 1u && s->frame_type == HW_FRAME_TYPE_P);
            /* P: the previous reconstruction is the reference */
            assert(list_ref_y == prev_recon && list_recon_y != prev_recon);
            /* vendor codec_enc (libimp 1.0.5 uclibc 4.7.2, 0x5d900-0x5d90c):
             * block 0x32/0x34 = reference +0x28/+0x2c as is, so MCE 0x30064
             * = luma | chroma << 16 with the chroma stride NOT halved
             * (the halved one drifted P pictures on the T30X, b40) */
            assert(list_has_ref_stride && list_ref_stride == (640u | (640u << 16)));
            /* slice_pic_order_cnt_lsb after first_slice (1), pps id ue 0
             * ('1') and slice_type ue 1 ('010'): the count since the IDR */
            assert(((((uint32_t)slice[2] & 7u) << 5) | (slice[3] >> 3)) == f % 10u);
        }
        assert(((list_sde_cfg0 >> 2) & 3u) == (idr ? 2u : 1u));
        /* codec_enc 0x5d87c-0x5d8a8: reconstruction 0x2e = +0x28, 0x30 =
         * +0x2c >> 1; ODMA 0x1001c = luma << 16 | chroma */
        assert(list_dst_stride == ((640u << 16) | 320u));
        assert(list_rawc == frame_phys + 640u * 368u);
        assert(escaped(slice, slice_len));
        assert(s->timestamp == f);
        free((void *)(uintptr_t)s->virt_addr);
        free(s);
    }
    assert(idrs == 3u && runs >= 25u && reg_reads >= 25u * 19u);

    /* run-time changes (I265eReconfig): a new bit rate waits for the next
     * IDR (picture 30) and then gives new parameter sets (level from the
     * bit rate); a new frame rate requests an IDR at once */
    {
        HWEncoderParams up = hp;
        int saw_new_headers = 0;

        up.bitrate = 40000000;
        assert(OpenIMP_T30_RadixUpdateParams(e, &up) == 0);
        for (f = 25; f < 31u; f++) {
            HWStreamBuffer *s = NULL;
            unsigned int types[8], n;
            const uint8_t *slice = NULL, *au;
            uint32_t slice_len = 0;

            frame.timeStamp = f;
            assert(OpenIMP_T30_RadixEncode(e, &frame, &s) == 0 && s);
            au = (const uint8_t *)(uintptr_t)s->virt_addr;
            n = nal_types(au, s->length, types, &slice, &slice_len);
            assert((s->frame_type == HW_FRAME_TYPE_I) == (f == 30u));
            if (f == 30u) {
                const uint8_t *sps;
                uint32_t sps_len = 0;

                assert(n == 4u && types[0] == 32u);
                sps = nal_find(au, s->length, 33u, &sps_len);
                assert(sps);
                saw_new_headers = sps_len != hdr0_len || memcmp(sps, hdr0, hdr0_len) != 0;
            }
            free((void *)(uintptr_t)s->virt_addr);
            free(s);
        }
        assert(saw_new_headers);
        /* the same values again: nothing pending, no IDR */
        assert(OpenIMP_T30_RadixUpdateParams(e, &up) == 0);
        {
            HWStreamBuffer *s = NULL;

            assert(OpenIMP_T30_RadixEncode(e, &frame, &s) == 0 && s);
            assert(s->frame_type == HW_FRAME_TYPE_P);
            free((void *)(uintptr_t)s->virt_addr);
            free(s);
        }
        up.fps_num = 15;
        assert(OpenIMP_T30_RadixUpdateParams(e, &up) == 0);
        {
            HWStreamBuffer *s = NULL;

            assert(OpenIMP_T30_RadixEncode(e, &frame, &s) == 0 && s);
            assert(s->frame_type == HW_FRAME_TYPE_I);
            free((void *)(uintptr_t)s->virt_addr);
            free(s);
        }
    }

    /* a failed RUN: no stream, counted, the next picture works again */
    {
        HWStreamBuffer *s = NULL;

        run_fail = 1;
        assert(OpenIMP_T30_RadixEncode(e, &frame, &s) != 0 && !s);
        assert(OpenIMP_T30_RadixFailures(e) == 1u);
        run_fail = 0;
        assert(OpenIMP_T30_RadixRequestIDR(e) == 0);
        assert(OpenIMP_T30_RadixEncode(e, &frame, &s) == 0 && s);
        assert(OpenIMP_T30_RadixFailures(e) == 0u);
        assert(s->frame_type == HW_FRAME_TYPE_I);
        free((void *)(uintptr_t)s->virt_addr);
        free(s);
    }
    OpenIMP_T30_RadixDestroy(e);
    assert(live_buffers == 0u);
    printf("radix_encoder_test: PASS (%u runs, %u register reads)\n", runs, reg_reads);
    return 0;
}
