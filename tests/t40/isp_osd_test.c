/* T40 ISP-drawn OSD (src/t40/openimp_t40_isp_osd.c): region table, handle
 * order, picture copy, show/overlap/notify and the 0x080000a2 driver control
 * as the vendor libimp 1.3.0/1.3.1 does them.  Host test with fake rmem,
 * frame source and tuning ioctl; needs the T40 1.3.1 vendor headers. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include <imp/imp_framesource.h>
#include <imp/imp_isp.h>

#include "dma_alloc.h"

int IMP_OSD_SetPoolSize_ISP(int size);
void IMP_OSD_Exit_ISP(void);

/* ---- fakes ---- */
static struct {
    int32_t vinum, direction, control;
    IMPISPSingleOSDAttr attr;
} sent[64];
static int nsent;
static int nalloc, nfree, nflush, ninval;
static unsigned char (*rmem)[4096];   /* below 4 GiB: virt_addr is 32-bit */

int OpenIMP_P1_TuningIOCtl(uint32_t command, void *argument)
{
    const struct {
        int32_t vinum, direction, control;
        uintptr_t payload;
    } *request = argument;

    assert(command == 0xc0105436U);
    assert(nsent < 64);
    sent[nsent].vinum = request->vinum;
    sent[nsent].direction = request->direction;
    sent[nsent].control = request->control;
    memcpy(&sent[nsent].attr, (void *)request->payload, sizeof(IMPISPSingleOSDAttr));
    nsent++;
    return 0;
}

int DMA_AllocDescriptor(IMPDMABufferInfo *out, int size, const char *tag)
{
    (void)tag;
    assert(size <= 4096 && nalloc < 8);
    memset(out, 0, sizeof(*out));
    out->virt_addr = (uint32_t)(uintptr_t)rmem[nalloc];
    out->phys_addr = 0x04000000U + 0x1000U * (uint32_t)nalloc;
    out->size = (uint32_t)size;
    nalloc++;
    return 0;
}

int DMA_FreePhys(uint32_t phys)
{
    assert(phys >= 0x04000000U);
    nfree++;
    return 0;
}

int DMA_RmemFlushCache(void *address, uint32_t length, int direction)
{
    (void)address;
    (void)length;
    assert(direction == 1 || direction == 2);
    if (direction == 2)
        ninval++;
    else
        nflush++;
    return 0;
}

/* IMP_ISP_GetRaw: the ISP-device ioctl */
static struct { uint32_t command; IMPISPGETRawiConfig attr; int calls, result; } rawio;
int OpenIMP_P1_IspIOCtlOpened(uint32_t command, void *argument);
int OpenIMP_P1_IspIOCtlOpened(uint32_t command, void *argument)
{
    rawio.command = command;
    memcpy(&rawio.attr, argument, sizeof(rawio.attr));
    rawio.calls++;
    if (rawio.result)
        errno = rawio.result;
    return rawio.result ? -1 : 0;
}

int IMP_FrameSource_GetChnAttr(int chn, IMPFSChnAttr *attr)
{
    if (chn > 3)
        return -1;
    memset(attr, 0, sizeof(*attr));
    attr->picWidth = chn ? 640 : 1920;
    attr->picHeight = chn ? 360 : 1080;
    return 0;
}

/* ---- helpers ---- */
static uint8_t pic[64 * 16 * 4];

static void fill(IMPIspOsdAttrAsm *a, int chx, int handle, int x, int y)
{
    memset(a, 0, sizeof(*a));
    a->type = ISP_OSD_REG_PIC;
    a->stsinglepicAttr.chx = chx;
    a->stsinglepicAttr.pic_num = handle;
    a->stsinglepicAttr.osd_type = IMP_ISP_PIC_ARGB_8888;
    a->stsinglepicAttr.osd_argb_type = IMP_ISP_ARGB_TYPE_BGRA;
    a->stsinglepicAttr.pic.osd_enable = 1;
    a->stsinglepicAttr.pic.osd_left = (uint16_t)x;
    a->stsinglepicAttr.pic.osd_top = (uint16_t)y;
    a->stsinglepicAttr.pic.osd_width = 64;
    a->stsinglepicAttr.pic.osd_height = 16;
    a->stsinglepicAttr.pic.osd_image = (char *)pic;
    a->stsinglepicAttr.pic.osd_stride = 64 * 4;
}

int main(void)
{
    IMPIspOsdAttrAsm a, g;
    IMPISPSingleOSDAttr single;
    int h[9], i, h0, h1, h2;

    rmem = mmap(NULL, 8 * 4096, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    assert(rmem != MAP_FAILED);
    for (i = 0; i < (int)sizeof(pic); i++)
        pic[i] = (uint8_t)(i * 7);

    /* pool: the vendor default is 128 bytes, a 4 KiB picture does not fit */
    assert(IMP_ISP_Tuning_SetOsdPoolSize(0) == -1);
    h0 = IMP_ISP_Tuning_CreateOsdRgn(0, NULL);
    assert(h0 == 0);
    fill(&a, 0, h0, 16, 16);
    assert(IMP_ISP_Tuning_SetOsdRgnAttr(0, h0, &a) == -1 && nalloc == 0);
    assert(IMP_ISP_Tuning_SetOsdPoolSize(64 * 1024) == 0);

    /* create: attr must be NULL, chn 0..5, 8 handles, freed handles queue last */
    assert(IMP_ISP_Tuning_CreateOsdRgn(0, &a) == -1);
    assert(IMP_ISP_Tuning_CreateOsdRgn(6, NULL) == -1);
    assert(IMP_ISP_Tuning_CreateOsdRgn(-1, NULL) == -1);
    for (i = 1; i < 8; i++)
        assert((h[i] = IMP_ISP_Tuning_CreateOsdRgn(0, NULL)) == i);
    assert(IMP_ISP_Tuning_CreateOsdRgn(0, NULL) == -1);
    assert(IMP_ISP_Tuning_DestroyOsdRgn(0, 3) == 0);
    assert(IMP_ISP_Tuning_DestroyOsdRgn(0, 5) == 0);
    assert(IMP_ISP_Tuning_CreateOsdRgn(0, NULL) == 3);
    assert(IMP_ISP_Tuning_CreateOsdRgn(0, NULL) == 5);
    for (i = 0; i < 8; i++)
        assert(IMP_ISP_Tuning_DestroyOsdRgn(0, i) == 0);
    assert(nsent == 0);

    /* set attr: frame bounds, created, type, copy; nothing goes to the driver */
    h0 = IMP_ISP_Tuning_CreateOsdRgn(1, NULL);          /* free order 1.. */
    assert(h0 >= 0);
    fill(&a, 1, h0, 600, 0);                             /* 600 + 64 > 640 */
    assert(IMP_ISP_Tuning_SetOsdRgnAttr(1, h0, &a) == -1);
    fill(&a, 1, h0, 8, 8);
    assert(IMP_ISP_Tuning_SetOsdRgnAttr(1, (h0 + 1) % 8, &a) == -1);   /* not created */
    assert(IMP_ISP_Tuning_SetOsdRgnAttr(4, h0, &a) == -1);             /* no frame source */
    a.type = ISP_OSD_REG_INV;
    assert(IMP_ISP_Tuning_SetOsdRgnAttr(1, h0, &a) == -1);
    fill(&a, 1, h0, 8, 8);
    assert(IMP_ISP_Tuning_SetOsdRgnAttr(1, h0, &a) == 0);
    assert(nsent == 0 && nalloc == 1 && nflush == 1);
    assert(memcmp(rmem[0], pic, sizeof(pic)) == 0);
    assert(IMP_ISP_Tuning_GetOsdRgnAttr(1, h0, &g) == 0);
    assert(g.type == ISP_OSD_REG_PIC && g.stsinglepicAttr.chx == 1);
    assert(g.stsinglepicAttr.pic.osd_enable == 0);       /* cleared until Show */
    assert(g.stsinglepicAttr.pic.osd_image == (char *)rmem[0]);
    assert(g.stsinglepicAttr.pic.osd_left == 8 && g.stsinglepicAttr.pic.osd_stride == 256);
    assert(IMP_ISP_Tuning_GetOsdRgnAttr(2, h0, &g) == -1);   /* vendor: chn 0/1 only */
    /* same size again reuses the copy, another size is refused */
    a.stsinglepicAttr.pic.osd_image = NULL;
    assert(IMP_ISP_Tuning_SetOsdRgnAttr(1, h0, &a) == 0 && nalloc == 1);
    assert(rmem[0][5] == 0);
    a.stsinglepicAttr.pic.osd_image = (char *)pic;
    assert(IMP_ISP_Tuning_SetOsdRgnAttr(1, h0, &a) == 0);
    a.stsinglepicAttr.pic.osd_height = 8;
    assert(IMP_ISP_Tuning_SetOsdRgnAttr(1, h0, &a) == -1);

    /* show: control 0x080000a2, sensor chn/3, the physical address */
    assert(IMP_ISP_Tuning_ShowOsdRgn(1, 7, 1) == -1);    /* free region */
    assert(IMP_ISP_Tuning_ShowOsdRgn(1, h0, 1) == 0);
    assert(nsent == 1);
    assert(sent[0].vinum == 0 && sent[0].direction == 0 && sent[0].control == 0x080000a2);
    assert(sent[0].attr.chx == 1 && sent[0].attr.pic_num == h0);
    assert(sent[0].attr.pic.osd_enable == 1 && sent[0].attr.pic.osd_left == 8);
    assert((uintptr_t)sent[0].attr.pic.osd_image == 0x04000000U);
    assert(sent[0].attr.osd_argb_type == IMP_ISP_ARGB_TYPE_BGRA);
    assert(IMP_ISP_Tuning_GetOsdRgnAttr(1, h0, &g) == 0);
    assert(g.stsinglepicAttr.pic.osd_image == (char *)rmem[0]);

    /* a second region overlapping the first is switched off; a region whose
     * pic_num is not its handle and a region without picture are not sent */
    h1 = IMP_ISP_Tuning_CreateOsdRgn(1, NULL);
    h2 = IMP_ISP_Tuning_CreateOsdRgn(1, NULL);
    assert(h1 >= 0 && h2 >= 0 && h1 != h0 && h2 != h1);
    fill(&a, 1, h1, 40, 16);                             /* crosses 8..72 x 8..24 */
    assert(IMP_ISP_Tuning_SetOsdRgnAttr(1, h1, &a) == 0);
    nsent = 0;
    assert(IMP_ISP_Tuning_ShowOsdRgn(1, h1, 1) == 0);
    assert(nsent == 2);
    assert(sent[0].attr.pic_num == h0 && sent[0].attr.pic.osd_enable == 1);
    assert(sent[1].attr.pic_num == h1 && sent[1].attr.pic.osd_enable == 0);
    fill(&a, 1, h1, 200, 100);
    assert(IMP_ISP_Tuning_SetOsdRgnAttr(1, h1, &a) == 0);
    nsent = 0;
    assert(IMP_ISP_Tuning_ShowOsdRgn(1, h1, 1) == 0);
    assert(nsent == 2 && sent[1].attr.pic.osd_enable == 1);
    assert((uintptr_t)sent[1].attr.pic.osd_image == 0x04001000U);
    fill(&a, 1, (h2 + 1) % 8, 300, 100);                 /* wrong pic_num */
    assert(IMP_ISP_Tuning_SetOsdRgnAttr(1, h2, &a) == 0);
    nsent = 0;
    assert(IMP_ISP_Tuning_ShowOsdRgn(1, h2, 1) == 0);
    assert(nsent == 2);

    /* sensor 1 = channels 3..5 */
    i = IMP_ISP_Tuning_CreateOsdRgn(3, NULL);
    fill(&a, 0, i, 0, 0);
    assert(IMP_ISP_Tuning_SetOsdRgnAttr(3, i, &a) == 0);
    nsent = 0;
    assert(IMP_ISP_Tuning_ShowOsdRgn(3, i, 1) == 0);
    assert(nsent == 1 && sent[0].vinum == 1);
    assert(IMP_ISP_Tuning_DestroyOsdRgn(3, i) == 0);

    /* destroy a shown region: switched off at the driver, picture freed */
    nsent = 0;
    nfree = 0;
    assert(IMP_ISP_Tuning_DestroyOsdRgn(1, h0) == 0);
    assert(nfree == 1 && nsent == 2);
    assert(sent[0].attr.pic_num == h0 && sent[0].attr.pic.osd_enable == 0);
    assert(IMP_ISP_Tuning_GetOsdRgnAttr(1, h0, &g) == 0 && g.type == 0);
    assert(IMP_ISP_Tuning_DestroyOsdRgn(1, h0) == 0);    /* vendor: 0 again */
    assert(IMP_ISP_Tuning_DestroyOsdRgn(6, 0) == -1);
    assert(IMP_ISP_Tuning_DestroyOsdRgn(0, 8) == -1);
    IMP_OSD_Exit_ISP();
    assert(nfree == 3);

    /* single OSD attr pass-through */
    memset(&single, 0, sizeof(single));
    assert(IMP_ISP_SetSingleOSDAttr((IMPVI_NUM)4, &single) == -4);
    assert(IMP_ISP_SetSingleOSDAttr(IMPVI_MAIN, NULL) == -9);
    nsent = 0;
    assert(IMP_ISP_GetSingleOSDAttr(IMPVI_MAIN, &single) == 0);
    assert(nsent == 1 && sent[0].direction == 1 && sent[0].control == 0x080000a2);

    /* GetRaw: sensor id written, w*h (8 bit) / w*h*2 (16 bit) rmem buffer,
     * kept per sensor and grown when needed, ioctl 0xc004542f, invalidate */
    {
        IMPISPGETRawiConfig r;
        int a0 = nalloc, f0 = nfree;

        memset(&r, 0, sizeof(r));
        r.raw_width = 32; r.raw_height = 16; r.raw_bit = 8;
        assert(IMP_ISP_GetRaw(IMPVI_MAIN, &r) == 0);
        assert(r.sensor_id == 0 && nalloc == a0 + 1 && r.buf.buf_size == 512);
        assert(r.buf.paddr >= 0x04000000U && r.buf.vaddr);
        assert(rawio.calls == 1 && rawio.command == 0xc004542fU && rawio.attr.buf.paddr == r.buf.paddr);
        assert(ninval == 1);
        assert(IMP_ISP_GetRaw(IMPVI_MAIN, &r) == 0 && nalloc == a0 + 1);   /* reused */
        r.raw_bit = 16;
        assert(IMP_ISP_GetRaw(IMPVI_MAIN, &r) == 0);                        /* grown */
        assert(nalloc == a0 + 2 && nfree == f0 + 1 && r.buf.buf_size == 1024);
        r.raw_bit = 12;
        assert(IMP_ISP_GetRaw(IMPVI_MAIN, &r) == -1);
        assert(IMP_ISP_GetRaw((IMPVI_NUM)4, &r) == -4 && r.sensor_id == 4);
        assert(IMP_ISP_GetRaw(IMPVI_MAIN, NULL) == -1);
        r.raw_bit = 8;
        rawio.result = ENODEV;
        assert(IMP_ISP_GetRaw(IMPVI_MAIN, &r) == -8);
        rawio.result = EIO;
        assert(IMP_ISP_GetRaw(IMPVI_MAIN, &r) == -1);
        rawio.result = 0;
    }

    printf("t40 isp osd: ok\n");
    return 0;
}
