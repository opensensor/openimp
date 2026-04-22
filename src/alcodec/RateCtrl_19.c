#include <stdint.h>

extern char _gp;
extern int32_t __assert(const char *expression, const char *file, int32_t line,
                        const char *function, void *caller);

/* Placement:
 * - IlOI/i0OI/I0OI/l0OI/O1OI/i1OI/I1OI/llOI/o1OI @ RateCtrl_19.c
 */

/* stock name: IlOI */
int32_t rc_IlOI(void);
/* stock name: i0OI */
int32_t *rc_i0OI(int32_t *arg1);
/* stock name: I0OI */
int32_t *rc_I0OI(int32_t *arg1);
/* stock name: l0OI */
int32_t rc_l0OI(void);
/* stock name: O1OI */
void *rc_O1OI(void *arg1, void *arg2, int16_t *arg3);
/* stock name: i1OI */
uint32_t rc_i1OI(void *arg1, uint32_t *arg2);
/* stock name: I1OI */
uint32_t rc_I1OI(void *arg1, uint32_t *arg2);
/* stock name: llOI */
char *rc_llOI(void *arg1, void *arg2);
/* stock name: o1OI */
uint32_t rc_o1OI(void *arg1, uint32_t *arg2);

int32_t rc_IlOI(void)
{
    return 0;
}

int32_t *rc_i0OI(int32_t *arg1)
{
    *arg1 = 0;
    return arg1;
}

int32_t *rc_I0OI(int32_t *arg1)
{
    *arg1 = 0xa;
    return arg1;
}

int32_t rc_l0OI(void)
{
    return 0;
}

void *rc_O1OI(void *arg1, void *arg2, int16_t *arg3)
{
    char *result = *(char **)((char *)arg1 + 0x30);
    int32_t mode = *(int32_t *)((char *)arg2 + 0x10);
    int32_t cur = *(int32_t *)(result + 4);
    int32_t next = (int32_t)*(int8_t *)((char *)arg2 + 0x25) + cur;
    int16_t clipped;

    if (mode == 1) {
        next += (int32_t)*(int16_t *)(result + 0x20);
    } else if (mode == 0) {
        next += (int32_t)*(int16_t *)(result + 0x22);
    }

    clipped = *(int16_t *)(result + 0x14);
    if (*(int32_t *)(result + 0x14) < next) {
        int32_t high = *(int32_t *)(result + 0x10);

        if (next >= high) {
            high = next;
        }
        clipped = (int16_t)high;
    }
    *arg3 = clipped;

    if (*(int32_t *)(result + 8) == 0) {
        if (cur < *(int32_t *)(result + 0x14)) {
            *(int32_t *)(result + 0x0c) = -1;
            *(int32_t *)(result + 4) = cur - 1;
            return result;
        }
        if (*(int32_t *)(result + 0x10) < cur) {
            *(int32_t *)(result + 0x0c) = 1;
            *(int32_t *)(result + 4) = cur + 1;
            return result;
        }
    }

    *(int32_t *)(result + 4) = cur + *(int32_t *)(result + 0x0c);
    return result;
}

uint32_t rc_i1OI(void *arg1, uint32_t *arg2)
{
    char *ctx = *(char **)((char *)arg1 + 0x30);
    uint32_t result = (uint32_t)(((uint64_t)((uint32_t)*(int32_t *)(ctx + 0x1c) >> 1) * 90000ULL) /
                                 (uint32_t)*(int32_t *)(ctx + 0x18));

    *arg2 = result;
    return result;
}

uint32_t rc_I1OI(void *arg1, uint32_t *arg2)
{
    uint32_t result = (uint32_t)*(int32_t *)(*(char **)((char *)arg1 + 0x30) + 0x1c) >> 1;

    *arg2 = result;
    return result;
}

char *rc_llOI(void *arg1, void *arg2)
{
    char *result = *(char **)((char *)arg1 + 0x30);
    int32_t min_qp = (int32_t)*(int16_t *)((char *)arg2 + 0x1a);
    int32_t max_qp = (int32_t)*(int16_t *)((char *)arg2 + 0x1c);

    *(int32_t *)(result + 0x10) = min_qp;
    *(int32_t *)(result + 0x14) = max_qp;
    if ((uint32_t)*result == 0) {
        int32_t init_qp = (int32_t)*(int16_t *)((char *)arg2 + 0x18);
        int32_t step = (int32_t)*(int16_t *)((char *)arg2 + 0x1e);
        int16_t extra = *(int16_t *)((char *)arg2 + 0x20);
        int16_t clamped_step = step < 0 ? 0 : (int16_t)step;

        if (init_qp < min_qp || max_qp < init_qp) {
            return (char *)(intptr_t)rc_o1OI(
                (void *)(intptr_t)__assert(
                    "IIoi -> iioo <= IIoi -> iIoo && IIoi -> iIoo <= IIoi -> lioo",
                    "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_rate_ctrl/RateCtrl_19.c",
                    0xa0, "llOI", &_gp),
                (uint32_t *)(intptr_t)arg2);
        }

        *(int32_t *)(result + 4) = init_qp;
        *(int32_t *)(result + 0x1c) = *(int32_t *)((char *)arg2 + 8);
        *(int32_t *)(result + 0x18) = *(int32_t *)((char *)arg2 + 0x14);
        *(int16_t *)(result + 0x20) = clamped_step;
        if ((int32_t)extra < 0) {
            extra = 0;
        }
        *(int16_t *)(result + 0x22) = (int16_t)(clamped_step + extra);
        *result = 0;
        return result;
    }

    {
        int32_t qp = *(int32_t *)(result + 4);

        if (qp >= min_qp) {
            if (qp >= max_qp) {
                qp = max_qp;
            }
            min_qp = qp;
        }
        *(int32_t *)(result + 4) = min_qp;
        return result;
    }
}

uint32_t rc_o1OI(void *arg1, uint32_t *arg2)
{
    uint32_t result = (uint32_t)*(int32_t *)(*(char **)((char *)arg1 + 0x30) + 0x1c) >> 1;

    *arg2 = result;
    return result;
}
