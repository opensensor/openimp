#include <stdint.h>
#include <stddef.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

extern char _gp;
extern int32_t __assert(const char *expression, const char *file, int32_t line,
                        const char *function, void *caller);

#define RC15_KMSG(fmt, ...)                                                                       \
    do {                                                                                          \
        int _kfd = open("/dev/kmsg", O_WRONLY | O_NONBLOCK);                                      \
        if (_kfd >= 0) {                                                                          \
            char _buf[256];                                                                       \
            int _n = snprintf(_buf, sizeof(_buf), "libimp/RC15: " fmt "\n", ##__VA_ARGS__);      \
            if (_n > 0) {                                                                         \
                size_t _len = (size_t)_n;                                                         \
                if (_len > sizeof(_buf)) {                                                        \
                    _len = sizeof(_buf);                                                          \
                }                                                                                 \
                (void)write(_kfd, _buf, _len);                                                    \
            }                                                                                     \
            close(_kfd);                                                                          \
        }                                                                                         \
    } while (0)

/* Placement:
 * - IIii/OOoI/l1OI @ RateCtrl_15.c
 * - Ioii/loii helper cluster pending continuation.
 * - l0ii/i0ii/O0ii/Ilii/i1Ii/Ioli/l1OI @ RateCtrl_15.c
 */

uint32_t rc_i0Io(void *arg1, int32_t arg2);
int32_t rc_illi(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5,
                int32_t arg6);
int32_t rc_OOlo(void *arg1);
int32_t rc_l1Io(int32_t *arg1);
int32_t rc_IIIi(uint32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5);
int32_t rc_ooIi(uint32_t arg1, int16_t arg2, int32_t arg3, int32_t arg4, int16_t arg5,
                int16_t arg6);
int32_t Ioii(void *arg1, void *arg2, void *arg3, int32_t arg4, char arg5, int32_t arg6,
             int32_t *arg7, uint32_t *arg8, uint32_t *arg9);
int32_t loii(void *arg1);

static int32_t rc_OOoI_impl(void *arg1, void *arg2, void *arg3, int32_t arg4, char arg5,
                            int32_t arg6);
static int32_t rc_IIii_impl(void *arg1, void *arg2, void *arg3, int32_t arg4, char arg6,
                            int32_t arg7);

/* stock name: l0ii */
int32_t rc_l0ii(void *arg1, char arg2)
{
    char *ctx = *(char **)((char *)arg1 + 0x30);
    int32_t factor = 3;

    if ((uint8_t)arg2 != 0 && *(uint8_t *)(ctx + 0x118) != 0 &&
        *(int16_t *)(ctx + 0x20) >= *(int16_t *)(ctx + 0x1c)) {
        factor = 0;
    }
    return factor * *(int32_t *)(ctx + 0x130);
}

/* stock name: i0ii */
uint32_t rc_i0ii(void *arg1, void *arg2, int32_t *arg3, int32_t arg4, int32_t arg5, int32_t arg6)
{
    char *ctx = *(char **)((char *)arg1 + 0x30);
    int32_t mode;
    uint32_t result;

    rc_i0Io(ctx + 0x48, arg4 + *arg3);
    mode = *(int32_t *)(ctx + 0xc0);
    if (mode == 1 && *(int32_t *)((char *)arg2 + 0x10) == mode) {
        int32_t prev = *(int32_t *)(ctx + 0xf0);

        if (arg5 > prev + 0x3c || arg5 < prev - 0x3c) {
            *arg3 = *(int32_t *)(ctx + 0xcc);
        }
    }

    *(int32_t *)(ctx + 0xf0) = arg5;
    if (*(int32_t *)((char *)arg2 + 0x10) == 2) {
        mode = 1;
    } else {
        mode = *(int32_t *)(ctx + 0x108) + 1;
    }
    *(int32_t *)(ctx + 0x108) = mode;

    if ((*(int32_t *)((char *)arg2 + 4) & 4) != 0) {
        int32_t qp = *(int16_t *)(ctx + 0x1c);
        int32_t base = *(int16_t *)(ctx + 0x20);
        int32_t level = *(int32_t *)(ctx + 0x8c);

        *(int32_t *)(ctx + 0xe4) = qp;
        *(int32_t *)(ctx + 0xd8) = qp;
        *(int32_t *)(ctx + 0xdc) = qp;
        *(int32_t *)(ctx + 0xd4) = level;
        *(int32_t *)(ctx + 0xc8) = level;
        *(int32_t *)(ctx + 0xcc) = level;
        if (base < qp) {
            *(int16_t *)(ctx + 0x20) = (int16_t)qp;
        }
    }

    result = (uint32_t)*(uint8_t *)(ctx + 0x143);
    if (result == 0 || arg5 >= 0x60) {
        return result;
    }
    if (arg5 < 0x56) {
        return 0;
    }
    return (uint32_t)(arg6 < 0x46);
}

/* stock name: O0ii */
uint32_t rc_O0ii(void *arg1, int32_t arg2)
{
    char *ctx = *(char **)((char *)arg1 + 0x30);
    int32_t step = 0;
    int32_t cur_qp = *(int16_t *)(ctx + 0x20);
    int32_t down_room = cur_qp - *(int16_t *)(ctx + 0x18);
    int32_t up_room = *(int16_t *)(ctx + 0x1a) - cur_qp;
    uint32_t delta;
    int32_t drift;
    uint32_t value;
    int32_t scale = *(int32_t *)(ctx + 0x100);

    if (arg2 < 0x51) {
        step = 1;
        if (arg2 < 0x3d) {
            step = 2;
            if (arg2 < 0x29) {
                step = 3;
                if (arg2 < 0x15) {
                    step = 4;
                }
            }
        }
    }

    delta = (uint32_t)(step * *(int32_t *)(ctx + 0x130));
    if (up_room >= down_room) {
        up_room = down_room;
    }
    if ((int32_t)delta > up_room) {
        delta = (uint32_t)up_room;
    }

    drift = *(int32_t *)(ctx + 0xa4) - (int32_t)delta;
    if (drift > 0) {
        value = (uint32_t)*(int32_t *)(ctx + 0xb0);
        while (drift-- > 0) {
            value = (uint32_t)(((uint64_t)value * 80ULL) / (uint64_t)(uint32_t)scale);
        }
        *(int32_t *)(ctx + 0xb0) = (int32_t)value;
    } else if (drift < 0) {
        value = (uint32_t)*(int32_t *)(ctx + 0xb0);
        while (drift++ < 0) {
            value = (uint32_t)(((uint64_t)value * (uint64_t)(uint32_t)scale) / 80ULL);
        }
        *(int32_t *)(ctx + 0xb0) = (int32_t)value;
    } else {
        value = (uint32_t)*(int32_t *)(ctx + 0xb0);
    }

    *(int32_t *)(ctx + 0xa4) = (int32_t)delta;
    return value;
}

/* stock name: Ilii */
uint32_t rc_Ilii(void *arg1, int32_t arg2, int32_t *arg3, uint32_t *arg4, uint32_t *arg5,
                 uint32_t *arg6, uint32_t *arg7)
{
    int32_t total = *(int32_t *)((char *)arg1 + 0x24) +
                    (*(int32_t *)((char *)arg1 + 0x28) << 2) +
                    (*(int32_t *)((char *)arg1 + 0x2c) << 4) +
                    (*(int32_t *)((char *)arg1 + 0x30) << 6);
    uint32_t result;

    *arg3 = total;
    if (total == 0) {
        RC15_KMSG("Ilii zero-total fallback stats=%p arg2=%d s20=%d s1c=%d s14=%d s18=%d s24=%d s28=%d s2c=%d s30=%d total=%d",
                  arg1, arg2, *(int32_t *)((char *)arg1 + 0x20), *(int32_t *)((char *)arg1 + 0x1c),
                  *(int32_t *)((char *)arg1 + 0x14), *(int32_t *)((char *)arg1 + 0x18),
                  *(int32_t *)((char *)arg1 + 0x24), *(int32_t *)((char *)arg1 + 0x28),
                  *(int32_t *)((char *)arg1 + 0x2c), *(int32_t *)((char *)arg1 + 0x30), total);
        *arg4 = 0;
        *arg5 = 0;
        *arg6 = 0;
        *arg7 = 0;
        return 0;
    }
    if (arg2 == 0) {
        RC15_KMSG("Ilii zero-bits trap stats=%p total=%d", arg1, total);
        __builtin_trap();
    }
    *arg4 = (uint32_t)*(int32_t *)((char *)arg1 + 0x20) * 100U / (uint32_t)total;
    *arg5 = (uint32_t)*(int32_t *)((char *)arg1 + 0x1c) * 100U / (uint32_t)total;
    *arg6 = (uint32_t)*(int32_t *)((char *)arg1 + 0x14) * 100U / (uint32_t)arg2;
    result = (uint32_t)*(int32_t *)((char *)arg1 + 0x18) * 25U / (uint32_t)total;
    *arg7 = result;
    return result;
}

/* stock name: i1Ii */
int32_t rc_i1Ii(void *arg1, int32_t arg2, int32_t arg3, int32_t *arg4, int32_t *arg5,
                int32_t *arg6)
{
    char *ctx = *(char **)((char *)arg1 + 0x30);
    int32_t qp;

    qp = *(int16_t *)(ctx + 0x20) + arg2 + *(int32_t *)(ctx + 0xa8);
    if (qp < 0) {
        qp = 0;
    }
    *arg4 = rc_illi(*(int32_t *)(ctx + 0xc8), *(int32_t *)(ctx + 0xd8), qp,
                    *(int32_t *)(ctx + 0xf8), *(int16_t *)(ctx + 0x22), arg3);

    qp = *(int16_t *)(ctx + 0x20) + arg2;
    if (qp < 0) {
        qp = 0;
    }
    *arg5 = rc_illi(*(int32_t *)(ctx + 0xcc), *(int32_t *)(ctx + 0xdc), qp,
                    *(int32_t *)(ctx + 0xfc), *(int16_t *)(ctx + 0x22), arg3);

    qp = *(int16_t *)(ctx + 0x20) + arg2;
    if (qp < 0) {
        qp = 0;
    }
    *arg6 = rc_illi(*(int32_t *)(ctx + 0xd4), *(int32_t *)(ctx + 0xe4), qp,
                    *(int32_t *)(ctx + 0x104), *(int16_t *)(ctx + 0x22), arg3);
    return *arg6;
}

/* stock name: Ioli */
int32_t rc_Ioli(void *arg1, int32_t arg2)
{
    char *ctx = *(char **)((char *)arg1 + 0x30);
    int32_t qp = *(int16_t *)(ctx + 0x20);
    int32_t target = *(int16_t *)(ctx + 0x1e);

    if (target < qp) {
        if (arg2 < 0) {
            return -*(int32_t *)(ctx + 0x130);
        }
        return arg2;
    }
    if (qp < target) {
        return *(int32_t *)(ctx + 0x130);
    }
    if (qp == target) {
        return 0;
    }
    return arg2;
}

/* stock name: l1OI */
int32_t rc_l1OI(void *arg1, int16_t arg2)
{
    char *ctx = *(char **)((char *)arg1 + 0x30);
    int16_t prev = *(int16_t *)(ctx + 0x20);

    *(int16_t *)(ctx + 0x20) = (int16_t)(prev + arg2);
    return prev;
}

/* stock name: Ioii */
int32_t Ioii(void *arg1, void *arg2, void *arg3, int32_t arg4, char arg5, int32_t arg6,
             int32_t *arg7, uint32_t *arg8, uint32_t *arg9)
{
    char *ctx = *(char **)((char *)arg1 + 0x30);
    int32_t total = 0;
    uint32_t weight0 = 0;
    uint32_t weight1 = 0;
    uint32_t weight2 = 0;
    uint32_t weight3 = 0;
    int32_t frame_delta = arg4;
    int32_t trend;

    if (arg7 != NULL) {
        *arg7 = 0;
    }
    if (arg8 != NULL) {
        *arg8 = 0;
    }
    if (arg9 != NULL) {
        *arg9 = 0;
    }

    rc_Ilii(arg3, arg4, &total, &weight0, &weight1, &weight2, &weight3);

    if (*(int32_t *)((char *)arg2 + 0x10) == 1 && *(uint8_t *)(ctx + 0x12e) != 0) {
        rc_O0ii(arg1, (int32_t)weight1);
    }

    trend = rc_i0ii(arg1, arg2, &frame_delta, arg6, (int32_t)weight0, (int32_t)weight2);
    if (*(uint8_t *)(ctx + 0x12d) != 0) {
        trend = 0;
    }
    if ((uint8_t)arg5 != 0) {
        trend = rc_l0ii(arg1, trend);
    }

    if (arg7 != NULL) {
        *arg7 = trend;
    }
    if (arg8 != NULL) {
        *arg8 = (uint32_t)*(int32_t *)(ctx + 0x84);
    }
    if (arg9 != NULL) {
        *arg9 = (uint32_t)*(int32_t *)(ctx + 0x84);
    }
    return trend;
}

/* stock name: loii */
int32_t loii(void *arg1)
{
    char *ctx = *(char **)((char *)arg1 + 0x30);
    int32_t result_1 = *(int16_t *)(ctx + 0x20);
    int32_t result = *(int16_t *)(ctx + 0x18);

    if (result_1 >= result) {
        result = *(int16_t *)(ctx + 0x1a);
        if (result_1 < result) {
            result = result_1;
        }
    }

    *(int16_t *)(ctx + 0x20) = (int16_t)result;
    return result;
}

/* stock name: OOoI */
static int32_t rc_OOoI_impl(void *arg1, void *arg2, void *arg3, int32_t arg4, char arg5,
                            int32_t arg6)
{
    int32_t delta = 0;
    uint32_t low = 0;
    uint32_t high = 0;

    Ioii(arg1, arg2, arg3, arg4, arg5, arg6, &delta, &low, &high);
    rc_l1OI(arg1, (int16_t)delta);
    return loii(arg1);
}

/* stock name: OOoI */
int32_t rc_OOoI(void *arg1, void *arg2, void *arg3, int32_t arg4, int32_t *arg5)
{
    if (arg5 != NULL) {
        *arg5 = 0;
    }
    return rc_OOoI_impl(arg1, arg2, arg3, arg4, 0, arg4);
}

/* stock name: IIii */
static int32_t rc_IIii_impl(void *arg1, void *arg2, void *arg3, int32_t arg4, char arg6,
                            int32_t arg7)
{
    char *ctx = *(char **)((char *)arg1 + 0x30);
    int32_t total = 0;
    uint32_t weight0 = 0;
    uint32_t weight1 = 0;
    uint32_t weight2 = 0;
    uint32_t weight3 = 0;
    int32_t frame_delta = arg4;
    int32_t trend;
    int32_t cur_qp;

    RC15_KMSG("IIii entry rc=%p ctx=%p frm=%p stats=%p bits=%d frame_type=%d flags4=0x%x flags2c=0x%x",
              arg1, ctx, arg2, arg3, arg4, *(int32_t *)((char *)arg2 + 0x10),
              *(int32_t *)((char *)arg2 + 4), *(int32_t *)((char *)arg2 + 0x2c));
    rc_Ilii(arg3, arg4, &total, &weight0, &weight1, &weight2, &weight3);
    RC15_KMSG("IIii post-Ilii rc=%p total=%d w0=%u w1=%u w2=%u w3=%u",
              arg1, total, weight0, weight1, weight2, weight3);
    if (*(int32_t *)((char *)arg2 + 0x10) == 2 || (*(int32_t *)((char *)arg2 + 4) & 2) == 0) {
        trend = (int32_t)weight3;
    } else {
        if (*(uint8_t *)(ctx + 0x12e) != 0) {
            rc_O0ii(arg1, (int32_t)weight1);
        }
        trend = (int32_t)weight3;
    }

    if (rc_i0ii(arg1, arg2, &frame_delta, arg7, (int32_t)weight0, trend) != 0 &&
        *(uint8_t *)(ctx + 0x12d) == 0) {
        if ((uint8_t)arg6 != 0) {
            trend = rc_l0ii(arg1, 1);
        } else if (arg7 != 0) {
            cur_qp = *(int16_t *)(ctx + 0x20);
            if (*(uint8_t *)(ctx + 0x12d) != 0) {
                trend = 0;
            } else if (*(int16_t *)(ctx + 0x1e) < cur_qp) {
                trend = -2 * *(int32_t *)(ctx + 0x130);
            } else {
                trend = 0;
            }
        } else {
            trend = -2 * *(int32_t *)(ctx + 0x130);
        }
    } else if ((*(int32_t *)((char *)arg2 + 4) & 2) == 0 ||
               (uint32_t)*(uint8_t *)(ctx + 0x2c) < 2) {
        trend = 1;
    } else {
        int32_t hist = rc_OOlo(ctx + 0x48);
        uint32_t target_size = (uint32_t)*(int32_t *)(ctx + 0x8c);
        int32_t frame_type = *(int32_t *)((char *)arg2 + 0x10);
        int32_t base = *(int32_t *)(ctx + 0x88);
        uint32_t hier = (uint32_t)*(uint8_t *)(ctx + 0x140);
        uint32_t frame_count;
        int32_t mode_flags;
        int32_t window = 1;
        int32_t avg0 = 0;
        int32_t avg1 = 0;
        int32_t avg2 = 0;
        int32_t target_qp;
        int32_t diff_metric;

        if (frame_type != 2 && (int32_t)weight1 >= 0x51 && hist < frame_delta) {
            int32_t bound = hist * 3;
            int16_t qp =
                (int16_t)rc_ooIi((uint32_t)frame_delta, *(int16_t *)(ctx + 0x20),
                                 (bound >= 0 ? bound : bound + 3) >> 2, *(int32_t *)(ctx + 0x100),
                                 *(int16_t *)(ctx + 0x18), *(int16_t *)(ctx + 0x1a));
            *(int16_t *)(ctx + 0x20) = qp;
        }

        frame_count = (uint32_t)*(uint8_t *)(ctx + 0x2c);
        mode_flags = *(int32_t *)(ctx + 0x28);
        if (hier == 0) {
            if (frame_count >= 2 && (mode_flags & 8) == 0) {
                int32_t slope;
                int32_t refs;
                int32_t tail;
                int32_t budget;
                uint32_t predicted;

                if (frame_type == 2) {
                    if (*(uint8_t *)(ctx + 0x12d) != 0) {
                        uint32_t scaled =
                            (uint32_t)(((uint64_t)(uint32_t)*(int32_t *)(ctx + 0x9c) *
                                        (uint64_t)(frame_count * (uint32_t)*(int32_t *)(ctx + 8))) /
                                       (uint64_t)(uint32_t)*(int32_t *)(ctx + 4));

                        if (frame_count == 1) {
                            __builtin_trap();
                        }
                        slope = ((int32_t)scaled - frame_delta) / ((int32_t)frame_count - 1);
                        *(int32_t *)(ctx + 0x90) = slope;
                    } else {
                        slope = *(int32_t *)(ctx + 0x90);
                    }
                } else {
                    slope = *(int32_t *)(ctx + 0x90);
                }

                if (*(uint8_t *)(ctx + 0x2e) == 0xff) {
                    __builtin_trap();
                }
                refs = (int32_t)frame_count / ((int32_t)(uint8_t)ctx[0x2e] + 1) *
                       (int32_t)(uint8_t)ctx[0x2e];
                tail = (int32_t)frame_count - refs - 1;
                budget = refs * *(int32_t *)(ctx + 0xb4) + tail * 1000;

                if (*(uint8_t *)(ctx + 0x12d) != 0) {
                    int32_t denom = ((int32_t)frame_count - 1) * 0x7c;

                    predicted = (uint32_t)(((uint64_t)(uint32_t)(((denom + (int32_t)frame_count - 1) << 3)) *
                                            (uint64_t)(uint32_t)slope) /
                                           (uint64_t)(uint32_t)budget);
                    if (frame_type == 1) {
                        int32_t qp_ref = *(int32_t *)(ctx + 0xe0);

                        if (predicted != 0 && (int32_t)predicted > frame_type) {
                            target_size = predicted;
                        } else {
                            target_size = (uint32_t)frame_type;
                        }
                        weight0 = (uint32_t)rc_IIIi((uint32_t)*(int32_t *)(ctx + 0xd0),
                                                    qp_ref & 0xffff,
                                                    *(uint16_t *)(ctx + 0x20) -
                                                        *(uint32_t *)(ctx + 0xa4),
                                                    *(int32_t *)(ctx + 0x100),
                                                    *(int16_t *)(ctx + 0x1a) - qp_ref);
                    } else {
                        if (frame_type == 0) {
                            target_size =
                                (uint32_t)(((uint64_t)(uint32_t)predicted *
                                            (uint64_t)(uint32_t)*(int32_t *)(ctx + 0xb4)) /
                                           1000ULL);
                        } else if (frame_type == 2) {
                            target_size =
                                (uint32_t)(((uint64_t)(uint32_t)*(int32_t *)(ctx + 0xb0) *
                                            (uint64_t)(uint32_t)predicted) /
                                           1000ULL);
                        } else {
                            __assert("0",
                                     "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_rate_ctrl/RateCtrl_15.c",
                                     0x381, "IIii", &_gp);
                            target_size = 1;
                        }
                        if (target_size == 0) {
                            target_size = 1;
                        }
                        weight0 = 0;
                    }
                } else {
                    int32_t total_budget = budget + *(int32_t *)(ctx + 0xb0);
                    uint32_t predicted64 =
                        (uint32_t)(((uint64_t)(uint32_t)slope *
                                    (uint64_t)((frame_count << 7) - frame_count * 3 << 3)) /
                                   (uint64_t)(uint32_t)total_budget) +
                        (uint32_t)*(int32_t *)(ctx + 0xbc);

                    if (frame_type == 1) {
                        target_size = (predicted64 != 0 && (int32_t)predicted64 > frame_type)
                                          ? predicted64
                                          : (uint32_t)frame_type;
                        weight0 = 0;
                    } else if (frame_type == 0) {
                        target_size =
                            (uint32_t)(((uint64_t)(uint32_t)predicted64 *
                                        (uint64_t)(uint32_t)*(int32_t *)(ctx + 0xb4)) /
                                       1000ULL);
                        if (target_size == 0) {
                            target_size = 1;
                        }
                        weight0 = 0;
                    } else if (frame_type == 2) {
                        target_size =
                            (uint32_t)(((uint64_t)(uint32_t)predicted64 *
                                        (uint64_t)(uint32_t)*(int32_t *)(ctx + 0xb0)) /
                                       1000ULL);
                        if (target_size == 0) {
                            target_size = 1;
                        }
                        weight0 = 0;
                    } else {
                        __assert("0",
                                 "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_rate_ctrl/RateCtrl_15.c",
                                 0x381, "IIii", &_gp);
                        target_size = 1;
                        weight0 = 0;
                    }

                    if (tail > 0) {
                        int32_t delta = (int32_t)target_size - frame_delta;
                        uint32_t adj =
                            (uint32_t)(((uint64_t)(uint32_t)(delta < 0 ? -delta : delta) *
                                        (uint64_t)(uint32_t)(tail * 1000)) /
                                       (uint64_t)(uint32_t)total_budget);

                        *(int32_t *)(ctx + 0xbc) =
                            (delta < 0) ? -(int32_t)(adj / (uint32_t)tail)
                                        : (int32_t)(adj / (uint32_t)tail);
                    }
                }
            } else {
                target_size = target_size;
                weight0 = 0;
            }
        } else {
            int32_t scale = *(uint8_t *)(ctx + 0x141);
            int32_t mix = *(int32_t *)(ctx + 0xb8);
            uint32_t predicted =
                (uint32_t)(((uint64_t)(uint32_t)(((scale * 0x81) - (scale << 2)) << 3) *
                            (uint64_t)target_size) /
                           (uint64_t)(uint32_t)(mix + (((scale * 0x81) - (scale << 2)) << 3)));

            if (frame_type == 2) {
                target_size =
                    (uint32_t)(((uint64_t)predicted *
                                (uint64_t)(uint32_t)*(int32_t *)(ctx + 0xb0)) /
                               1000ULL);
                if (target_size == 0) {
                    target_size = 1;
                }
            } else if ((*(int32_t *)((char *)arg2 + 4) & 0x80) != 0) {
                target_size =
                    (uint32_t)(((uint64_t)predicted * (uint64_t)(uint32_t)mix) / 1000ULL);
                if (target_size == 0) {
                    target_size = 1;
                }
            } else {
                target_size = (predicted != 0 && (int32_t)predicted > 0) ? predicted : 1;
            }
            weight0 = 0;
        }

        if (frame_type == 1) {
            *(int32_t *)(ctx + 0xec) = (int32_t)target_size;
        }

        if ((mode_flags & 8) != 0) {
            window = *(int32_t *)(ctx + 0x11c);
            diff_metric = window - *(int32_t *)(ctx + 0x128);
            if (diff_metric >= *(int32_t *)(ctx + 0x120)) {
                int32_t clamp = *(int32_t *)(ctx + 0x124);

                if (diff_metric < clamp) {
                    clamp = diff_metric;
                }
                *(int32_t *)(ctx + 0x11c) = clamp;
            }
        } else if (*(int32_t *)(ctx + 0x108) < (int32_t)frame_count) {
            window = (int32_t)frame_count - *(int32_t *)(ctx + 0x108);
        }

        cur_qp = rc_l1Io((int32_t *)(ctx + 0x48));
        rc_i1Ii(arg1, 0, cur_qp, &avg0, &avg1, &avg2);

        if (*(int32_t *)(ctx + 0x28) == 9) {
            target_qp = avg0;
        } else {
            uint32_t slots = (uint32_t)*(uint8_t *)(ctx + 0x2e);

            if (slots != 0) {
                target_qp = ((int32_t)slots * avg0 + avg1) / ((int32_t)slots + 1);
            } else if (hier != 0) {
                uint32_t w = (uint32_t)*(uint8_t *)(ctx + 0x141);

                target_qp = ((int32_t)w * avg1 + avg2) / ((int32_t)w + 1);
            } else {
                target_qp = avg0;
            }
        }

        diff_metric = hist + ((int32_t)target_size - target_qp) * window;
        trend = 0;
        if (frame_delta < (int32_t)target_size) {
            int32_t budget = *(int32_t *)(ctx + 0x9c) / 10;
            int32_t margin = rc_l1Io((int32_t *)(ctx + 0x48)) - base;

            if (budget >= margin) {
                budget = margin;
            }
            if (budget + base < diff_metric) {
                int32_t sel =
                    (hier != 0 && (*(int32_t *)((char *)arg2 + 4) & 0x80) != 0 &&
                     *(int32_t *)((char *)arg2 + 0x10) != 2)
                        ? 3
                        : *(int32_t *)((char *)arg2 + 0x10);
                int32_t rate = *(int32_t *)(ctx + ((sel + 0x3c) << 2) + 8);
                int32_t next = frame_delta;
                int32_t steps = 0;

                do {
                    next = (int32_t)(((uint64_t)(uint32_t)next * (uint64_t)(uint32_t)rate) /
                                     10000ULL);
                    --steps;
                } while ((uint32_t)rate <
                             (uint32_t)(((uint64_t)0x64ULL * 10000ULL) /
                                        (uint64_t)(uint32_t)next) &&
                         steps > -*(int16_t *)(ctx + 0x22));
                frame_delta = next;
                trend = steps;
            }
        } else if ((int32_t)target_size < frame_delta) {
            int32_t budget = *(int32_t *)(ctx + 0x9c) / 10;
            int32_t room = base;

            if (budget < room) {
                room = budget;
            }
            if (diff_metric < base - room) {
                int32_t sel =
                    (hier != 0 && (*(int32_t *)((char *)arg2 + 4) & 0x80) != 0 &&
                     *(int32_t *)((char *)arg2 + 0x10) != 2)
                        ? 3
                        : *(int32_t *)((char *)arg2 + 0x10);
                int32_t rate = *(int32_t *)(ctx + ((sel + 0x3c) << 2) + 8);
                int32_t next = frame_delta;
                int32_t steps = 0;

                while (steps < *(int16_t *)(ctx + 0x22)) {
                    next = rc_IIIi((uint32_t)next, next, next + 1, rate, 1);
                    ++steps;
                    if ((uint32_t)rate >=
                        (uint32_t)(((uint64_t)0x64ULL * 10000ULL) / (uint64_t)(uint32_t)target_size)) {
                        frame_delta = next;
                        trend = steps;
                        break;
                    }
                }
            }
        }

        if ((*(int32_t *)(ctx + 0x28) & 8) == 0) {
            uint32_t depth = (uint32_t)*(uint8_t *)(ctx + 0x2c);

            if (depth >= 2 && *(int32_t *)((char *)arg2 + 0x10) != 2) {
                if (base == *(int32_t *)(ctx + 0x88)) {
                    int32_t hist_now = rc_OOlo(ctx + 0x48);
                    int32_t sixth = cur_qp / 6;

                    if (hist_now < sixth) {
                        trend += *(int32_t *)(ctx + 0x130);
                    } else if (sixth * 5 < hist_now) {
                        trend -= *(int32_t *)(ctx + 0x130);
                    }
                } else {
                    uint32_t slots = (uint32_t)*(uint8_t *)(ctx + 0x2e);
                    uint32_t est = 0;

                    if (*(uint8_t *)(ctx + 0x12d) == 0) {
                        est = (uint32_t)(((uint64_t)(uint32_t)*(int32_t *)(ctx + 0xb0) *
                                          (uint64_t)(uint32_t)avg1) /
                                         (uint64_t)(uint32_t)base);
                    }
                    if (slots != 0) {
                        diff_metric = (int32_t)target_size * window + hist -
                                      ((int32_t)(((uint64_t)(uint32_t)window *
                                                  (uint64_t)(slots * (uint32_t)avg0 + (uint32_t)avg1)) /
                                                 (uint64_t)(slots + 1)));
                    } else if (hier != 0) {
                        uint32_t w = (uint32_t)*(uint8_t *)(ctx + 0x141);

                        diff_metric = (int32_t)target_size * window + hist -
                                      ((int32_t)(((uint64_t)(uint32_t)window *
                                                  (uint64_t)(w * (uint32_t)avg1 + (uint32_t)avg2)) /
                                                 (uint64_t)(w + 1)));
                    }

                    {
                        int32_t quarter = (cur_qp + 3) >> 2;

                        if (diff_metric < (int32_t)est + quarter) {
                            if (trend <= 0) {
                                trend += *(int32_t *)(ctx + 0x130);
                            }
                        } else if (quarter * 3 < diff_metric && trend >= 0) {
                            trend -= *(int32_t *)(ctx + 0x130);
                        }
                    }
                }
            }
        }

        if (*(uint8_t *)(ctx + 0x119) != 0) {
            *(uint8_t *)(ctx + 0x119) = 0;
            if ((int32_t)weight1 < 0x1e && diff_metric > 0) {
                trend = *(int32_t *)(ctx + 0x130);
            } else {
                trend = 0;
            }
        } else if (*(int32_t *)(ctx + 0xc4) == 2) {
            trend = 0;
        }

        if (*(uint8_t *)(ctx + 0x140) != 0 && (*(int32_t *)((char *)arg2 + 4) & 0x80) != 0 &&
            *(int32_t *)((char *)arg2 + 0x10) != 2 &&
            (int32_t)((uint32_t)*(uint8_t *)(ctx + 0x2c) - (uint32_t)*(uint8_t *)(ctx + 0x2e)) >= 3) {
            *(uint8_t *)(ctx + 0x119) = (trend < 0) ? 1 : 0;
            trend = 0;
        }

        trend = rc_Ioli(arg1, trend);
        if (diff_metric >= cur_qp && trend >= 1) {
            trend = 0;
        }
    }

    {
        int32_t limit = *(int16_t *)(ctx + 0x22);

        if (limit < trend) {
            trend = limit;
        }
        if (trend < -limit) {
            trend = -limit;
        }
    }

    cur_qp = *(int16_t *)(ctx + 0x20) + trend;
    if (cur_qp < *(int16_t *)(ctx + 0x18)) {
        cur_qp = *(int16_t *)(ctx + 0x18);
    }
    if (*(int16_t *)(ctx + 0x1a) < cur_qp) {
        cur_qp = *(int16_t *)(ctx + 0x1a);
    }
    *(int16_t *)(ctx + 0x20) = (int16_t)cur_qp;
    RC15_KMSG("IIii exit rc=%p qp=%d trend=%d frame_type=%d total=%d w1=%u w3=%u",
              arg1, cur_qp, trend, *(int32_t *)((char *)arg2 + 0x10), total, weight1, weight3);
    return cur_qp;
}

/* stock name: IIii */
int32_t rc_IIii(void *arg1, void *arg2, void *arg3, int32_t arg4, int32_t *arg5)
{
    if (arg5 != NULL) {
        *arg5 = 0;
    }
    return rc_IIii_impl(arg1, arg2, arg3, arg4, 0, arg4);
}
