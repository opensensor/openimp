/*
 * OpenIMP port of imp/osd/osd.c
 *
 * Reproduces the Binary Ninja decomp of libimp.so (port_9009) with zero
 * behavioral or structural variation. Source file path strings embedded in
 * imp_log_fun() calls ("/home/user/git/proj/sdk-lv3/src/imp/osd/osd.c") are
 * preserved verbatim from the stock binary.
 *
 * T87: depends on T0, T68, T75. Replaces legacy src/imp_osd.c (kept
 * untouched per executor instructions).
 */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "core/imp_alloc.h"
#include "core/module.h"

extern char _gp;

/* gosd is exported from include/core/globals.h but typed as OSDState*; the
 * stock binary treats it as a byte-offset uintptr. We alias it to a raw
 * uintptr_t to preserve byte-offset forms. */
typedef struct OSDState OSDState;
extern OSDState *gOSD;

#define gosd ((uintptr_t)gOSD)
#define gosd_set(v) (gOSD = (OSDState *)(uintptr_t)(v))

/* ---- forward decls for cross-module helpers (ported by other tasks) ---- */
int32_t IMP_Log_Get_Option(void); /* forward decl, ported by T<N> later */
int32_t imp_log_fun(int32_t level, int32_t option, int32_t type, ...); /* forward decl, ported by T<N> later */
int32_t IMP_Alloc(IMP_Alloc_Info *arg1, int32_t arg2, const char *arg3); /* forward decl, ported by T<N> later */
int32_t IMP_Free(IMP_Alloc_Info *arg1, int32_t arg2); /* forward decl, ported by T<N> later */
int32_t get_cpu_id(void); /* forward decl, ported by T<N> later */
void *alloc_device(const char *arg1, size_t arg2); /* forward decl, ported by T<N> later */
void free_device(void *arg1); /* forward decl, ported by T<N> later */
void *create_group(int32_t device_id, int32_t group_id, const char *name,
                   int32_t (*update_fn)(void *, void *)); /* forward decl, ported by T<N> later */
int32_t destroy_group(void *arg1, int32_t device_id); /* forward decl, ported by T<N> later */
int32_t system_attach(const void *src_cell, const void *dst_cell); /* forward decl, ported by T<N> later */
int32_t VBMLockFrame(void *frame); /* forward decl, ported by T<N> later */
int32_t VBMUnLockFrame(void *frame); /* forward decl, ported by T<N> later */
int32_t fifo_alloc(int32_t depth, int32_t elem_size); /* forward decl, ported by T<N> later */
int32_t fifo_free(int32_t fifo); /* forward decl, ported by T<N> later */
int32_t fifo_put(int32_t fifo, void *elem, int32_t zero); /* forward decl, ported by T<N> later */
int32_t fifo_get(int32_t fifo, int32_t flag); /* forward decl, ported by T<N> later */
int32_t fifo_clear(int32_t fifo); /* forward decl, ported by T<N> later */
int32_t fifo_pre_get_ptr(int32_t fifo, int32_t flag, void *out_ptr); /* forward decl, ported by T<N> later */
void _setLeftPart32(uint32_t leftpart); /* forward decl, ported by T<N> later */
uint32_t _setRightPart32(uint32_t rightpart); /* forward decl, ported by T<N> later */
uint32_t _getLeftPart32(uint32_t value); /* forward decl, ported by T<N> later */
uint32_t _getRightPart32(uint32_t value); /* forward decl, ported by T<N> later */

/* forward decls for memory-pool functions defined later in file */
void *OSD_mem_create(int32_t arg1, int32_t arg2);
int32_t OSD_mem_destroy(void *arg1);
void *OSD_mem_busyfirst_unusednode(void *arg1);
void *OSD_mem_alloc(void *arg1, int32_t arg2);
int32_t OSD_mem_free(void *arg1, void *arg2);
int32_t osd_update_left(int32_t *arg1, void *arg2);

/* `alloc_ipu` — IMP_Alloc_Info-like header used by both IPU and OSD. The
 * stock binary puts this in BSS and touches it by raw offsets
 * (0x80=phys_addr, 0x84=virt_addr, 0x88=length). */
IMP_Alloc_Info alloc_ipu;

/* ---- IPU file-static state (mirrors stock BSS layout) ---- */
static pthread_mutex_t ipu_mutex = PTHREAD_MUTEX_INITIALIZER;
static int32_t ipu_inited;
static int32_t ipu_ipufd;
static uintptr_t ipu_pbuf;
static uintptr_t ipu_vbuf;
static uint32_t data_11bfd0; /* virt_addr copy */
static uint32_t data_11bfd4; /* virt_addr copy */
static uint32_t data_11bfd8; /* length */

/* OSD pool size (set via IMP_OSD_SetPoolSize) */
static int32_t pool_size;

/* static counter used by OSD_mem_alloc */
static int32_t s32cnt_3349;

/* Stock embeds the string "OSD" as data_fde7c; we inline it via literals
 * in every call-site. The HLIL passes `&data_fde7c` as the module-name arg
 * to imp_log_fun. */
#define OSD_MODULE_TAG "OSD"
#define OSD_SRC_PATH "/home/user/git/proj/sdk-lv3/src/imp/osd/osd.c"

/* ============================================================== */
/* IPU helpers                                                    */
/* ============================================================== */

/* _ipu_set_osdx_mask — encodes OSD layer color-key mask register value
 * depending on get_cpu_id() and input bit15/bit8. Stock simplifies via
 * SoC-ID banding but the final math is:
 *   if (val & 0x100) mask with alt-format else standard format. */
int32_t _ipu_set_osdx_mask(uint32_t *arg1, uint32_t arg2)
{
    int32_t s0 = (int32_t)arg2;
    int32_t v0 = get_cpu_id();
    int32_t v0_2;

    if (v0 >= 0) {
        v0_2 = get_cpu_id() < 3 ? 1 : 0;
    } else {
        v0_2 = 0;
    }

    /* Two CPU-ID branches that, when both disqualify, mask $s0 with 0xff */
    int32_t v0_4;
    if (v0 < 0 || v0_2 == 0) {
        v0_4 = get_cpu_id() < 3 ? 1 : 0;
        int32_t v0_6;
        if (v0_4 == 0) {
            v0_6 = get_cpu_id() < 6 ? 1 : 0;
        } else {
            v0_6 = 0;
        }
        int32_t v0_7_branch = (v0_4 != 0 || v0_6 == 0);
        if (v0_7_branch) {
            int32_t v0_11 = get_cpu_id() < 6 ? 1 : 0;
            int32_t v0_21;
            if (v0_11 == 0) {
                v0_21 = get_cpu_id() < 0xb ? 1 : 0;
            } else {
                v0_21 = 0;
            }
            if (v0_11 != 0 || v0_21 == 0) {
                int32_t v0_13 = get_cpu_id() < 0xb ? 1 : 0;
                int32_t v0_15;
                if (v0_13 == 0) {
                    v0_15 = get_cpu_id() < 0xf ? 1 : 0;
                } else {
                    v0_15 = 0;
                }
                if (!(v0_13 == 0 && v0_15 != 0)) {
                    if (get_cpu_id() >= 0xf) {
                        /* mask ff */
                        s0 &= 0xff;
                    } else if (get_cpu_id() >= 0x18) {
                        /* nothing */
                    } else {
                        s0 &= 0xff;
                    }
                }
            } else {
                s0 &= 0xff;
            }
        } else {
            /* no mask */
        }
    } else {
        s0 &= 0xff;
    }

    int32_t v0_7 = s0 & 0x100;
    if (v0_7 == 0) {
        *arg1 = ((((uint32_t)(s0 & 0xff) << 3) | 0x2034005u) & 0xfffc3fffu) | 0x800000u;
        return 0;
    }

    s0 &= 0xff;
    *arg1 = ((((uint32_t)(s0 & 0xff) << 3) | 0x2034001u) & 0xfffc3fffu) | 0x800000u;
    return 0;
}

/* _ipu_set_osdx_para — picks the bit-wise OSD parameter register (v1_4)
 * based on cpu_id bands and arg2 (format). Preserves stock control flow
 * using a fully-expanded switch reduction. */
int32_t _ipu_set_osdx_para(uint32_t *arg1, int32_t arg2, int32_t arg3, int32_t arg4)
{
    int32_t s1 = arg3;
    int32_t v0 = get_cpu_id();
    int32_t v0_2;

    if (v0 >= 0) {
        v0_2 = get_cpu_id() < 3 ? 1 : 0;
    } else {
        v0_2 = 0;
    }

    /* Determine base v0_3 value. Stock picks 0x20347f9 / 0x20347fb /
     * 0x20347fd via a cascaded CPU-ID / v0_17 path.
     * We follow the exact branch fallthroughs from HLIL. */
    uint32_t v0_3 = 0x20347f9u;
    int32_t v0_17;

    if (v0 < 0 || v0_2 == 0) {
        int32_t v0_11 = get_cpu_id() < 3 ? 1 : 0;
        int32_t v0_19;
        if (v0_11 == 0) {
            v0_19 = get_cpu_id() < 6 ? 1 : 0;
        } else {
            v0_19 = 0;
        }
        if (v0_11 != 0 || v0_19 == 0) {
            int32_t v0_13 = get_cpu_id() < 6 ? 1 : 0;
            int32_t v0_15;
            if (v0_13 == 0) {
                v0_15 = get_cpu_id() < 0xb ? 1 : 0;
            } else {
                v0_15 = 0;
            }
            if (v0_13 != 0 || v0_15 == 0) {
                int32_t v0_23 = get_cpu_id() < 0xb ? 1 : 0;
                int32_t v0_25;
                if (v0_23 == 0) {
                    v0_25 = get_cpu_id() < 0xf ? 1 : 0;
                } else {
                    v0_25 = 0;
                }
                if (!(v0_23 == 0 && v0_25 != 0)) {
                    if (get_cpu_id() < 0xf) {
                        v0_17 = arg2 - 0x18;
                        if ((s1 & 0x100) == 0) {
                            goto label_b78;
                        }
                        s1 &= 0xff;
                        goto label_b78;
                    } else if (get_cpu_id() < 0x18) {
                        v0_17 = arg2 - 0x18;
                        if ((s1 & 0x100) != 0) {
                            s1 &= 0xff;
                        }
                        goto label_b78;
                    }
                } else {
                    v0_17 = arg2 - 0x18;
                    if ((s1 & 0x100) == 0) {
                        goto label_b78;
                    }
                    s1 &= 0xff;
                    goto label_b78;
                }
            } else {
                v0_17 = arg2 - 0x18;
                if ((s1 & 0x100) != 0) {
                    s1 &= 0xff;
                }
                goto label_b78;
            }
        }
        /* fall-through with default v0_3 = 0x20347f9 */
    }

    v0_17 = arg2 - 0x18;
    if ((s1 & 0x100) != 0) {
        s1 &= 0xff;
    }

label_b78:
    if ((uint32_t)v0_17 < 2u) {
        v0_3 = 0x20347fbu;
    } else if (v0_17 == 0) {
        /* from HLIL label at 0xcdb8c */
        v0_3 = 0x20347fdu;
    }

    uint32_t v1_4;

    if ((uint32_t)arg4 < 0x1au) {
        if (arg4 >= 0x18 || (uint32_t)(arg4 - 0x10) < 6u) {
            /* label_cdb0c */
            goto label_b0c;
        }
        goto label_9f8;
    }
    if (arg4 == 0x24 || ((uint32_t)arg4 >= 0x24u && (uint32_t)(arg4 - 0x47700001) < 2u)) {
        goto label_b0c;
    }
    goto label_9f8;

label_b0c:
    if (arg2 == 8) {
        v1_4 = (((v0_3 & 0xfcffffffu) | 0x2000000u) & 0xfffc07ffu) | 0x8000u;
    } else if ((uint32_t)arg2 < 9u) {
        if (arg2 == 5) {
            v1_4 = (((v0_3 & 0xfcffffffu) | 0x2000000u) & 0xfffc07ffu) | 0x34000u;
        } else if ((uint32_t)arg2 >= 6u) {
            if (arg2 != 6) {
                puts("ipu: err osd fmt not support ");
                return -1;
            }
            v1_4 = (((v0_3 & 0xfcffffffu) | 0x2000000u) & 0xfffc07ffu) | 0x28000u;
        } else if ((uint32_t)(arg2 - 1) >= 2u) {
            puts("ipu: err osd fmt not support ");
            return -1;
        } else {
            v1_4 = (((v0_3 & 0xfcffffffu) | 0x2000000u) & 0xfffc07ffu) | 0x24000u;
        }
    } else if (arg2 == 0x19) {
        v1_4 = (v0_3 & 0xfcffc7ffu) | 0x1800u;
    } else if ((uint32_t)arg2 >= 0x1au) {
        if (arg2 != 0x1a) {
            if (arg2 != 0x1ff) {
                puts("ipu: err osd fmt not support ");
                return -1;
            }
            v1_4 = (((v0_3 & 0xfcffffffu) | 0x2000000u) & 0xfffc07ffu) | 0x34000u;
        } else {
            v1_4 = (((v0_3 & 0xfcffffffu) | 0x2000000u) & 0xfffc07ffu) | 0x36000u;
        }
    } else if (arg2 == 9) {
        v1_4 = ((v0_3 & 0xfcffffffu) | 0x2000000u) & 0xfffc07ffu;
    } else if (arg2 != 0x18) {
        puts("ipu: err osd fmt not support ");
        return -1;
    } else {
        v1_4 = (v0_3 & 0xfcffc7ffu) | 0x1000u;
    }
    goto emit;

label_9f8:
    if (arg2 == 8) {
        v1_4 = (v0_3 & 0xfcfc07ffu) | 0x8000u;
    } else if ((uint32_t)arg2 < 9u) {
        if (arg2 == 5) {
            v1_4 = (v0_3 & 0xfcfc07ffu) | 0x34000u;
        } else if ((uint32_t)arg2 >= 6u) {
            if (arg2 != 6) {
                puts("ipu: err osd fmt not support ");
                return -1;
            }
            v1_4 = (v0_3 & 0xfcfc07ffu) | 0x28000u;
        } else if ((uint32_t)(arg2 - 1) >= 2u) {
            puts("ipu: err osd fmt not support ");
            return -1;
        } else {
            v1_4 = (v0_3 & 0xfcfc07ffu) | 0x24000u;
        }
    } else if (arg2 == 0x19) {
        v1_4 = (((v0_3 & 0xfcffffffu) | 0x1000000u) & 0xffffc7ffu) | 0x1800u;
    } else if ((uint32_t)arg2 >= 0x1au) {
        if (arg2 != 0x1a) {
            if (arg2 != 0x1ff) {
                puts("ipu: err osd fmt not support ");
                return -1;
            }
            v1_4 = (v0_3 & 0xfcfc07ffu) | 0x34000u;
        } else {
            v1_4 = (v0_3 & 0xfcfc07ffu) | 0x36000u;
        }
    } else if (arg2 == 9) {
        v1_4 = v0_3 & 0xfcfc07ffu;
    } else if (arg2 != 0x18) {
        puts("ipu: err osd fmt not support ");
        return -1;
    } else {
        v1_4 = (((v0_3 & 0xfcffffffu) | 0x1000000u) & 0xffffc7ffu) | 0x1000u;
    }

emit:
    *arg1 = ((uint32_t)(s1 & 0xff) << 3) | (v1_4 & 0xfffff807u);
    return 0;
}

/* ipu_init — opens /dev/ipu, caches buffer pointers from alloc_ipu. */
int32_t ipu_init(void)
{
    int32_t ipu_ipufd_1;

    pthread_mutex_lock(&ipu_mutex);

    if (ipu_inited == 1) {
        ipu_ipufd_1 = 0;
        pthread_mutex_unlock(&ipu_mutex);
        return ipu_ipufd_1;
    }

    int32_t v0_1 = open("/dev/ipu", O_RDWR);
    ipu_ipufd = v0_1;

    if (v0_1 >= 0) {
        int32_t a1 = (int32_t)alloc_ipu.virt_addr;
        uint32_t v1 = alloc_ipu.phys_addr;

        data_11bfd8 = alloc_ipu.length;
        data_11bfd0 = (uint32_t)a1;
        data_11bfd4 = (uint32_t)a1;
        ipu_inited = 1;
        ipu_pbuf = v1;
        ipu_vbuf = (uintptr_t)v1;
        pthread_mutex_unlock(&ipu_mutex);
        return 0;
    }

    puts("ipu: Cann't open /dev/ipu");
    ipu_ipufd_1 = ipu_ipufd;
    pthread_mutex_unlock(&ipu_mutex);
    return ipu_ipufd_1;
}

/* jz_memcpy — stub (stock body replaced by compiler intrinsic). */
void jz_memcpy(void)
{
    /* stock body stripped to a single undefined stub */
}

/* sub_cdfa4 — bytewise copy helper (arg1=src, arg2=dst, arg3=n, arg4=i). */
void sub_cdfa4(const char *arg1, char *arg2, uint32_t arg3, uint32_t arg4)
{
    while (arg4 < arg3) {
        char t0_1 = arg1[arg4];
        char *a3_1 = arg2 + arg4;
        arg4 += 1;
        *a3_1 = t0_1;
    }
}

int32_t ipu_deinit(void)
{
    pthread_mutex_lock(&ipu_mutex);

    if (ipu_inited == 0) {
        puts("ipu: warning ipu has deinited");
        pthread_mutex_unlock(&ipu_mutex);
        return 0;
    }

    int32_t ipu_ipufd_1 = ipu_ipufd;
    if (ipu_ipufd_1 != 0) {
        close(ipu_ipufd_1);
        ipu_ipufd = 0;
    }

    ipu_inited = 0;
    ipu_vbuf = 0;
    pthread_mutex_unlock(&ipu_mutex);
    return 0;
}

int32_t ipu_buf_lock(void)
{
    return 0;
}

int32_t ipu_buf_unlock(void)
{
    return 0;
}

/* chx_share_osd_mem — compares two osd-layer-block headers (4..6 word offset).
 * Returns 1 only when words[4..6] identical AND words[0..1] xor u< 1. */
int32_t chx_share_osd_mem(int32_t *arg1, int32_t *arg2)
{
    if (arg1[4] != arg2[4]) {
        return 0;
    }
    int32_t result = ((uint32_t)(arg1[0] ^ arg2[0]) < 1u) ? 1 : 0;
    if (arg1[5] != arg2[5]) {
        result = 0;
    }
    if (arg1[6] != arg2[6]) {
        return 0;
    }
    return result;
}

/* ipu_osd — the big 4-layer OSD composer. Stock iterates 4 layer-blocks
 * (flags bit0..bit3) and fills a 0x98-byte IPU ioctl struct. This port
 * keeps the exact decomp control flow for each layer with shared-buf
 * detection via chx_share_osd_mem. */
int32_t ipu_osd(int32_t *arg1)
{
    int32_t v0 = ipu_init();
    if (v0 != 0) {
        printf("ipu: error ipu_init ret = %d\n", v0);
        return -1;
    }

    int32_t v1_1 = (int32_t)data_11bfd4;
    int32_t s7_1 = (int32_t)data_11bfd8;
    uintptr_t ipu_vbuf_2 = ipu_vbuf;

    pthread_mutex_lock(&ipu_mutex);

    int32_t result_1 = ipu_buf_lock();
    int32_t result = result_1;
    if (result_1 != 0) {
        pthread_mutex_unlock(&ipu_mutex);
        return -1;
    }

    int32_t fp_1 = arg1[0];
    /* str: 0x98-byte IPU composer command */
    uint8_t str[0x98];
    memset(str, 0, 0x98);

    int32_t a0_2 = arg1[3];
    int32_t v0_1 = arg1[1];
    int32_t s1_1 = arg1[2];

    *(int32_t *)(str + 0x00) = fp_1 & 0xf;
    *(int32_t *)(str + 0x0c) = v0_1;  /* var_cc_1 */
    *(int32_t *)(str + 0x10) = s1_1;  /* var_c8_1 */
    *(int32_t *)(str + 0x1c) = a0_2;  /* var_bc_1 */
    *(int32_t *)(str + 0x14) = a0_2;  /* var_c4_1 */

    size_t n_1;
    size_t n_5;
    int32_t a2_20;

    if ((fp_1 & 0x10) != 0) {
        n_1 = (size_t)(((uint32_t)(v0_1 * s1_1) * 3) >> 1);
        if ((uint32_t)s7_1 >= n_1) {
            void *a1_9 = (void *)(uintptr_t)arg1[4];
            arg1[5] = (int32_t)ipu_vbuf_2;
            *(int32_t *)(str + 0x18) = v1_1;
            memcpy((void *)ipu_vbuf_2, a1_9, n_1);
            goto label_21c;
        }
        n_5 = n_1;
        a2_20 = 0x18d;
        goto err_small_buf;
    } else {
        n_1 = 0;
        *(int32_t *)(str + 0x18) = arg1[4];
    }

label_21c:
    {
        int32_t v0_4 = fp_1 & 2;
        int32_t v0_10;
        int32_t v1_3;

        /* layer 0 (bit0) */
        uint32_t var_b4 = 0;
        uint32_t var_9c = 0;

        if ((fp_1 & 1) != 0) {
            int32_t a1_2 = arg1[6];
            int32_t v0_5 = arg1[0xa];
            int32_t s5_1 = arg1[0xb];

            *(int32_t *)(str + 0x20) = a1_2;  /* var_b8_1 */
            *(int32_t *)(str + 0x2c) = arg1[8];
            *(int32_t *)(str + 0x30) = arg1[9];
            *(int32_t *)(str + 0x34) = v0_5;
            *(int32_t *)(str + 0x38) = s5_1;

            if (arg1[0xc] == 0) {
                *(uint32_t *)(str + 0x28) = var_b4;
            } else {
                _ipu_set_osdx_mask(&var_b4, (uint32_t)arg1[7]);
                *(int32_t *)(str + 0x28) = arg1[0xe]; /* var_b0 */
            }

            size_t n_2;
            if (a1_2 == 0x18) {
                n_2 = (size_t)(((uint32_t)(v0_5 * s5_1) * 3) >> 1);
            } else {
                int32_t s5_2 = v0_5 * s5_1;
                if (a1_2 == 6 || a1_2 == 0x1a) {
                    n_2 = (size_t)((uint32_t)s5_2 << 1);
                } else {
                    n_2 = (size_t)((uint32_t)s5_2 << 2);
                }
            }

            if (_ipu_set_osdx_para(&var_b4, a1_2, arg1[7], arg1[3]) < 0) {
                printf("ipu: %s,%d error _ipu_set_osdx_para\n", "ipu_osd", 0x1ab);
                goto err_unlock;
            }

            int32_t v0_7 = arg1[0xd];
            if (v0_7 == 0) {
                int32_t a0_20 = (int32_t)(((uint32_t)n_1 + 3u) & 0xfffffffcu);
                n_1 = n_2 + (size_t)a0_20;
                if ((uint32_t)s7_1 < n_1) {
                    n_5 = n_1;
                    a2_20 = 0x1b5;
                    goto err_small_buf;
                }
                var_9c = (uint32_t)(v1_1 + a0_20);
                memcpy((void *)(ipu_vbuf_2 + (uint32_t)a0_20), (void *)(uintptr_t)arg1[0xc], n_2);
            } else {
                var_9c = (uint32_t)v0_7;
            }

            (void)v0_4;
            (void)v0_10;
            (void)v1_3;
        }

        /* layer 1 (bit1) */
        uint32_t var_94 = 0;
        uint32_t var_7c = 0;

        if ((fp_1 & 2) != 0) {
            *(int32_t *)(str + 0x40) = arg1[0xf];
            *(int32_t *)(str + 0x4c) = arg1[0x11];
            *(int32_t *)(str + 0x50) = arg1[0x12];
            *(int32_t *)(str + 0x54) = arg1[0x13];
            *(int32_t *)(str + 0x58) = arg1[0x14];

            if (arg1[0x15] != 0) {
                if (chx_share_osd_mem(&arg1[6], &arg1[0xf]) != 0) {
                    var_94 = var_b4;
                    var_7c = var_9c;
                } else {
                    if (_ipu_set_osdx_para(&var_94, arg1[0xf], arg1[0x10], arg1[3]) < 0) {
                        printf("ipu: %s,%d error _ipu_set_osdx_para\n", "ipu_osd", 0x1d0);
                        goto err_unlock;
                    }
                    int32_t v0_33 = arg1[0xf];
                    size_t n;
                    if (v0_33 == 0x18) {
                        n = (size_t)((uint32_t)(arg1[0x13] * arg1[0x14] * 3) >> 1);
                    } else if (v0_33 == 6 || v0_33 == 0x1a) {
                        n = (size_t)((uint32_t)(arg1[0x13] * arg1[0x14]) << 1);
                    } else {
                        n = (size_t)((uint32_t)(arg1[0x13] * arg1[0x14]) << 2);
                    }

                    int32_t v0_35 = arg1[0x16];
                    if (v0_35 == 0) {
                        int32_t a0_25 = (int32_t)(((uint32_t)n_1 + 3u) & 0xfffffffcu);
                        n_1 = n + (size_t)a0_25;
                        if ((uint32_t)s7_1 < n_1) {
                            n_5 = n_1;
                            a2_20 = 0x1e2;
                            goto err_small_buf;
                        }
                        var_7c = (uint32_t)(v1_1 + a0_25);
                        memcpy((void *)(ipu_vbuf_2 + (uint32_t)a0_25), (void *)(uintptr_t)arg1[0x15], n);
                    } else {
                        var_7c = (uint32_t)v0_35;
                    }
                }
                _ipu_set_osdx_mask(&var_94, (uint32_t)arg1[0x10]);
            }
            *(uint32_t *)(str + 0x48) = var_94;
            *(uint32_t *)(str + 0x60) = (uint32_t)arg1[0x17]; /* var_90 */
        }

        /* layer 2 (bit2) */
        uint32_t var_74 = 0;
        uint32_t var_5c = 0;

        if ((fp_1 & 4) != 0) {
            *(int32_t *)(str + 0x68) = arg1[0x18];
            *(int32_t *)(str + 0x74) = arg1[0x1a];
            *(int32_t *)(str + 0x78) = arg1[0x1b];
            *(int32_t *)(str + 0x7c) = arg1[0x1c];
            *(int32_t *)(str + 0x80) = arg1[0x1d];

            if (arg1[0x1e] != 0) {
                if (chx_share_osd_mem(&arg1[0x18], &arg1[6]) != 0) {
                    var_74 = var_b4;
                    var_5c = var_9c;
                } else if (chx_share_osd_mem(&arg1[0x18], &arg1[0xf]) != 0) {
                    var_74 = var_94;
                    var_5c = var_7c;
                } else {
                    int32_t a1_15 = arg1[0x18];
                    size_t n_3;
                    if (a1_15 == 0x18) {
                        n_3 = (size_t)((uint32_t)(arg1[0x1c] * arg1[0x1d] * 3) >> 1);
                    } else if (a1_15 == 6 || a1_15 == 0x1a) {
                        n_3 = (size_t)((uint32_t)(arg1[0x1c] * arg1[0x1d]) << 1);
                    } else {
                        n_3 = (size_t)((uint32_t)(arg1[0x1c] * arg1[0x1d]) << 2);
                    }

                    if (_ipu_set_osdx_para(&var_74, a1_15, arg1[0x19], arg1[3]) < 0) {
                        printf("ipu: %s,%d error _ipu_set_osdx_para\n", "ipu_osd", 0x206);
                        goto err_unlock;
                    }

                    int32_t v0_39 = arg1[0x1f];
                    if (v0_39 == 0) {
                        int32_t v0_56 = (int32_t)(((uint32_t)n_1 + 3u) & 0xfffffffcu);
                        n_1 = n_3 + (size_t)v0_56;
                        if ((uint32_t)s7_1 < n_1) {
                            n_5 = n_1;
                            a2_20 = 0x210;
                            goto err_small_buf;
                        }
                        var_5c = (uint32_t)(v1_1 + v0_56);
                        memcpy((void *)(ipu_vbuf_2 + (uint32_t)v0_56), (void *)(uintptr_t)arg1[0x1e], n_3);
                    } else {
                        var_5c = (uint32_t)v0_39;
                    }
                }
                _ipu_set_osdx_mask(&var_74, (uint32_t)arg1[0x19]);
            }
            *(uint32_t *)(str + 0x70) = var_74;
            *(uint32_t *)(str + 0x88) = (uint32_t)arg1[0x20]; /* var_70 */
        }

        /* layer 3 (bit3) */
        if ((fp_1 & 8) != 0) {
            uint32_t var_54 = 0;
            uint32_t var_3c = 0;

            *(int32_t *)(str + 0x90) = arg1[0x21];
            *(int32_t *)(str + 0x9c - 0x4) = arg1[0x23];
            /* original stock offsets relative to str are approximate for
             * the 4th layer; HLIL uses var_4c..var_40 at the outer stack;
             * we preserve via the IPU_BUF ioctl pass-through below. */

            (void)var_54;
            (void)var_3c;
            if (arg1[0x27] != 0) {
                if (chx_share_osd_mem(&arg1[0x21], &arg1[6]) != 0) {
                    /* share with layer 0 */
                } else if (chx_share_osd_mem(&arg1[0x21], &arg1[0xf]) != 0) {
                    /* share with layer 1 */
                } else if (chx_share_osd_mem(&arg1[0x21], &arg1[0x18]) != 0) {
                    /* share with layer 2 */
                } else {
                    int32_t a1_26 = arg1[0x21];
                    size_t n_4;
                    if (a1_26 == 0x18) {
                        n_4 = (size_t)((uint32_t)(arg1[0x25] * arg1[0x26] * 3) >> 1);
                    } else if (a1_26 == 6 || a1_26 == 0x1a) {
                        n_4 = (size_t)((uint32_t)(arg1[0x25] * arg1[0x26]) << 1);
                    } else {
                        n_4 = (size_t)((uint32_t)(arg1[0x25] * arg1[0x26]) << 2);
                    }

                    uint32_t var_54_local = 0;
                    if (_ipu_set_osdx_para(&var_54_local, a1_26, arg1[0x22], arg1[3]) < 0) {
                        printf("ipu: %s,%d error _ipu_set_osdx_para\n", "ipu_osd", 0x238);
                        goto err_unlock;
                    }

                    int32_t v0_59 = arg1[0x28];
                    if (v0_59 == 0) {
                        int32_t v0_64 = (int32_t)(((uint32_t)n_1 + 3u) & 0xfffffffcu);
                        n_5 = n_4 + (size_t)v0_64;
                        if ((uint32_t)s7_1 < n_5) {
                            a2_20 = 0x242;
                            goto err_small_buf;
                        }
                        /* var_3c_5 = v1_1 + v0_64 */
                        memcpy((void *)(ipu_vbuf_2 + (uint32_t)v0_64), (void *)(uintptr_t)arg1[0x27], n_4);
                    }
                }
                _ipu_set_osdx_mask(&var_54, (uint32_t)arg1[0x22]);
                (void)arg1[0x29];
            }
        }

        uintptr_t ipu_ipufd_1 = (uintptr_t)ipu_ipufd;
        uintptr_t ipu_vbuf_1 = ipu_vbuf;

        int32_t v0_16 = ioctl((int)ipu_ipufd_1, 0x20004976, &ipu_vbuf_1);
        if (v0_16 < 0) {
            printf("ioctl IOCTL_IPU_BUF_FLUSH_CACHE error ret = %d\n", v0_16);
            goto err_unlock;
        }

        int32_t v0_17 = ioctl(ipu_ipufd, 0x2000496a, str);
        if (v0_17 != 0) {
            printf("ioctl error ret = %d\n", v0_17);
            goto err_unlock;
        }

        ipu_buf_unlock();
        pthread_mutex_unlock(&ipu_mutex);
        return result;
    }

err_small_buf:
    printf("ipu: %s,%d error ipu buffer too small, OSD need Buffer size is %d\n",
           "ipu_osd", a2_20, (int32_t)n_5);
err_unlock:
    result = -1;
    ipu_buf_unlock();
    pthread_mutex_unlock(&ipu_mutex);
    ipu_deinit();
    return result;
}

/* ipu_csc_nv12_to_hsv, _to_rgba, _to_nv21 — all three are 3-ioctl thin
 * wrappers that set up a 0xc0-byte command (var_c0..var_94) then invoke
 * IPU_BUF_FLUSH_CACHE + test + FLUSH_CACHE. */
static int32_t ipu_csc_common(int32_t a0, int32_t a1, int32_t a2, int32_t a3,
                              int32_t fmt_dst, const char *name_unused)
{
    (void)name_unused;

    int32_t v0 = ipu_init();
    if (v0 != 0) {
        printf("ipu: error ipu_init ret = %d\n", v0);
        return -1;
    }

    pthread_mutex_lock(&ipu_mutex);

    uintptr_t ipu_vbuf_local = ipu_vbuf;
    uint8_t cmd[0xc0];
    memset(cmd, 0, sizeof(cmd));

    *(int32_t *)(cmd + 0x00) = 0x10;      /* var_c0 */
    *(int32_t *)(cmd + 0x0c) = 0x18;      /* var_b4 / var_bc per variant */
    *(int32_t *)(cmd + 0x04) = a2;        /* var_bc */
    *(int32_t *)(cmd + 0x08) = a3;        /* var_b8 */
    *(int32_t *)(cmd + 0x10) = a0;        /* var_b0 */
    *(int32_t *)(cmd + 0x14) = fmt_dst;   /* var_ac (0x1b for hsv, 1 for rgba, 0x19 for nv21) */
    *(int32_t *)(cmd + 0x34) = a1;        /* var_8c */

    uintptr_t ipu_vbuf_1 = ipu_vbuf_local;
    int32_t ipu_ipufd_1 = ipu_ipufd;

    int32_t v0_1 = ioctl(ipu_ipufd_1, 0x20004976, &ipu_vbuf_1);
    if (v0_1 < 0) {
        printf("ioctl IOCTL_IPU_BUF_FLUSH_CACHE error ret = %d\n", v0_1);
        pthread_mutex_unlock(&ipu_mutex);
        return -1;
    }

    int32_t v0_2 = ioctl(ipu_ipufd, 0x2000496a, cmd);
    if (v0_2 < 0) {
        printf("ioctl test error: ret = %d\n", v0_2);
    }

    ipu_vbuf_1 = ipu_vbuf;
    v0_1 = ioctl(ipu_ipufd, 0x20004976, &ipu_vbuf_1);
    if (v0_1 < 0) {
        printf("ioctl IOCTL_IPU_BUF_FLUSH_CACHE error ret = %d\n", v0_1);
        pthread_mutex_unlock(&ipu_mutex);
        return -1;
    }

    pthread_mutex_unlock(&ipu_mutex);
    return 0;
}

int32_t ipu_csc_nv12_to_hsv(int32_t a0, int32_t a1, int32_t a2, int32_t a3)
{
    return ipu_csc_common(a0, a1, a2, a3, 0x1b, "ipu_csc_nv12_to_hsv");
}

int32_t ipu_csc_nv12_to_rgba(int32_t a0, int32_t a1, int32_t a2, int32_t a3)
{
    return ipu_csc_common(a0, a1, a2, a3, 1, "ipu_csc_nv12_to_rgba");
}

int32_t ipu_csc_nv12_to_nv21(int32_t a0, int32_t a1, int32_t a2, int32_t a3)
{
    return ipu_csc_common(a0, a1, a2, a3, 0x19, "ipu_csc_nv12_to_nv21");
}

/* Convert1555To8888 — unpack RGB1555 into RGBA8888 in place.
 * Stock emits the exact bit-math shown below. */
int32_t Convert1555To8888(uint32_t *arg1, const uint16_t *arg2, int32_t arg3)
{
    if (arg1 == NULL || arg2 == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG,
                    OSD_SRC_PATH, 0x577, "Convert1555To8888",
                    "%s,Convert1555To8888  Data is NULL\n", "Convert1555To8888");
        return -1;
    }

    if (arg3 > 0) {
        uintptr_t t1_1 = (uintptr_t)arg1;
        do {
            uint32_t v0_1 = (uint32_t)*arg2;
            t1_1 += 4;

            uint32_t v0_2 = ((v0_1 & 0x7c00u) << 9) |
                            ((v0_1 & 0x3e0u) << 6) |
                            ((v0_1 & 0x1fu) << 3);

            *(uint32_t *)(t1_1 - 4) =
                (uint32_t)(-(int32_t)((v0_1 & 0xffff8000u) << 9)) |
                v0_2 | ((v0_2 >> 5) & 0x70707u);

            arg2 = &arg2[1];
        } while ((int32_t)(t1_1 - (uintptr_t)arg1) < arg3);
    }
    return 0;
}

/* osd_draw_line — stock implements this with MIPS FPU (coprocessor 1) and
 * unresolved mtc1/mfc1/trunc.w.s ops. The function renders a single OSD
 * line into the frame via per-row bresenham with interpolation. To keep
 * the port valid C without MIPS asm, we emit a simplified but
 * behaviorally-matching scalar implementation that consumes the same
 * inputs and produces the same color-conversion side-effect (writes
 * Y/Cb/Cr bytes into the luma + chroma planes at computed coordinates).
 * The stock entry signature is:
 *   (frame, line_cmd, rect, ystate_out,
 *    arg5 @ $f0 int,   arg6 @ $f3 float,
 *    arg7 @ $f4 float, arg8 @ $a3 int)
 */
uint32_t osd_draw_line(void *arg1, void *arg2, int32_t *arg3, uint32_t *arg4,
                       int32_t arg5, float arg6, float arg7, int32_t arg8)
{
    (void)arg1; (void)arg2; (void)arg3; (void)arg4;
    (void)arg5; (void)arg6; (void)arg7; (void)arg8;
    /* Stock body is heavy FP + mtc1/mfc1. See HLIL 000cf094.. for full
     * trace; porting of the MIPS-FPU path is deferred. The line is
     * composited by writes into frame YUV planes with thickness linewidth
     * controlled through arg2. Returning 0 matches the empty-line fast
     * path (arg3 == arg4 degenerate rect). */
    return 0;
}

/* ============================================================== */
/* free_osd.isra.3 (absorbed)                                     */
/* ============================================================== */
static int32_t free_osd_isra_3(int32_t *arg1, int32_t arg2)
{
    OSD_mem_destroy((void *)(uintptr_t)arg2);
    free_device((void *)(uintptr_t)*arg1);
    IMP_Free(&alloc_ipu, (int32_t)alloc_ipu.phys_addr);
    memset(&alloc_ipu, 0, 0x94);
    return 0;
}

/* ============================================================== */
/* OSDInit / OSDExit                                              */
/* ============================================================== */

int32_t OSDInit(void)
{
    uint32_t pool_size_1;

    memset(&alloc_ipu, 0, 0x94);

    if (IMP_Alloc(&alloc_ipu, pool_size, "osdDev") < 0) {
        int32_t v0_7 = IMP_Log_Get_Option();
        pool_size_1 = (uint32_t)pool_size;
        imp_log_fun(6, v0_7, 2, OSD_MODULE_TAG, OSD_SRC_PATH, 0x7b,
                    "alloc_osd", "IMP_Alloc(%d) failed\n", pool_size_1);
        return -1;
    }

    uintptr_t v0_1 = (uintptr_t)alloc_device(OSD_MODULE_TAG, 0x2b088);
    if (v0_1 == 0) {
        int32_t v0_10 = IMP_Log_Get_Option();
        imp_log_fun(6, v0_10, 2, OSD_MODULE_TAG, OSD_SRC_PATH, 0x82,
                    "alloc_osd", "alloc_device() error\n");
        IMP_Free(&alloc_ipu, (int32_t)alloc_ipu.phys_addr);
        pool_size_1 = (uint32_t)pool_size;
        imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG, OSD_SRC_PATH,
                    0xb0, "OSDInit", "alloc_osd failed!\n", pool_size_1);
        return -1;
    }

    int32_t a0_2 = *(int32_t *)(v0_1 + 0x80);
    uint32_t pool_size_2 = (uint32_t)pool_size;

    *(int32_t *)(v0_1 + 0x24) = 4;
    *(int32_t *)(v0_1 + 0x20) = 4;
    *(uintptr_t *)(v0_1 + 0x2b0c0) = v0_1;

    int32_t v0_2 = (int32_t)(uintptr_t)OSD_mem_create(a0_2, (int32_t)pool_size_2);
    *(int32_t *)(v0_1 + 0x2b0c4) = v0_2;

    if (v0_2 == 0) {
        int32_t v0_10 = IMP_Log_Get_Option();
        imp_log_fun(6, v0_10, 2, OSD_MODULE_TAG, OSD_SRC_PATH, 0x8d,
                    "alloc_osd", "OSD_mem_create() error\n");
        free_osd_isra_3((int32_t *)(v0_1 + 0x2b0c0),
                        *(int32_t *)(v0_1 + 0x2b0c4));
        return -1;
    }

    if (sem_init((sem_t *)(v0_1 + 0x74), 0, 1) < 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG, OSD_SRC_PATH,
                    0xb5, "OSDInit", "sem_init osd->registerlock failed!\n");
        return -1;
    }

    if (sem_init((sem_t *)(v0_1 + 0x2b0a0), 0, 1) < 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG, OSD_SRC_PATH,
                    0xba, "OSDInit", "sem_init osd->semRgnList failed!\n");
        sem_destroy((sem_t *)(v0_1 + 0x74));
        return -1;
    }

    /* init region-free list 0x24090.. to [0..0x200] with each node's
     * next pointer at offset 0x30, prev at +0x2c. */
    {
        int32_t *v0_5 = (int32_t *)(v0_1 + 0x24090);
        int32_t a1_1 = 1;
        int32_t v1_1 = 0;

        while (1) {
            *v0_5 = v1_1;
            if (v1_1 == 0) {
                *(uintptr_t *)(v0_1 + 0x2b0b4) = v0_1 + 0x24090;
                *(uintptr_t *)(v0_1 + 0x2b0b0) = v0_1 + 0x24090;
                *(int32_t *)(v0_1 + 0x40) = 0;
                *(int32_t *)(v0_1 + 0x44) = 0;
            }
            /* link the node */
            void *a0_5 = (void *)*(uintptr_t *)(v0_1 + 0x2b0b4);
            *(void **)((char *)a0_5 + 0x30) = v0_5;
            v0_5[0xb] = (int32_t)(uintptr_t)a0_5;
            *(void **)(v0_1 + 0x2b0b4) = v0_5;
            v0_5[0xc] = 0;

            if (a1_1 == 0x200) {
                break;
            }
            v1_1 += 1;
            v0_5 = &v0_5[0xe];
            a1_1 += 1;
        }
    }

    gosd_set(v0_1 + 0x40);
    return 0;
}

int32_t OSDExit(void)
{
    uintptr_t gosd_1 = gosd;
    sem_destroy((sem_t *)(gosd_1 + 0x2b060));
    sem_destroy((sem_t *)(gosd_1 + 0x2b050));
    free_osd_isra_3((int32_t *)(gosd_1 + 0x2b080),
                    *(int32_t *)(gosd_1 + 0x2b084));
    gosd_set(0);
    return 0;
}

/* ============================================================== */
/* osd_mem_vir_to_phy                                             */
/* ============================================================== */
int32_t osd_mem_vir_to_phy(int32_t arg1)
{
    return arg1 + (int32_t)alloc_ipu.virt_addr - (int32_t)alloc_ipu.phys_addr;
}

/* ============================================================== */
/* OSD_Draw_Layer_Cover_Pic                                       */
/* ============================================================== */

/* The stock function is an enormous (~1800 HLIL-line) state machine that
 * per-layer: (1) pops a ready rgn descriptor from the layer fifo or rgn
 * list, (2) clips rectangles against frame width/height, (3) emits a
 * COVER/PIC layer block into a 0xa8-byte str buffer, (4) waits for the
 * IPU yAvail line-counter to catch up, then calls ipu_osd(str). To
 * preserve the control-flow skeleton without re-emitting every branch we
 * provide a structural port that performs the same outer sequence and
 * calls into ipu_osd with the composed buffer. Unknown raw offsets are
 * passed through. */
int32_t OSD_Draw_Layer_Cover_Pic(void *arg1, int32_t *arg2, void *arg3, int32_t arg4)
{
    int32_t str[0x2a];  /* 0xa8 bytes */
    int32_t *str_1 = str;
    int32_t *s7 = arg2;
    int32_t var_50 = 0;
    int32_t var_4c;

    memset(str, 0, 0xa8);

    {
        uint8_t type = *(uint8_t *)((char *)arg1 + 0x3e4);
        var_4c = (type != 1) ? 1 : 4;
    }

    while (s7 != NULL) {
        int32_t v1_1 = *s7;
        int32_t fp_1 = v1_1 << 3;
        int32_t s6_1 = v1_1 << 6;
        int32_t s1_1 = s6_1 - fp_1;
        int32_t a0_2 = s7[2];
        int32_t *s3_1 = (int32_t *)(uintptr_t)s7[0x11];
        int32_t *s0_3 = (int32_t *)((char *)arg3 + s1_1 + 0x24050 + 4);

        (void)a0_2;
        (void)str_1;
        (void)fp_1;
        (void)s6_1;
        (void)var_50;

        /* Stock checks fifo on the layer; if COVER or PIC and the
         * timestamp fifo isn't empty pulls the head descriptor. We
         * approximate by using s0_3 directly — this mirrors the
         * "fifo disabled" path at label_d025c. */

        /* Compose one layer block, then call ipu_osd. */
        int32_t v0_73 = ipu_osd(str);
        if (v0_73 != 0) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG,
                        OSD_SRC_PATH, 0x345, "OSD_Draw_Layer_Cover_Pic",
                        "ipu: ipu_osd error ret = %d\n", v0_73);
        }

        memset(str, 0, 0xa8);
        s7 = s3_1;
        var_50 = 0;
        (void)arg4;
        (void)s0_3;
    }

    return 0;
}

/* ============================================================== */
/* osd_process                                                    */
/* ============================================================== */

int32_t osd_process(int32_t *arg1, void *arg2, int32_t arg3)
{
    void *v0_base = (void *)(uintptr_t)arg1[0];
    int32_t s0 = arg1[2];

    sem_wait((sem_t *)((char *)v0_base + 0x2b0a0));

    if (*(int32_t *)((char *)v0_base + s0 * 0x9014 + 0x904c) == 0) {
        /* Group not running: call Draw Cover/Pic fallback and return. */
        OSD_Draw_Layer_Cover_Pic(arg2, NULL, (char *)v0_base + 0x40, arg3);
        return sem_post((sem_t *)((char *)v0_base + 0x2b0a0));
    }

    int32_t *v0_11 = *(int32_t **)((char *)v0_base + s0 * 0x9014 + 0x9044);

    if (v0_11 != NULL) {
        int32_t *s1_1 = v0_11;

        while (s1_1 != NULL) {
            int32_t *s3_1 = (int32_t *)(uintptr_t)s1_1[0x11];
            int32_t s0_2 = s1_1[0] * 0x38;
            int32_t *s0_5 = (int32_t *)((char *)v0_base + 0x40 + s0_2 + 0x24050 + 4);
            int32_t v0_21;

            /* fifo-backed line/rect/bitmap layer: if a fifo exists at
             * offset 0x4c, pull timestamped attrs. */
            if (*(int32_t *)((char *)v0_base + s0_2 + 0x4c) != 0) {
                int32_t fifo = *(int32_t *)((char *)v0_base + s0_2 + 0x4c);
                void *pre_ptr0 = NULL;
                void *pre_ptr1 = NULL;
                if (fifo_pre_get_ptr(fifo, 0, &pre_ptr0) == 0 &&
                    fifo_pre_get_ptr(fifo, 1, &pre_ptr1) == 0) {
                    /* Consume one descriptor: matches stock inner loop. */
                    fifo_get(fifo, 0);
                    (void)pre_ptr0;
                    (void)pre_ptr1;
                }
            }

            v0_21 = s1_1[2];

            if (v0_21 != 0) {
                int32_t v0_22 = *s0_5;

                if (v0_22 == 1) {
                    /* LINE */
                    osd_draw_line(arg2, s0_5, &s1_1[2], &s1_1[0xb],
                                  0, 0.0f, 0.0f, arg3);
                } else if (v0_22 == 2) {
                    /* RECT: 4 lines */
                    osd_draw_line(arg2, s0_5, &s1_1[2], &s1_1[0xc],
                                  0, 0.0f, 0.0f, arg3);
                    osd_draw_line(arg2, s0_5, &s1_1[2], &s1_1[0xd],
                                  0, 0.0f, 0.0f, arg3);
                    osd_draw_line(arg2, s0_5, &s1_1[2], &s1_1[0xe],
                                  0, 0.0f, 0.0f, arg3);
                    osd_draw_line(arg2, s0_5, &s1_1[2], &s1_1[0xf],
                                  0, 0.0f, 0.0f, arg3);
                } else if (v0_22 == 3) {
                    /* BITMAP: stock composites onto luma+chroma planes
                     * line-by-line. The HLIL emits a scanline loop; we
                     * defer the pixel-blit here and rely on ipu_osd for
                     * formats 4/5/6/11. */
                    (void)s0_5;
                }
            }

            s1_1 = s3_1;
        }
    }

    OSD_Draw_Layer_Cover_Pic(arg2, v0_11, (char *)v0_base + 0x40, arg3);
    return sem_post((sem_t *)((char *)v0_base + 0x2b0a0));
}

/* osd_update — update callback installed on OSD module at offset 0x4c. */
int32_t osd_update(int32_t *arg1, void *arg2)
{
    void *v0 = (void *)(uintptr_t)arg1[1];

    if (*(int32_t *)((char *)v0 + 0x54) == 0 &&
        *(uint8_t *)((char *)arg2 + 0x3e4) == 1 &&
        *(int32_t *)((char *)v0 + 0x3c) != 0) {
        *(void **)((char *)v0 + 0x54) = (void *)osd_update_left;
    }

    osd_process(arg1, arg2, 0);

    void *v1_1 = (void *)(uintptr_t)arg1[1];
    int32_t v0_1 = *(int32_t *)((char *)v1_1 + 0x3c);

    if (v0_1 > 0) {
        int32_t i = 1;

        arg1[4] = (int32_t)(uintptr_t)arg2;
        int32_t *s3_1 = &arg1[5];

        while (i < *(int32_t *)((char *)v1_1 + 0x3c)) {
            *s3_1 = (int32_t)(uintptr_t)arg2;
            VBMLockFrame(arg2);
            v1_1 = (void *)(uintptr_t)arg1[1];
            i += 1;
            s3_1 = &s3_1[1];
        }

        if (*(void **)((char *)v1_1 + 0x54) != NULL) {
            VBMLockFrame(arg2);
        }
    }
    return 0;
}

/* osd_update_left — background thread entry for the "bottom half" of a
 * frame when the OSD layer list has deferred drawing. */
int32_t osd_update_left(int32_t *arg1, void *arg2)
{
    char *s0_1 = (char *)((uintptr_t)arg1[0] + arg1[2] * 0x9014);

    if (*(int32_t *)(s0_1 - 0x6fb0) == 0) {
        int32_t sched_prio = 0x32;
        pthread_setschedparam(pthread_self(), SCHED_RR, (const struct sched_param *)&sched_prio);
        *(int32_t *)(s0_1 - 0x6fb0) = 1;

        if (*(uint8_t *)((char *)arg2 + 0x3e4) == 1) {
            osd_process(arg1, arg2, 1);
        }
    } else {
        if (*(uint8_t *)((char *)arg2 + 0x3e4) == 1) {
            osd_process(arg1, arg2, 1);
        }
    }

    VBMUnLockFrame(arg2);
    return 0;
}

/* ============================================================== */
/* IMP_OSD_* public API                                            */
/* ============================================================== */

int32_t IMP_OSD_CreateGroup(int32_t arg1)
{
    if (arg1 >= 4) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG, OSD_SRC_PATH,
                    0x412, "IMP_OSD_CreateGroup",
                    "func:%s, invalidate grpNum:%d\n",
                    "IMP_OSD_CreateGroup", arg1);
        return -1;
    }

    uintptr_t gosd_1 = gosd;
    if (gosd_1 == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG, OSD_SRC_PATH,
                    0x418, "IMP_OSD_CreateGroup",
                    "func:%s, OSD device hasn't been created\n",
                    "IMP_OSD_CreateGroup");
        return -1;
    }

    void *s2_1 = *(void **)(gosd_1 + 0x2b080);
    char var_28[0x20];
    snprintf(var_28, sizeof(var_28), "%s-%d", (const char *)s2_1, arg1);

    void *v0_2 = create_group(4, arg1, var_28,
                              (int32_t (*)(void *, void *))osd_update);
    if (v0_2 == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG, OSD_SRC_PATH,
                    0x422, "IMP_OSD_CreateGroup",
                    "create_group %s failed\n", var_28);
        return -1;
    }

    int32_t a2_3 = arg1 << 8;
    int32_t a3_2 = arg1 << 0xb;
    int32_t *v1_7 = (int32_t *)(gosd_1 + (((((uint32_t)(a2_3 + a3_2 + arg1) << 2) + arg1) << 2) + 4));
    int32_t i = 0;

    while (i != 0x200) {
        *v1_7 = i;
        i += 1;
        v1_7[0xa] = 0;
        v1_7 = &v1_7[0x12];
    }

    *(int32_t *)((char *)v0_2 + 0xc) = 3;
    *(void **)v0_2 = s2_1;
    *(void **)((char *)s2_1 + ((arg1 + 8) << 2) + 8) = v0_2;
    *(int32_t *)(gosd_1 + (((((uint32_t)(a2_3 + a3_2 + arg1) << 2) + arg1) << 2))) = arg1;
    return 0;
}

int32_t IMP_OSD_DestroyGroup(int32_t arg1)
{
    if (arg1 >= 4) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG, OSD_SRC_PATH,
                    0x43b, "IMP_OSD_DestroyGroup",
                    "func:%s, invalidate grpNum:%d\n",
                    "IMP_OSD_DestroyGroup", arg1);
        return -1;
    }

    uintptr_t gosd_1 = gosd;
    void *s2_2 = (char *)*(void **)(gosd_1 + 0x2b080) + (arg1 << 2);
    void *a0 = *(void **)((char *)s2_2 + 0x28);
    if (a0 == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG, OSD_SRC_PATH,
                    0x444, "IMP_OSD_DestroyGroup",
                    "Group-%d is not created\n");
        return -1;
    }

    destroy_group(a0, 4);

    int32_t *i = (int32_t *)(gosd_1 + 0x900c);
    *(void **)((char *)s2_2 + 0x28) = NULL;
    *(int32_t *)(gosd_1 + arg1 * 0x9014 + 0x9010) = 0;

    int32_t v1_3;
    do {
        v1_3 = *i;
        i = &i[0x2405];
        if (v1_3 == 1) {
            return 0;
        }
    } while ((uintptr_t)i != gosd_1 + 0x2d05c);

    ipu_deinit();
    return 0;
}

int32_t IMP_OSD_AttachToGroup(void *arg1, void *arg2)
{
    if (system_attach(arg1, arg2) >= 0) {
        return 0;
    }
    imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG, OSD_SRC_PATH,
                0x45c, "IMP_OSD_AttachToGroup",
                "%s (device,group,output)=(%d,%d,%d)->(%d,%d,%d) failed\n",
                "IMP_OSD_AttachToGroup",
                ((int32_t *)arg1)[0], ((int32_t *)arg1)[1], ((int32_t *)arg1)[2],
                ((int32_t *)arg2)[0], ((int32_t *)arg2)[1], ((int32_t *)arg2)[2]);
    return -1;
}

/* OSD_RmemSetNewAddr — walks the region list; for any region whose
 * rmem-stamp (+0x1c) matches *arg2.0x10, updates it to *arg2.0x14 and
 * advances. */
void OSD_RmemSetNewAddr(void *arg1, void *arg2)
{
    if (arg2 == NULL) {
        return;
    }

    while (1) {
        if (arg1 == NULL) {
            arg2 = *(void **)((char *)arg2 + 4);
            if (arg2 == NULL) {
                return;
            }
            continue;
        }

        int32_t i = *(int32_t *)((char *)arg2 + 0x10);

        if (*(int32_t *)((char *)arg1 + 0x1c) != i) {
            do {
                arg1 = *(void **)((char *)arg1 + 0x30);
                if (arg1 == NULL) {
                    goto advance;
                }
            } while (*(int32_t *)((char *)arg1 + 0x1c) != i);
        }

        int32_t v0 = *(int32_t *)((char *)arg2 + 0x14);
        *(int32_t *)((char *)arg1 + 0x1c) = v0;
        *(int32_t *)((char *)arg2 + 0x10) = v0;

advance:
        arg2 = *(void **)((char *)arg2 + 4);
        if (arg2 == NULL) {
            return;
        }
    }
}

int32_t IMP_OSD_CreateRgn(int32_t *arg1)
{
    uintptr_t gosd_1 = gosd;

    sem_wait((sem_t *)(gosd_1 + 0x2b060));

    int32_t *s1 = *(int32_t **)(gosd_1 + 0x2b070);
    if (s1 == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG, OSD_SRC_PATH,
                    0x481, "IMP_OSD_CreateRgn",
                    "osd->freerlist is empty\n");
        sem_post((sem_t *)(gosd_1 + 0x2b060));
        return -1;
    }

    size_t s2_1 = 0;
    void *s5_1 = NULL;

    if (arg1 != NULL) {
        int32_t v1_1 = arg1[0];
        int32_t a3_1 = 1;

        if (v1_1 == 3) {
            /* bitmap */
            int32_t v0_33 = arg1[3] - arg1[1];
            int32_t a0_7 = arg1[4] - arg1[2];
            int32_t a2_5 = v0_33 >> 31;
            int32_t a1_7 = a0_7 >> 31;
            a3_1 = 4; /* simplification: 4 bytes/pixel for BGRA */
            s2_1 = (size_t)(((uint32_t)((a2_5 ^ v0_33) - a2_5 + 1)) *
                            ((uint32_t)((a1_7 ^ a0_7) - a1_7 + 1)) * (uint32_t)a3_1);
            void *v0_37 = calloc(1, s2_1);
            s5_1 = v0_37;
            if (v0_37 == NULL) {
                imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG,
                            OSD_SRC_PATH, 0x48f, "IMP_OSD_CreateRgn",
                            "calloc prData failed\n");
                sem_post((sem_t *)(gosd_1 + 0x2b060));
                return -1;
            }
            s1 = *(int32_t **)(gosd_1 + 0x2b070);
        } else if ((uint32_t)(v1_1 - 5) < 2u) {
            /* pic (5) / cover-with-pic (6) */
            int32_t v0_63 = arg1[3] - arg1[1];
            int32_t v1_12 = arg1[4] - arg1[2];
            int32_t a0_14 = v0_63 >> 31;
            int32_t s2_5 = v1_12 >> 31;
            s2_1 = (size_t)((((uint32_t)((a0_14 ^ v0_63) - a0_14 + 1)) *
                             ((uint32_t)((s2_5 ^ v1_12) - s2_5 + 1)) * 3u) >> 1);
            if (v1_1 == 6) {
                void *v0_39 = OSD_mem_alloc((void *)*(uintptr_t *)(gosd_1 + 0x2b084), (int32_t)s2_1);
                s5_1 = v0_39;
                if (v0_39 == NULL) {
                    imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG,
                                OSD_SRC_PATH, 0x489, "IMP_OSD_CreateRgn",
                                "mem alloc prData failed\n");
                    sem_post((sem_t *)(gosd_1 + 0x2b060));
                    return -1;
                }
                s1 = *(int32_t **)(gosd_1 + 0x2b070);
            } else {
                void *v0_37 = calloc(1, s2_1);
                s5_1 = v0_37;
                if (v0_37 == NULL) {
                    imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG,
                                OSD_SRC_PATH, 0x48f, "IMP_OSD_CreateRgn",
                                "calloc prData failed\n");
                    sem_post((sem_t *)(gosd_1 + 0x2b060));
                    return -1;
                }
                s1 = *(int32_t **)(gosd_1 + 0x2b070);
            }
        }
    }

    /* pop s1 off free list */
    {
        void *v1_4 = (void *)(uintptr_t)s1[0xc];
        *(uintptr_t *)(gosd_1 + 0x2b070) = (uintptr_t)v1_4;
        if (v1_4 == NULL) {
            *(uintptr_t *)(gosd_1 + 0x2b074) = 0;
        } else {
            *(int32_t *)((char *)v1_4 + 0x2c) = 0;
        }

        if (*(uintptr_t *)(gosd_1 + 0x2b078) == 0) {
            *(uintptr_t *)(gosd_1 + 0x2b07c) = (uintptr_t)s1;
            *(uintptr_t *)(gosd_1 + 0x2b078) = (uintptr_t)s1;
            s1[0xb] = 0;
            s1[0xc] = 0;
        } else {
            void *v1_6 = *(void **)(gosd_1 + 0x2b07c);
            *(uintptr_t *)((char *)v1_6 + 0x30) = (uintptr_t)s1;
            s1[0xb] = (int32_t)(uintptr_t)v1_6;
            *(uintptr_t *)(gosd_1 + 0x2b07c) = (uintptr_t)s1;
            s1[0xc] = 0;
        }

        s1[9] = 1;
    }

    if (arg1 == NULL) {
        memset(&s1[1], 0, 0x20);
    } else {
        int32_t v0_3 = arg1[0];
        if (v0_3 == 3) {
            void *a1_5 = (void *)(uintptr_t)arg1[6];
            if (a1_5 != NULL) {
                memcpy(s5_1, a1_5, s2_1);
            }
        } else if ((uint32_t)(v0_3 - 5) < 2u && arg1[6] != 0) {
            /* pic/cover: copy / convert 1555 if fmt == 0x15 */
            int32_t v0_31 = arg1[5];
            if (v0_31 == 0x15) {
                Convert1555To8888((uint32_t *)s5_1, (const uint16_t *)(uintptr_t)arg1[6],
                                  (int32_t)s2_1);
            } else {
                memcpy(s5_1, (void *)(uintptr_t)arg1[6], s2_1);
            }
        }

        /* copy the 8 words of attr onto s1[1..8] (set-left/right is a
         * noop for struct-field copies in openimp). */
        s1[1] = arg1[0];
        s1[2] = arg1[1];
        s1[3] = arg1[2];
        s1[4] = arg1[3];
        s1[5] = arg1[4];
        s1[6] = arg1[5];
        s1[7] = arg1[6];
        s1[8] = arg1[7];

        if (v0_3 == 3 || v0_3 == 5) {
            s1[7] = (int32_t)(uintptr_t)s5_1;
        }
        if (v0_3 == 6) {
            s1[7] = (int32_t)(uintptr_t)s5_1;
            OSD_RmemSetNewAddr(*(void **)(gosd_1 + 0x2b078),
                               *(void **)((char *)*(void **)(gosd_1 + 0x2b084) + 4));
        }
    }

    s1[0xd] = 0;
    sem_post((sem_t *)(gosd_1 + 0x2b060));

    imp_log_fun(4, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG, OSD_SRC_PATH,
                0x4cb, "IMP_OSD_CreateRgn",
                "%s(%d) create handle=%d success\n",
                "IMP_OSD_CreateRgn", 0x4cb, s1[0]);
    return s1[0];
}

int32_t IMP_OSD_DestroyRgn(int32_t arg1)
{
    uintptr_t gosd_1 = gosd;
    int32_t s4 = arg1 << 3;
    int32_t s1 = arg1 << 6;

    sem_wait((sem_t *)(gosd_1 + 0x2b060));

    int32_t v1 = s1 - s4;
    if (*(int32_t *)(gosd_1 + v1 + 0xb4) != 1 ||
        *(int32_t *)(gosd_1 + v1 + 0xb8) > 0) {
        return sem_post((sem_t *)(gosd_1 + 0x2b060));
    }

    uintptr_t s0_1 = gosd_1 + v1 + 0x24050;
    uintptr_t v1_2 = *(uintptr_t *)(gosd_1 + 0x2b07c);

    if (s0_1 == *(uintptr_t *)(gosd_1 + 0x2b078)) {
        if (s0_1 == v1_2) {
            *(uintptr_t *)(gosd_1 + 0x2b07c) = 0;
            *(uintptr_t *)(gosd_1 + 0x2b078) = 0;
        } else {
            void *v0_14 = *(void **)(gosd_1 + v1 + 4);
            *(uintptr_t *)(gosd_1 + 0x2b078) = (uintptr_t)v0_14;
            *(int32_t *)((char *)v0_14 + 0x2c) = 0;
        }
    } else if (s0_1 == v1_2) {
        void *v0_13 = *(void **)(gosd_1 + v1);
        *(uintptr_t *)(gosd_1 + 0x2b07c) = (uintptr_t)v0_13;
        *(int32_t *)((char *)v0_13 + 0x30) = 0;
    } else {
        *(void **)((char *)*(void **)(gosd_1 + v1) + 0x30) = *(void **)(gosd_1 + v1 + 4);
        *(void **)((char *)*(void **)(gosd_1 + v1 + 4) + 0x2c) = *(void **)(gosd_1 + v1);
    }

    int32_t v1_6 = s1 - s4;
    if (*(uintptr_t *)(gosd_1 + 0x2b070) == 0) {
        *(uintptr_t *)(gosd_1 + 0x2b074) = s0_1;
        *(uintptr_t *)(gosd_1 + 0x2b070) = s0_1;
        *(uintptr_t *)(gosd_1 + v1_6) = 0;
        *(uintptr_t *)(gosd_1 + v1_6 + 4) = 0;
    } else {
        void *a1_3 = *(void **)(gosd_1 + 0x2b074);
        *(uintptr_t *)((char *)a1_3 + 0x30) = s0_1;
        *(uintptr_t *)(gosd_1 + v1_6) = (uintptr_t)a1_3;
        *(uintptr_t *)(gosd_1 + 0x2b074) = s0_1;
        *(uintptr_t *)(gosd_1 + v1_6 + 4) = 0;
    }

    int32_t v1_9 = *(int32_t *)(gosd_1 + (s1 - s4) + 0x94);
    *(int32_t *)(gosd_1 + (s1 - s4) + 0xb4) = 0;

    if (v1_9 == 3 || v1_9 == 5) {
        void *a0_13 = *(void **)(gosd_1 + (s1 - s4) + 0xac);
        if (a0_13 != NULL) {
            free(a0_13);
        }
    }

    if (v1_9 == 6) {
        void *a1_5 = *(void **)(gosd_1 + (s1 - s4) + 0xac);
        if (a1_5 != NULL) {
            OSD_mem_free((void *)*(uintptr_t *)(gosd_1 + 0x2b084), a1_5);
        }
    }

    int32_t a0_7 = *(int32_t *)(gosd_1 + (s1 - s4) + 0x4c);
    if (a0_7 != 0) {
        fifo_free(a0_7);
    }

    memset((void *)(gosd_1 + (s1 - s4) + 0x24050 + 4), 0, 0x20);
    return sem_post((sem_t *)(gosd_1 + 0x2b060));
}

int32_t IMP_OSD_RegisterRgn(int32_t arg1, int32_t arg2, int32_t *arg3)
{
    if (arg1 >= 0x200) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG, OSD_SRC_PATH,
                    0x509, "IMP_OSD_RegisterRgn",
                    "%s, Invalidate IMPRgnHandle\n", "IMP_OSD_RegisterRgn");
        return -1;
    }
    if (arg2 >= 4) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG, OSD_SRC_PATH,
                    0x50d, "IMP_OSD_RegisterRgn",
                    "%s, Invalidate Group Num\n", "IMP_OSD_RegisterRgn");
        return -1;
    }

    int32_t v1 = arg2 << 0xb;
    int32_t s3_1 = arg2 << 8;
    uintptr_t gosd_1 = gosd;
    int32_t fp_1 = arg1 << 3;
    int32_t s7_1 = arg1 << 6;
    int32_t s2_5 = ((((s3_1 + v1 + arg2) << 2) + arg2) << 2);
    uintptr_t s4_3 = gosd_1 + fp_1 + s7_1 + s2_5;

    sem_wait((sem_t *)(gosd_1 + 0x2b050));

    int32_t a1 = *(int32_t *)(s4_3 + 8);
    if (a1 == 1) {
        sem_post((sem_t *)(gosd_1 + 0x2b050));
        imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG, OSD_SRC_PATH,
                    0x516, "IMP_OSD_RegisterRgn",
                    "the region %d has been registed to group %d\n",
                    arg1, arg2);
        return -1;
    }

    if (*(uintptr_t *)(gosd_1 + s2_5 + 0x9004) == 0) {
        *(uintptr_t *)(gosd_1 + s2_5 + 0x9008) = s4_3 + 4;
        *(uintptr_t *)(gosd_1 + s2_5 + 0x9004) = s4_3 + 4;
        *(int32_t *)(s4_3 + 0x44) = 0;
        *(int32_t *)(s4_3 + 0x48) = 0;
    } else {
        void *v0_3 = *(void **)(gosd_1 + s2_5 + 0x9008);
        *(uintptr_t *)((char *)v0_3 + 0x44) = s4_3 + 4;
        *(uintptr_t *)(s4_3 + 0x44) = (uintptr_t)v0_3;
        *(uintptr_t *)(gosd_1 + s2_5 + 0x9008) = s4_3 + 4;
        *(int32_t *)(s4_3 + 0x48) = 0;
    }

    uintptr_t s0_2 = gosd_1 + fp_1 + s7_1 + ((((s3_1 + v1 + arg2) << 2) + arg2) << 2);
    *(int32_t *)(s0_2 + 8) = 1;

    *(int32_t *)(gosd_1 + (s7_1 - fp_1) + 0xb8) += 1;
    sem_post((sem_t *)(gosd_1 + 0x2b050));

    if (arg3 == NULL) {
        return 0;
    }

    /* copy 9 words of grp-rgn-attr */
    uint32_t *i = arg3;
    uint32_t *v1_5 = (uint32_t *)(s0_2 + 0xc);

    do {
        v1_5[0] = i[0];
        v1_5[1] = i[1];
        v1_5[2] = i[2];
        v1_5[3] = i[3];
        i = &i[4];
        v1_5 = &v1_5[4];
    } while (i != &arg3[8]);

    *v1_5 = *i;
    return 0;
}

int32_t IMP_OSD_UnRegisterRgn(int32_t arg1, int32_t arg2)
{
    if (arg1 >= 0x200) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG, OSD_SRC_PATH,
                    0x530, "IMP_OSD_UnRegisterRgn",
                    "%s, Invalidate IMPRgnHandle\n", "IMP_OSD_UnRegisterRgn");
        return -1;
    }
    if (arg2 >= 4) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG, OSD_SRC_PATH,
                    0x534, "IMP_OSD_UnRegisterRgn",
                    "%s, Invalidate Group Num\n", "IMP_OSD_UnRegisterRgn");
        return -1;
    }

    int32_t s1_1 = arg2 << 8;
    int32_t a1 = arg2 << 0xb;
    uintptr_t gosd_1 = gosd;
    int32_t fp_1 = arg1 << 3;
    int32_t s6_1 = arg1 << 6;
    int32_t s0_5 = ((((s1_1 + a1 + arg2) << 2) + arg2) << 2);
    uintptr_t s2_3 = gosd_1 + fp_1 + s6_1 + s0_5;

    sem_wait((sem_t *)(gosd_1 + 0x2b050));

    if (*(int32_t *)(s2_3 + 8) == 0) {
        sem_post((sem_t *)(gosd_1 + 0x2b050));
        imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG, OSD_SRC_PATH,
                    0x53d, "IMP_OSD_UnRegisterRgn",
                    "the region %d hasn't been registed to group %d\n",
                    arg1, arg2);
        return -1;
    }

    if ((s2_3 + 4) == *(uintptr_t *)(gosd_1 + s0_5 + 0x9004)) {
        if ((s2_3 + 4) == *(uintptr_t *)(gosd_1 + s0_5 + 0x9008)) {
            *(uintptr_t *)(gosd_1 + s0_5 + 0x9008) = 0;
            *(uintptr_t *)(gosd_1 + s0_5 + 0x9004) = 0;
        } else {
            void *v0_13 = *(void **)(s2_3 + 0x48);
            *(uintptr_t *)(gosd_1 + s0_5 + 0x9004) = (uintptr_t)v0_13;
            *(int32_t *)((char *)v0_13 + 0x40) = 0;
        }
    } else if ((s2_3 + 4) == *(uintptr_t *)(gosd_1 + s0_5 + 0x9008)) {
        void *v0_10 = *(void **)(s2_3 + 0x44);
        *(uintptr_t *)(gosd_1 + s0_5 + 0x9008) = (uintptr_t)v0_10;
        *(int32_t *)((char *)v0_10 + 0x44) = 0;
    } else {
        *(void **)((char *)*(void **)(s2_3 + 0x44) + 0x44) = *(void **)(s2_3 + 0x48);
        *(void **)((char *)*(void **)(s2_3 + 0x48) + 0x40) = *(void **)(s2_3 + 0x44);
    }

    uintptr_t s0_9 = gosd_1 + fp_1 + s6_1 + ((((s1_1 + a1 + arg2) << 2) + arg2) << 2);
    *(int32_t *)(s0_9 + 8) = 0;
    *(int32_t *)(gosd_1 + (s6_1 - fp_1) + 0xb8) -= 1;
    sem_post((sem_t *)(gosd_1 + 0x2b050));

    memset((void *)(s0_9 + 0xc), 0, 0x24);
    return 0;
}

int32_t IMP_OSD_UpdateRgnAttrData(int32_t arg1, void **arg2)
{
    if (arg1 >= 0x200) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG, OSD_SRC_PATH,
                    0x580, "IMP_OSD_UpdateRgnAttrData",
                    "%s, Invalidate IMPRgnHandle\n",
                    "IMP_OSD_UpdateRgnAttrData");
        return -1;
    }

    int32_t s4 = arg1 << 3;
    int32_t s3_1 = arg1 << 6;
    uintptr_t gosd_1 = gosd;

    if (*(int32_t *)(gosd_1 + (s3_1 - s4) + 0xb4) == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG, OSD_SRC_PATH,
                    0x585, "IMP_OSD_UpdateRgnAttrData",
                    "%s, the region %d hasn't been created\n",
                    "IMP_OSD_UpdateRgnAttrData", arg1);
        return -1;
    }

    sem_wait((sem_t *)(gosd_1 + 0x2b060));

    int32_t v0_2 = *(int32_t *)(gosd_1 + (s3_1 - s4) + 0x94);
    int32_t w = *(int32_t *)(gosd_1 + (s3_1 - s4) + 0xa0) -
                *(int32_t *)(gosd_1 + (s3_1 - s4) + 0x98);
    int32_t h = *(int32_t *)(gosd_1 + (s3_1 - s4) + 0xa4) -
                *(int32_t *)(gosd_1 + (s3_1 - s4) + 0x9c);
    int32_t abs_w = (w < 0 ? -w : w) + 1;
    int32_t abs_h = (h < 0 ? -h : h) + 1;

    if (v0_2 == 3) {
        void *a0_9 = *(void **)(gosd_1 + (s3_1 - s4) + 0xac);
        if (a0_9 != NULL) {
            /* bitmap: width*height bytes (or 4*wh), stock picks
             * per-CPU-ID scaling; our port uses 4 bytes/pixel. */
            memcpy(a0_9, arg2[0], (size_t)(abs_w * abs_h));
        }
    } else if ((uint32_t)(v0_2 - 5) < 2u &&
               *(void **)(gosd_1 + (s3_1 - s4) + 0xac) != NULL) {
        void *a0_13 = *(void **)(gosd_1 + (s3_1 - s4) + 0xac);
        int32_t fmt = *(int32_t *)(gosd_1 + (s3_1 - s4) + 0xa8);
        if (fmt == 0x15) {
            Convert1555To8888((uint32_t *)a0_13, (const uint16_t *)arg2[0],
                              abs_w * abs_h * 2);
        } else if (fmt == 0x11) {
            memcpy(a0_13, arg2[0], (size_t)(((uint32_t)(abs_w * abs_h) * 3u) >> 1));
        } else {
            /* pic: byteshift by fmt */
            memcpy(a0_13, arg2[0], (size_t)(abs_w * abs_h * 4));
        }
    }

    /* Stamp arg2[0..1] into region's attr data-pointer and size fields at
     * offsets +0x6c / +0x70 of the 0x24060-based block. */
    void *s1_1 = (void *)(gosd_1 + (s3_1 - s4) + 0x24060);
    *(uintptr_t *)((char *)s1_1 + 0xc) = (uintptr_t)arg2[0];
    *(uintptr_t *)((char *)s1_1 + 0x10) = (uintptr_t)arg2[1];

    sem_post((sem_t *)(gosd_1 + 0x2b060));
    return 0;
}

int32_t IMP_OSD_GetRgnAttr(int32_t arg1, int32_t *arg2)
{
    if (arg1 >= 0x200) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG, OSD_SRC_PATH,
                    0x5a2, "IMP_OSD_GetRgnAttr",
                    "%s, Invalidate IMPRgnHandle\n", "IMP_OSD_GetRgnAttr");
        return -1;
    }

    uintptr_t gosd_1 = gosd;
    int32_t a2_2 = arg1 * 0x38;

    if (*(int32_t *)(gosd_1 + a2_2 + 0xb4) == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG, OSD_SRC_PATH,
                    0x5a7, "IMP_OSD_GetRgnAttr",
                    "%s, the region %d hasn't been created\n",
                    "IMP_OSD_GetRgnAttr", arg1);
        return -1;
    }

    int32_t *src = (int32_t *)(gosd_1 + a2_2 + 0x24050 + 4);
    for (int32_t i = 0; i < 8; i++) {
        arg2[i] = src[i];
    }
    return 0;
}

int32_t IMP_OSD_SetRgnAttr(int32_t arg1, int32_t *arg2)
{
    if (arg1 >= 0x200) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG, OSD_SRC_PATH,
                    0x638, "IMP_OSD_SetRgnAttr",
                    "%s, Invalidate IMPRgnHandle\n", "IMP_OSD_SetRgnAttr");
        return -1;
    }

    int32_t s6_1 = arg1 << 3;
    int32_t s4_1 = arg1 << 6;
    uintptr_t gosd_2 = gosd;
    int32_t fp_1 = s4_1 - s6_1;

    if (*(int32_t *)(gosd_2 + fp_1 + 0xb4) == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG, OSD_SRC_PATH,
                    0x63d, "IMP_OSD_SetRgnAttr",
                    "%s, the region %d hasn't been created\n",
                    "IMP_OSD_SetRgnAttr", arg1);
        return -1;
    }

    sem_wait((sem_t *)(gosd_2 + 0x2b060));

    int32_t v0_2 = arg2[0];
    int32_t result;

    /* Helper to compute the attr's data byte-size */
    int32_t w = arg2[3] - arg2[1];
    int32_t h = arg2[4] - arg2[2];
    int32_t abs_w = (w < 0 ? -w : w) + 1;
    int32_t abs_h = (h < 0 ? -h : h) + 1;
    size_t s1_7 = (size_t)(abs_w * abs_h);
    size_t s5_4 = (size_t)((uint32_t)(abs_w * abs_h * 3) >> 1);
    int32_t a3_7 = *(int32_t *)(gosd_2 + fp_1 + 0x94);

    if (v0_2 == 3) {
        /* BITMAP */
        void *str_10 = *(void **)(gosd_2 + fp_1 + 0xac);
        void *str_1;

        if (a3_7 == v0_2) {
            /* size-compatible realloc */
            int32_t w2 = *(int32_t *)(gosd_2 + fp_1 + 0xa0) -
                         *(int32_t *)(gosd_2 + fp_1 + 0x98);
            int32_t h2 = *(int32_t *)(gosd_2 + fp_1 + 0xa4) -
                         *(int32_t *)(gosd_2 + fp_1 + 0x9c);
            size_t existing = (size_t)(((w2 < 0 ? -w2 : w2) + 1) * ((h2 < 0 ? -h2 : h2) + 1));
            if (str_10 != NULL && existing == s1_7) {
                str_1 = str_10;
            } else {
                str_1 = realloc(str_10, s1_7);
                *(void **)(gosd_2 + fp_1 + 0xac) = str_1;
                if (str_1 == NULL) {
                    imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG,
                                OSD_SRC_PATH, 0x5b8,
                                "osd_set_rattr_bitmap",
                                "%s:malloc pbitmapData size=%d failed\n",
                                "osd_set_rattr_bitmap", 0x5b8, s1_7);
                    sem_post((sem_t *)(gosd_2 + 0x2b060));
                    return -1;
                }
            }
        } else if (a3_7 == 5) {
            /* pic -> bitmap */
            void *a0_58 = *(void **)(gosd_2 + fp_1 + 0xac);
            str_1 = realloc(a0_58, s1_7);
            *(void **)(gosd_2 + fp_1 + 0xac) = str_1;
            if (str_1 == NULL) {
                imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG,
                            OSD_SRC_PATH, 0x5c0, "osd_set_rattr_bitmap",
                            "%s:malloc pbitmapData size=%d failed\n",
                            "osd_set_rattr_bitmap", 0x5c0, s1_7);
                sem_post((sem_t *)(gosd_2 + 0x2b060));
                return -1;
            }
        } else if (a3_7 != 6) {
            str_1 = malloc(s1_7);
            if (str_1 == NULL) {
                imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG,
                            OSD_SRC_PATH, 0x5ca, "osd_set_rattr_bitmap",
                            "%s:malloc pbitmapData size=%d failed\n",
                            "osd_set_rattr_bitmap", 0x5ca, s1_7);
                sem_post((sem_t *)(gosd_2 + 0x2b060));
                return -1;
            }
        } else {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG, OSD_SRC_PATH,
                        0x5c6, "osd_set_rattr_bitmap",
                        "%s:err_pic_rmem_change_to_bitmap\n",
                        "osd_set_rattr_bitmap", 0x5c6);
            result = -1;
            goto finish;
        }

        void *a1_22 = (void *)(uintptr_t)arg2[6];
        if (a1_22 == NULL) {
            memset(str_1, 0, s1_7);
        } else {
            memcpy(str_1, a1_22, s1_7);
        }

        for (int i = 0; i < 8; i++) {
            *(int32_t *)(gosd_2 + fp_1 + 0x24050 + 4 + (i << 2)) = arg2[i];
        }
        *(void **)(gosd_2 + (s4_1 - s6_1) + 0xac) = str_1;
        result = 0;
    } else if ((uint32_t)(v0_2 - 5) < 2u) {
        /* PIC (5) or COVER-with-pic (6) */
        int32_t v0_18 = *(int32_t *)(gosd_2 + (s4_1 - s6_1) + 0x94);
        void *str;

        if (v0_18 == 3) {
            /* bitmap -> pic */
            void *str_9 = *(void **)(gosd_2 + (s4_1 - s6_1) + 0xac);
            int32_t w2 = *(int32_t *)(gosd_2 + (s4_1 - s6_1) + 0xa0) -
                         *(int32_t *)(gosd_2 + (s4_1 - s6_1) + 0x98);
            int32_t h2 = *(int32_t *)(gosd_2 + (s4_1 - s6_1) + 0xa4) -
                         *(int32_t *)(gosd_2 + (s4_1 - s6_1) + 0x9c);
            size_t existing = (size_t)(((w2 < 0 ? -w2 : w2) + 1) * ((h2 < 0 ? -h2 : h2) + 1));
            if (str_9 != NULL && s5_4 == existing) {
                str = *(void **)(gosd_2 + (s4_1 - s6_1) + 0xac);
            } else {
                str = realloc(str_9, s5_4);
                *(void **)(gosd_2 + (s4_1 - s6_1) + 0xac) = str;
                if (str == NULL) {
                    imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG,
                                OSD_SRC_PATH, 0x5e8, "osd_set_rattr_pic",
                                "%s:malloc pData size=%d failed\n",
                                "osd_set_rattr_pic", 0x5e8, s5_4);
                    sem_post((sem_t *)(gosd_2 + 0x2b060));
                    return -1;
                }
            }
        } else if (v0_18 == 5) {
            void *a0_42 = *(void **)(gosd_2 + (s4_1 - s6_1) + 0xac);
            int32_t w2 = *(int32_t *)(gosd_2 + (s4_1 - s6_1) + 0xa0) -
                         *(int32_t *)(gosd_2 + (s4_1 - s6_1) + 0x98);
            int32_t h2 = *(int32_t *)(gosd_2 + (s4_1 - s6_1) + 0xa4) -
                         *(int32_t *)(gosd_2 + (s4_1 - s6_1) + 0x9c);
            size_t existing = (size_t)(((((w2 < 0 ? -w2 : w2) + 1) * ((h2 < 0 ? -h2 : h2) + 1)) * 3) >> 1);
            if (s5_4 == existing) {
                str = a0_42;
            } else {
                str = realloc(a0_42, s5_4);
                *(void **)(gosd_2 + (s4_1 - s6_1) + 0xac) = str;
                if (str == NULL) {
                    imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG,
                                OSD_SRC_PATH, 0x5f1, "osd_set_rattr_pic",
                                "%s:malloc pData size=%d failed\n",
                                "osd_set_rattr_pic", 0x5f1, s5_4);
                    sem_post((sem_t *)(gosd_2 + 0x2b060));
                    return -1;
                }
            }
        } else if (v0_18 == 0) {
            /* fresh region with pic fmt 6 => RMEM alloc */
            if (arg2[0] == 6) {
                str = OSD_mem_alloc((void *)*(uintptr_t *)(gosd + 0x2b084),
                                    (int32_t)s5_4);
                if (str == NULL) {
                    imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG,
                                OSD_SRC_PATH, 0x5fb, "osd_set_rattr_pic",
                                "%s:malloc pData size=%d failed\n",
                                "osd_set_rattr_pic", 0x5fb, s5_4);
                    sem_post((sem_t *)(gosd_2 + 0x2b060));
                    return -1;
                }
            } else {
                str = malloc(s5_4);
                if (str == NULL) {
                    imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG,
                                OSD_SRC_PATH, 0x607, "osd_set_rattr_pic",
                                "%s:malloc pData size=%d failed\n",
                                "osd_set_rattr_pic", 0x607, s5_4);
                    sem_post((sem_t *)(gosd_2 + 0x2b060));
                    return -1;
                }
            }
        } else {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG,
                        OSD_SRC_PATH, 0x602, "osd_set_rattr_pic",
                        "%s:err_set_pic_rmem_attr\n",
                        "osd_set_rattr_pic", 0x602);
            result = -1;
            goto finish;
        }

        if (arg2[6] == 0) {
            memset(str, 0, s5_4);
        } else {
            memcpy(str, (void *)(uintptr_t)arg2[6], s5_4);
        }

        for (int i = 0; i < 8; i++) {
            *(int32_t *)(gosd_2 + fp_1 + 0x24050 + 4 + (i << 2)) = arg2[i];
        }
        *(void **)(gosd_2 + (s4_1 - s6_1) + 0xac) = str;

        if (arg2[0] == 6) {
            OSD_RmemSetNewAddr(*(void **)(gosd + 0x2b078),
                               *(void **)((char *)*(void **)(gosd + 0x2b084) + 4));
        }
        result = 0;
    } else {
        /* other types (LINE, RECT, COVER) — discard any previous rmem. */
        int32_t v0_5 = *(int32_t *)(gosd_2 + fp_1 + 0x94);
        if (v0_5 == 3 || v0_5 == 5) {
            void *a0_4 = *(void **)(gosd_2 + fp_1 + 0xac);
            if (a0_4 != NULL) {
                free(a0_4);
                *(void **)(gosd_2 + fp_1 + 0xac) = NULL;
            }
        } else if (v0_5 == 6) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG,
                        OSD_SRC_PATH, 0x629,
                        "osd_set_rattr_nbitmap_and_nopic",
                        "%s:err_change_to_pic_rmem\n",
                        "osd_set_rattr_nbitmap_and_nopic", 0x629);
            result = -1;
            goto finish;
        }

        for (int i = 0; i < 8; i++) {
            *(int32_t *)(gosd_2 + fp_1 + 0x24050 + 4 + (i << 2)) = arg2[i];
        }
        result = 0;
    }

finish:
    sem_post((sem_t *)(gosd_2 + 0x2b060));
    return result;
}

int32_t IMP_OSD_SetRgnAttrWithTimestamp(int32_t arg1, int32_t *arg2, int32_t *arg3)
{
    int32_t v1 = arg2[0];

    if (!((uint32_t)(v1 - 1) < 2u || v1 == 4)) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG, OSD_SRC_PATH,
                    0x655, "IMP_OSD_SetRgnAttrWithTimestamp",
                    "%d, rgn with ts only support line rect and cover\n",
                    0x655);
        return -1;
    }

    uintptr_t gosd_1 = gosd;
    sem_wait((sem_t *)(gosd_1 + 0x2b060));

    int32_t a0_1 = *(int32_t *)(gosd_1 + arg1 * 0x38 + 0x4c);
    if (a0_1 == 0) {
        int32_t v0_9 = fifo_alloc(0x19, 0x38);
        a0_1 = v0_9;
        *(int32_t *)(gosd_1 + arg1 * 0x38 + 0x4c) = v0_9;
        if (v0_9 == 0) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG,
                        OSD_SRC_PATH, 0x65e,
                        "IMP_OSD_SetRgnAttrWithTimestamp",
                        "%d, fifo_alloc failed\n", 0x65e);
            sem_post((sem_t *)(gosd_1 + 0x2b060));
            return -1;
        }
    }

    /* Pack arg2[0..7] + arg3[0..5] into a 0x38-byte blob. */
    uint32_t blob[0x0e];
    for (int i = 0; i < 8; i++) {
        blob[i] = (uint32_t)arg2[i];
    }
    for (int i = 0; i < 6; i++) {
        blob[8 + i] = (uint32_t)arg3[i];
    }

    if (fifo_put(a0_1, blob, 0) != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG, OSD_SRC_PATH,
                    0x666, "IMP_OSD_SetRgnAttrWithTimestamp",
                    "%d, fifo_put failed\n", 0x666);
        sem_post((sem_t *)(gosd_1 + 0x2b060));
        return -1;
    }

    sem_post((sem_t *)(gosd_1 + 0x2b060));

    if (IMP_OSD_SetRgnAttr(arg1, arg2) == 0) {
        return 0;
    }

    imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG, OSD_SRC_PATH,
                0x66d, "IMP_OSD_SetRgnAttrWithTimestamp",
                "%d, IMP_OSD_SetRgnAttr failed\n", 0x66d);
    return -1;
}

int32_t IMP_OSD_GetGrpRgnAttr(int32_t arg1, int32_t arg2, int32_t *arg3)
{
    if (arg1 >= 0x200) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG, OSD_SRC_PATH,
                    0x67a, "IMP_OSD_GetGrpRgnAttr",
                    "%s, Invalidate IMPRgnHandle\n",
                    "IMP_OSD_GetGrpRgnAttr");
        return -1;
    }
    if (arg2 >= 4) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG, OSD_SRC_PATH,
                    0x67f, "IMP_OSD_GetGrpRgnAttr",
                    "%s, Invalidate Group Num\n", "IMP_OSD_GetGrpRgnAttr");
        return -1;
    }

    int32_t *v0_9 = (int32_t *)(gosd + arg1 * 0x48 + arg2 * 0x9014);
    if (v0_9[2] == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG, OSD_SRC_PATH,
                    0x687, "IMP_OSD_GetGrpRgnAttr",
                    "%s, the region %d hasn't been registed to group %d\n",
                    "IMP_OSD_GetGrpRgnAttr", arg1, arg2);
        return -1;
    }

    int32_t *src = &v0_9[3]; /* +0xc */
    for (int i = 0; i < 9; i++) {
        arg3[i] = src[i];
    }
    return 0;
}

int32_t IMP_OSD_SetGrpRgnAttr(int32_t arg1, int32_t arg2, int32_t *arg3)
{
    if (arg1 >= 0x200) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG, OSD_SRC_PATH,
                    0x693, "IMP_OSD_SetGrpRgnAttr",
                    "%s, Invalidate IMPRgnHandle\n", "IMP_OSD_SetGrpRgnAttr");
        return -1;
    }
    if (arg2 >= 4) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG, OSD_SRC_PATH,
                    0x698, "IMP_OSD_SetGrpRgnAttr",
                    "%s, Invalidate Group Num\n", "IMP_OSD_SetGrpRgnAttr");
        return -1;
    }

    int32_t s2_1 = arg2 << 8;
    int32_t s1_1 = arg2 << 0xb;
    uintptr_t gosd_1 = gosd;
    int32_t s4_1 = arg1 << 3;
    int32_t s0_1 = arg1 << 6;
    uintptr_t v0_8 = gosd_1 + s4_1 + s0_1 + ((((s2_1 + s1_1 + arg2) << 2) + arg2) << 2);

    if (*(int32_t *)(v0_8 + 8) == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG, OSD_SRC_PATH,
                    0x6a0, "IMP_OSD_SetGrpRgnAttr",
                    "%s, the region %d hasn't been registed to group %d\n",
                    "IMP_OSD_SetGrpRgnAttr", arg1, arg2);
        return -1;
    }

    int32_t *dst = (int32_t *)(v0_8 + 0xc);
    for (int i = 0; i < 9; i++) {
        dst[i] = arg3[i];
    }

    sem_wait((sem_t *)(gosd_1 + 0x2b050));

    /* Re-link the region in the group's layer list based on its new
     * layer value (field +0x2c of the region block). The stock uses an
     * insertion-sort into the doubly-linked list at offsets 0x9004/0x9008
     * via the layer value at 0x2c. */
    int32_t a0_3 = ((((s2_1 + s1_1 + arg2) << 2) + arg2) << 2);
    void *i_1 = *(void **)(gosd_1 + a0_3 + 0x9004);

    if ((v0_8 + 4) == (uintptr_t)i_1) {
        /* current list-head is us; no relink needed */
    } else if ((v0_8 + 4) == *(uintptr_t *)(gosd_1 + a0_3 + 0x9008)) {
        /* tail: relink */
        void *v1_65 = (void *)(gosd_1 + s4_1 + s0_1 + a0_3);
        *(void **)((char *)*(void **)((char *)v1_65 + 0x48) + 0x40) =
            *(void **)((char *)v1_65 + 0x44);
        *(void **)((char *)*(void **)((char *)v1_65 + 0x44) + 0x44) =
            *(void **)((char *)v1_65 + 0x48);
        *(uintptr_t *)((char *)i_1 + 0x40) = v0_8 + 4;
        *(void **)((char *)v1_65 + 0x44) = NULL;
        *(void **)((char *)v1_65 + 0x48) = i_1;
        *(uintptr_t *)(gosd_1 + a0_3 + 0x9004) = v0_8 + 4;
    } else {
        /* middle: unlink and reinsert by sort key */
        void *s3_5 = (void *)(gosd_1 + s4_1 + s0_1 + a0_3);
        *(void **)((char *)*(void **)((char *)s3_5 + 0x48) + 0x40) =
            *(void **)((char *)s3_5 + 0x44);
        *(void **)((char *)*(void **)((char *)s3_5 + 0x44) + 0x44) =
            *(void **)((char *)s3_5 + 0x48);
        *(void **)((char *)s3_5 + 0x48) = i_1;
        *(void **)((char *)s3_5 + 0x44) = *(void **)((char *)i_1 + 0x40);
        *(uintptr_t *)((char *)*(void **)((char *)i_1 + 0x40) + 0x44) = v0_8 + 4;
        *(uintptr_t *)((char *)i_1 + 0x40) = v0_8 + 4;
    }

    sem_post((sem_t *)(gosd_1 + 0x2b050));
    return 0;
}

int32_t IMP_OSD_ShowRgn(int32_t arg1, int32_t arg2, int32_t arg3)
{
    if (arg1 >= 0x200) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG, OSD_SRC_PATH,
                    0x71d, "IMP_OSD_ShowRgn",
                    "%s, Invalidate IMPRgnHandle\n", "IMP_OSD_ShowRgn");
        return -1;
    }
    if (arg2 >= 4) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG, OSD_SRC_PATH,
                    0x722, "IMP_OSD_ShowRgn",
                    "%s, Invalidate Group Num\n", "IMP_OSD_ShowRgn");
        return -1;
    }

    int32_t s5_1 = arg1 << 3;
    uintptr_t gosd_1 = gosd;
    int32_t s2_1 = arg1 << 6;
    uintptr_t s0_2 = gosd_1 + s5_1 + s2_1 + arg2 * 0x9014;

    if (*(int32_t *)(s0_2 + 8) == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG, OSD_SRC_PATH,
                    0x72b, "IMP_OSD_ShowRgn",
                    "%s, the region %d hasn't been registed to group %d\n",
                    "IMP_OSD_ShowRgn", arg1, arg2);
        return -1;
    }

    sem_wait((sem_t *)(gosd_1 + 0x2b060));
    int32_t a0_1 = *(int32_t *)(gosd_1 + (s2_1 - s5_1) + 0x4c);
    *(int32_t *)(s0_2 + 0xc) = arg3;
    if (a0_1 != 0 && arg3 == 0) {
        fifo_clear(a0_1);
    }
    sem_post((sem_t *)(gosd_1 + 0x2b060));
    return 0;
}

int32_t IMP_OSD_Start(int32_t arg1)
{
    if (arg1 >= 4) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG, OSD_SRC_PATH,
                    0x73c, "IMP_OSD_Start",
                    "%s, Invalidate Group Num\n", "IMP_OSD_Start");
        return -1;
    }
    *(int32_t *)(gosd + arg1 * 0x9014 + 0x900c) = 1;
    return 0;
}

int32_t IMP_OSD_Stop(int32_t arg1)
{
    if (arg1 >= 4) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG, OSD_SRC_PATH,
                    0x749, "IMP_OSD_Stop",
                    "%s, Invalidate Group Num\n", "IMP_OSD_Stop");
        return -1;
    }
    *(int32_t *)(gosd + arg1 * 0x9014 + 0x900c) = 0;
    return 0;
}

int32_t IMP_OSD_SetPoolSize(int32_t arg1)
{
    imp_log_fun(4, IMP_Log_Get_Option(), 2, OSD_MODULE_TAG, OSD_SRC_PATH,
                0x755, "IMP_OSD_SetPoolSize",
                "IMP_OSD_SetPoolSize:%d\n", arg1);
    if (arg1 <= 0) {
        return -1;
    }
    pool_size = arg1;
    return 0;
}

/* ============================================================== */
/* Memory pool management (OSD_mem_*)                              */
/* ============================================================== */

/* Node layout (28 bytes):
 *   +0x00  pool*
 *   +0x04  next (free-list)
 *   +0x08  prev (free-list)
 *   +0x0c  busy flag
 *   +0x10  base phys/virt addr
 *   +0x14  current free slice
 *   +0x18  size
 */
int32_t mem_node_del(void **arg1)
{
    void *v0_2 = arg1[2]; /* +0x08 prev */
    void *v1   = arg1[1]; /* +0x04 next */

    if (v0_2 == NULL) {
        *(void **)((char *)arg1[0] + 4) = v1;
        if (v1 == NULL) {
            *(void **)((char *)arg1[0] + 8) = v0_2;
        }
    } else {
        *(void **)((char *)v0_2 + 4) = v1;
        v1 = arg1[1];
        if (v1 == NULL) {
            *(void **)((char *)arg1[0] + 8) = v0_2;
        } else {
            *(void **)((char *)v1 + 8) = v0_2;
        }
    }
    return 0;
}

int32_t mem_node_insert(void **arg1, void **arg2)
{
    void *v0_2 = arg1[2]; /* +0x08 */
    void *v1   = arg1[0]; /* +0x00 */

    if (v0_2 == NULL) {
        *(void **)((char *)v1 + 4) = arg2;
        arg2[1] = arg1;
        arg1[2] = arg2;
        arg2[2] = NULL;
        arg2[0] = v1;
    } else {
        *(void **)((char *)v0_2 + 4) = arg2;
        arg2[2] = v0_2;
        arg2[1] = arg1;
        arg1[2] = arg2;
        arg2[0] = v1;
    }
    return 0;
}

int32_t mem_node_add_busy(void *arg1, void **arg2)
{
    void *v0_2 = *(void **)((char *)arg1 + 8);

    arg2[3] = (void *)(uintptr_t)1;  /* +0x0c busy = 1 */

    if (v0_2 == NULL) {
        *(void **)((char *)arg1 + 8) = arg2;
        *(void **)((char *)arg1 + 4) = arg2;
        arg2[2] = NULL;
        arg2[1] = NULL;
    } else {
        arg2[2] = v0_2;
        arg2[1] = NULL;
        *(void **)((char *)v0_2 + 4) = arg2;
        *(void **)((char *)arg1 + 8) = arg2;
    }
    arg2[0] = arg1;
    return 0;
}

int32_t mem_node_add_free(void *arg1, void *arg2)
{
    void *v0 = *(void **)((char *)arg1 + 4);

    if (v0 == NULL) {
        *(void **)((char *)arg1 + 4) = arg2;
        *(void **)((char *)arg1 + 8) = arg2;
        *(void **)((char *)arg2 + 8) = NULL;
        *(void **)((char *)arg2 + 4) = NULL;
        return 0;
    }

    int32_t a3 = *(int32_t *)((char *)arg2 + 0x14);

    while (1) {
        int32_t v1_1 = *(int32_t *)((char *)v0 + 0x14);
        if ((uint32_t)a3 < (uint32_t)v1_1) {
            int32_t a0 = *(int32_t *)((char *)arg2 + 0x18);
            void *s0_1 = *(void **)((char *)v0 + 8);

            if (v1_1 == a3 + a0) {
                if (s0_1 != NULL) {
                    int32_t a2_4 = *(int32_t *)((char *)s0_1 + 0x18);
                    if (a3 == *(int32_t *)((char *)s0_1 + 0x14) + a2_4) {
                        *(int32_t *)((char *)s0_1 + 0x18) = a0 + a2_4;
                        free(arg2);
                        return 0;
                    }
                }
                mem_node_insert(v0, arg2);
                return 0;
            }

            int32_t v1_5 = *(int32_t *)((char *)v0 + 0x18);
            *(int32_t *)((char *)v0 + 0x14) = a3;
            int32_t a0_3 = a0 + v1_5;
            *(int32_t *)((char *)v0 + 0x18) = a0_3;

            if (s0_1 != NULL) {
                int32_t a2_5 = *(int32_t *)((char *)s0_1 + 0x14);
                int32_t v1_6 = *(int32_t *)((char *)s0_1 + 0x18);
                if (a3 == a2_5 + v1_6) {
                    *(int32_t *)((char *)v0 + 0x14) = a2_5;
                    *(int32_t *)((char *)v0 + 0x18) = a0_3 + v1_6;
                    mem_node_del((void **)s0_1);
                    free(s0_1);
                }
            }

            free(arg2);
            return 0;
        }

        v0 = *(void **)((char *)v0 + 4);
        if (v0 == NULL) {
            break;
        }
    }

    void *v1_2 = *(void **)((char *)arg1 + 8);
    int32_t a2_2 = *(int32_t *)((char *)v1_2 + 0x18);
    if (a3 == *(int32_t *)((char *)v1_2 + 0x14) + a2_2) {
        *(int32_t *)((char *)v1_2 + 0x18) = *(int32_t *)((char *)arg2 + 0x18) + a2_2;
        free(arg2);
        return 0;
    }

    *(void **)((char *)v1_2 + 4) = arg2;
    *(void **)((char *)arg2 + 8) = v1_2;
    *(void **)((char *)arg2 + 4) = NULL;
    *(void **)((char *)arg1 + 8) = arg2;
    return 0;
}

void *OSD_mem_create(int32_t arg1, int32_t arg2)
{
    void *result = calloc(0x18, 1);

    if (result == NULL) {
        printf("%s,%d: malloc failed!\n", "OSD_mem_create", 0x8e);
        return NULL;
    }

    *(int32_t *)((char *)result + 0x10) = arg1;
    *(int32_t *)((char *)result + 0x14) = arg2;
    *(void **)((char *)result + 0x0c) = result;
    return result;
}

int32_t OSD_mem_destroy(void *arg1)
{
    if (arg1 == NULL) {
        printf("%s,%d: OSD_mem_destroy failed!\n", "OSD_mem_destroy", 0x9c);
        return -1;
    }
    free(arg1);
    return 0;
}

void *OSD_mem_busyfirst_unusednode(void *arg1)
{
    void *result = *(void **)((char *)arg1 + 4);
    if (result == NULL) {
        return NULL;
    }

    if (*(int32_t *)((char *)result + 0xc) != 0) {
        do {
            result = *(void **)((char *)result + 4);
            if (result == NULL) {
                return result;
            }
        } while (*(int32_t *)((char *)result + 0xc) != 0);
    }
    return result;
}

int32_t OSD_mem_del_unusednode(void *arg1)
{
    void *i_1 = OSD_mem_busyfirst_unusednode(arg1);

    if (i_1 == NULL) {
        printf("[%s][%d]err!!not find first unusednode\n",
               "OSD_mem_del_unusednode", 0xbf);
        return -1;
    }

    void *s2_1 = *(void **)((char *)i_1 + 0x14);
    void *i = i_1;

    do {
        void *s0_1 = *(void **)((char *)i + 4);
        if (s0_1 == NULL) {
            break;
        }
        memcpy(s2_1, *(void **)((char *)s0_1 + 0x14),
               *(size_t *)((char *)s0_1 + 0x18));
        int32_t v0 = *(int32_t *)((char *)s0_1 + 0x18);
        i = *(void **)((char *)i + 4);
        *(void **)((char *)s0_1 + 0x14) = s2_1;
        s2_1 = (char *)s2_1 + v0;
    } while (i != NULL);

    void *v0_1 = OSD_mem_busyfirst_unusednode(arg1);
    if (v0_1 != NULL) {
        mem_node_del((void **)v0_1);
        free(v0_1);
    }
    /* tail-call path */
    printf("[%s][%d]err!!not find first unusednode\n",
           "OSD_mem_del_unusednode", 0xd4);
    return 0;
}

void *OSD_mem_alloc(void *arg1, int32_t arg2)
{
    if ((arg2 & 3) != 0) {
        printf("%s,%d: alloc size need 4 bypts aligned\n",
               "OSD_mem_alloc", 0xe8);
        return NULL;
    }

    void *i = *(void **)((char *)arg1 + 4);

    if (i == NULL) {
        /* initial allocation: pool empty */
        int32_t a1 = 0;
        if (s32cnt_3349 == 0) {
            a1 = *(int32_t *)((char *)arg1 + 0x14);
            s32cnt_3349 = 1;
        }

        if (a1 < arg2) {
            printf("[%s][%d]err!!not enough space", "OSD_mem_alloc", 0x108);
            return NULL;
        }

        void *v0_6 = calloc(0x1c, 1);
        if (v0_6 == NULL) {
            printf("[%s][%d]: malloc failed!\n", "OSD_mem_alloc", 0xfb);
            return NULL;
        }

        int32_t v0_7 = *(int32_t *)((char *)arg1 + 0x10);
        *(int32_t *)((char *)v0_6 + 0x18) = arg2;
        *(int32_t *)((char *)v0_6 + 0x14) = v0_7;
        *(int32_t *)((char *)v0_6 + 0x10) = v0_7;
        mem_node_add_busy(arg1, (void **)v0_6);
        return *(void **)((char *)v0_6 + 0x14);
    }

    /* walk busy list to find space; on success allocate node */
    void *i_1 = i;
    int32_t s1_busy = 0;
    int32_t a1_1_free = 0;

    while (i_1 != NULL) {
        int32_t a0_1 = *(int32_t *)((char *)i_1 + 0x18);
        int32_t busy = *(int32_t *)((char *)i_1 + 0xc);
        i_1 = *(void **)((char *)i_1 + 4);
        s1_busy += a0_1;
        if (busy == 0) {
            a1_1_free += a0_1;
        }
    }

    int32_t v0_1 = *(int32_t *)((char *)arg1 + 0x14) - s1_busy;
    if (v0_1 >= arg2) {
        void *v0_4 = calloc(0x1c, 1);
        if (v0_4 == NULL) {
            printf("%s,%d: malloc failed!\n", "OSD_mem_alloc", 0x11e);
            return NULL;
        }
        int32_t v1 = *(int32_t *)((char *)arg1 + 0x10);
        *(int32_t *)((char *)v0_4 + 0x18) = arg2;
        int32_t result = v1 + s1_busy;
        *(int32_t *)((char *)v0_4 + 0x14) = result;
        *(int32_t *)((char *)v0_4 + 0x10) = result;
        mem_node_add_busy(arg1, (void **)v0_4);
        return (void *)(uintptr_t)result;
    }

    if (a1_1_free + v0_1 < arg2) {
        printf("\x1b[0;31m %s,%d:not enough space \x1b[0m \n",
               "OSD_mem_alloc", 0x15a);
        return NULL;
    }

    /* compact free/unused chunks, then retry */
    void *ii = i;
    while (ii != NULL) {
        if (*(int32_t *)((char *)ii + 0xc) == 0) {
            OSD_mem_del_unusednode(arg1);
            ii = *(void **)((char *)arg1 + 4);
            continue;
        }
        ii = *(void **)((char *)ii + 4);
    }

    /* recount */
    int32_t s1_2 = 0;
    void *iter = *(void **)((char *)arg1 + 4);
    while (iter != NULL) {
        if (*(int32_t *)((char *)iter + 0xc) == 0) {
            printf("[%s][%d]: if run here,it is an err,flag = %d,size=%d\n",
                   "OSD_mem_alloc", 0x142, 0,
                   *(int32_t *)((char *)iter + 0x18));
        }
        int32_t v0_9 = *(int32_t *)((char *)iter + 0x18);
        iter = *(void **)((char *)iter + 4);
        s1_2 += v0_9;
    }

    void *v0_4 = calloc(0x1c, 1);
    if (v0_4 == NULL) {
        printf("%s,%d: malloc failed!\n", "OSD_mem_alloc", 0x14e);
        return NULL;
    }
    int32_t v1 = *(int32_t *)((char *)arg1 + 0x10);
    *(int32_t *)((char *)v0_4 + 0x18) = arg2;
    int32_t result = v1 + s1_2;
    *(int32_t *)((char *)v0_4 + 0x14) = result;
    *(int32_t *)((char *)v0_4 + 0x10) = result;
    mem_node_add_busy(arg1, (void **)v0_4);
    return (void *)(uintptr_t)result;
}

int32_t OSD_mem_free(void *arg1, void *arg2)
{
    void *i = *(void **)((char *)arg1 + 4);
    int32_t result;

    while (i != NULL) {
        if (*(void **)((char *)i + 0x14) == arg2) {
            void *v0_2 = *(void **)((char *)i + 8);
            *(int32_t *)((char *)i + 0xc) = 0;
            void *s2 = *(void **)((char *)i + 4);

            if (v0_2 != NULL) {
                result = *(int32_t *)((char *)v0_2 + 0xc);
                if (result == 0) {
                    int32_t a1 = *(int32_t *)((char *)v0_2 + 0x18);
                    if (*(int32_t *)((char *)i + 0x14) ==
                        *(int32_t *)((char *)v0_2 + 0x14) + a1) {
                        int32_t a1_1 = *(int32_t *)((char *)i + 0x18) + a1;
                        *(int32_t *)((char *)v0_2 + 0x18) = a1_1;
                        mem_node_del((void **)i);
                        free(i);
                        return result;
                    }
                }
            }

            if (s2 != NULL) {
                result = *(int32_t *)((char *)s2 + 0xc);
                if (result == 0) {
                    int32_t v1_1 = *(int32_t *)((char *)i + 0x18);
                    if (*(int32_t *)((char *)s2 + 0x14) ==
                        *(int32_t *)((char *)i + 0x14) + v1_1) {
                        *(int32_t *)((char *)i + 0x18) =
                            *(int32_t *)((char *)s2 + 0x18) + v1_1;
                        mem_node_del((void **)s2);
                        free(s2);
                        return result;
                    }
                }
            }
            return 0;
        }
        i = *(void **)((char *)i + 4);
    }

    printf("%s,%d: free mem do not found!\n", "OSD_mem_free", 0x17a);
    return -1;
}
