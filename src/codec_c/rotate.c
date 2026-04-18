#include <stdint.h>

static uint32_t SRC_WIDTH;
static uint32_t SRC_STRIDE;
static uint32_t DST_WIDTH;
static uint32_t DST_STRIDE;
static uint32_t SRC_WIDTH1;
static uint32_t SRC_STRIDE1;
static uint32_t DST_WIDTH1;
static uint32_t DST_STRIDE1;
static uint32_t SRC_WIDTH_16;
static uint32_t SRC_STRIDE_16;
static uint32_t DST_WIDTH_16;
static uint32_t DST_STRIDE_16;

static void nv12_left_rotate_90_y_plane(uint8_t *dst, const uint8_t *src)
{
    uint32_t src_width = SRC_WIDTH;
    uint32_t src_height = DST_WIDTH;

    for (uint32_t y = 0; y < src_height; ++y) {
        for (uint32_t x = 0; x < src_width; ++x) {
            dst[(src_width - 1U - x) * DST_STRIDE + y] = src[y * SRC_STRIDE + x];
        }
    }
}

static void nv12_right_rotate_90_y_plane(uint8_t *dst, const uint8_t *src)
{
    uint32_t src_width = SRC_WIDTH;
    uint32_t src_height = DST_WIDTH;

    for (uint32_t y = 0; y < src_height; ++y) {
        for (uint32_t x = 0; x < src_width; ++x) {
            dst[x * DST_STRIDE + (src_height - 1U - y)] = src[y * SRC_STRIDE + x];
        }
    }
}

static void nv12_left_rotate_90_uv_tail(uint8_t *dst, const uint8_t *src)
{
    uint32_t src_width = SRC_WIDTH;
    uint32_t src_height = DST_WIDTH;
    uint32_t chroma_height = src_height >> 1;

    for (uint32_t y = 0; y < chroma_height; ++y) {
        for (uint32_t x = 0; x < src_width; x += 2) {
            uint32_t dst_row = (src_width - 2U - x) >> 1;
            uint32_t dst_col = y << 1;
            uint32_t src_off = y * SRC_STRIDE1 + x;
            uint32_t dst_off = dst_row * DST_STRIDE1 + dst_col;
            dst[dst_off] = src[src_off];
            dst[dst_off + 1] = src[src_off + 1];
        }
    }
}

static void nv12_right_rotate_90_uv_tail(uint8_t *dst, const uint8_t *src)
{
    uint32_t src_width = SRC_WIDTH;
    uint32_t src_height = DST_WIDTH;
    uint32_t chroma_height = src_height >> 1;

    for (uint32_t y = 0; y < chroma_height; ++y) {
        for (uint32_t x = 0; x < src_width; x += 2) {
            uint32_t dst_row = x >> 1;
            uint32_t dst_col = (src_height - 2U) - (y << 1);
            uint32_t src_off = y * SRC_STRIDE1 + x;
            uint32_t dst_off = dst_row * DST_STRIDE1 + dst_col;
            dst[dst_off] = src[src_off];
            dst[dst_off + 1] = src[src_off + 1];
        }
    }
}

int32_t nv12_left_rotate_90_block32_uv(char *arg1, char *arg2)
{
    uint32_t src_stride1 = SRC_STRIDE1;
    uint32_t dst_step = DST_STRIDE1 + DST_STRIDE_16;

    for (int32_t src_col = 30, dst_off = 0; src_col >= 0; src_col -= 2, dst_off += 2) {
        for (int32_t row = 0; row < 32; ++row) {
            char *src_row = arg1 + row * (int32_t)src_stride1;
            char *dst_row = arg2 + row * (int32_t)dst_step;
            dst_row[dst_off] = src_row[src_col];
            dst_row[dst_off + 1] = src_row[src_col + 1];
        }
    }

    return 0;
}

int32_t nv12_right_rotate_90_block32_uv(char *arg1, char *arg2)
{
    uint32_t src_stride1 = SRC_STRIDE1;
    uint32_t dst_step = DST_STRIDE1 + DST_STRIDE_16;

    for (int32_t src_col = 0, dst_off = 62; src_col < 32; src_col += 2, dst_off -= 2) {
        for (int32_t row = 0; row < 32; ++row) {
            char *src_row = arg1 + row * (int32_t)src_stride1;
            char *dst_row = arg2 + row * (int32_t)dst_step;
            dst_row[dst_off] = src_row[src_col];
            dst_row[dst_off + 1] = src_row[src_col + 1];
        }
    }

    return 0;
}

uint32_t nv12_rotate_init(uint32_t arg1, uint32_t arg2)
{
    uint32_t v1 = ((int32_t)arg2 >> 31) >> 26;
    uint32_t result = ((arg2 + v1) & 0x3fU) - v1;
    uint32_t v1_1 = arg2 - result;

    SRC_WIDTH = arg1;
    SRC_STRIDE = arg1;
    DST_WIDTH = arg2;
    DST_STRIDE = arg2;
    SRC_WIDTH1 = arg1;
    SRC_STRIDE1 = arg1;
    DST_WIDTH1 = v1_1;
    DST_STRIDE1 = v1_1;
    SRC_WIDTH_16 = arg1;
    SRC_STRIDE_16 = arg1;
    DST_WIDTH_16 = result;
    DST_STRIDE_16 = result;

    return result;
}

int32_t nv12_left_rotate_90(int32_t arg1, void *arg2, void *arg3)
{
    int32_t result_1 = (int32_t)DST_WIDTH;
    uint32_t v0 = SRC_WIDTH;
    int32_t result_4 = result_1 + 0x1f;
    int32_t a1 = (int32_t)(v0 * (uint32_t)result_1);
    int32_t v1 = result_1 < 0 ? 1 : 0;
    int32_t result;

    (void)arg1;

    if ((int32_t)v0 < 0) {
        v0 += 0x1f;
    }

    int32_t v0_1 = (int32_t)v0 >> 5;

    if (v1 == 0) {
        result_4 = result_1;
    }

    int32_t v1_2 = result_4 >> 5;
    uint8_t *v0_3 = (uint8_t *)arg2 + a1;
    uint8_t *v0_5 = (uint8_t *)arg3 + a1;

    if (v1_2 > 0) {
        nv12_left_rotate_90_y_plane((uint8_t *)arg3, (const uint8_t *)arg2);
        result = result_1;

        if (result > 0) {
            int32_t s0_4 = 0;
            do {
                if (v0_1 > 0) {
                    int32_t i_1 = (v0_1 - 1) << 4;
                    int32_t s2_20 = 0;
                    do {
                        int32_t v0_1729 = (int32_t)(DST_WIDTH1 + DST_WIDTH_16) * i_1;
                        int32_t a0_56 = s0_4 * (int32_t)SRC_STRIDE1 + s2_20;
                        i_1 -= 0x10;
                        s2_20 += 0x20;
                        nv12_left_rotate_90_block32_uv((char *)(v0_3 + a0_56), (char *)(v0_5 + v0_1729 + (s0_4 << 1)));
                    } while (i_1 != -0x10);
                }

                result = result_1 << 5;
                s0_4 += 0x20;
            } while (s0_4 != result);
        }
    } else {
        int32_t result_3 = result_1 + 0x3f;
        if (v1 == 0) {
            result_3 = result_1;
        }

        int32_t v1_54 = result_3 >> 6;
        if (v1_54 > 0) {
            int32_t s0_2 = 0;
            do {
                if (v0_1 > 0) {
                    int32_t i_2 = (v0_1 - 1) << 4;
                    int32_t s1_25 = 0;
                    do {
                        int32_t v0_1073 = (int32_t)(DST_WIDTH1 + DST_WIDTH_16) * i_2;
                        int32_t a0_45 = s0_2 * (int32_t)SRC_STRIDE1 + s1_25;
                        i_2 -= 0x10;
                        s1_25 += 0x20;
                        nv12_left_rotate_90_block32_uv((char *)(v0_3 + a0_45), (char *)(v0_5 + v0_1073 + (s0_2 << 1)));
                    } while (i_2 != -0x10);
                }

                result = v1_54 << 5;
                s0_2 += 0x20;
            } while (result != s0_2);
        } else {
            result = result_1;
        }
    }

    nv12_left_rotate_90_uv_tail(v0_5, v0_3);
    return result;
}

int32_t nv12_right_rotate_90(int32_t arg1, void *arg2, void *arg3)
{
    int32_t result_2 = (int32_t)DST_WIDTH;
    uint32_t v0 = SRC_WIDTH;
    int32_t result_4 = result_2 + 0x1f;
    int32_t a1 = (int32_t)(v0 * (uint32_t)result_2);
    int32_t v1 = result_2 < 0 ? 1 : 0;
    int32_t result;

    (void)arg1;

    if ((int32_t)v0 < 0) {
        v0 += 0x1f;
    }

    int32_t v0_1 = (int32_t)v0 >> 5;

    if (v1 == 0) {
        result_4 = result_2;
    }

    int32_t v1_2 = result_4 >> 5;
    uint8_t *v0_3 = (uint8_t *)arg2 + a1;
    uint8_t *v0_5 = (uint8_t *)arg3 + a1;

    if (v1_2 > 0) {
        nv12_right_rotate_90_y_plane((uint8_t *)arg3, (const uint8_t *)arg2);

        if (v0_1 > 0) {
            int32_t var_64_1 = 0;
            do {
                int32_t i_2 = 0;
                int32_t var_10c_4 = (result_2 << 6) - 0x40;

                do {
                    int32_t a2_15 = i_2 << 1;
                    int32_t v0_1579 = (int32_t)(DST_WIDTH1 + DST_WIDTH_16) * i_2;
                    i_2 += 0x10;
                    nv12_right_rotate_90_block32_uv((char *)(v0_3 + var_64_1 * (int32_t)SRC_STRIDE1 + a2_15),
                        (char *)(v0_5 + v0_1579 + var_10c_4 + 0x20));
                } while ((v0_1 << 4) != i_2);

                var_64_1 += 0x20;
                var_10c_4 -= 0x40;
                result = result_2 << 5;
            } while (var_64_1 != result);
        } else {
            result = result_2;
        }
    } else {
        int32_t result_3 = result_2 + 0x3f;
        if (v1 == 0) {
            result_3 = result_2;
        }

        int32_t s4_2 = result_3 >> 6;
        if (s4_2 > 0) {
            int32_t s1_26 = (s4_2 << 6) - 0x40;
            int32_t s0_2 = 0;
            do {
                if (v0_1 > 0) {
                    int32_t i_3 = 0;
                    do {
                        int32_t a2_11 = i_3 << 1;
                        int32_t v0_1065 = (int32_t)(DST_WIDTH1 + DST_WIDTH_16) * i_3;
                        i_3 += 0x10;
                        nv12_right_rotate_90_block32_uv((char *)(v0_3 + s0_2 * (int32_t)SRC_STRIDE1 + a2_11),
                            (char *)(v0_5 + v0_1065 + s1_26));
                    } while ((v0_1 << 4) != i_3);
                }

                result = s4_2 << 5;
                s0_2 += 0x20;
                s1_26 -= 0x40;
            } while (s0_2 != result);
        } else {
            result = result_2;
        }
    }

    nv12_right_rotate_90_uv_tail(v0_5, v0_3);
    return result;
}
