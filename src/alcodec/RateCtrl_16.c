#include <stdint.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "alcodec/al_rtos.h"

extern char _gp;
extern int32_t __assert(const char *expression, const char *file, int32_t line,
                        const char *function, void *caller);

#define RC16_KMSG(fmt, ...)                                                                       \
    do {                                                                                          \
        int _kfd = open("/dev/kmsg", O_WRONLY);                                                   \
        if (_kfd >= 0) {                                                                          \
            char _buf[256];                                                                       \
            int _n = snprintf(_buf, sizeof(_buf), "libimp/RC16: " fmt "\n", ##__VA_ARGS__);      \
            if (_n > 0) {                                                                         \
                write(_kfd, _buf, (size_t)_n);                                                    \
            }                                                                                     \
            close(_kfd);                                                                          \
        }                                                                                         \
    } while (0)

#define READ_U8(base, off) (*(uint8_t *)((uint8_t *)(base) + (off)))
#define READ_U16(base, off) (*(uint16_t *)((uint8_t *)(base) + (off)))
#define READ_S16(base, off) (*(int16_t *)((uint8_t *)(base) + (off)))
#define READ_U32(base, off) (*(uint32_t *)((uint8_t *)(base) + (off)))
#define READ_S32(base, off) (*(int32_t *)((uint8_t *)(base) + (off)))
#define RC16_STDERR(fmt, ...) dprintf(2, "libimp/RC16: " fmt "\n", ##__VA_ARGS__)

/* Placement:
 * - lI1i/Il1i/llli/l01i/o11i/oiii @ RateCtrl_16.c
 * - lo0i/lI0i/il0i/l11i/OOOI/oOOI/loli.isra.4/iili/IIIi/illi/ooIi @ RateCtrl_16.c
 */

int32_t rc_l0io(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6,
                char arg7, char arg8);
int32_t rc_iiIo(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4);
uint32_t rc_IIIo(int32_t *arg1, int32_t arg2);
int32_t rc_OOlo(void *arg1);
int32_t rc_i1Io(void *arg1);
int32_t rc_l1Io(int32_t *arg1);
int32_t rc_lo0i(void *arg1);
int32_t rc_lI0i(void *arg1);
int32_t rc_loli_isra_4(void *arg1, int32_t arg2);
int32_t rc_iili(void *arg1, void *arg2, int16_t arg3);
int32_t rc_IIIi(uint32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5);
int32_t rc_ooIi(uint32_t arg1, int16_t arg2, int32_t arg3, int32_t arg4, int16_t arg5,
                int16_t arg6);

static const int32_t rc_O1oo_table[103] = {
    0x5b, 0x66, 0x72, 0x80, 0x90, 0xa1, 0xb5, 0xcb, 0xe4, 0x100, 0x11f, 0x143, 0x16a,
    0x196, 0x1c8, 0x200, 0x23f, 0x285, 0x2d4, 0x32d, 0x390, 0x400, 0x47d, 0x50a, 0x5a8,
    0x659, 0x721, 0x800, 0x8fb, 0xa14, 0xb50, 0xcb3, 0xe41, 0x1000, 0x11f6, 0x1429,
    0x16a1, 0x1966, 0x1c82, 0x2000, 0x23eb, 0x2851, 0x2d41, 0x32cc, 0x3904, 0x4000,
    0x47d6, 0x50a3, 0x5a82, 0x6598, 0x7209, 0x8000, 0x8fad, 0xa145, 0xb505, 0xcb30,
    0xe412, 0x10000, 0x11f5a, 0x1428a, 0x16a0a, 0x19660, 0x1c824, 0x20000, 0x23eb3,
    0x28514, 0x2d414, 0x32cc0, 0x39048, 0x40000, 0x47d67, 0x50a29, 0x5a828, 0x65980,
    0x72090, 0x80000, 0x8facd, 0xa1451, 0xb504f, 0xcb2ff, 0xe411f, 0x100000, 0x11f59b,
    0x1428a3, 0x16a09e, 0x1965ff, 0x1c823e, 0x200000, 0x23eb36, 0x285146, 0x2d413d,
    0x32cbfd, 0x39047c, 0x400000, 0x47d66b, 0x50a28c, 0x5a827a, 0x6597fb, 0x7208f8,
    0x800000, 0x8facd6, 0xa14518, 0xb504f3
};

/* stock name: lI1i */
uint32_t rc_lI1i(void *arg1, int32_t *arg2, void *arg3, const char *arg4, int32_t arg5,
                 uint32_t arg6)
{
    char *ctx = *(char **)((char *)arg1 + 0x30);
    char *stats = (char *)arg3;
    uint8_t *rc = (uint8_t *)arg2;

    (void)arg4;
    (void)arg5;
    (void)arg6;

    RC16_KMSG("lI1i entry rc=%p ctx=%p rcAttr=%p stats=%p init=%u mode=%u bitrate=%d max=%d min=%d fps=%u/%u",
              arg1, ctx, arg2, arg3, ctx ? (unsigned)*(uint8_t *)ctx : 0xffU,
              rc ? (unsigned)READ_U32(rc, 0x00) : 0xffffffffU, rc ? READ_S32(rc, 0x08) : -1,
              rc ? READ_S32(rc, 0x14) : -1, rc ? READ_S32(rc, 0x10) : -1,
              rc ? (unsigned)READ_U16(rc, 0x0e) : 0U, rc ? (unsigned)READ_U16(rc, 0x0c) : 0U);
    RC16_KMSG("lI1i raw rc=%p w0=%08x w1=%08x w2=%08x w3=%08x w4=%08x w5=%08x w6=%08x w7=%08x",
              arg1, rc ? READ_U32(rc, 0x00) : 0U, rc ? READ_U32(rc, 0x04) : 0U,
              rc ? READ_U32(rc, 0x08) : 0U, rc ? READ_U32(rc, 0x0c) : 0U,
              rc ? READ_U32(rc, 0x10) : 0U, rc ? READ_U32(rc, 0x14) : 0U,
              rc ? READ_U32(rc, 0x18) : 0U, rc ? READ_U32(rc, 0x1c) : 0U);

    if (*(uint8_t *)ctx == 0) {
        uint32_t mode = READ_U32(rc, 0x00);
        uint32_t frame_rate = (uint32_t)READ_U16(rc, 0x0e);
        int32_t frame_period = (int32_t)READ_U16(rc, 0x0c) * 1000;
        int32_t min_size;
        int32_t max_size;
        uint32_t bitrate = READ_U32(rc, 0x08);
        uint32_t floor = READ_U32(rc, 0x04);
        int32_t signed_a8;
        int32_t codec_mode;

        if (mode == 1U) {
            min_size = READ_S32(rc, 0x10);
            max_size = min_size;
            if (READ_U32(rc, 0x14) != (uint32_t)min_size) {
                return (uint32_t)rc_iili(
                    (void *)(intptr_t)__assert("( Il0i && IIoi -> i0Oo == IIoi -> iiio ) || ( ! Il0i && IIoi -> i0Oo >= IIoi -> iiio )",
                                               "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_rate_ctrl/RateCtrl_16.c",
                                               0x5d8, "lI1i", &_gp),
                    NULL, 0);
            }
        } else {
            min_size = READ_S32(rc, 0x10);
            max_size = READ_S32(rc, 0x14);
            if ((uint32_t)max_size < (uint32_t)min_size) {
                return (uint32_t)rc_iili(
                    (void *)(intptr_t)__assert("( Il0i && IIoi -> i0Oo == IIoi -> iiio ) || ( ! Il0i && IIoi -> i0Oo >= IIoi -> iiio )",
                                               "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_rate_ctrl/RateCtrl_16.c",
                                               0x5d8, "lI1i", &_gp),
                    NULL, 0);
            }
        }
        min_size &= ~0x3f;
        max_size &= ~0x3f;

        *(int32_t *)(ctx + 0x9c) = min_size;
        *(int32_t *)(ctx + 0xa0) = max_size;
        RC16_KMSG("lI1i pre-memcpy ctx=%p stats=%p min=%d max=%d bitrate=%u floor=%u",
                  ctx, stats, min_size, max_size, bitrate, floor);
        RC16_STDERR("lI1i pre-memcpy ctx=%p stats=%p", ctx, stats);
        Rtos_Memcpy(ctx + 0x28, stats, 0x1c);
        RC16_KMSG("lI1i post-memcpy ctx=%p stats=%p", ctx, stats);
        RC16_STDERR("lI1i post-memcpy ctx=%p stats=%p", ctx, stats);
        if (*(uint32_t *)(ctx + 0x2c) == 0) {
            *(int32_t *)(ctx + 0x2c) = 1;
        }

        RC16_KMSG("lI1i pre-check rc=%p bitrate=%u floor=%u max=%u frame_rate=%u period=%d",
                  arg1, bitrate, floor, (uint32_t)READ_U32(rc, 0x14), frame_rate, frame_period);
        if (bitrate < floor) {
            RC16_KMSG("lI1i assert-path rc=%p bitrate=%u floor=%u", arg1, bitrate,
                      floor);
            return (uint32_t)rc_iili(
                (void *)(intptr_t)__assert("IIoi -> IIio >= IIoi -> oilo",
                                           "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_rate_ctrl/RateCtrl_16.c",
                                           0x629, "lI1i", &_gp),
                NULL, 0);
        }

        rc_l0io(ctx + 0x48, (int32_t)(((uint64_t)bitrate * (uint64_t)(uint32_t)READ_U32(rc, 0x14)) / 90000ULL),
                (int32_t)floor, (int32_t)frame_rate, frame_period, max_size, (char)(mode == 1U),
                (char)(mode != 9U));
        RC16_KMSG("lI1i post-rc_l0io ctx=%p frame_rate=%u period=%d min=%d max=%d",
                  ctx, frame_rate, frame_period, min_size, max_size);
        RC16_STDERR("lI1i post-rc_l0io ctx=%p", ctx);

        int16_t qp_min = READ_S16(rc, 0x1a);
        int16_t qp_max = READ_S16(rc, 0x1c);
        int16_t qp_init = READ_S16(rc, 0x18);
        uint32_t mode_bits = READ_U32(rc, 0x28);
        *(int16_t *)(ctx + 0x18) = qp_min;
        *(int16_t *)(ctx + 0x1a) = qp_max;
        *(int32_t *)(ctx + 4) = frame_period;
        *(int32_t *)(ctx + 8) = (int32_t)frame_rate;
        *(int16_t *)(ctx + 0x1c) = qp_init;
        *(uint8_t *)(ctx + 0x12d) = (uint8_t)(((mode_bits & 5U) == 1U) ? 1 : 0);
        *(int32_t *)(ctx + 0x88) = rc_OOlo(ctx + 0x48);
        *(uint8_t *)(ctx + 0x119) = 0;
        *(int32_t *)(ctx + 0xbc) = 0;
        *(int32_t *)(ctx + 0x8c) =
            (int32_t)(((uint64_t)frame_rate * (uint64_t)(uint32_t)min_size) / (uint64_t)frame_period);
        *(int32_t *)(ctx + 0x94) =
            (int32_t)(((uint64_t)frame_rate * (uint64_t)(uint32_t)max_size) / (uint64_t)frame_period);

        if (READ_U8(rc, 0x22) != 0) {
            *(uint8_t *)(ctx + 0x140) = 1;
            *(uint8_t *)(ctx + 0x141) = READ_U8(rc, 0x26);
            *(int32_t *)(ctx + 0xac) = (READ_S16(rc, 0x24) < 0)
                                           ? (*(uint8_t *)(ctx + 0x12c) != 0 ? 0xa : 2)
                                           : (int32_t)READ_S16(rc, 0x24);
        } else {
            *(uint8_t *)(ctx + 0x140) = 0;
            *(uint8_t *)(ctx + 0x141) = 0;
        }

        signed_a8 = READ_S16(rc, 0x1e);
        codec_mode = (int32_t)mode;
        *(uint8_t *)(ctx + 0x12e) = (uint8_t)(signed_a8 < 0);
        if (*(uint32_t *)(ctx + 0x2c) < 2) {
            *(int32_t *)(ctx + 0xa4) = 0;
            if (signed_a8 >= 0) {
                signed_a8 = 0;
            }
        } else if (signed_a8 < 0) {
            *(int32_t *)(ctx + 0xa4) = (*(uint8_t *)(ctx + 0x12c) != 0) ? 0xf : 4;
        } else {
            *(int32_t *)(ctx + 0xa4) = signed_a8;
        }

        {
            int32_t value_a8 = READ_S16(rc, 0x20);

            if (value_a8 < 0) {
                if (codec_mode == 9 || (codec_mode & 4) != 0) {
                    value_a8 = 0;
                } else {
                    value_a8 = (*(uint8_t *)(ctx + 0x12c) != 0) ? 0xa : 2;
                }
            }
            *(int32_t *)(ctx + 0xa8) = value_a8;
        }

        *(int16_t *)(ctx + 0x1e) = qp_init;
        if (*(uint32_t *)(ctx + 0x2c) >= 2) {
            int32_t qp = qp_init + *(int32_t *)(ctx + 0xa4);

            if (qp < qp_min) {
                qp = qp_min;
            }
            if (qp_max < qp) {
                qp = qp_max;
            }
            *(int16_t *)(ctx + 0x1c) = (int16_t)qp;
        }

        *(uint8_t *)(ctx + 0x118) = (uint8_t)(mode_bits & 1U);
        if (*(uint8_t *)(ctx + 0x12c) != 0) {
            *(int32_t *)(ctx + 0x130) = 5;
            *(int32_t *)(ctx + 0x134) = 0x36b0;
            *(int32_t *)(ctx + 0x138) = 0x36b0;
            *(int32_t *)(ctx + 0x13c) = 0x27ce;
        } else {
            *(int32_t *)(ctx + 0x130) = 1;
            *(int32_t *)(ctx + 0x134) = 0x4e20;
            *(int32_t *)(ctx + 0x138) = (codec_mode == 9) ? 0x4e20 : 0x3e80;
            *(int32_t *)(ctx + 0x13c) = 0x2bac;
        }

        if ((codec_mode & 8) != 0) {
            int32_t step = *(int32_t *)(ctx + 4) / *(int32_t *)(ctx + 8);

            *(int32_t *)(ctx + 0x128) = 2;
            *(int32_t *)(ctx + 0x120) = step;
            *(int32_t *)(ctx + 0x124) = step * 3;
        } else {
            *(int32_t *)(ctx + 0x124) = 0;
            *(int32_t *)(ctx + 0x128) = 0;
            *(int32_t *)(ctx + 0x120) = 0;
        }

        *(int32_t *)(ctx + 0x14) = (int32_t)READ_U16(rc, 0x32);
        *(int32_t *)(ctx + 0x10) = (int32_t)READ_U16(rc, 0x30);
        *(int32_t *)(ctx + 0x0c) = READ_S32(rc, 0x2c);
        *(uint8_t *)(ctx + 0x142) = (uint8_t)((mode_bits >> 1) & 1U);
        *(uint8_t *)(ctx + 0x143) = (uint8_t)((mode_bits >> 4) & 1U);
        RC16_KMSG("lI1i pre-rc_lI0i ctx=%p qp=%d bounds=%d..%d mode_bits=0x%x gop=%d cpb=%d",
                  ctx, *(int16_t *)(ctx + 0x1c), *(int16_t *)(ctx + 0x18), *(int16_t *)(ctx + 0x1a),
                  mode_bits, *(int32_t *)(ctx + 0x14), *(int32_t *)(ctx + 0x10));
        {
            uint32_t result = (uint32_t)rc_lI0i(ctx);

            *(uint8_t *)ctx = 0;
            RC16_KMSG("lI1i exit-init rc=%p ctx=%p result=%u qp=%d target=%d",
                      arg1, ctx, result, *(int16_t *)(ctx + 0x1c), *(int16_t *)(ctx + 0x20));
            return result;
        }
    }

    {
        int32_t frame_period = *(int32_t *)(ctx + 4);
        int32_t new_period = (int32_t)READ_U16(rc, 0x0c) * 1000;
        int32_t min_size = READ_S32(rc, 0x10) & ~0x3f;
        int32_t max_size = READ_S32(rc, 0x14) & ~0x3f;
        int32_t prev_max_size = *(int32_t *)(ctx + 0xa0);
        uint32_t frame_rate = (uint32_t)READ_U16(rc, 0x0e);
        int32_t need_reinit = 1;

        RC16_KMSG("lI1i reconfig-entry ctx=%p codec=%d period=%d->%d min=%d max=%d fps=%u",
                  ctx, *(int32_t *)(ctx + 0x28), frame_period, new_period, min_size, max_size, frame_rate);
        if (frame_period == 0 && *(int32_t *)(ctx + 0x0c) == 0 && *(int32_t *)(ctx + 0x10) == 0) {
            RC16_KMSG("lI1i cold-reconfig ctx=%p forcing init path", ctx);
            *(uint8_t *)ctx = 0;
            return rc_lI1i(arg1, arg2, arg3, arg4, arg5, arg6);
        }

        if (frame_period == new_period) {
            need_reinit = (*(int32_t *)(ctx + 8) != (int32_t)frame_rate);
            if (*(int32_t *)(ctx + 0x9c) != min_size) {
                need_reinit = 1;
            }
        }

        if (*(int32_t *)(ctx + 0x9c) != min_size) {
            *(int32_t *)(ctx + 0x9c) = min_size;
        }
        if (prev_max_size != max_size) {
            *(int32_t *)(ctx + 0xa0) = max_size;
        }

        if (need_reinit != 0) {
            *(int32_t *)(ctx + 0x8c) =
                (int32_t)(((uint64_t)frame_rate * (uint64_t)(uint32_t)min_size) / (uint64_t)new_period);
            *(int32_t *)(ctx + 0x94) =
                (int32_t)(((uint64_t)frame_rate * (uint64_t)(uint32_t)max_size) / (uint64_t)new_period);
            rc_iiIo(ctx + 0x48, (int32_t)frame_rate, new_period, max_size);
            *(int32_t *)(ctx + 4) = new_period;
            *(int32_t *)(ctx + 8) = (int32_t)frame_rate;
            RC16_KMSG("lI1i reconfig-reinit ctx=%p frame_rate=%u period=%d max=%d",
                      ctx, frame_rate, new_period, max_size);
        } else if (prev_max_size != max_size) {
            *(int32_t *)(ctx + 0x94) =
                (int32_t)(((uint64_t)frame_rate * (uint64_t)(uint32_t)max_size) / (uint64_t)new_period);
            rc_iiIo(ctx + 0x48, (int32_t)frame_rate, new_period, max_size);
            RC16_KMSG("lI1i reconfig-max-only ctx=%p frame_rate=%u period=%d max=%d",
                      ctx, frame_rate, new_period, max_size);
        }

        {
            int32_t qp_min = READ_S16(rc, 0x1a);
            int32_t qp_max = READ_S16(rc, 0x1c);
            int32_t qp_target = *(int16_t *)(ctx + 0x20);

            *(int16_t *)(ctx + 0x18) = (int16_t)qp_min;
            *(int16_t *)(ctx + 0x1a) = (int16_t)qp_max;
            if (qp_target < qp_min) {
                qp_target = qp_min;
            }
            if (qp_max < qp_target) {
                qp_target = qp_max;
            }
            if (qp_target != *(int16_t *)(ctx + 0x20)) {
                *(int16_t *)(ctx + 0x20) = (int16_t)qp_target;
                rc_lo0i(ctx);
                RC16_KMSG("lI1i reconfig-qptarget ctx=%p target=%d", ctx, qp_target);
            }

            {
                int32_t signed_a4 = READ_S16(rc, 0x1e);
                int32_t desired_a4;

                if (*(uint32_t *)(ctx + 0x2c) < 2) {
                    desired_a4 = 0;
                    if (signed_a4 >= 0) {
                        signed_a4 = 0;
                    }
                } else if (signed_a4 < 0) {
                    desired_a4 = (*(uint8_t *)(ctx + 0x12c) != 0) ? 0xf : 4;
                } else {
                    desired_a4 = signed_a4;
                }

                if (desired_a4 != *(int32_t *)(ctx + 0xa4)) {
                    int32_t delta = *(int32_t *)(ctx + 0xa4) - desired_a4;
                    int32_t scale = *(int32_t *)(ctx + 0x100);
                    uint32_t value = (uint32_t)*(int32_t *)(ctx + 0xb0);

                    if (delta > 0) {
                        while (delta-- != 0) {
                            value = (uint32_t)(((uint64_t)value * 125ULL) / (uint64_t)(uint32_t)scale);
                        }
                    } else {
                        while (delta++ != 0) {
                            value = (uint32_t)(((uint64_t)value * (uint64_t)(uint32_t)scale) / 10000ULL);
                        }
                    }
                    *(int32_t *)(ctx + 0xb0) = (int32_t)value;
                }
                *(int32_t *)(ctx + 0xa4) = desired_a4;
                *(uint8_t *)(ctx + 0x12e) = (uint8_t)(signed_a4 < 0);
            }

            {
                int32_t desired_a8 = READ_S16(rc, 0x20);

                if (desired_a8 < 0) {
                    int32_t codec_mode = *(int32_t *)(ctx + 0x28);

                    desired_a8 = (codec_mode == 9 || (codec_mode & 4) != 0)
                                     ? 0
                                     : (*(uint8_t *)(ctx + 0x12c) != 0 ? 0xa : 2);
                }

                if (desired_a8 != *(int32_t *)(ctx + 0xa8)) {
                    int32_t delta = desired_a8 - *(int32_t *)(ctx + 0xa8);
                    int32_t scale = *(int32_t *)(ctx + 0xf8);
                    uint32_t value = (uint32_t)*(int32_t *)(ctx + 0xb4);

                    if (delta > 0) {
                        while (delta-- != 0) {
                            value = (uint32_t)(((uint64_t)value * 125ULL) / (uint64_t)(uint32_t)scale);
                        }
                    } else {
                        while (delta++ != 0) {
                            value = (uint32_t)(((uint64_t)value * (uint64_t)(uint32_t)scale) / 10000ULL);
                        }
                    }
                    *(int32_t *)(ctx + 0xb4) = (int32_t)value;
                }
                *(int32_t *)(ctx + 0xa8) = desired_a8;
            }
        }

        *(uint8_t *)(ctx + 0x2e) = *(uint8_t *)(stats + 6);
        {
            uint32_t count = (uint32_t)*(uint8_t *)(stats + 4);

            if (count == 0) {
                count = 1;
            }
            *(int32_t *)(ctx + 0x2c) = (int32_t)count;
            RC16_KMSG("lI1i reconfig-exit ctx=%p count=%u stats4=%u stats6=%u",
                      ctx, count, (unsigned)*(uint8_t *)(stats + 4), (unsigned)*(uint8_t *)(stats + 6));
            return count;
        }
    }
}

/* stock name: lo0i */
int32_t rc_lo0i(void *arg1)
{
    uint32_t rate = (uint32_t)*(int32_t *)((char *)arg1 + 0x88);
    int32_t size = *(int32_t *)((char *)arg1 + 0x8c);
    int32_t qp = *(int16_t *)((char *)arg1 + 0x1c);
    uint32_t value = (uint32_t)(((uint64_t)rate * 1000ULL) / (uint64_t)(uint32_t)size);

    if ((int32_t)value >= 0x2711) {
        value = 0x2710;
    }

    *(int32_t *)((char *)arg1 + 0xe4) = qp;
    *(int32_t *)((char *)arg1 + 0xd8) = qp;
    *(int32_t *)((char *)arg1 + 0xdc) = qp;
    *(int32_t *)((char *)arg1 + 0xe0) = qp;
    *(int32_t *)((char *)arg1 + 0xd0) = 0;
    *(int32_t *)((char *)arg1 + 0xcc) = size;
    *(int32_t *)((char *)arg1 + 0xc8) = size;
    *(int32_t *)((char *)arg1 + 0xd4) = size;
    *(int32_t *)((char *)arg1 + 0x114) = 0;
    *(int32_t *)((char *)arg1 + 0x110) = 0;
    *(int32_t *)((char *)arg1 + 0x10c) = 0;
    *(int32_t *)((char *)arg1 + 0xf0) = 0x3c;
    *(int32_t *)((char *)arg1 + 0xf4) = size;
    *(int32_t *)((char *)arg1 + 0xec) = size;
    *(int32_t *)((char *)arg1 + 0xb0) = (int32_t)value;
    *(int32_t *)((char *)arg1 + 0xb4) = 0x14d;
    *(int32_t *)((char *)arg1 + 0xb8) =
        (*(uint8_t *)((char *)arg1 + 0x140) != 0) ? (*(int32_t *)((char *)arg1 + 0xac) * 1000) : 0;
    value = (*(uint8_t *)((char *)arg1 + 0x12c) != 0) ? 0x27f9 : 0x2bd9;
    *(int32_t *)((char *)arg1 + 0x104) = (int32_t)value;
    *(int32_t *)((char *)arg1 + 0x100) = (int32_t)value;
    *(int32_t *)((char *)arg1 + 0xfc) = (int32_t)value;
    *(int32_t *)((char *)arg1 + 0xf8) = (int32_t)value;
    return (int32_t)value;
}

/* stock name: lI0i */
int32_t rc_lI0i(void *arg1)
{
    int16_t qp = *(int16_t *)((char *)arg1 + 0x1c);
    int32_t size = *(int32_t *)((char *)arg1 + 0x8c);
    int32_t step = *(int32_t *)((char *)arg1 + 0x124);

    *(int32_t *)((char *)arg1 + 0x98) = 0;
    *(int32_t *)((char *)arg1 + 0x108) = 0;
    *(int32_t *)((char *)arg1 + 0x90) = size;
    *(int32_t *)((char *)arg1 + 0xc4) = -1;
    *(int32_t *)((char *)arg1 + 0xc0) = -1;
    *(int32_t *)((char *)arg1 + 0x11c) = step;
    *(int16_t *)((char *)arg1 + 0x24) = qp;
    *(int16_t *)((char *)arg1 + 0x20) = qp;
    if (*(uint8_t *)((char *)arg1 + 0x2c) >= 2) {
        int32_t value = 4;

        if (*(uint8_t *)((char *)arg1 + 0x12e) != 0) {
            value = (*(uint8_t *)((char *)arg1 + 0x12c) != 0) ? 0xf : 4;
            *(int32_t *)((char *)arg1 + 0xa4) = value;
        }
    }
    return rc_lo0i(arg1);
}

/* stock name: il0i */
uint32_t rc_il0i(void *arg1)
{
    char *ctx = *(char **)((char *)arg1 + 0x30);
    uint32_t result = (uint32_t)*(uint8_t *)ctx;

    if (result == 0) {
        return (uint32_t)rc_lI0i(ctx);
    }
    return result;
}

/* stock name: l11i */
int32_t rc_l11i(void *arg1, int32_t *arg2)
{
    int32_t result = rc_OOlo(*(void **)((char *)arg1 + 0x30) + 0x48);

    *arg2 = result;
    return result;
}

/* stock name: OOOI */
int32_t rc_OOOI(void *arg1, int32_t *arg2)
{
    int32_t result = rc_i1Io(*(void **)((char *)arg1 + 0x30) + 0x48);

    *arg2 = result;
    return result;
}

/* stock name: oOOI */
int32_t rc_oOOI(void *arg1, int32_t *arg2)
{
    int32_t *ctx = (int32_t *)((char *)*(void **)((char *)arg1 + 0x30) + 0x48);
    int32_t result = rc_l1Io(ctx) - rc_OOlo(ctx);

    *arg2 = result;
    return result;
}

/* stock name: loli.isra.4 */
int32_t rc_loli_isra_4(void *arg1, int32_t arg2)
{
    switch (arg2) {
    case 0:
        return *(int32_t *)((char *)arg1 + 0xa8);
    case 1:
    case 8:
        return 0;
    case 2:
        return -*(int32_t *)((char *)arg1 + 0xa4);
    case 3:
        return -*(int32_t *)((char *)arg1 + 0xac);
    default:
        __builtin_trap();
    }
}

/* stock name: iili */
int32_t rc_iili(void *arg1, void *arg2, int16_t arg3)
{
    char *ctx = *(char **)((char *)arg1 + 0x30);
    int16_t prev_a = *(int16_t *)(ctx + 0x20);
    int16_t prev_b = (*(uint8_t *)(ctx + 0x142) != 0) ? *(int16_t *)(ctx + 0x24) : prev_a;
    int16_t result =
        (int16_t)(arg3 - rc_loli_isra_4(ctx, *(int32_t *)((char *)arg2 + 0x10)) - prev_b + prev_a);

    *(int16_t *)(ctx + 0x24) = prev_a;
    *(int16_t *)(ctx + 0x20) = result;
    return result;
}

/* stock name: IIIi */
int32_t rc_IIIi(uint32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5)
{
    uint32_t result = arg1;
    uint32_t from = (uint32_t)arg2;
    uint32_t to = (uint32_t)arg3;

    if (arg4 == 0) {
        return (int32_t)(((uint64_t)rc_O1oo_table[(from + 0x33U - to)] * (uint64_t)result) >> 15);
    }
    if (arg5 < 0) {
        __builtin_trap();
    }

    if (from >= to) {
        uint32_t count = from - to;

        if ((uint32_t)arg5 < count) {
            count = (uint32_t)arg5;
        }
        while (count-- != 0) {
            result = (uint32_t)(((uint64_t)result * (uint64_t)(uint32_t)arg4) / 10000ULL);
        }
    } else {
        uint32_t count = to - from;

        if ((uint32_t)arg5 < count) {
            count = (uint32_t)arg5;
        }
        while (count-- != 0) {
            result = (uint32_t)(((uint64_t)result * 125ULL) / (uint64_t)(uint32_t)arg4);
        }
    }
    return (int32_t)result;
}

/* stock name: illi */
int32_t rc_illi(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5,
                int32_t arg6)
{
    int32_t result = rc_IIIi((uint32_t)arg1, arg2, arg3, arg4, arg5);

    if (arg6 < result) {
        return arg6;
    }
    return result;
}

/* stock name: ooIi */
int32_t rc_ooIi(uint32_t arg1, int16_t arg2, int32_t arg3, int32_t arg4, int16_t arg5,
                int16_t arg6)
{
    uint32_t value = arg1;
    int32_t result = arg2;
    int32_t low = arg5;
    int32_t high = arg6;

    if (arg3 < (int32_t)arg1) {
        while (result < high && arg3 < (int32_t)value) {
            ++result;
            value = (uint32_t)(((uint64_t)value * 125ULL) / (uint64_t)(uint32_t)arg4);
        }
        return result;
    }

    if (low < result && (int32_t)arg1 < arg3) {
        do {
            --result;
            value = (uint32_t)(((uint64_t)value * (uint64_t)(uint32_t)arg4) / 10000ULL);
            if (!(low < result)) {
                break;
            }
        } while ((int32_t)value < arg3);
    }
    return result;
}

/* stock name: Il1i */
int32_t rc_Il1i(void *arg1, void *arg2, int16_t *arg3)
{
    char *ctx = *(char **)((char *)arg1 + 0x30);

    RC16_KMSG("Il1i entry rc=%p ctx=%p pic=%p out=%p pic_flags=0x%x pic_type=%d forced=%u delta=%d qp_cur=%d qp_bounds=%d..%d",
              arg1, ctx, arg2, arg3, *(int32_t *)((char *)arg2 + 4), *(int32_t *)((char *)arg2 + 0x10),
              (unsigned)*(uint8_t *)((char *)arg2 + 0x2c), (int)*(int8_t *)((char *)arg2 + 0x2d),
              ctx ? *(int16_t *)(ctx + 0x20) : -1, ctx ? *(int16_t *)(ctx + 0x18) : -1,
              ctx ? *(int16_t *)(ctx + 0x1a) : -1);

    if (*(uint8_t *)((char *)arg2 + 0x2c) != 0) {
        int32_t value = *(int8_t *)((char *)arg2 + 0x2d);

        if (value < *(int16_t *)(ctx + 0x18)) {
            value = *(int16_t *)(ctx + 0x18);
        }
        if (*(int16_t *)(ctx + 0x1a) < value) {
            value = *(int16_t *)(ctx + 0x1a);
        }
        *arg3 = (int16_t)value;
        return value;
    }

    {
        uint32_t has_hier = (uint32_t)*(uint8_t *)(ctx + 0x140);
        int32_t flags = *(int32_t *)((char *)arg2 + 4);
        int32_t frame_type =
            (has_hier != 0 && (flags & 0x80) != 0 && *(int32_t *)((char *)arg2 + 0x10) != 2)
                ? 3
                : *(int32_t *)((char *)arg2 + 0x10);
        int32_t base_qp = (flags & 4) != 0 ? (*(int16_t *)(ctx + 0x20) < *(int16_t *)(ctx + 0x1c)
                                                       ? *(int16_t *)(ctx + 0x1c)
                                                       : *(int16_t *)(ctx + 0x20))
                                           : *(int16_t *)(ctx + 0x20);

        if (*(uint8_t *)(ctx + 0x12d) != 0 && *(int32_t *)(ctx + ((frame_type + 0x30) << 2) + 8) != 0 &&
            frame_type == 2 && (flags & 4) != 0) {
            uint32_t frame_count = (uint32_t)*(uint8_t *)(ctx + 0x2c);
            int32_t b_count = 0;

            if (frame_count >= 2) {
                uint32_t b_period = (uint32_t)*(uint8_t *)(ctx + 0x2e);

                if (b_period == 0xffffffffU) {
                    __builtin_trap();
                }
                b_count = (int32_t)(frame_count - frame_count / (b_period + 1) * b_period - 1);
                if (has_hier != 0) {
                    uint32_t layer_div = (uint32_t)*(uint8_t *)(ctx + 0x141);

                    if (layer_div == 0) {
                        __builtin_trap();
                    }
                    b_count -= b_count / (int32_t)layer_div;
                }
                if (b_count <= 0) {
                    b_count = 1;
                }
                {
                    int32_t average = (b_count / 2 + *(int32_t *)(ctx + 0xe8)) / b_count;

                    if (*(int32_t *)(ctx + 0xec) < *(int32_t *)(ctx + 0xcc)) {
                        if (*(int32_t *)(ctx + 0xec) == 0) {
                            __builtin_trap();
                        }
                        average += *(int32_t *)(ctx + 0xcc) / *(int32_t *)(ctx + 0xec);
                    }
                    base_qp = average;
                }
            }
        }

        {
            int32_t predicted = base_qp + rc_loli_isra_4(ctx, frame_type);
            int32_t target = rc_OOlo(ctx + 0x48);
            char *slot = ctx + (frame_type << 2);
            int32_t size = *(int32_t *)(slot + 0xc8);
            int32_t qp = predicted;

            if (size != 0) {
                int32_t target_qp = (target * 3 + (target < 0 ? 3 : 0)) >> 2;

                if (0 < target_qp && qp < *(int16_t *)(ctx + 0x1a) && (flags & 4) == 0) {
                    int32_t last_qp = *(int32_t *)(slot + 0xd8);

                    if (target_qp < rc_IIIi((uint32_t)size, last_qp, predicted, *(int32_t *)(slot + 0xf8),
                                            *(int16_t *)(ctx + 0x1a) - last_qp)) {
                        qp = rc_ooIi((uint32_t)size, (int16_t)last_qp, target_qp,
                                     *(int32_t *)(slot + 0xf8), *(int16_t *)(ctx + 0x18),
                                     *(int16_t *)(ctx + 0x1a));
                    }
                }
            }

        {
            int32_t result = qp + *(int8_t *)((char *)arg2 + 0x25);

            if (result < *(int16_t *)(ctx + 0x18)) {
                result = *(int16_t *)(ctx + 0x18);
                }
                if (*(int16_t *)(ctx + 0x1a) < result) {
                    result = *(int16_t *)(ctx + 0x1a);
                }
                *arg3 = (int16_t)result;
                RC16_KMSG("Il1i exit rc=%p ctx=%p qp=%d raw=%d frame_type=%d target=%d",
                          arg1, ctx, result, qp + *(int8_t *)((char *)arg2 + 0x25), frame_type,
                          rc_OOlo(ctx + 0x48));
                return qp + *(int8_t *)((char *)arg2 + 0x25);
            }
        }
    }
}

/* stock name: llli */
uint32_t rc_llli(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, uint32_t arg5,
                 uint32_t arg6, uint32_t arg7)
{
    uint32_t cur = arg5;
    uint32_t scale;
    uint32_t steps;

    if (arg2 == arg4) {
        return cur;
    }

    if (arg3 < arg1) {
        if ((uint32_t)arg2 >= (uint32_t)arg4) {
            uint32_t avg = cur + arg6;

            return (uint32_t)(((avg >> 31) + avg) >> 1);
        }
        scale = (uint32_t)(((uint64_t)10000U * (uint64_t)(uint32_t)arg1) / (uint64_t)(uint32_t)arg3);
        steps = (uint32_t)(arg4 - arg2);
    } else {
        if ((uint32_t)arg4 >= (uint32_t)arg2 || arg1 >= arg3) {
            uint32_t avg = cur + arg6;

            return (uint32_t)(((avg >> 31) + avg) >> 1);
        }
        scale = (uint32_t)(((uint64_t)10000U * (uint64_t)(uint32_t)arg3) / (uint64_t)(uint32_t)arg1);
        steps = (uint32_t)(arg2 - arg4);
    }

    while (1) {
        uint32_t denom = 10000;

        if (steps >= 2) {
            for (uint32_t i = 1; i < steps; ++i) {
                denom = (uint32_t)(((uint64_t)denom * (uint64_t)cur) / 10000ULL);
            }
            if (denom == 0) {
                denom = arg7;
            }
        }

        {
            uint32_t next = (uint32_t)((((uint64_t)(steps - 1) * (uint64_t)cur) +
                                        (((uint64_t)10000U * (uint64_t)scale) / (uint64_t)denom)) /
                                       (uint64_t)steps);

            if ((next + 1U < cur || cur + 1U < next) && next < arg7 && arg6 < next) {
                cur = next;
                if (steps >= 2) {
                    continue;
                }
            } else {
                if ((int32_t)next < (int32_t)arg6) {
                    return arg6;
                }
                if (arg7 <= next) {
                    return arg7;
                }
                return next;
            }
        }
    }
}

/* stock name: l01i */
int32_t rc_l01i(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4)
{
    char *ctx = *(char **)((char *)arg1 + 0x30);
    int32_t result;

    switch (arg2) {
    case 0:
        if (*(int32_t *)(ctx + 0xcc) != 0) {
            int32_t qp = *(int16_t *)(ctx + 0x20);

            if (qp < *(int16_t *)(ctx + 0x18)) {
                qp = *(int16_t *)(ctx + 0x18);
            }
            if (*(int16_t *)(ctx + 0x1a) < qp) {
                qp = *(int16_t *)(ctx + 0x1a);
            }

            {
                int32_t value = rc_IIIi((uint32_t)*(int32_t *)(ctx + 0xcc), *(int16_t *)(ctx + 0xdc), qp,
                                        *(int32_t *)(ctx + 0xfc), *(int16_t *)(ctx + 0x22));

                if (value == 0 || value / arg3 >= 0x1f5) {
                    *(int32_t *)(ctx + 0xb4) = 2;
                } else {
                    *(int32_t *)(ctx + 0xb4) =
                        (int32_t)(((uint64_t)1000U * (uint64_t)(uint32_t)arg3) / (uint64_t)(uint32_t)value);
                }
            }
            result = (int32_t)rc_llli(*(int32_t *)(ctx + 0xc8), *(int16_t *)(ctx + 0xd8), arg3, arg4,
                                      (uint32_t)*(int32_t *)(ctx + 0xf8), (uint32_t)*(int32_t *)(ctx + 0x13c),
                                      (uint32_t)*(int32_t *)(ctx + 0x138));
            *(int32_t *)(ctx + 0xf8) = result;
            return result;
        }
        return 2;

    case 1: {
        int32_t var_28 = *(int32_t *)(ctx + 0x134);
        int32_t var_2c = *(int32_t *)(ctx + 0x13c);

        if (*(int32_t *)(ctx + 0xcc) != 0) {
            *(int32_t *)(ctx + 0xfc) =
                (int32_t)rc_llli(*(int32_t *)(ctx + 0xcc), *(int16_t *)(ctx + 0xdc), arg3, arg4,
                                 (uint32_t)*(int32_t *)(ctx + 0xfc), (uint32_t)var_2c, (uint32_t)var_28);
        }

        if (*(int32_t *)(ctx + 0xd0) != 0) {
            int32_t qp = *(int16_t *)(ctx + 0x20) - *(int32_t *)(ctx + 0xa4);

            if (qp < *(int16_t *)(ctx + 0x18)) {
                qp = *(int16_t *)(ctx + 0x18);
            }
            if (*(int16_t *)(ctx + 0x1a) < qp) {
                qp = *(int16_t *)(ctx + 0x1a);
            }

            result = rc_IIIi((uint32_t)*(int32_t *)(ctx + 0xd0), *(int16_t *)(ctx + 0xe0), qp,
                             *(int32_t *)(ctx + 0x100), *(int16_t *)(ctx + 0x22));
            if (*(int32_t *)(ctx + 0xc4) == 2) {
                if (result == 0 || result / arg3 >= 0x1f5) {
                    *(int32_t *)(ctx + 0xb0) = 0x7a120;
                } else {
                    *(int32_t *)(ctx + 0xb0) =
                        (int32_t)(((uint64_t)1000U * (uint64_t)(uint32_t)result) / (uint64_t)(uint32_t)arg3);
                }
            }
        }

        result = 3;
        if (*(int32_t *)(ctx + 0xd4) != 0 && *(int32_t *)(ctx + 0xc4) == 3) {
            int32_t qp = *(int16_t *)(ctx + 0x20) - *(int32_t *)(ctx + 0xac);

            if (qp < *(int16_t *)(ctx + 0x18)) {
                qp = *(int16_t *)(ctx + 0x18);
            }
            if (*(int16_t *)(ctx + 0x1a) < qp) {
                qp = *(int16_t *)(ctx + 0x1a);
            }

            {
                int32_t value = rc_IIIi((uint32_t)*(int32_t *)(ctx + 0xd4), *(int16_t *)(ctx + 0xe4), qp,
                                        *(int32_t *)(ctx + 0x104), *(int16_t *)(ctx + 0x22));

                if (arg3 == 0) {
                    __builtin_trap();
                }
                if (value / arg3 < 0x1f5) {
                    result = (int32_t)(((uint64_t)1000U * (uint64_t)(uint32_t)value) /
                                       (uint64_t)(uint32_t)arg3);
                    if ((uint32_t)result < 0x3e9) {
                        result = 0x3e8;
                    }
                } else {
                    result = 0x7a120;
                }
                *(int32_t *)(ctx + 0xb8) = result;
            }
        }

        if (*(int32_t *)(ctx + 0xc8) != 0) {
            int32_t qp = *(int16_t *)(ctx + 0x20) + *(int32_t *)(ctx + 0xa8);

            if (qp < *(int16_t *)(ctx + 0x18)) {
                qp = *(int16_t *)(ctx + 0x18);
            }
            if (*(int16_t *)(ctx + 0x1a) < qp) {
                qp = *(int16_t *)(ctx + 0x1a);
            }

            {
                int32_t value = rc_IIIi((uint32_t)*(int32_t *)(ctx + 0xc8), *(int16_t *)(ctx + 0xd8), qp,
                                        *(int32_t *)(ctx + 0xf8), *(int16_t *)(ctx + 0x22));

                if (value == 0 || arg3 / value >= 0x1f5) {
                    result = 2;
                    *(int32_t *)(ctx + 0xb4) = 2;
                } else {
                    result = (int32_t)(((uint64_t)1000U * (uint64_t)(uint32_t)arg3) /
                                       (uint64_t)(uint32_t)value);
                    *(int32_t *)(ctx + 0xb4) = result;
                }
            }
        }
        return result;
    }

    case 2:
        if (*(int32_t *)(ctx + 0xcc) != 0) {
            int32_t qp = *(int16_t *)(ctx + 0x20);

            if (qp < *(int16_t *)(ctx + 0x18)) {
                qp = *(int16_t *)(ctx + 0x18);
            }
            if (*(int16_t *)(ctx + 0x1a) < qp) {
                qp = *(int16_t *)(ctx + 0x1a);
            }

            {
                int32_t value = rc_IIIi((uint32_t)*(int32_t *)(ctx + 0xcc), *(int16_t *)(ctx + 0xdc), qp,
                                        *(int32_t *)(ctx + 0xfc), *(int16_t *)(ctx + 0x22));

                if (value == 0 || arg3 / value >= 0x1f5) {
                    *(int32_t *)(ctx + 0xb0) = 0x7a120;
                } else {
                    *(int32_t *)(ctx + 0xb0) =
                        (int32_t)(((uint64_t)1000U * (uint64_t)(uint32_t)arg3) / (uint64_t)(uint32_t)value);
                }
            }
        }

        result = 3;
        if (*(uint8_t *)(ctx + 0x12d) != 0 || *(uint32_t *)(ctx + 0x2c) < 2) {
            if (*(int32_t *)(ctx + 0xd0) != 0) {
                result = (int32_t)rc_llli(*(int32_t *)(ctx + 0xd0), *(int16_t *)(ctx + 0xe0), arg3, arg4,
                                          (uint32_t)*(int32_t *)(ctx + 0x100),
                                          (uint32_t)*(int32_t *)(ctx + 0x13c),
                                          (uint32_t)*(int32_t *)(ctx + 0x134));
                *(int32_t *)(ctx + 0x100) = result;
            }
        }
        return result;

    case 3:
        if (*(int32_t *)(ctx + 0xcc) != 0) {
            int32_t qp = *(int16_t *)(ctx + 0x20);

            if (qp < *(int16_t *)(ctx + 0x18)) {
                qp = *(int16_t *)(ctx + 0x18);
            }
            if (*(int16_t *)(ctx + 0x1a) < qp) {
                qp = *(int16_t *)(ctx + 0x1a);
            }

            {
                int32_t value = rc_IIIi((uint32_t)*(int32_t *)(ctx + 0xcc), *(int16_t *)(ctx + 0xdc), qp,
                                        *(int32_t *)(ctx + 0xfc), *(int16_t *)(ctx + 0x22));

                if (value == 0 || arg3 / value >= 0x1f5) {
                    result = 0x7a120;
                } else {
                    result = (int32_t)(((uint64_t)1000U * (uint64_t)(uint32_t)arg3) /
                                       (uint64_t)(uint32_t)value);
                    if ((uint32_t)result < 0x3e9) {
                        result = 0x3e8;
                    }
                }
                *(int32_t *)(ctx + 0xb8) = result;
            }
        } else {
            result = 3;
        }

        if (*(int32_t *)(ctx + 0xd4) != 0) {
            result = (int32_t)rc_llli(*(int32_t *)(ctx + 0xd4), *(int16_t *)(ctx + 0xe4), arg3, arg4,
                                      (uint32_t)*(int32_t *)(ctx + 0x104), (uint32_t)*(int32_t *)(ctx + 0x13c),
                                      (uint32_t)*(int32_t *)(ctx + 0x134));
            *(int32_t *)(ctx + 0x104) = result;
        }
        return result;

    default:
        return __assert("0", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_rate_ctrl/RateCtrl_16.c",
                        0x1077, "l01i", &_gp);
    }
}

/* stock name: o11i */
int32_t rc_o11i(void *arg1, void *arg2, void *arg3, int32_t arg4, int32_t *arg5)
{
    char *ctx = *(char **)((char *)arg1 + 0x30);
    int32_t result = (int32_t)rc_IIIo((int32_t *)(ctx + 0x48), arg4);
    int32_t frame_type = *(int32_t *)((char *)arg2 + 0x10);
    int32_t chosen_type = 2;
    int32_t total;
    int32_t percent_b;

    *arg5 = result;

    if (frame_type != 7) {
        total = *(int32_t *)((char *)arg3 + 0x24) + (*(int32_t *)((char *)arg3 + 0x28) << 2) +
                (*(int32_t *)((char *)arg3 + 0x2c) << 4) + (*(int32_t *)((char *)arg3 + 0x30) << 6);
        if (total == 0) {
            __builtin_trap();
        }

        percent_b = *(int32_t *)((char *)arg3 + 0x1c) * 100 / total;
        chosen_type = (percent_b < 0x51) ? frame_type : 2;
        if (*(uint8_t *)(ctx + 0x140) != 0 && (*(int32_t *)((char *)arg2 + 4) & 0x80) != 0) {
            chosen_type = 3;
        }
    }

    if (*(uint8_t *)((char *)arg2 + 0x2c) == 0 && result >= 0) {
        rc_iili(arg1, arg2, (int16_t)(*(int32_t *)((char *)arg3 + 0x3c) - *(int8_t *)((char *)arg2 + 0x25)));
    }

    {
        int32_t qp = *(int16_t *)(ctx + 0x20) + rc_loli_isra_4(ctx, chosen_type);

        if (qp < *(int16_t *)(ctx + 0x18)) {
            qp = *(int16_t *)(ctx + 0x18);
        }
        if (*(int16_t *)(ctx + 0x1a) < qp) {
            qp = *(int16_t *)(ctx + 0x1a);
        }

        if (percent_b < 0x5f) {
            rc_l01i(arg1, chosen_type, arg4, qp);
        }

        if (chosen_type == frame_type || chosen_type == 3) {
            *(int32_t *)(ctx + ((frame_type + 0x40) << 2) + 0x0c) = 0;
        } else {
            *(int32_t *)(ctx + (frame_type << 2) + 0x10c) += 1;
        }

        if (*(int32_t *)(ctx + 0xc0) == 1 && chosen_type == 1) {
            int32_t prev = *(int32_t *)(ctx + 0xf0);

            if (prev + 0x3c < percent_b || percent_b < prev - 0x3c) {
                int32_t avg = arg4 + *(int32_t *)(ctx + 0xf4);

                *(int32_t *)(ctx + 0xcc) = ((avg >> 31) + avg) >> 1;
            }
        }

        *(int32_t *)(ctx + (chosen_type << 2) + 0xc8) = arg4;
        *(int32_t *)(ctx + (chosen_type << 2) + 0xd8) = qp;
        *(int32_t *)(ctx + 0xf4) = arg4;

        {
            int32_t *slot = (int32_t *)(ctx + (frame_type << 2) + 0x10c);

            if (*slot >= 3) {
                *(int32_t *)(ctx + (frame_type << 2) + 0xc8) = arg4;
                *(int32_t *)(ctx + (frame_type << 2) + 0xd8) = qp;
                *slot = 0;
            }
        }

        if (frame_type == 1) {
            *(int32_t *)(ctx + 0xe8) += qp;
        } else if (frame_type == 2) {
            *(int32_t *)(ctx + 0xe8) = 0;
        }

        result = *(int32_t *)((char *)arg2 + 4) & 2;
        if (result != 0) {
            result = *(int32_t *)(ctx + 0xc0);
            *(int32_t *)(ctx + 0xc0) = frame_type;
            *(int32_t *)(ctx + 0xc4) = result;
        }
        return result;
    }
}

/* stock name: oiii */
int32_t rc_oiii(void *arg1, uint32_t arg2, uint32_t arg3)
{
    int32_t *ctx = *(int32_t **)((char *)arg1 + 0x24);
    int32_t (*alloc_fn)(void *, int32_t) = *(int32_t (**)(void *, int32_t))(*(intptr_t *)ctx + 4);

    if (alloc_fn == NULL) {
        *(int32_t *)((char *)arg1 + 0x28) = 0;
        return 0;
    }

    {
        void *buf = (void *)(intptr_t)alloc_fn(ctx, 0x148);

        *(intptr_t *)((char *)arg1 + 0x28) = (intptr_t)buf;
        if (buf == NULL) {
            return 0;
        }

        {
            char *virt = (char *)(*(void *(**)(void *, void *))(*(intptr_t *)*(void **)((char *)arg1 + 0x24) + 0xc))(
                *(void **)((char *)arg1 + 0x24), buf);
            int16_t qp_range = (arg2 == 0) ? 4 : 0x14;

            *(intptr_t *)((char *)arg1 + 0x30) = (intptr_t)virt;
            *(intptr_t *)((char *)arg1 + 0x04) = (intptr_t)rc_lI1i;
            *(intptr_t *)((char *)arg1 + 0x00) = (intptr_t)rc_il0i;
            *(intptr_t *)((char *)arg1 + 0x08) = (intptr_t)rc_o11i;
            *(intptr_t *)((char *)arg1 + 0x10) = (intptr_t)rc_Il1i;
            *(intptr_t *)((char *)arg1 + 0x14) = (intptr_t)rc_l11i;
            *(intptr_t *)((char *)arg1 + 0x18) = (intptr_t)rc_OOOI;
            *(intptr_t *)((char *)arg1 + 0x1c) = (intptr_t)rc_oOOI;
            *(int32_t *)(virt + 0x00) = 1;
            *(int16_t *)(virt + 0x18) = 0;
            *(int16_t *)(virt + 0x1a) = 0x33;
            *(uint8_t *)(virt + 0x12c) = (uint8_t)arg2;
            *(int16_t *)(virt + 0x22) = qp_range;
            *(uint8_t *)(virt + 0x12e) = 1;
            *(uint8_t *)(virt + 0x12f) = (uint8_t)arg3;
            return 1;
        }
    }
}
