#ifndef OPENIMP_HELIX_JPEG_H
#define OPENIMP_HELIX_JPEG_H

/*
 * Hardware baseline JPEG on the Ingenic Helix VPU (JPGC block fed by the
 * EFE), driven through /dev/soc_vpu like the native H.264 path.  One
 * implementation for T20 (JZ NVPU), T21, T30 and T23; see
 * docs/HELIX_JPEG.md for the interface.
 */

#include <stddef.h>
#include <stdint.h>

#include "hw_encoder.h"

/* Command-list generations.  T20, T21 and T30 share one list; T23 places
 * the VPU at 0x13100000 (so its SRAM at 0x131f0000), adds the JPGC
 * bitstream limit and two EFE_CTRL flags. */
typedef enum {
    HELIX_JPEG_T21 = 0,
    HELIX_JPEG_T23 = 1,
} HelixJpegVariant;

#if defined(PLATFORM_T23)
#define HELIX_JPEG_VARIANT HELIX_JPEG_T23
#else
#define HELIX_JPEG_VARIANT HELIX_JPEG_T21
#endif

/* EFE_CTRL plane formats (raw_format) */
#define HELIX_JPEG_PLANE_NV12 0x08u
#define HELIX_JPEG_PLANE_NV21 0x0cu

/* Width limits of the stock encoder (ijpege_init / jpege validate): the
 * width is a whole number of macroblocks and at least 16 of them. */
#define HELIX_JPEG_MIN_WIDTH  256u
#define HELIX_JPEG_MAX_DIM    65535u

/* The T23 list is 660 register writes (5.2 KiB), the T21 one 659. */
#define HELIX_JPEG_DESCRIPTOR_PAIRS 664u

/* Everything the command list encodes; addresses are bus addresses. */
typedef struct {
    HelixJpegVariant variant;
    uint32_t raw_y;            /* NV12/NV21 luma plane */
    uint32_t raw_c;            /* interleaved chroma plane */
    uint32_t stride;           /* luma and chroma line pitch, bytes */
    uint32_t width;            /* visible width (T23 EFE flag) */
    uint32_t mb_width;         /* 16x16 MCUs per row */
    uint32_t mb_height;        /* MCU rows */
    uint8_t raw_format;        /* HELIX_JPEG_PLANE_* */
    uint32_t bitstream;        /* entropy-coded output */
    uint32_t bitstream_limit;  /* JPGC_MAX_BS, 0 = no limit (the register
                                * is written on T23 always, on the other
                                * cores only for a limit) */
    const uint8_t *qt;         /* 128 quantizers: luma then chroma, DQT
                                * (zigzag) order */
} HelixJpegSlice;

/* JPGC_QMEM word for a quantizer step (1..255). */
uint32_t HelixJpeg_QmemEntry(uint8_t quantizer);
/* The 384-word JPGC_HUFE table for the standard (Annex K) codes. */
void HelixJpeg_HuffmanTable(uint32_t table[384]);
/* IJG-scaled Annex K tables for quality 1..100 (out of range: 75), luma
 * then chroma, DQT order, steps clamped to 1..255. */
void HelixJpeg_QualityTables(uint32_t quality, uint8_t qt[128]);
/* Writes the VDMA command list (value, address|flags pairs) into words.
 * Returns the number of pairs or -1. */
int HelixJpeg_BuildDescriptor(const HelixJpegSlice *slice, uint32_t *words,
                              size_t capacity_words);
/* SOI, JFIF APP0, both DQT, SOF0 (4:2:0), the four Annex K DHT and SOS.
 * Returns the length (HELIX_JPEG_HEADER_SIZE) or 0 when out is too small. */
#define HELIX_JPEG_HEADER_SIZE 623u
size_t HelixJpeg_WriteHeader(uint8_t *out, size_t capacity, uint32_t width,
                             uint32_t height, const uint8_t qt[128]);

/* Runtime encoder (one VPU channel for the process, serialised). */
typedef struct {
    uint32_t virt_addr;        /* CPU mapping of the NV12 frame */
    uint32_t phys_addr;        /* bus address, 0 = not DMA-able memory */
    uint32_t size;             /* bytes at virt_addr; a framesource-layout
                                * frame with a bus address may end before
                                * its padded chroma rows (sizeimage) */
    uint32_t width;
    uint32_t height;
    uint32_t chroma_offset;    /* chroma plane start relative to luma; 0 =
                                * framesource layout (width * aligned
                                * height) */
    uint32_t pixfmt;
    uint64_t timestamp;
} HelixJpegFrame;

/* Off when OPENIMP_HELIX_HW_JPEG=0 (only honoured with the software
 * encoder built in), after an initialisation failure, or after repeated
 * hardware errors.  Run-time options: OPENIMP_HELIX_JPEG_STATS=1 logs every
 * job (path, status, sizes, time, rmem, failure reason);
 * OPENIMP_HELIX_JPEG_RMEM_RESERVE_KB (default 1024) is the free rmem the
 * encoder never allocates into; OPENIMP_HELIX_JPEG_MAX_BS=1 also programs
 * the bitstream limit on T20/T21/T30. */
int OpenIMP_HelixJpeg_Available(void);
/* Encodes one picture with the 128 quantizers qt.  On success stream holds
 * a heap JPEG (phys_addr 0, freed by the consumer).  Returns -1 on any
 * failure; nothing is allocated then. */
int OpenIMP_HelixJpeg_Encode(const HelixJpegFrame *frame,
                             const uint8_t qt[128], HWStreamBuffer *stream);
/* Releases the VPU channel and DMA buffers (idempotent). */
void OpenIMP_HelixJpeg_Shutdown(void);

#endif
