#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "alcodec/al_allocator.h"
#include "alcodec/al_buffer.h"
#include "alcodec/al_rtos.h"
#include "alcodec/al_types.h"
#include "imp_log_int.h"

extern char _gp;
extern int32_t __assert(const char *expression, const char *file, int32_t line, const char *function,
                        void *caller);

typedef struct StaticFifoCompat {
    int32_t *elems;
    int32_t read_idx;
    int32_t write_idx;
    int32_t capacity;
} StaticFifoCompat;

typedef struct AL_IpCtrl AL_IpCtrl;
typedef struct AL_IpCtrlVtable {
    void *Destroy;
    int32_t (*ReadRegister)(AL_IpCtrl *self, uint32_t reg);
    int32_t (*WriteRegister)(AL_IpCtrl *self, uint32_t reg, uint32_t value);
    int32_t (*RegisterCallBack)(AL_IpCtrl *self, void (*cb)(void *), void *user_data, uint32_t irq_idx);
} AL_IpCtrlVtable;

struct AL_IpCtrl {
    const AL_IpCtrlVtable *vtable;
};

typedef struct AL_EncCoreCtxCompat {
    AL_IpCtrl *ip_ctrl;   /* +0x00 */
    int32_t cmd_regs_1;   /* +0x04 */
    int32_t cmd_regs_2;   /* +0x08 */
    uint8_t core_id;      /* +0x0c */
    uint8_t pad_0d[3];
} AL_EncCoreCtxCompat;

#define READ_U8(base, off) (*(uint8_t *)((uint8_t *)(base) + (off)))
#define READ_S8(base, off) (*(int8_t *)((uint8_t *)(base) + (off)))
#define READ_U16(base, off) (*(uint16_t *)((uint8_t *)(base) + (off)))
#define READ_S16(base, off) (*(int16_t *)((uint8_t *)(base) + (off)))
#define READ_U32(base, off) (*(uint32_t *)((uint8_t *)(base) + (off)))
#define READ_S32(base, off) (*(int32_t *)((uint8_t *)(base) + (off)))
#define READ_PTR(base, off) (*(void **)((uint8_t *)(base) + (off)))
#define WRITE_U8(base, off, val) (*(uint8_t *)((uint8_t *)(base) + (off)) = (uint8_t)(val))
#define WRITE_U16(base, off, val) (*(uint16_t *)((uint8_t *)(base) + (off)) = (uint16_t)(val))
#define WRITE_U32(base, off, val) (*(uint32_t *)((uint8_t *)(base) + (off)) = (uint32_t)(val))
#define WRITE_S32(base, off, val) (*(int32_t *)((uint8_t *)(base) + (off)) = (int32_t)(val))
#define WRITE_PTR(base, off, val) (*(void **)((uint8_t *)(base) + (off)) = (void *)(val))

/*
 * The GOP manager lives at channel + 0x128 and uses channel + 0x174 as its
 * state pointer. Keep the channel's started/gate/freeze bookkeeping away from
 * that tail so post-encode GOP callbacks don't dereference a clobbered state.
 */
#define CH_STARTED_OFF 0x1aec
#define CH_DRAINED_OFF 0x1aed
#define CH_GATE_OFF 0x1aee
#define CH_FREEZE_OFF 0x1aef

/*
 * OEM stores the scheduler callback bundle and adjacent ME-range/trace state in
 * the high channel tail. The low 0x48..0x4f range remains copied codec settings
 * and must not be used for callbacks or ME-range state.
 */
#define CH_OEM_CHANNEL_ID_OFF 0x12d58
#define CH_OEM_CORE_BASE_OFF 0x12d59
#define CH_OUT_CB_OFF 0x12f9c
#define CH_OUT_CTX_OFF 0x12fa0
#define CH_DESTROY_CB_OFF 0x12fa4
#define CH_DESTROY_CTX_OFF 0x12fa8
#define CH_FRAME_DONE_CB_OFF 0x12fac
#define CH_FRAME_DONE_CTX_OFF 0x12fb0
#define CH_ME_RANGE_OFF 0x12fb8
#define CH_CMD_TRACE_FLAGS_OFF 0x12fc0
#define CH_TRACE_CB_OFF 0x12fc4
#define CH_TRACE_CTX_OFF 0x12fc8

#define LIVE_T31_FORCE_SINGLE_CORE_AVC 1
#define LIVE_T31_FORCE_DUAL_PUSH_AVC 1
#define LIVE_T31_DELAYED_POST_CL_PROBE 0
#define LIVE_T31_AVC_HEADER_BUDGET 512
#define LIVE_T31_VERBOSE_REQ_WINDOW 0

int32_t AL_DmaAlloc_FlushCache(int32_t arg1, int32_t arg2, int32_t arg3); /* forward decl */

static uint32_t AlignUpPow2(uint32_t value, uint32_t align)
{
    return (value + align - 1U) & ~(align - 1U);
}

static int32_t ComputeLiveT31StreamPartOffset(void *ch, int32_t stream_size)
{
    uint32_t width;
    uint32_t height;
    uint32_t lcu_w;
    uint32_t lcu_h;
    uint32_t part_size;

    if (ch == NULL || stream_size <= 0) {
        return 0;
    }

    width = READ_U16(ch, 4);
    height = READ_U16(ch, 6);
    if (width == 0U || height == 0U) {
        return 0;
    }

    lcu_w = (width + 15U) >> 4;
    lcu_h = (height + 15U) >> 4;
    part_size = AlignUpPow2(((lcu_w * lcu_h) + 0x10U) << 4, 128U);
    if (part_size >= (uint32_t)stream_size) {
        return 0;
    }

    return (int32_t)((uint32_t)stream_size - part_size);
}

static uint32_t PackLiveT31LcuPos(uint32_t pos, uint32_t lcu_w)
{
    if (lcu_w == 0U) {
        return 0;
    }

    return (pos % lcu_w) | (((pos / lcu_w) & 0x3ffU) << 12);
}

typedef struct LiveT31BitWriter {
    uint8_t *buf;
    int32_t cap;
    int32_t bit_pos;
    int32_t overflow;
} LiveT31BitWriter;

static void LiveT31BsInit(LiveT31BitWriter *bs, uint8_t *buf, int32_t cap)
{
    bs->buf = buf;
    bs->cap = cap;
    bs->bit_pos = 0;
    bs->overflow = 0;
    if (buf != NULL && cap > 0) {
        memset(buf, 0, (size_t)cap);
    }
}

static void LiveT31BsPutBit(LiveT31BitWriter *bs, int value)
{
    int32_t byte_pos;
    int32_t bit_off;

    if (bs == NULL || bs->buf == NULL || bs->overflow != 0) {
        return;
    }

    byte_pos = bs->bit_pos >> 3;
    if (byte_pos < 0 || byte_pos >= bs->cap) {
        bs->overflow = 1;
        return;
    }

    bit_off = 7 - (bs->bit_pos & 7);
    if (value != 0) {
        bs->buf[byte_pos] |= (uint8_t)(1U << bit_off);
    } else {
        bs->buf[byte_pos] &= (uint8_t)~(1U << bit_off);
    }
    bs->bit_pos += 1;
}

static void LiveT31BsPutBits(LiveT31BitWriter *bs, uint32_t value, int32_t count)
{
    int32_t bit;

    for (bit = count - 1; bit >= 0; --bit) {
        LiveT31BsPutBit(bs, (int)((value >> bit) & 1U));
    }
}

static void LiveT31BsPutUE(LiveT31BitWriter *bs, uint32_t value)
{
    uint32_t code_num = value + 1U;
    uint32_t tmp = code_num;
    int32_t leading_zero_bits = 0;
    int32_t bit;

    while (tmp > 1U) {
        tmp >>= 1;
        leading_zero_bits += 1;
    }

    for (bit = 0; bit < leading_zero_bits; ++bit) {
        LiveT31BsPutBit(bs, 0);
    }
    for (bit = leading_zero_bits; bit >= 0; --bit) {
        LiveT31BsPutBit(bs, (int)((code_num >> bit) & 1U));
    }
}

static void LiveT31BsPutSE(LiveT31BitWriter *bs, int32_t value)
{
    uint32_t code_num;

    if (value > 0) {
        code_num = (uint32_t)(value * 2 - 1);
    } else {
        code_num = (uint32_t)(-value * 2);
    }
    LiveT31BsPutUE(bs, code_num);
}

static void LiveT31BsTrailingBits(LiveT31BitWriter *bs)
{
    LiveT31BsPutBit(bs, 1);
    while (bs != NULL && bs->overflow == 0 && (bs->bit_pos & 7) != 0) {
        LiveT31BsPutBit(bs, 0);
    }
}

static int32_t LiveT31BsBytes(const LiveT31BitWriter *bs)
{
    if (bs == NULL || bs->overflow != 0) {
        return -1;
    }
    return (bs->bit_pos + 7) >> 3;
}

static int32_t LiveT31WriteNal(uint8_t *dst, int32_t cap, uint8_t nal_header, const uint8_t *rbsp,
                               int32_t rbsp_len)
{
    int32_t pos = 0;
    int32_t i;
    int32_t zero_count = 0;

    if (dst == NULL || rbsp == NULL || rbsp_len < 0 || cap < 5) {
        return -1;
    }

    dst[pos++] = 0x00;
    dst[pos++] = 0x00;
    dst[pos++] = 0x00;
    dst[pos++] = 0x01;
    dst[pos++] = nal_header;

    for (i = 0; i < rbsp_len; ++i) {
        uint8_t byte = rbsp[i];

        if (zero_count >= 2 && byte <= 0x03U) {
            if (pos >= cap) {
                return -1;
            }
            dst[pos++] = 0x03;
            zero_count = 0;
        }
        if (pos >= cap) {
            return -1;
        }
        dst[pos++] = byte;
        zero_count = (byte == 0U) ? (zero_count + 1) : 0;
    }

    return pos;
}

static int32_t LiveT31WriteAudNal(uint8_t *dst, int32_t cap, int is_idr)
{
    if (dst == NULL || cap < 6) {
        return -1;
    }

    dst[0] = 0x00;
    dst[1] = 0x00;
    dst[2] = 0x00;
    dst[3] = 0x01;
    dst[4] = 0x09;
    dst[5] = is_idr ? 0x10 : 0x30;
    return 6;
}

static int32_t LiveT31GenerateAvcSpsRbsp(uint8_t *rbsp, int32_t cap, uint32_t width, uint32_t height,
                                         int high_profile)
{
    LiveT31BitWriter bs;
    uint32_t mb_w;
    uint32_t mb_h;
    uint32_t crop_right;
    uint32_t crop_bottom;
    uint32_t need_crop;
    uint32_t profile_idc;
    uint32_t level_idc;

    if (rbsp == NULL || cap <= 0 || width == 0U || height == 0U) {
        return -1;
    }

    LiveT31BsInit(&bs, rbsp, cap);
    profile_idc = high_profile ? 100U : 66U;
    level_idc = (width > 1280U || height > 720U) ? 40U : 31U;

    LiveT31BsPutBits(&bs, profile_idc, 8);
    LiveT31BsPutBit(&bs, high_profile ? 0 : 1);
    LiveT31BsPutBit(&bs, high_profile ? 0 : 1);
    LiveT31BsPutBit(&bs, 0);
    LiveT31BsPutBit(&bs, 0);
    LiveT31BsPutBits(&bs, 0, 4);
    LiveT31BsPutBits(&bs, level_idc, 8);
    LiveT31BsPutUE(&bs, 0);

    if (high_profile) {
        LiveT31BsPutUE(&bs, 1);
        LiveT31BsPutUE(&bs, 0);
        LiveT31BsPutUE(&bs, 0);
        LiveT31BsPutBit(&bs, 0);
        LiveT31BsPutBit(&bs, 0);
    }

    LiveT31BsPutUE(&bs, 0);
    LiveT31BsPutUE(&bs, 0);
    LiveT31BsPutUE(&bs, 0);
    LiveT31BsPutUE(&bs, 1);
    LiveT31BsPutBit(&bs, 0);

    mb_w = (width + 15U) >> 4;
    mb_h = (height + 15U) >> 4;
    LiveT31BsPutUE(&bs, mb_w - 1U);
    LiveT31BsPutUE(&bs, mb_h - 1U);
    LiveT31BsPutBit(&bs, 1);
    LiveT31BsPutBit(&bs, 1);

    crop_right = (mb_w * 16U - width) >> 1;
    crop_bottom = (mb_h * 16U - height) >> 1;
    need_crop = (crop_right != 0U || crop_bottom != 0U) ? 1U : 0U;
    LiveT31BsPutBit(&bs, (int)need_crop);
    if (need_crop != 0U) {
        LiveT31BsPutUE(&bs, 0);
        LiveT31BsPutUE(&bs, crop_right);
        LiveT31BsPutUE(&bs, 0);
        LiveT31BsPutUE(&bs, crop_bottom);
    }

    LiveT31BsPutBit(&bs, 1);
    LiveT31BsPutBit(&bs, 0);
    LiveT31BsPutBit(&bs, 0);
    LiveT31BsPutBit(&bs, 0);
    LiveT31BsPutBit(&bs, 0);
    LiveT31BsPutBit(&bs, 1);
    LiveT31BsPutBits(&bs, 1, 32);
    LiveT31BsPutBits(&bs, 50, 32);
    LiveT31BsPutBit(&bs, 1);
    LiveT31BsPutBit(&bs, 0);
    LiveT31BsPutBit(&bs, 0);
    LiveT31BsPutBit(&bs, 0);
    LiveT31BsPutBit(&bs, 0);

    LiveT31BsTrailingBits(&bs);
    return LiveT31BsBytes(&bs);
}

static int32_t LiveT31GenerateAvcPpsRbsp(uint8_t *rbsp, int32_t cap, int high_profile, int entropy_mode)
{
    LiveT31BitWriter bs;

    if (rbsp == NULL || cap <= 0) {
        return -1;
    }

    LiveT31BsInit(&bs, rbsp, cap);
    LiveT31BsPutUE(&bs, 0);
    LiveT31BsPutUE(&bs, 0);
    LiveT31BsPutBit(&bs, entropy_mode ? 1 : 0);
    LiveT31BsPutBit(&bs, 0);
    LiveT31BsPutUE(&bs, 0);
    LiveT31BsPutUE(&bs, 0);
    LiveT31BsPutUE(&bs, 0);
    LiveT31BsPutBit(&bs, 0);
    LiveT31BsPutBits(&bs, 0, 2);
    LiveT31BsPutSE(&bs, 0);
    LiveT31BsPutSE(&bs, 0);
    LiveT31BsPutSE(&bs, 0);
    LiveT31BsPutBit(&bs, 1);
    LiveT31BsPutBit(&bs, 0);
    LiveT31BsPutBit(&bs, 0);
    if (high_profile) {
        LiveT31BsPutBit(&bs, 0);
        LiveT31BsPutBit(&bs, 0);
        LiveT31BsPutSE(&bs, 0);
    }
    LiveT31BsTrailingBits(&bs);
    return LiveT31BsBytes(&bs);
}

static int32_t LiveT31GenerateAvcSliceRbsp(uint8_t *rbsp, int32_t cap, int is_idr, int entropy_mode,
                                           uint32_t first_mb, uint32_t frame_num)
{
    LiveT31BitWriter bs;

    if (rbsp == NULL || cap <= 0) {
        return -1;
    }

    LiveT31BsInit(&bs, rbsp, cap);
    LiveT31BsPutUE(&bs, first_mb);
    LiveT31BsPutUE(&bs, is_idr ? 7U : 5U);
    LiveT31BsPutUE(&bs, 0);
    LiveT31BsPutBits(&bs, frame_num & 0x0fU, 4);
    if (is_idr) {
        LiveT31BsPutUE(&bs, 0);
    }
    LiveT31BsPutBits(&bs, (frame_num * 2U) & 0x0fU, 4);
    if (is_idr) {
        LiveT31BsPutBit(&bs, 0);
        LiveT31BsPutBit(&bs, 0);
    } else {
        LiveT31BsPutBit(&bs, 0);
        LiveT31BsPutBit(&bs, 0);
        LiveT31BsPutBit(&bs, 0);
    }
    if (entropy_mode != 0) {
        LiveT31BsPutUE(&bs, 0);
    }
    LiveT31BsPutSE(&bs, 0);
    LiveT31BsPutUE(&bs, 0);
    LiveT31BsTrailingBits(&bs);
    return LiveT31BsBytes(&bs);
}

static int32_t PrewriteLiveT31AvcHeaders(void *ch, int32_t *req, void *src_meta, int32_t dst_off,
                                         int include_param_sets, int32_t core_idx, int32_t first_mb,
                                         int high_profile, int entropy_mode)
{
    uint8_t rbsp[256];
    uint8_t *stream_base;
    uint8_t *dst;
    int32_t stream_size;
    int32_t budget;
    int32_t pos = 0;
    int32_t wrote;
    int32_t rbsp_len;
    int32_t flush_len;
    uint32_t width;
    uint32_t height;
    int is_idr;

    if (ch == NULL || req == NULL || src_meta == NULL || dst_off < 0) {
        return 0;
    }

    stream_base = (uint8_t *)READ_PTR(src_meta, 8);
    stream_size = READ_S32(src_meta, 0x10);
    width = READ_U16(ch, 4);
    height = READ_U16(ch, 6);
    if (stream_base == NULL || stream_size <= dst_off || width == 0U || height == 0U) {
        IMP_LOG_INFO("ENC", "encode1 avc-prewrite skip core=%d base=%p size=%d off=%d wxh=%ux%u",
                     core_idx, stream_base, stream_size, dst_off, width, height);
        return 0;
    }

    budget = stream_size - dst_off;
    if (budget > LIVE_T31_AVC_HEADER_BUDGET) {
        budget = LIVE_T31_AVC_HEADER_BUDGET;
    }
    if (budget < 64) {
        IMP_LOG_INFO("ENC", "encode1 avc-prewrite tiny-budget core=%d size=%d off=%d budget=%d",
                     core_idx, stream_size, dst_off, budget);
        return 0;
    }

    dst = stream_base + dst_off;
    memset(dst, 0, (size_t)budget);
    is_idr = (READ_U8(req, 0x164) == 0U);

    wrote = LiveT31WriteAudNal(dst + pos, budget - pos, is_idr);
    if (wrote < 0) {
        return 0;
    }
    pos += wrote;

    if (include_param_sets != 0 && is_idr) {
        rbsp_len = LiveT31GenerateAvcSpsRbsp(rbsp, (int32_t)sizeof(rbsp), width, height, high_profile);
        wrote = LiveT31WriteNal(dst + pos, budget - pos, 0x67, rbsp, rbsp_len);
        if (rbsp_len < 0 || wrote < 0) {
            return 0;
        }
        pos += wrote;

        rbsp_len = LiveT31GenerateAvcPpsRbsp(rbsp, (int32_t)sizeof(rbsp), high_profile, entropy_mode);
        wrote = LiveT31WriteNal(dst + pos, budget - pos, 0x68, rbsp, rbsp_len);
        if (rbsp_len < 0 || wrote < 0) {
            return 0;
        }
        pos += wrote;
    }

    rbsp_len = LiveT31GenerateAvcSliceRbsp(rbsp, (int32_t)sizeof(rbsp), is_idr, entropy_mode,
                                           (uint32_t)first_mb, 0);
    wrote = LiveT31WriteNal(dst + pos, budget - pos, is_idr ? 0x65 : 0x41, rbsp, rbsp_len);
    if (rbsp_len < 0 || wrote < 0) {
        return 0;
    }
    pos += wrote;

    flush_len = (int32_t)AlignUpPow2((uint32_t)pos, 32U);
    if (flush_len > stream_size - dst_off) {
        flush_len = stream_size - dst_off;
    }
    AL_DmaAlloc_FlushCache((int32_t)(intptr_t)dst, flush_len, 1);

    IMP_LOG_INFO("ENC",
                 "encode1 avc-prewrite core=%d off=%d bytes=%d flush=%d idr=%d ps=%d high=%d cabac=%d first_mb=%d"
                 " data=%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                 core_idx, dst_off, pos, flush_len, is_idr, include_param_sets, high_profile, entropy_mode, first_mb,
                 dst[0], dst[1], dst[2], dst[3], dst[4], dst[5], dst[6], dst[7],
                 dst[8], dst[9], dst[10], dst[11], dst[12], dst[13], dst[14], dst[15]);
    return pos;
}

static void PatchLiveT31AvcIdrEnc1Control(void *ch, int32_t *req, uint8_t *cmd_regs)
{
    uint32_t *cmd = (uint32_t *)cmd_regs;
    uint32_t width;
    uint32_t height;
    uint32_t lcu_w;
    uint32_t lcu_h;
    uint32_t last_lcu;
    uint32_t old0;
    uint32_t old1;
    uint32_t old2;
    uint32_t old8;
    uint32_t old9;
    int high_profile;

    if (ch == NULL || req == NULL || cmd == NULL || READ_U8(ch, 0x1f) != 0U || READ_U8(req, 0x164) != 0U) {
        return;
    }

    width = READ_U16(ch, 4);
    height = READ_U16(ch, 6);
    if (width == 0U || height == 0U) {
        return;
    }

    old0 = cmd[0x00];
    old1 = cmd[0x01];
    old2 = cmd[0x02];
    old8 = cmd[0x08];
    old9 = cmd[0x09];
    high_profile = (old0 & 0x00000400U) != 0U;

    lcu_w = (width + 15U) >> 4;
    lcu_h = (height + 15U) >> 4;
    last_lcu = (lcu_w * lcu_h) - 1U;

    cmd[0x00] = high_profile ? 0x80700411U : 0x80700011U;
    cmd[0x01] = ((((width + 7U) >> 3) - 1U) & 0x7ffU) |
                (((((height + 7U) >> 3) - 1U) & 0x7ffU) << 12) |
                (8U << 24) | (8U << 28);
    cmd[0x02] = high_profile ? 0x4010ad50U : 0x4010a950U;
    cmd[0x03] = (30U << 16) | 0x21000000U;
    cmd[0x04] = 0x00083f1fU;
    cmd[0x05] = 0;
    cmd[0x06] = PackLiveT31LcuPos(last_lcu, lcu_w);
    cmd[0x07] = (((lcu_h - 1U) & 0x3ffU) << 12) | ((lcu_w - 1U) & 0x3ffU) | 0x80000000U;
    cmd[0x08] = 0x77000000U;
    cmd[0x09] = high_profile ? 0xfc010000U : 0x3c010000U;
    cmd[0x0a] = 0x00000c80U;
    cmd[0x0b] = 0x00027000U;
    cmd[0x0c] = 0;
    cmd[0x0d] = 0;
    cmd[0x0e] = 0;
    cmd[0x0f] = 0;
    cmd[0x10] = 0xffffffffU;
    cmd[0x11] = 0xffffffffU;
    cmd[0x12] = 0x003ff3ffU;
    cmd[0x13] = 0;
    cmd[0x14] = 0xf4000107U;
    cmd[0x15] = 0x00000664U;
    cmd[0x16] = 0x3f00006cU;
    cmd[0x17] = 0x101e2d1eU;
    cmd[0x18] = 0xc210000cU;
    cmd[0x19] = 0;
    cmd[0x1a] = 0;

    IMP_LOG_INFO("ENC",
                 "encode1 stock-enc1-control old0=%08x old1=%08x old2=%08x old8=%08x old9=%08x"
                 " new0=%08x new1=%08x new2=%08x new8=%08x new9=%08x w0a=%08x w0b=%08x w12=%08x",
                 old0, old1, old2, old8, old9,
                 cmd[0x00], cmd[0x01], cmd[0x02], cmd[0x08], cmd[0x09],
                 cmd[0x0a], cmd[0x0b], cmd[0x12]);
}

int32_t AL_GetAllocSizeEP1(void); /* forward decl, ported by T<N> later */
void AL_CoreState_SetChannelRunState(uint8_t *arg1, uint8_t arg2, int32_t arg3, uint8_t arg4); /* CoreManager.c */

#define ENC_KMSG(fmt, ...) IMP_LOG_INFO("ENC", fmt, ##__VA_ARGS__)

static void ClearCompletedPhaseRunState(void *ch, int32_t *req, int32_t phase)
{
    uint8_t core_base = READ_U8(ch, 0x3d);
    uint8_t *core_arr = (uint8_t *)READ_PTR(ch, 0x164);
    int32_t *grp = (int32_t *)((uint8_t *)req + phase * 0x110 + 0x84c);
    int32_t mod_count = READ_S32(req, phase * 0x110 + 0x8cc);
    int i;

    if (core_arr == NULL) {
        ENC_KMSG("EndEncoding clear-run skip req=%p phase=%d core_arr=(nil)", req, phase);
        return;
    }

    /*
     * The channel stores the per-core encoder table at scheduler+0x8c with
     * 0x44-byte stride. Rewind from the current core base back to the owning
     * scheduler so we can clear the module run-state bits in the core-state
     * table at scheduler+0x5d4.
     */
    {
        uint8_t *sched = core_arr - 0x8c - core_base * 0x44;

        for (i = 0; i < mod_count; ++i) {
            int32_t core = grp[0];
            int32_t mod = grp[1];

            if (core >= 0 && mod >= 0) {
                uint8_t *state = sched + core * 0x98 + 0x5d4;

                ENC_KMSG("EndEncoding clear-run req=%p phase=%d idx=%d core=%d mod=%d state=%p",
                         req, phase, i, core, mod, state);
                AL_CoreState_SetChannelRunState(state, 0xff, mod, 0);
            }
            grp += 2;
        }
    }
}

static void PrepareSourceConfigForLaunch(AL_EncCoreCtxCompat *core, void *ch, void *req, uint32_t *cmd_regs,
                                         const char *tag)
{
    AL_IpCtrl *ip;
    uint32_t width;
    uint32_t height;
    uint32_t src_y;
    uint32_t src_uv;
    uint32_t ep1;
    uint32_t wpp;
    uint32_t stream_part;
    uint32_t hdr_off;
    uint32_t stream_budget;
    uint32_t core_base;
    uint32_t reg_85f4;
    uint32_t reg_85f0;
    uint32_t reg_83f4;
    uint32_t reg_8400;
    uint32_t reg_8420;
    uint32_t reg_8424;
    uint32_t reg_8428;
    uint32_t reg_85e4;

    if (core == NULL || ch == NULL || req == NULL || cmd_regs == NULL) {
        return;
    }

    ip = core->ip_ctrl;
    if (ip == NULL || ip->vtable == NULL || ip->vtable->WriteRegister == NULL) {
        return;
    }

    width = READ_U16(ch, 4);
    height = READ_U16(ch, 6);
    src_y = READ_U32(req, 0x298);
    src_uv = READ_U32(req, 0x29c);
    ep1 = READ_U32(req, 0x2fc);
    wpp = READ_U32(req, 0x2f8);
    stream_part = cmd_regs[0x31];
    hdr_off = cmd_regs[0x32];
    stream_budget = cmd_regs[0x33];
    core_base = ((uint32_t)core->core_id) << 9;

    if (width == 0 || height == 0 || src_y == 0 || src_uv == 0 || ep1 == 0) {
        ENC_KMSG("encode1 source-config-%s skip core=%u w=%u h=%u srcY=0x%x srcUV=0x%x ep1=0x%x",
                 tag ? tag : "?", (unsigned)core->core_id, width, height, src_y, src_uv, ep1);
        return;
    }

    if (wpp == 0) {
        wpp = ep1 + (uint32_t)AL_GetAllocSizeEP1();
    }

    ip->vtable->WriteRegister(ip, 0x85f4, 1);
    ip->vtable->WriteRegister(ip, 0x85f0, 1);
    ip->vtable->WriteRegister(ip, core_base + 0x83f4, 1);
    ip->vtable->WriteRegister(ip, 0x8400, 0x00000131U);
    ip->vtable->WriteRegister(ip, 0x8404, (((width - 1U) & 0xffffU) << 16) | ((height - 1U) & 0xffffU));
    ip->vtable->WriteRegister(ip, 0x8408, 0x00010001U);
    ip->vtable->WriteRegister(ip, 0x840c, width);
    ip->vtable->WriteRegister(ip, 0x8410, src_y);
    ip->vtable->WriteRegister(ip, 0x8414, src_uv);
    ip->vtable->WriteRegister(ip, 0x8418, wpp);
    ip->vtable->WriteRegister(ip, 0x841c, ep1);
    ip->vtable->WriteRegister(ip, 0x8420, stream_part);
    ip->vtable->WriteRegister(ip, 0x8424, hdr_off);
    ip->vtable->WriteRegister(ip, 0x8428, stream_budget);
    ip->vtable->WriteRegister(ip, 0x85e4, 1);

    reg_85f4 = (uint32_t)ip->vtable->ReadRegister(ip, 0x85f4);
    reg_85f0 = (uint32_t)ip->vtable->ReadRegister(ip, 0x85f0);
    reg_83f4 = (uint32_t)ip->vtable->ReadRegister(ip, core_base + 0x83f4);
    reg_8400 = (uint32_t)ip->vtable->ReadRegister(ip, 0x8400);
    reg_8420 = (uint32_t)ip->vtable->ReadRegister(ip, 0x8420);
    reg_8424 = (uint32_t)ip->vtable->ReadRegister(ip, 0x8424);
    reg_8428 = (uint32_t)ip->vtable->ReadRegister(ip, 0x8428);
    reg_85e4 = (uint32_t)ip->vtable->ReadRegister(ip, 0x85e4);
    ENC_KMSG("encode1 source-config-%s core=%u srcY=0x%x srcUV=0x%x ep1=0x%x wpp=0x%x part=0x%x hdr=0x%x budget=0x%x"
             " rb85f4=0x%x rb85f0=0x%x rb83f4=0x%x rb8400=0x%x rb8420=0x%x rb8424=0x%x rb8428=0x%x rb85e4=0x%x",
             tag ? tag : "?", (unsigned)core->core_id, src_y, src_uv, ep1, wpp, stream_part, hdr_off, stream_budget,
             reg_85f4, reg_85f0, reg_83f4, reg_8400, reg_8420, reg_8424, reg_8428, reg_85e4);
}

static void RemapLiveT31Irq4(void *core_ctx, void (*cb)(void *), const char *tag)
{
    AL_EncCoreCtxCompat *core = (AL_EncCoreCtxCompat *)core_ctx;

    if (core == NULL || core->ip_ctrl == NULL || core->ip_ctrl->vtable == NULL ||
        core->ip_ctrl->vtable->RegisterCallBack == NULL) {
        return;
    }

    core->ip_ctrl->vtable->RegisterCallBack(core->ip_ctrl, cb, core_ctx, 4);
    ENC_KMSG("%s remap-irq4 core=%u ctx=%p cb=%p",
             tag ? tag : "irq4", (unsigned)core->core_id, core_ctx, cb);
}

static void ApplyLiveT31Core0LegacyIrqMask(void *core_ctx, const char *tag)
{
    AL_EncCoreCtxCompat *core = (AL_EncCoreCtxCompat *)core_ctx;
    AL_IpCtrl *ip;
    uint32_t before;
    uint32_t after;

    if (core == NULL || core->ip_ctrl == NULL || core->ip_ctrl->vtable == NULL ||
        core->ip_ctrl->vtable->ReadRegister == NULL || core->ip_ctrl->vtable->WriteRegister == NULL ||
        core->core_id != 0U) {
        return;
    }

    ip = core->ip_ctrl;
    before = (uint32_t)ip->vtable->ReadRegister(ip, 0x8014);
    ip->vtable->WriteRegister(ip, 0x8014, 0x00000011U);
    after = (uint32_t)ip->vtable->ReadRegister(ip, 0x8014);
    ENC_KMSG("%s legacy-core0-irq-mask old=0x%08x forced=0x00000011 rb=0x%08x",
             tag ? tag : "irq", before, after);
}

static void PushStreamBufferToAvpu(void *ch, uint32_t phys, const char *tag)
{
    AL_EncCoreCtxCompat *core;
    int32_t ret;

    if (ch == NULL || phys == 0) {
        return;
    }

    core = (AL_EncCoreCtxCompat *)READ_PTR(ch, 0x164);
    if (core == NULL || core->ip_ctrl == NULL || core->ip_ctrl->vtable == NULL ||
        core->ip_ctrl->vtable->WriteRegister == NULL) {
        ENC_KMSG("strm-push-%s skip chctx=%p core=%p phys=0x%x",
                 tag ? tag : "?", ch, core, phys);
        return;
    }

    ret = core->ip_ctrl->vtable->WriteRegister(core->ip_ctrl, 0x8094, phys);
    ENC_KMSG("strm-push-%s chctx=%p core=%p phys=0x%x ret=%d",
             tag ? tag : "?", ch, core, phys, ret);
}

void StaticFifo_Init(StaticFifoCompat *arg1, int32_t *arg2, int32_t arg3); /* forward decl, ported by T<N> later */
int32_t StaticFifo_Queue(StaticFifoCompat *arg1, int32_t arg2); /* forward decl, ported by T<N> later */
int32_t StaticFifo_Front(StaticFifoCompat *arg1); /* forward decl, ported by T<N> later */
int32_t PopCommandListAddresses(void *arg1, void *arg2); /* forward decl, ported by T<N> later */
void *RequestsBuffer_Init(void *arg1); /* forward decl, ported by T<N> later */
void *RequestsBuffer_Pop(void *arg1); /* forward decl, ported by T<N> later */
void EndRequestsBuffer_Init(void *arg1); /* forward decl, ported by T<N> later */
int32_t PreprocessHwRateCtrl(int32_t *arg1, int32_t arg2, int32_t arg3, int32_t arg4,
                             void *arg5); /* forward decl, ported by T<N> later */
int32_t InitHwRateCtrl(void *arg1, void *arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6,
                       int32_t arg7, int32_t arg8,
                       void *arg9); /* forward decl, ported by T<N> later */
int32_t ResetChannelParam(void *arg1); /* forward decl, ported in this unit */
int32_t AL_RefMngr_GetAvailRef(void *arg1, void *arg2, int32_t *arg3); /* forward decl, ported by T<N> later */
int32_t AL_RefMngr_UpdateDPB(void *arg1, int32_t *arg2); /* forward decl, ported by T<N> later */
int32_t AL_DPB_AVC_CheckMMCORequired(void *arg1, void *arg2,
                                     int32_t *arg3); /* forward decl, ported by T<N> later */
int32_t AL_RefMngr_ReleaseFrmBuffer(void *arg1, char arg2); /* forward decl, ported by T<N> later */
int32_t AL_RefMngr_GetFrmBufAddrs(void *arg1, char arg2, int32_t *arg3, int32_t *arg4,
                                  void *arg5); /* forward decl, ported by T<N> later */
int32_t AL_RefMngr_GetMvBufAddr(void *arg1, char arg2, int32_t *arg3); /* forward decl, ported by T<N> later */
int32_t AL_RefMngr_GetRefInfo(int32_t arg1, int32_t arg2, void *arg3, void *arg4,
                              int32_t *arg5); /* forward decl, ported by T<N> later */
int32_t AL_RefMngr_GetRefBufferFromPOC(void *arg1, int32_t arg2); /* forward decl, ported by T<N> later */
int32_t AL_RefMngr_GetFrmBuffer(void *arg1, char arg2); /* forward decl, ported by T<N> later */
int32_t AL_GetEncoderFbcMapSize(int32_t arg1, int32_t arg2, int32_t arg3,
                                int32_t arg4); /* forward decl, ported by T<N> later */
int32_t AL_RefMngr_GetMapBufAddr(void *arg1, char arg2); /* forward decl, ported by T<N> later */
int32_t AL_RefMngr_MarkAsReadyForOutput(void *arg1, char arg2); /* forward decl, ported by T<N> later */
int32_t AL_RefMngr_StorePicture(void *arg1, void *arg2, char arg3); /* forward decl, ported by T<N> later */
static int32_t SetSourceBuffer_isra_74(void *arg1, int32_t *arg2, int32_t arg3,
                                       int32_t *arg4); /* forward decl */
int32_t AL_SrcReorder_MarkSrcBufferAsUsed(void *arg1, int32_t arg2); /* forward decl, ported by T<N> later */
int32_t AL_SrcReorder_EndSrcBuffer(void *arg1, int32_t arg2); /* forward decl, ported by T<N> later */
extern void AL_SrcReorder_Cancel(void *arg1, int32_t arg2);
int32_t AL_IntermMngr_ReleaseBuffer(void *arg1, void *arg2); /* forward decl, ported by T<N> later */
extern int32_t AL_EncCore_SetJpegInterrupt(void *arg1);
extern int32_t rc_Iol(void);

typedef struct AL_TIntermBufferCompat {
    int32_t addr;
    int32_t location;
} AL_TIntermBufferCompat;

#define SLICE_PARAM_COPY_SIZE 0x128

static void PrepareSliceParamForCore(void *ch, void *req, const uint8_t *base_slice, uint32_t core,
                                     uint8_t *slice_out)
{
    uint32_t core_count;
    uint32_t partition_count;
    uint32_t lcu_w;
    uint32_t lcu_h;
    uint32_t start_lcu_y;
    uint32_t end_lcu_y;
    uint32_t span_lcu_y;
    uint32_t start_lcu;
    uint32_t last_lcu;
    uint32_t total_lcu;

    if (ch == NULL || req == NULL || base_slice == NULL || slice_out == NULL) {
        return;
    }

    memcpy(slice_out, base_slice, SLICE_PARAM_COPY_SIZE);

    if (ch != NULL && READ_U8(ch, 0x1f) == 0U) {
        /* AVC stock packs MaxCuSize=4 into the low 3 bits of b3. The upper
         * bits in this byte carry unrelated state, so only normalize the
         * low field before SliceParamToCmdRegsEnc1 consumes it. */
        WRITE_U8(slice_out, 3, (READ_U8(slice_out, 3) & 0xf8U) | 4U);
    }

    core_count = READ_U8(ch, 0x3c);
    if (core_count == 0U) {
        core_count = 1U;
    }
    partition_count = core_count;

    lcu_w = READ_U16(slice_out, 0x108);
    lcu_h = READ_U16(slice_out, 0x10a);
    if (lcu_w <= 1U) {
        uint32_t pic_w8 = READ_U16(slice_out, 0x0a);

        lcu_w = pic_w8 != 0U ? ((pic_w8 << 3) + 15U) >> 4 : (READ_U16(ch, 4) + 1U) >> 1;
    }
    if (lcu_h <= 1U) {
        uint32_t pic_h8 = READ_U16(slice_out, 0x0c);

        lcu_h = pic_h8 != 0U ? ((pic_h8 << 3) + 15U) >> 4 : (READ_U16(ch, 6) + 1U) >> 1;
    }
    if (READ_U16(slice_out, 0x108) <= 1U) {
        WRITE_U16(slice_out, 0x108, (uint16_t)lcu_w);
    }
    if (READ_U16(slice_out, 0x10a) <= 1U) {
        WRITE_U16(slice_out, 0x10a, (uint16_t)lcu_h);
    }

    start_lcu_y = (core * lcu_h) / partition_count;
    end_lcu_y = ((core + 1U) * lcu_h) / partition_count;
    if (core + 1U == partition_count) {
        end_lcu_y = lcu_h;
    }
    if (end_lcu_y < start_lcu_y) {
        end_lcu_y = start_lcu_y;
    }

    span_lcu_y = end_lcu_y - start_lcu_y;
    start_lcu = lcu_w * start_lcu_y;
    total_lcu = lcu_w * lcu_h;
    last_lcu = span_lcu_y * lcu_w + start_lcu;
    if (last_lcu == total_lcu && total_lcu >= start_lcu) {
        last_lcu = total_lcu;
    }

    WRITE_U8(slice_out, 0x08, (core + 1U == partition_count) ? 1U : 0U);
    WRITE_U8(slice_out, 0x35, (READ_U32(req, 0x00) >> 4) & 1U);
    WRITE_U8(slice_out, 0x48, READ_U8(req, 0x34));
    WRITE_U8(slice_out, 0x49, READ_U8(req, 0xd1));
    WRITE_S32(slice_out, 0x3c, (int32_t)start_lcu);
    WRITE_S32(slice_out, 0x44, last_lcu > start_lcu ? (int32_t)(last_lcu - 1U) : (int32_t)start_lcu);
    WRITE_U16(slice_out, 0x4c, (uint16_t)span_lcu_y);
    WRITE_S32(slice_out, 0x54, (int32_t)start_lcu);
    WRITE_U32(slice_out, 0xfc, span_lcu_y * lcu_w);
    WRITE_U8(slice_out, 0x72, READ_U8(slice_out, 0x71) ? 1U : (start_lcu == 0U ? 1U : 0U));
    WRITE_U8(slice_out, 0x7e, (uint8_t)core_count);

    if (READ_U16(slice_out, 0x7a) == 0U) {
        WRITE_U16(slice_out, 0x7a, (READ_U16(ch, 4) + 7U) >> 3);
    }
    if (READ_U16(slice_out, 0x7c) == 0U) {
        WRITE_U16(slice_out, 0x7c, READ_U16(slice_out, 0x0c));
    }
    if (READ_U16(slice_out, 0x58) == 0U) {
        WRITE_U16(slice_out, 0x58, 16);
    }
    if (READ_U16(slice_out, 0x5a) == 0U) {
        WRITE_U16(slice_out, 0x5a, 16);
    }
    if (READ_U16(slice_out, 0xa8) == 0U && READ_S32(slice_out, 0x30) != 2) {
        WRITE_U16(slice_out, 0xa8, (READ_U16(ch, 4) + 63U) & ~63U);
    }
    if (READ_U16(slice_out, 0xaa) == 0U && READ_S32(slice_out, 0x30) != 2) {
        uint32_t width_64 = (READ_U16(ch, 4) + 63U) >> 6;
        uint16_t aa = 0x37;

        if (width_64 >= 0x1fU) {
            aa = 0x1f7;
        } else if (width_64 >= 0x15U) {
            aa = 0x0f7;
        } else if (width_64 >= 0x0bU) {
            aa = 0x077;
        }
        WRITE_U16(slice_out, 0xaa, aa);
    }
    if (READ_U8(slice_out, 0xee) == 0U && READ_U8(ch, 0x3e) != 0U) {
        WRITE_U16(slice_out, 0xec, READ_U8(ch, 0x3e));
        WRITE_U8(slice_out, 0xee, 0x20);
    }
    if (READ_U32(slice_out, 0xfc) == 0U) {
        WRITE_U32(slice_out, 0xfc, lcu_w * (READ_U32(slice_out, 0x4c) ? READ_U32(slice_out, 0x4c) : 1U));
    }
}

static uint16_t DefaultEnc2Slice78ForLcuHeight(uint16_t lcu_h)
{
    if (lcu_h == 68U) {
        return 10U;
    }
    if (lcu_h == 23U) {
        return 7U;
    }
    if (lcu_h <= 23U) {
        return 7U;
    }
    if (lcu_h >= 68U) {
        return 10U;
    }
    return (uint16_t)(7U + (((uint32_t)(lcu_h - 23U) * 3U + 22U) / 45U));
}

static void RepairStandaloneAvcEnc2Slice(void *ch, uint8_t *slice)
{
    uint16_t lcu_h;

    if (slice == NULL) {
        return;
    }

    lcu_h = READ_U16(slice, 0x10a);
    if (lcu_h <= 1U) {
        uint16_t pic_h8 = READ_U16(slice, 0x0c);

        if (pic_h8 != 0U) {
            lcu_h = (uint16_t)((((uint32_t)pic_h8 << 3) + 15U) >> 4);
        } else if (ch != NULL) {
            lcu_h = (uint16_t)((READ_U16(ch, 6) + 1U) >> 1);
        }
    }

    /* Stock 1080p CLs keep these late Entropy slice fields stable even when
     * the shared runtime slice object does not materialize them for us. */
    WRITE_U16(slice, 0x74, 0x0c80);
    WRITE_U16(slice, 0x78, DefaultEnc2Slice78ForLcuHeight(lcu_h));
    WRITE_U8(slice, 0x10, 1U);
    WRITE_U8(slice, 0x12, 1U);
    WRITE_U8(slice, 0x19, 8U);
    WRITE_U8(slice, 0x1a, 8U);
    WRITE_U32(slice, 0x1c, 1U);
    WRITE_U32(slice, 0x30, (READ_U32(slice, 0x24) & 1U) != 0U ? 2U : 1U);
    WRITE_U8(slice, 0x66, 0U);
    WRITE_U8(slice, 0xf6, 1U);
}

uint32_t InitMERange(int32_t arg1, void *arg2)
{
    int32_t v0_3;
    uint8_t *me_range = (uint8_t *)(intptr_t)arg1 + CH_ME_RANGE_OFF;

    if ((uint32_t)READ_U8(arg2, 0x1f) != 0U) {
        int16_t v1_5 = READ_S16(arg2, 6);
        int16_t a2_2 = READ_S16(arg2, 4);

        WRITE_U16(me_range, 0x00, (uint16_t)a2_2);
        WRITE_U16(me_range, 0x02, (uint16_t)a2_2);
        WRITE_U16(me_range, 0x04, (uint16_t)v1_5);
        WRITE_U16(me_range, 0x06, (uint16_t)v1_5);
        v0_3 = READ_S32(arg2, 0x2c);

        if ((v0_3 & 0x20) != 0) {
            goto label_63fa4;
        }
    } else {
        uint32_t v0_1 = (uint32_t)READ_U8(arg2, 0x20);
        int16_t v0_2;
        int16_t a2;

        WRITE_U16(me_range, 0x02, 0x780);
        WRITE_U16(me_range, 0x00, 0x780);
        if (v0_1 >= 0xbU) {
            if (v0_1 >= 0x15U) {
                int32_t v1_9 = (v0_1 < 0x1fU) ? 1 : 0;

                a2 = 0x1ef;
                if (v1_9 != 0) {
                    a2 = 0xef;
                }

                v0_2 = 0x1f7;
                if (v1_9 != 0) {
                    v0_2 = 0xf7;
                }
            } else {
                a2 = 0x6f;
                v0_2 = 0x77;
            }
        } else {
            a2 = 0x2f;
            v0_2 = 0x37;
        }

        WRITE_U16(me_range, 0x04, (uint16_t)v0_2);
        v0_3 = READ_S32(arg2, 0x2c);
        WRITE_U16(me_range, 0x06, (uint16_t)a2);
        if ((v0_3 & 0x20) != 0) {
label_63fa4:
            if ((uint32_t)READ_U16(me_range, 0x04) >= 0xe1U) {
                WRITE_U16(me_range, 0x04, 0xe0);
            }

            if ((uint32_t)READ_U16(me_range, 0x06) >= 0xe1U) {
                WRITE_U16(me_range, 0x06, 0xe0);
            }
        }
    }

    {
        uint32_t result = 0;

        if (((uint32_t)v0_3 >> 0x11 & 1U) != 0U) {
            uint32_t v1_4 = (uint32_t)READ_U16(arg2, 0x42);
            uint32_t t4_1 = (uint32_t)READ_U16(me_range, 0x00);
            uint32_t t3_1 = (uint32_t)READ_U16(me_range, 0x02);
            uint32_t result_2 = (uint32_t)READ_U16(me_range, 0x04);
            uint32_t result_1 = (uint32_t)READ_U16(me_range, 0x06);

            result = (uint32_t)READ_U16(arg2, 0x44);
            if ((int32_t)v1_4 < (int32_t)t4_1) {
                t4_1 = v1_4;
            }
            if ((int32_t)result < (int32_t)result_2) {
                result_2 = result;
            }
            if ((int32_t)v1_4 >= (int32_t)t3_1) {
                v1_4 = t3_1;
            }
            if ((int32_t)result >= (int32_t)result_1) {
                result = result_1;
            }

            WRITE_U16(me_range, 0x00, (uint16_t)t4_1);
            WRITE_U16(me_range, 0x02, (uint16_t)v1_4);
            WRITE_U16(me_range, 0x04, (uint16_t)result_2);
            WRITE_U16(me_range, 0x06, (uint16_t)result);
        }

        return result;
    }
}

int32_t FillSliceParamFromPicParam(int32_t *arg1, void *arg2, int32_t *arg3)
{
    void *var_20 = &_gp;
    int32_t s2 = 0;
    uint32_t a1 = (uint32_t)arg3[0x14];
    int32_t v0 = (int32_t)READ_S8(arg1, 0xe4);
    int32_t v0_1;
    int32_t t2;
    int32_t result;

    (void)var_20;
    if ((uint32_t)READ_U8(arg1, 0x7c) == 1U) {
        s2 = ((arg3[0xc] ^ 7) < 1) ? 1 : 0;
    }

    if (a1 != 0U) {
        s2 = 1;
    }

    if (v0 == -1) {
        char v0_16 = 2;

        if (arg3[0xc] != 0) {
            v0_16 = 3;
        }

        WRITE_U8(arg2, 0xf, (uint8_t)v0_16);
    } else {
        WRITE_U8(arg2, 0xf, (uint8_t)v0);
    }

    v0_1 = arg3[0xc];
    if (v0_1 == 2) {
        char v1_11 = READ_S8(arg3, 0x364);

        WRITE_U8(arg2, 0x21, 0);
        WRITE_U8(arg2, 0x20, (uint8_t)v1_11);
label_641f0:
        {
            void *rc_mutex = (void *)(uintptr_t)arg1[0x48];

            WRITE_S32(arg2, 0x30, v0_1);
            ENC_KMSG("FillSliceParam rc_pre ch=%p rc_cb=%p rc_mutex=%p pic=%p slice=%p pict_type=%d scene=%u",
                     arg1, (void *)(intptr_t)arg1[0x41], rc_mutex, &arg3[8], arg2,
                     v0_1, (unsigned)READ_U8(arg3, 0x364));
            Rtos_GetMutex(rc_mutex);
            ((void (*)(void *, void *, void *))(intptr_t)arg1[0x41])(&arg1[0x3d], &arg3[8],
                                                                      (uint8_t *)arg2 + 0x28);
            ENC_KMSG("FillSliceParam rc_unlock_pre ch=%p locked=%p current=%p",
                     arg1, rc_mutex, (void *)(uintptr_t)arg1[0x48]);
            Rtos_ReleaseMutex(rc_mutex);
            ENC_KMSG("FillSliceParam rc_unlock_post ch=%p locked=%p current=%p",
                     arg1, rc_mutex, (void *)(uintptr_t)arg1[0x48]);
            ENC_KMSG("FillSliceParam rc_post ch=%p pic=%p slice=%p rc_type=%d qp=%d",
                     arg1, &arg3[8], arg2, READ_S32(arg2, 0x30), READ_S16((uint8_t *)arg2 + 0x28, 4));
            t2 = READ_S32(arg2, 0x30);
        }
    } else {
        a1 = (uint32_t)READ_U8(arg3, 0x364);
        WRITE_U8(arg2, 0x21, (uint8_t)((READ_U32(arg3, 0) >> 3) & 1U));
        WRITE_U8(arg2, 0x20, (uint8_t)a1);
        if (v0_1 != 7) {
            goto label_641f0;
        }

        WRITE_S32(arg2, 0x30, 1);
        WRITE_S32(arg2, 0x28, 0x1a);
        t2 = 1;
    }

    if (s2 != 0) {
        WRITE_S32(arg2, 0x38, 1);
    }

    {
        uint32_t v0_2 = (uint32_t)READ_U8(arg3, 0xcc);
        uint32_t v0_4 = (uint32_t)READ_U8(arg3, 0xcd);
        char v0_8;
        char a1_1;
        char a0_1;
        char v1_7;
        int32_t v0_13;

        WRITE_S32(arg2, 0x3c, 0);
        if (v0_2 != 0U) {
            v0_2 = (uint32_t)((uint8_t)v0_2 - 1U);
        }
        WRITE_U8(arg2, 0x41, (uint8_t)v0_2);
        if (v0_4 != 0U) {
            v0_4 = (uint32_t)((uint8_t)v0_4 - 1U);
        }
        WRITE_U8(arg2, 0x40, (uint8_t)v0_4);
        v0_8 = (((uint32_t)arg3[0x1b] ^ 1U) < 1U) ? 1 : 0;
        WRITE_U8(arg2, 0x8e, (((uint32_t)arg3[0x16] ^ 1U) < 1U) ? 1 : 0);
        WRITE_U8(arg2, 0x96, (uint8_t)v0_8);
        WRITE_U8(arg2, 0x8c, READ_U8(arg3, 0xce));
        WRITE_U8(arg2, 0x94, READ_U8(arg3, 0xcf));
        a1_1 = READ_S8(arg3, 0xdb);
        a0_1 = READ_S8(arg3, 0xdc);
        v1_7 = READ_S8(arg3, 0xdd);
        WRITE_U8(arg2, 0x5f, READ_U8(arg3, 0xda));
        WRITE_U8(arg2, 0x60, (uint8_t)a1_1);
        WRITE_U8(arg2, 0x61, (uint8_t)a0_1);
        WRITE_U8(arg2, 0x62, (uint8_t)v1_7);
        if ((arg3[9] & 2) != 0) {
            WRITE_U8(arg2, 0x69, 0);
            WRITE_U8(arg2, 0x6a, 0);
        }

        v0_13 = arg3[0x21];
        if (~(uint32_t)v0_13 == 0U) {
            v0_13 = 0;
        }

        {
            int32_t a0_2 = arg3[0x17];
            int32_t v1_8 = arg3[0x1c];
            int32_t t1 = arg3[0x22];
            int32_t t0 = arg3[0x23];
            int32_t a3 = arg3[0x14];
            int16_t a2_1 = READ_S16(arg3, 0x138);
            uint32_t word9 = (uint32_t)arg3[9];
            uint32_t word11 = (uint32_t)arg3[0xb];
            uint32_t word12 = (uint32_t)arg3[0x12];

            if (~(uint32_t)a0_2 == 0U) {
                a0_2 = 0;
            }
            if (~(uint32_t)v1_8 == 0U) {
                v1_8 = 0;
            }

            WRITE_S32(arg2, 0x84, arg3[0xa]);
            WRITE_S32(arg2, 0x88, a0_2);
            WRITE_S32(arg2, 0x90, v1_8);
            WRITE_S32(arg2, 0x98, v0_13);
            WRITE_S32(arg2, 0xa0, t1);
            WRITE_S32(arg2, 0xa4, t0);
            WRITE_S32(arg2, 0xf0, a3);
            WRITE_U16(arg2, 0xf4, (uint16_t)a2_1);

            if (a3 == 0) {
                WRITE_U8(arg2, 0x60, (uint8_t)((word9 >> 0xf) & 1U));
                WRITE_U8(arg2, 0x61, (uint8_t)((word9 >> 0x12) & 1U));
                WRITE_U8(arg2, 0x62, (uint8_t)((word9 >> 0xb) & 1U));
            } else if (a3 == 1) {
                WRITE_U8(arg2, 0x5f, (uint8_t)((word9 >> 0xf) & 1U));
                WRITE_U8(arg2, 0x65, (uint8_t)((word9 >> 0x12) & 1U));
            }

            WRITE_U8(arg2, 0x63, (uint8_t)((word9 >> 0x10) & 1U));
            WRITE_U8(arg2, 0x64, (uint8_t)((word9 >> 0x11) & 1U));
            WRITE_U8(arg2, 0x66, (uint8_t)((word9 >> 0x13) & 1U));
            WRITE_U8(arg2, 0x67, (uint8_t)((word9 >> 0x14) & 1U));
            WRITE_U8(arg2, 0x69, (uint8_t)((word9 >> 0x16) & 1U));
            WRITE_U8(arg2, 0x6a, (uint8_t)((word9 >> 0x17) & 1U));
            WRITE_U8(arg2, 0x6b, (uint8_t)((word9 >> 0x18) & 1U));
            WRITE_U8(arg2, 0x6c, (uint8_t)((word9 >> 0x19) & 1U));
            WRITE_U8(arg2, 0x6d, (uint8_t)((word9 >> 0x1a) & 1U));
            WRITE_U8(arg2, 0x6e, (uint8_t)((word9 >> 0x1b) & 1U));
            WRITE_U8(arg2, 0x6f, (uint8_t)((word9 >> 0x1c) & 1U));
            WRITE_U8(arg2, 0x70, (uint8_t)((word9 >> 0x1d) & 1U));
            WRITE_U8(arg2, 0x71, (uint8_t)((word9 >> 0x1e) & 1U));
            WRITE_U8(arg2, 0x72, (uint8_t)((word9 >> 0x1f) & 1U));
            WRITE_U16(arg2, 0x74, (uint16_t)arg3[0xa]);
            WRITE_U8(arg2, 0x7e, (uint8_t)(((word11 >> 0x18) & 3U) + 1U));
            WRITE_U8(arg2, 0x7f, (uint8_t)((word11 >> 0x1e) & 1U));
            WRITE_U8(arg2, 0x80, (uint8_t)((word11 >> 0x1f) & 1U));
            WRITE_U16(arg2, 0x7a, (uint16_t)(word11 & 0x3ffU));
            WRITE_U16(arg2, 0x7c, (uint16_t)(((word11 >> 0xc) & 0x3ffU) + 1U));
            WRITE_U16(arg2, 0xa8, (uint16_t)((((word12 & 0x3ffU) + 1U) << 6) & 0xffffU));
            WRITE_U16(arg2, 0xaa, (uint16_t)((((((word12 >> 0xc) & 0x3ffU) + 1U) << 3) & 0xffffU)));
            WRITE_U8(arg2, 0xac, (uint8_t)((word12 >> 0x1e) & 1U));
            ENC_KMSG("FillSliceParam slice_tail pic=%p slice=%p pic9=%08x picb=%08x pic12=%08x s63=%u 7a=%u 7c=%u a8=%u aa=%u ac=%u",
                     arg3, arg2, (unsigned)word9, (unsigned)word11, (unsigned)word12,
                     (unsigned)READ_U8(arg2, 0x63), (unsigned)READ_U16(arg2, 0x7a),
                     (unsigned)READ_U16(arg2, 0x7c), (unsigned)READ_U16(arg2, 0xa8),
                     (unsigned)READ_U16(arg2, 0xaa), (unsigned)READ_U8(arg2, 0xac));
        }
    }

    if (t2 == 2) {
        result = *arg1;
        if (t2 == 2 && result <= 0) {
            return result;
        }
    }

    result = ((uint32_t)(arg3[0x1b] ^ 2) > 0U) ? 1 : 0;
    WRITE_U8(arg2, 0x8f, ((uint32_t)(arg3[0x16] ^ 2) > 0U) ? 1 : 0);
    WRITE_U8(arg2, 0x97, (uint8_t)result);
    return result;
}

uint32_t SetTileOffsets(void *arg1)
{
    uint32_t t4 = (uint32_t)READ_U8(arg1, 0x4e);
    uint32_t t1 = (uint32_t)READ_U8(arg1, 0x3c);
    int32_t t5 = 1 << (t4 & 0x1f);
    int32_t a3 = 1 << ((6 - t4) & 0x1f);
    int32_t t2_3 = (READ_S32(arg1, 4) + t5 - 1) >> (t4 & 0x1f);

    if (READ_S32(arg1, 0x35b8) < (int32_t)t1 - 1) {
        return ResetChannelParam((void *)(intptr_t)__assert(
            "(pCtx->ChanParam.uNumCore - 1) <= pCtx->iIpCoresCount",
            "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/ChannelMngr.c", 0x851,
            "SetTileOffsets", &_gp));
    }

    {
        int32_t a1 = 0;
        int32_t clear_idx = 0;

        while (clear_idx < 0x10) {
            WRITE_U16(arg1, 0x1b28 + clear_idx * 2, 0);
            clear_idx += 1;
        }
        WRITE_S32(arg1, 0x1ae8, 0);

        if ((int32_t)t1 - 1 > 0) {
            int32_t *t0_1 = (int32_t *)((uint8_t *)arg1 + 0x12f54);
            int16_t *v1_1 = (int16_t *)((uint8_t *)arg1 + 0x12d7c);
            int32_t a2_2 = t2_3 + (int32_t)t1 - 1;
            int16_t *t3_3 = (int16_t *)((uint8_t *)arg1 + (((int32_t)t1 + 0x96bd) << 1));

            while (1) {
                int32_t v0_7;

                if (t1 == 0) {
                    __builtin_trap();
                }

                *t0_1 = 0;
                v0_7 = a2_2 / (int32_t)t1 - a1;
                *v1_1 = (int16_t)a1;
                if (v0_7 >= 0) {
                    int32_t v0_5;

                    if (a3 == 0) {
                        __builtin_trap();
                    }

                    v1_1 = &v1_1[1];
                    t0_1 = &t0_1[1];
                    a2_2 += t2_3;
                    v0_5 = ((a3 + v0_7) - 1) / a3 * a3;
                    /* HLIL used a byte offset here. Writing through the typed
                     * int16_t* as "-0x22 elements" shifts the tile-width table
                     * down to 0x12d3a instead of the live 0x12d5c array. */
                    WRITE_U16((uint8_t *)v1_1, -0x22, (uint16_t)v0_5);
                    a1 += v0_5;
                    if (v1_1 == t3_3) {
                        break;
                    }
                } else {
                    int32_t v0_9;

                    if (a3 == 0) {
                        __builtin_trap();
                    }

                    v1_1 = &v1_1[1];
                    t0_1 = &t0_1[1];
                    a2_2 += t2_3;
                    v0_9 = v0_7 / a3 * a3;
                    WRITE_U16((uint8_t *)v1_1, -0x22, (uint16_t)v0_9);
                    a1 += v0_9;
                    if (v1_1 == t3_3) {
                        break;
                    }
                }
            }

            a1 &= 0xffff;
        }

        WRITE_S32(arg1, (((int32_t)t1 + 0x4bd3) << 2) + 4, 0);
        WRITE_U16(arg1, ((int32_t)t1 << 1) + 0x12d7a, (uint16_t)a1);
        WRITE_U16(arg1, ((int32_t)t1 << 1) + 0x12d5a, (uint16_t)(t2_3 - a1));
        WRITE_S32(arg1, 0x1ae8, (int32_t)(READ_U16(arg1, 0x12d5c) & 0x7fffU));

        {
            uint32_t v0_13 = (uint32_t)READ_U16(arg1, 6);
            int32_t t4_1 = (v0_13 + t5 - 1) >> (t4 & 0x1f);

            if (t1 == 1U) {
                WRITE_U16(arg1, 0x12d9c, (uint16_t)t4_1);
                WRITE_U16(arg1, 0x1b28, (uint16_t)t4_1);
                ENC_KMSG("SetTileOffsets cores=%u cols=[%u %u %u %u %u] rows=[%u %u %u %u %u]",
                         (unsigned)t1,
                         (unsigned)READ_U32(arg1, 0x1ae8),
                         (unsigned)READ_U16(arg1, 0x12d5c), (unsigned)READ_U16(arg1, 0x12d5e),
                         (unsigned)READ_U16(arg1, 0x12d60), (unsigned)READ_U16(arg1, 0x12d62),
                         (unsigned)READ_U16(arg1, 0x1b28), (unsigned)READ_U16(arg1, 0x1b2a),
                         (unsigned)READ_U16(arg1, 0x1b2c), (unsigned)READ_U16(arg1, 0x1b2e),
                         (unsigned)READ_U16(arg1, 0x1b30));
                return v0_13;
            }

            {
                uint32_t a1_1 = (uint32_t)READ_U8(arg1, 0x40);
                int32_t v0_14 = (int32_t)a1_1 - 1;

                if (a1_1 != 0U) {
                    int16_t *v1_2 = (int16_t *)((uint8_t *)arg1 + 0x12d9c);
                    int16_t *a0 = (int16_t *)((uint8_t *)arg1 + (((v0_14 & 0xffff) + 0x96cf) << 1));
                    int32_t row_idx = 0;

                    v0_14 = 0;
                    do {
                        int16_t lo_4 = (int16_t)(v0_14 / (int32_t)a1_1);
                        uint16_t row_span;

                        if (a1_1 == 0U) {
                            __builtin_trap();
                        }

                        v0_14 += t4_1;
                        v1_2 = &v1_2[1];
                        if (a1_1 == 0U) {
                            __builtin_trap();
                        }

                        row_span = (uint16_t)((int16_t)(v0_14 / (int32_t)a1_1) - lo_4);
                        *(v1_2 - 1) = (int16_t)row_span;
                        WRITE_U16(arg1, 0x1b28 + row_idx * 2, row_span);
                        row_idx += 1;
                    } while (a0 != v1_2);
                }

                ENC_KMSG("SetTileOffsets cores=%u cols=[%u %u %u %u %u] rows=[%u %u %u %u %u]",
                         (unsigned)t1,
                         (unsigned)READ_U32(arg1, 0x1ae8),
                         (unsigned)READ_U16(arg1, 0x12d5c), (unsigned)READ_U16(arg1, 0x12d5e),
                         (unsigned)READ_U16(arg1, 0x12d60), (unsigned)READ_U16(arg1, 0x12d62),
                         (unsigned)READ_U16(arg1, 0x1b28), (unsigned)READ_U16(arg1, 0x1b2a),
                         (unsigned)READ_U16(arg1, 0x1b2c), (unsigned)READ_U16(arg1, 0x1b2e),
                         (unsigned)READ_U16(arg1, 0x1b30));
                return (uint32_t)v0_14;
            }
        }
    }
}

int32_t ResetChannelParam(void *arg1)
{
    void *var_18 = &_gp;

    (void)var_18;
    StaticFifo_Init((StaticFifoCompat *)((uint8_t *)arg1 + 0x129b4), (int32_t *)((uint8_t *)arg1 + 0x12968), 0x13);
    StaticFifo_Init((StaticFifoCompat *)((uint8_t *)arg1 + 0x12a6c), (int32_t *)((uint8_t *)arg1 + 0x12a20), 0x13);
    StaticFifo_Init((StaticFifoCompat *)((uint8_t *)arg1 + 0x12a10), (int32_t *)((uint8_t *)arg1 + 0x129c4), 0x13);
    StaticFifo_Init((StaticFifoCompat *)((uint8_t *)arg1 + 0x12ac8), (int32_t *)((uint8_t *)arg1 + 0x12a7c), 0x13);
    StaticFifo_Init((StaticFifoCompat *)((uint8_t *)arg1 + 0x12b24), (int32_t *)((uint8_t *)arg1 + 0x12ad8), 0x13);
    WRITE_S32(arg1, 0x35bc, 0);
    WRITE_S32(arg1, 0x12960, 0);
    WRITE_S32(arg1, 0x35b0, 0);
    WRITE_S32(arg1, 0x35b4, 0);
    WRITE_S32(arg1, 0x3de0, 1);
    RequestsBuffer_Init((uint8_t *)arg1 + 0x3de8);
    EndRequestsBuffer_Init((uint8_t *)arg1 + 0x12878);
    return 0;
}

uint32_t InitHwRC_Content(void *arg1, void *arg2)
{
    int32_t *s4 = (int32_t *)((uint8_t *)arg1 + 0x35d0);
    int32_t i;
    uint32_t i_1;
    int32_t v1;
    uint32_t s0;

    for (i = 0; i != 3; ) {
        int32_t i_2 = i;

        i += 1;
        ENC_KMSG("InitHwRC_Content preprocess idx=%d dst=%p", i_2, (void *)(intptr_t)(*s4));
        PreprocessHwRateCtrl((int32_t *)((uint8_t *)arg2 + 0x68),
                             (int32_t)(intptr_t)((uint8_t *)arg2 + 0xa8),
                             (int32_t)READ_U8(arg2, 0x3c), i_2, (void *)(intptr_t)(*s4));
        ENC_KMSG("InitHwRC_Content preprocess done idx=%d dst=%p", i_2, (void *)(intptr_t)(*s4));
        s4 = &s4[1];
    }

    i_1 = (uint32_t)READ_U8(arg2, 0x3c);
    v1 = 0xd48;
    if ((uint32_t)READ_U8(arg2, 0x1f) == 0U) {
        v1 = 0x352;
    }

    s0 = 0;
    if (i_1 != 0U) {
        do {
            uint32_t tile_cols = READ_U32(arg1, 0x1ae8);

            ENC_KMSG("InitHwRC_Content init idx=%u hwrc=%p tile_cols=%u",
                     (unsigned)s0, (uint8_t *)arg1 + s0 * 0x78 + 0x35e4, (unsigned)tile_cols);
            InitHwRateCtrl((uint8_t *)arg1 + s0 * 0x78 + 0x35e4, (uint8_t *)arg2 + 0x68,
                           (int32_t)(intptr_t)((uint8_t *)arg2 + 0xa8),
                           (int32_t)READ_U16(arg2, 4), (int32_t)READ_U16(arg2, 6),
                           (int32_t)READ_U8(arg2, 0x4e), v1, (int32_t)tile_cols, &_gp);
            ENC_KMSG("InitHwRC_Content init done idx=%u hwrc=%p",
                     (unsigned)s0, (uint8_t *)arg1 + s0 * 0x78 + 0x35e4);
            i_1 = (uint32_t)READ_U8(arg2, 0x3c);
            s0 = (uint32_t)((uint8_t)s0 + 1U);
        } while (s0 < i_1);
    }

    if (i_1 != 0U) {
        i_1 = 0;

        {
            int32_t t0_2 = (READ_S32(arg1, 0x35b8) < 2) ? 1 : 0;

            do {
                uint32_t t2_1 = i_1 << 3;

                while (1) {
                    uint32_t v1_10 = i_1 << 7;
                    uint8_t *a1_6 = (uint8_t *)arg1 + v1_10 - t2_1;

                    if (t0_2 == 0) {
                        break;
                    }

                    {
                        int32_t a3_2 = (int32_t)i_1 + 1;
                        char t1_2 = 0;
                        char a1_4 = 0;
                        uint8_t *v1_7 = (uint8_t *)arg1 + v1_10 - t2_1;

                        if (i_1 != 0U) {
                            t1_2 = ((uint32_t)READ_U8(arg1, (int32_t)(i_1 - 1U) * 0x78 + 0x35fa) < 2U) ? 1 : 0;
                        }

                        *(v1_7 + 0x3654) = (uint8_t)t1_2;
                        *(v1_7 + 0x3618) = (uint8_t)t1_2;
                        if ((int32_t)i_1 < (int32_t)READ_U8(arg1, 0x3c) - 1) {
                            a1_4 = ((uint32_t)READ_U8(arg1, a3_2 * 0x78 + 0x35fa) < 2U) ? 1 : 0;
                        }

                        *(v1_7 + 0x3656) = (uint8_t)i_1;
                        *(v1_7 + 0x361a) = (uint8_t)i_1;
                        *(v1_7 + 0x3655) = (uint8_t)a1_4;
                        *(v1_7 + 0x3619) = (uint8_t)a1_4;
                        i_1 = (uint32_t)(a3_2 & 0xff);
                        if (i_1 >= (uint32_t)READ_U8(arg2, 0x3c)) {
                            return i_1;
                        }
                    }
                }

                {
                    uint8_t *v1_13 = (uint8_t *)arg1 + i_1 * 0x78;

                    *(v1_13 + 0x3654) = 0;
                    *(v1_13 + 0x3618) = 0;
                    *(v1_13 + 0x3655) = 0;
                    *(v1_13 + 0x3619) = 0;
                    *(v1_13 + 0x3656) = 0;
                    *(v1_13 + 0x361a) = 0;
                    i_1 = (uint32_t)((uint8_t)i_1 + 1U);
                    if (i_1 >= (uint32_t)READ_U8(arg2, 0x3c)) {
                        return i_1;
                    }
                }
            } while (1);
        }
    }

    return i_1;
}

void *getFifoRunning(void *arg1, int32_t arg2)
{
    void *s0 = (uint8_t *)arg1 + 0x12a6c;
    void *var_18 = &_gp;
    int32_t v0 = StaticFifo_Front((StaticFifoCompat *)s0);
    void *v0_3;
    int32_t v1_1;

    (void)var_18;
    if (v0 != 0) {
        v1_1 = READ_S32((void *)(intptr_t)v0, 0xa70);
        v0_3 = s0;
        if (v0 != 0 && READ_S32((void *)(intptr_t)v0, (((v1_1 * 0x44) + arg2 + 0x230) << 2) + 0x10) > 0) {
            return v0_3;
        }
    }

    {
        void *s0_1 = (uint8_t *)arg1 + 0x12ac8;
        int32_t v0_4 = StaticFifo_Front((StaticFifoCompat *)s0_1);

        if (v0_4 == 0) {
            return 0;
        }

        v0_3 = 0;
        if (READ_S32((void *)(intptr_t)v0_4,
                     (((READ_S32((void *)(intptr_t)v0_4, 0xa70) * 0x44) + arg2 + 0x230) << 2) + 0x10) > 0) {
            return s0_1;
        }

        return v0_3;
    }
}

uint32_t SetPictureReferences(void *arg1, void *arg2)
{
    void *var_280 = &_gp;
    int32_t var_278;
    int32_t str[0x33];

    (void)var_280;
    AL_RefMngr_GetAvailRef((uint8_t *)arg1 + 0x22c8, (uint8_t *)arg2 + 0x20, &var_278);
    memset(&str, 0, 0xcc);
    Rtos_GetMutex(READ_PTR(arg1, 0x170));
    ((void (*)(void *, void *, void *, void *, void *))(intptr_t)READ_S32(arg1, 0x13c))((uint8_t *)arg1 + 0x128,
                                                                                        (uint8_t *)arg2 + 0x20,
                                                                                        &var_278,
                                                                                        (uint8_t *)arg2 + 0x54, &str);
    Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
    WRITE_U8(arg2, 0x164, (uint8_t)str[0] + READ_U8(&str, 0x60));

    {
        uint32_t result = (uint32_t)READ_U8(arg1, 0x1f);

        if (result == 1U) {
            return (uint32_t)AL_RefMngr_UpdateDPB((uint8_t *)arg1 + 0x22c8, &str[0]);
        }

        if (result == 0U) {
            result = (uint32_t)AL_DPB_AVC_CheckMMCORequired((uint8_t *)arg1 + 0x22c8, (uint8_t *)arg2 + 0x20, &str[0]);
            WRITE_S32(arg2, 0xd4, (int32_t)result);
        }

        return result;
    }
}

int32_t ReleaseRefBuffers(void *arg1, void *arg2)
{
    void *var_18 = &_gp;
    int32_t result;

    (void)var_18;
    if (READ_S32(arg2, 0x2c0) != 0) {
        AL_RefMngr_ReleaseFrmBuffer((uint8_t *)arg1 + 0x22c8, (char)READ_U8(arg2, 0x291));
    }

    if (READ_S32(arg2, 0x2d0) != 0) {
        AL_RefMngr_ReleaseFrmBuffer((uint8_t *)arg1 + 0x22c8, (char)READ_U8(arg2, 0x292));
    }

    result = READ_S32(arg2, 0x2f0);
    if (result != 0) {
        return AL_RefMngr_ReleaseFrmBuffer((uint8_t *)arg1 + 0x22c8, (char)READ_U8(arg2, 0x293));
    }

    return result;
}

void *SetPictureRefBuffers(void *arg1, void *arg2, void *arg3, void *arg4, char arg5, void *arg6)
{
    uint32_t s5 = (uint32_t)(uint8_t)arg5;
    void *var_30 = &_gp;
    uint32_t var_28;

    (void)var_30;
    ENC_KMSG("SetPictureRefBuffers entry chctx=%p req=%p cur=%u info=%p work=%p rec=%u",
             arg1, arg2, s5, arg4, arg6, (unsigned)READ_U8(arg2, 0x290));
    AL_RefMngr_GetFrmBufAddrs((uint8_t *)arg1 + 0x22c8, (char)s5, (int32_t *)((uint8_t *)arg6 + 0x48),
                              (int32_t *)((uint8_t *)arg6 + 0x4c), (uint8_t *)arg6 + 0x74);
    ENC_KMSG("SetPictureRefBuffers cur-addrs rec=%u y=0x%x uv=0x%x trace=0x%x/0x%x/%u",
             s5, READ_S32(arg6, 0x48), READ_S32(arg6, 0x4c),
             READ_S32(arg6, 0x74), READ_S32(arg6, 0x78), (unsigned)READ_U8(arg6, 0x7c));

    {
        int32_t v0_1 = AL_RefMngr_GetMvBufAddr((uint8_t *)arg1 + 0x22c8, (char)s5, (int32_t *)((uint8_t *)arg6 + 0x84));
        int32_t v1 = READ_S32(arg6, 0x84);
        int32_t a1_2 = READ_S32(arg3, 0x1c);
        int32_t v0_2;
        int32_t a1_6;
        int32_t v0_3;
        int32_t v0_4;
        int32_t v0_5;
        int32_t v1_2;

        WRITE_S32(arg6, 0x5c, v0_1);
        ENC_KMSG("SetPictureRefBuffers cur-mv rec=%u mv=0x%x coloc=%p",
                 s5, v0_1, (void *)(intptr_t)v1);
        ENC_KMSG("SetPictureRefBuffers before-ref-info mode=0x%x", READ_S32(arg3, 0x1c));
        AL_RefMngr_GetRefInfo((int32_t)(intptr_t)((uint8_t *)arg1 + 0x22c8), a1_2, (uint8_t *)arg4 + 0x20,
                              (uint8_t *)arg4 + 0x54, (int32_t *)(intptr_t)v1);
        ENC_KMSG("SetPictureRefBuffers after-ref-info pocL0=%d pocL1=%d coloc0=%d coloc1=%d",
                 READ_S32(arg4, 0x5c), READ_S32(arg4, 0x70),
                 READ_S32(arg4, 0x84), READ_S32(arg4, 0x88));
        ENC_KMSG("SetPictureRefBuffers before-ref0-from-poc poc=%d", READ_S32(arg4, 0x5c));
        v0_2 = AL_RefMngr_GetRefBufferFromPOC((uint8_t *)arg1 + 0x22c8, READ_S32(arg4, 0x5c));
        ENC_KMSG("SetPictureRefBuffers after-ref0-from-poc buf=%d", v0_2);
        AL_RefMngr_GetFrmBuffer((uint8_t *)arg1 + 0x22c8, (char)v0_2);
        WRITE_U8(arg2, 0x291, (uint8_t)v0_2);
        AL_RefMngr_GetFrmBufAddrs((uint8_t *)arg1 + 0x22c8, (char)v0_2, (int32_t *)((uint8_t *)arg6 + 0x28),
                                  (int32_t *)((uint8_t *)arg6 + 0x2c), 0);
        ENC_KMSG("SetPictureRefBuffers ref0 buf=%d y=0x%x uv=0x%x",
                 v0_2, READ_S32(arg6, 0x28), READ_S32(arg6, 0x2c));
        WRITE_U8(arg2, 0x290, (uint8_t)s5);
        a1_6 = READ_S32(arg4, 0x70);
        WRITE_S32(arg6, 0x50, 0);
        WRITE_S32(arg6, 0x54, 0);
        WRITE_S32(arg6, 0x30, 0);
        WRITE_S32(arg6, 0x34, 0);
        WRITE_S32(arg6, 0x40, 0);
        WRITE_S32(arg6, 0x44, 0);
        ENC_KMSG("SetPictureRefBuffers before-ref1-from-poc poc=%d", a1_6);
        v0_3 = AL_RefMngr_GetRefBufferFromPOC((uint8_t *)arg1 + 0x22c8, a1_6);
        ENC_KMSG("SetPictureRefBuffers after-ref1-from-poc buf=%d", v0_3);
        AL_RefMngr_GetFrmBuffer((uint8_t *)arg1 + 0x22c8, (char)v0_3);
        WRITE_U8(arg2, 0x292, (uint8_t)v0_3);
        AL_RefMngr_GetFrmBufAddrs((uint8_t *)arg1 + 0x22c8, (char)v0_3, (int32_t *)((uint8_t *)arg6 + 0x38),
                                  (int32_t *)((uint8_t *)arg6 + 0x3c), 0);
        ENC_KMSG("SetPictureRefBuffers ref1 buf=%d y=0x%x uv=0x%x",
                 v0_3, READ_S32(arg6, 0x38), READ_S32(arg6, 0x3c));
        ENC_KMSG("SetPictureRefBuffers before-coloc-from-poc poc=%d", READ_S32(arg4, 0x84));
        v0_4 = AL_RefMngr_GetRefBufferFromPOC((uint8_t *)arg1 + 0x22c8, READ_S32(arg4, 0x84));
        ENC_KMSG("SetPictureRefBuffers after-coloc-from-poc buf=%d", v0_4);
        AL_RefMngr_GetFrmBuffer((uint8_t *)arg1 + 0x22c8, (char)v0_4);
        WRITE_U8(arg2, 0x293, (uint8_t)v0_4);
        v0_5 = AL_RefMngr_GetMvBufAddr((uint8_t *)arg1 + 0x22c8, (char)v0_4, (int32_t *)&var_28);
        ENC_KMSG("SetPictureRefBuffers coloc buf=%d mv=0x%x trace=0x%x",
                 v0_4, v0_5, var_28);
        v1_2 = READ_S32(arg3, 0x2c) & 0x20;
        WRITE_S32(arg6, 0x58, v0_5);
        if (v1_2 != 0) {
            uint32_t v0_6 = (uint32_t)READ_U16(arg3, 4);
            uint32_t a3_4 = (uint32_t)READ_U8(arg3, 0x1f);
            uint32_t a2_6 = (uint32_t)READ_U16(arg3, 6);
            int32_t v0_8;
            int32_t v0_9;

            var_28 = v0_6;
            v0_8 = 0x10;
            if (a3_4 != 0U) {
                v0_8 = 8;
            }

            {
                int32_t v0_7 = AL_GetEncoderFbcMapSize(0, (int32_t)v0_6, (int32_t)a2_6, v0_8);

                ENC_KMSG("SetPictureRefBuffers fbc map-size=%d bitdepth=%d rec=%u ref0=%u ref1=%u",
                         v0_7, v0_8, (unsigned)READ_U8(arg2, 0x290),
                         (unsigned)READ_U8(arg2, 0x291), (unsigned)READ_U8(arg2, 0x292));
                v0_9 = AL_RefMngr_GetMapBufAddr((uint8_t *)arg1 + 0x22c8, (char)READ_U8(arg2, 0x290));
                WRITE_S32(arg6, 0x50, v0_9);
                WRITE_S32(arg6, 0x54, v0_7 + v0_9);
                if (READ_S32(arg6, 0x30) == 0) {
                    int32_t v0_12 = AL_RefMngr_GetMapBufAddr((uint8_t *)arg1 + 0x22c8, (char)READ_U8(arg2, 0x291));

                    WRITE_S32(arg6, 0x30, v0_12);
                    WRITE_S32(arg6, 0x34, v0_7 + v0_12);
                    if (READ_S32(arg6, 0x40) == 0) {
                        int32_t v0_11 = AL_RefMngr_GetMapBufAddr((uint8_t *)arg1 + 0x22c8, (char)READ_U8(arg2, 0x292));

                        WRITE_S32(arg6, 0x40, v0_11);
                        WRITE_S32(arg6, 0x44, v0_7 + v0_11);
                    }
                } else if (READ_S32(arg6, 0x40) == 0) {
                    int32_t v0_11 = AL_RefMngr_GetMapBufAddr((uint8_t *)arg1 + 0x22c8, (char)READ_U8(arg2, 0x292));

                    WRITE_S32(arg6, 0x40, v0_11);
                    WRITE_S32(arg6, 0x44, v0_7 + v0_11);
                }
            }
        }

        {
            void *result = READ_PTR(arg6, 0x84);

            ENC_KMSG("SetPictureRefBuffers done rec=%u ref0=%u ref1=%u coloc=%u result=%p map0=0x%x map1=0x%x map2=0x%x",
                     (unsigned)READ_U8(arg2, 0x290), (unsigned)READ_U8(arg2, 0x291),
                     (unsigned)READ_U8(arg2, 0x292), (unsigned)READ_U8(arg2, 0x293),
                     result, READ_S32(arg6, 0x50), READ_S32(arg6, 0x30), READ_S32(arg6, 0x40));
            if (result != 0) {
                WRITE_S32(result, 0x84, READ_S32(arg4, 0x5c));
                WRITE_S32(result, 0x88, READ_S32(arg4, 0x70));
            }

            return result;
        }
    }
}

int32_t AddNewRequest(int32_t arg1)
{
    void *var_18 = &_gp;
    int32_t v0;

    (void)var_18;
    ENC_KMSG("AddNewRequest entry chctx=%p reqbuf=%p endbuf=%p active=%d eos=%d lane0_r=%d lane0_w=%d lane1_r=%d lane1_w=%d",
             (void *)(intptr_t)arg1,
             (void *)(intptr_t)(arg1 + 0x3de8),
             (void *)(intptr_t)(arg1 + 0x12878),
             READ_S32((void *)(intptr_t)arg1, 0x2c),
             READ_S32((void *)(intptr_t)arg1, 0x30),
             READ_S32((void *)(intptr_t)arg1, 0x3d8),
             READ_S32((void *)(intptr_t)arg1, 0x3dc),
             READ_S32((void *)(intptr_t)arg1, 0x434),
             READ_S32((void *)(intptr_t)arg1, 0x438));
    v0 = (int32_t)(intptr_t)RequestsBuffer_Pop((void *)(intptr_t)(arg1 + 0x3de8));
    ENC_KMSG("AddNewRequest pop chctx=%p req=%p", (void *)(intptr_t)arg1, (void *)(intptr_t)v0);
    Rtos_Memset((void *)(intptr_t)v0, 0, 0xc58);
    /*
     * Request-local module tables mirror the channel step template exactly:
     * channel 0x18c0..0x1adf -> request 0x84c..0xa6b and channel 0x1ae0 -> request 0xa6c.
     * Without this copy, freshly queued requests never advertise any modules to
     * CheckAndEncode, so they are prepared and then dropped before AVPU launch.
     */
    Rtos_Memcpy((void *)(intptr_t)(v0 + 0x84c), (void *)(intptr_t)(arg1 + 0x18c0), 0x220);
    WRITE_S32((void *)(intptr_t)v0, 0xa6c, READ_S32((void *)(intptr_t)arg1, 0x1ae0));
    {
        int32_t pict_id = READ_S32((void *)(intptr_t)arg1, 0x22b8);

        WRITE_S32((void *)(intptr_t)v0, 0x20, pict_id);
        ENC_KMSG("AddNewRequest set-pict chctx=%p req=%p pict=%d ready=%d",
                 (void *)(intptr_t)arg1, (void *)(intptr_t)v0, pict_id,
                 READ_S32((void *)(intptr_t)arg1, 0x22bc));
    }
    ENC_KMSG("AddNewRequest seeded req=%p grp_cnt=%d lane0_mods=%d lane0_slices=%d lane1_mods=%d lane1_slices=%d",
             (void *)(intptr_t)v0, READ_S32((void *)(intptr_t)v0, 0xa6c),
             READ_S32((void *)(intptr_t)v0, 0x8cc), READ_S32((void *)(intptr_t)v0, 0x958),
             READ_S32((void *)(intptr_t)v0, 0x9dc), READ_S32((void *)(intptr_t)v0, 0xa68));
    PopCommandListAddresses((void *)(intptr_t)(arg1 + 0x2c20), (void *)(intptr_t)(v0 + 0xa78));
    ENC_KMSG("AddNewRequest ready req=%p lane=%d cmdsrc_count=%d cmdsrc_next=%d cmdlist=%p"
             " cmd1=%08x/%08x/%08x/%08x/%08x cmd2=%08x/%08x/%08x/%08x/%08x",
             (void *)(intptr_t)v0, READ_S32((void *)(intptr_t)v0, 0xa70),
             READ_S32((void *)(intptr_t)(arg1 + 0x2c20), 0x980), READ_S32((void *)(intptr_t)(arg1 + 0x2c20), 0x984),
             (void *)(intptr_t)(v0 + 0xa78),
             READ_S32((void *)(intptr_t)v0, 0xa78), READ_S32((void *)(intptr_t)v0, 0xa7c),
             READ_S32((void *)(intptr_t)v0, 0xa80), READ_S32((void *)(intptr_t)v0, 0xa84),
             READ_S32((void *)(intptr_t)v0, 0xa88),
             READ_S32((void *)(intptr_t)v0, 0xab8), READ_S32((void *)(intptr_t)v0, 0xabc),
             READ_S32((void *)(intptr_t)v0, 0xac0), READ_S32((void *)(intptr_t)v0, 0xac4),
             READ_S32((void *)(intptr_t)v0, 0xac8));
    {
        StaticFifoCompat *fifo =
            (StaticFifoCompat *)(intptr_t)(arg1 + READ_S32((void *)(intptr_t)v0, 0xa70) * 0x5c + 0x129b4);
        int32_t qret =
            StaticFifo_Queue(fifo, v0);
        ENC_KMSG("AddNewRequest queued req=%p lane=%d qret=%d fifo=%p read=%d write=%d cap=%d",
                 (void *)(intptr_t)v0, READ_S32((void *)(intptr_t)v0, 0xa70), qret,
                 fifo, fifo->read_idx, fifo->write_idx, fifo->capacity);
        return qret <= 0 ? 0 : v0;
    }
}

int32_t StorePicture(int32_t arg1, void *arg2)
{
    void *var_18 = &_gp;

    (void)var_18;
    if (READ_S32(arg2, 0x2e0) != 0) {
        AL_RefMngr_MarkAsReadyForOutput((void *)(intptr_t)arg1, (char)READ_U8(arg2, 0x290));
    }

    return AL_RefMngr_StorePicture((void *)(intptr_t)arg1, (uint8_t *)arg2 + 0x20, (char)READ_U8(arg2, 0x290));
}

int32_t ReleaseWorkBuffers(void *arg1, void *arg2)
{
    void *var_18 = &_gp;

    (void)var_18;
    if ((uint32_t)READ_U8(arg1, 0x1f) != 4U) {
        ReleaseRefBuffers(arg1, arg2);
    }

    if (READ_S32(arg2, 0xa70) == 1) {
        StorePicture((int32_t)(intptr_t)((uint8_t *)arg1 + 0x22c8), arg2);
    }

    if (READ_S32(arg2, 0x2e0) != 0) {
        AL_RefMngr_ReleaseFrmBuffer((uint8_t *)arg1 + 0x22c8, (char)READ_U8(arg2, 0x290));
    }

    if (READ_S32(arg2, 0x298) != 0) {
        if (READ_S32(arg2, 0x30) == 8) {
            AL_SrcReorder_MarkSrcBufferAsUsed((uint8_t *)arg1 + 0x178, READ_S32(arg2, 0x20));
        }

        if (READ_S32(arg2, 0x24) >= 0) {
            AL_SrcReorder_EndSrcBuffer((uint8_t *)arg1 + 0x178, READ_S32(arg2, 0x20));
        }
    }

    return AL_IntermMngr_ReleaseBuffer((uint8_t *)arg1 + 0x2a54, READ_PTR(arg2, 0x838));
}

int32_t GenerateAvcSliceHeader(void *arg1, void *arg2, void *arg3, void *arg4, int32_t arg5,
                               int32_t arg6); /* forward decl, ported by T<N> later */
int32_t GenerateHevcSliceHeader(void *arg1, void *arg2, void *arg3, void *arg4, void *arg5, int32_t arg6,
                                int32_t arg7); /* forward decl, ported by T<N> later */
int32_t CmdRegsEnc1ToSliceParam(void *arg1, void *arg2, int32_t arg3); /* forward decl, ported by T<N> later */
void InitSliceStatus(void *arg1); /* forward decl, ported by T<N> later */
void EncodingStatusRegsToSliceStatus(void *arg1, void *arg2); /* forward decl, ported by T<N> later */
void EntropyStatusRegsToSliceStatus(void *arg1, void *arg2, int32_t arg3); /* forward decl, ported by T<N> later */
int32_t MergeEncodingStatus(void *arg1, void *arg2); /* forward decl, ported by T<N> later */
int32_t MergeEntropyStatus(void *arg1, void *arg2); /* forward decl, ported by T<N> later */
int16_t GetSliceEnc2CmdOffset(uint32_t arg1, uint32_t arg2, int32_t arg3); /* forward decl, ported by T<N> later */
void getAsyncEntropyChannel(int32_t *arg1); /* forward decl, ported by T<N> later */
void getNoEntropyChannel(int32_t *arg1); /* forward decl, ported by T<N> later */
int32_t AL_StreamMngr_GetBuffer(void *arg1, void *arg2); /* forward decl, ported by T<N> later */
int32_t AL_StreamMngr_AddBufferBack(void *arg1, void *arg2); /* forward decl, ported by T<N> later */
int32_t AL_SrcReorder_GetSrcBuffer(void *arg1, int32_t arg2); /* forward decl, ported by T<N> later */
int32_t AL_RefMngr_GetFrmBufTraceAddrs(void *arg1, char arg2); /* forward decl, ported by T<N> later */
int32_t AL_IntermMngr_GetMapAddr(void *arg1, void *arg2, int32_t *arg3); /* forward decl, ported by T<N> later */
int32_t AL_IntermMngr_GetDataAddr(void *arg1, void *arg2, int32_t *arg3); /* forward decl, ported by T<N> later */
int32_t StaticFifo_Empty(void *arg1); /* forward decl, ported by T<N> later */
int32_t StaticFifo_Dequeue(void *arg1); /* forward decl, ported by T<N> later */
uint32_t GetCoreFirstEnc2CmdOffset(char arg1, char arg2, char arg3); /* forward decl, ported by T<N> later */
void AL_EncCore_EnableEnc2Interrupt(void *arg1); /* forward decl, ported by T<N> later */
int32_t AL_EncCore_DisableEnc1Interrupt(void *arg1); /* forward decl */
int32_t AL_EncCore_DisableEnc2Interrupt(void *arg1); /* forward decl */
void AL_EncCore_Encode2(void *arg1, int32_t arg2, int32_t arg3); /* forward decl, ported by T<N> later */
void EndEncoding(void *arg1); /* CoreManager.c */
void EndAvcEntropy(void *arg1); /* CoreManager.c */
void AL_EncCore_TurnOffRAM(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5); /* forward decl */
void AL_EncCore_ReadStatusRegsJpeg(void *arg1, void *arg2); /* forward decl, ported by T<N> later */
void JpegStatusToStatusRegs(void *arg1, void *arg2); /* forward decl, ported by T<N> later */
void *EndRequestsBuffer_Pop(void *arg1); /* forward decl, ported by T<N> later */
int32_t Rtos_GetTime(void); /* forward decl, ported by T<N> later */
void AL_CoreConstraintEnc_Init(void *arg1, int32_t arg2, uint32_t arg3); /* forward decl */
uint32_t AL_CoreConstraintEnc_GetExpectedNumberOfCores(void *arg1, void *arg2); /* forward decl */
int32_t AL_CoreConstraintEnc_GetResources(void *arg1, int32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5,
                                          uint32_t arg6, uint32_t arg7); /* forward decl */
int32_t AL_SrcReorder_GetWaitingSrcBufferCount(void *arg1); /* forward decl, ported by T<N> later */
int32_t AL_RefMngr_PushBuffer(void *arg1, int32_t arg2, int32_t arg3, uint32_t arg4, int32_t arg5,
                              int32_t arg6); /* forward decl, ported by T<N> later */
int32_t AL_StreamMngr_AddBuffer(void *arg1, void *arg2); /* forward decl, ported by T<N> later */
int32_t AL_IntermMngr_GetEp1Location(void *arg1, void *arg2); /* forward decl, ported by T<N> later */
int32_t AL_GetAllocSizeEP1(void); /* forward decl, ported by T<N> later */
int32_t AL_IntermMngr_AddBuffer(void *arg1, void *arg2); /* forward decl, ported by T<N> later */
int32_t AL_DmaAlloc_FlushCache(int32_t arg1, int32_t arg2, int32_t arg3); /* forward decl */
int32_t AL_DPBConstraint_GetMaxDPBSize(void *arg1); /* forward decl, ported by T<N> later */
int32_t AL_RefMngr_GetBufferSize(void *arg1); /* forward decl, ported by T<N> later */
int32_t AL_IntermMngr_GetBufferSize(void *arg1); /* forward decl, ported by T<N> later */
int32_t AL_SrcReorder_AddSrcBuffer(void *arg1, void *arg2); /* forward decl, ported by T<N> later */

static void ProbeLane1WhileOutputEmpty(void *ch)
{
    static int32_t last_probe_ms;
    static int32_t last_probe_req;
    static int32_t last_probe_phase = -1;
    StaticFifoCompat *running = (StaticFifoCompat *)((uint8_t *)ch + 0x12ac8);
    int32_t req;
    int32_t phase;
    int32_t lane_pending;
    int32_t now;
    uint32_t lane_active;
    AL_EncCoreCtxCompat *core0;
    AL_IpCtrl *ip;
    uint32_t core_base;
    uint32_t mask;
    uint32_t pending;
    uint32_t cl_addr;
    uint32_t status;
    uint32_t stat_8230;
    uint32_t stat_8234;
    uint32_t stat_8238;

    if (ch == NULL || running->elems == NULL || running->capacity == 0 || StaticFifo_Empty(running) != 0) {
        return;
    }

    req = StaticFifo_Front(running);
    if (req == 0) {
        return;
    }

    phase = READ_S32((void *)(intptr_t)req, 0xa70);
    now = Rtos_GetTime();
    if (req == last_probe_req && phase == last_probe_phase && (now - last_probe_ms) < 250) {
        return;
    }

    last_probe_ms = now;
    last_probe_req = req;
    last_probe_phase = phase;
    lane_pending = READ_S32((void *)(intptr_t)req, 0x8d0 + ((phase * 0x44 + 1) << 2));
    lane_active = (uint32_t)READ_U8((void *)(intptr_t)(req + phase * 0x110 + 0x8d4), 0);
    core0 = (AL_EncCoreCtxCompat *)READ_PTR(ch, 0x164);
    if (core0 == NULL || core0->ip_ctrl == NULL || core0->ip_ctrl->vtable == NULL ||
        core0->ip_ctrl->vtable->ReadRegister == NULL) {
        ENC_KMSG("GetNextFrameToOutput lane1-probe req=%p phase=%d pending=%d active=%u core0=%p",
                 (void *)(intptr_t)req, phase, lane_pending, (unsigned)lane_active, core0);
        return;
    }

    ip = core0->ip_ctrl;
    core_base = ((uint32_t)core0->core_id) << 9;
    mask = (uint32_t)ip->vtable->ReadRegister(ip, 0x8014);
    pending = (uint32_t)ip->vtable->ReadRegister(ip, 0x8018);
    cl_addr = (uint32_t)ip->vtable->ReadRegister(ip, core_base + 0x83e0);
    status = (uint32_t)ip->vtable->ReadRegister(ip, core_base + 0x83f8);
    stat_8230 = (uint32_t)ip->vtable->ReadRegister(ip, core_base + 0x8230);
    stat_8234 = (uint32_t)ip->vtable->ReadRegister(ip, core_base + 0x8234);
    stat_8238 = (uint32_t)ip->vtable->ReadRegister(ip, core_base + 0x8238);

    ENC_KMSG("GetNextFrameToOutput lane1-probe req=%p phase=%d pending=%d active=%u"
             " runq=%d/%d mask=0x%08x irq=0x%08x cl=0x%08x status=0x%08x"
             " 8230=0x%08x 8234=0x%08x 8238=0x%08x",
             (void *)(intptr_t)req, phase, lane_pending, (unsigned)lane_active,
             running->read_idx, running->write_idx, mask, pending, cl_addr, status,
             stat_8230, stat_8234, stat_8238);
}

static void DisableCompletedPhaseInterrupts(void *ch, int32_t *req, int32_t phase)
{
    int32_t *grp = (int32_t *)((uint8_t *)req + phase * 0x110 + 0x84c);
    int32_t mod_count = READ_S32(req, phase * 0x110 + 0x8cc);
    int32_t core_base = READ_U8(ch, 0x3d);
    int32_t core_count = READ_U8(ch, 0x3c);
    int32_t mod_idx;

    for (mod_idx = 0; mod_idx < mod_count; ++mod_idx) {
        int32_t core_num = grp[0];
        int32_t mod = grp[1];
        int32_t core_idx = core_num - core_base;

        if (core_idx >= 0 && core_idx < core_count) {
            void *core_ctx = (uint8_t *)READ_PTR(ch, 0x164) + core_idx * 0x44;

            ENC_KMSG("EndEncoding non-gate disable-irq req=%p phase=%d idx=%d core=%d mod=%d ctx=%p",
                     req, phase, mod_idx, core_num, mod, core_ctx);
            if (mod == 0) {
                AL_EncCore_DisableEnc1Interrupt(core_ctx);
            } else if (mod == 1) {
                AL_EncCore_DisableEnc2Interrupt(core_ctx);
            }
        } else {
            ENC_KMSG("EndEncoding non-gate disable-irq skip req=%p phase=%d idx=%d core=%d base=%d count=%d",
                     req, phase, mod_idx, core_num, core_base, core_count);
        }

        grp += 2;
    }
}

static void SyncCompletedCmdListCache(void *ch, int32_t *req, const char *tag)
{
    int32_t active_cores = (int32_t)READ_U8(req, 0x1ee);
    int32_t max_cores = (int32_t)READ_U8(ch, 0x3c);
    int32_t core_idx;

    if (active_cores <= 0 || active_cores > max_cores) {
        active_cores = max_cores;
    }

    for (core_idx = 0; core_idx < active_cores; ++core_idx) {
        void *cmd = READ_PTR(req, 0xa78 + core_idx * 4);

        if (cmd == NULL) {
            continue;
        }

        /* T31 command/status rings are cached DMA mappings. Use a whole-ring
         * bidirectional cache sync after the AVPU completes so subsequent
         * CPU status reads don't see stale zeroed CL state. */
        AL_DmaAlloc_FlushCache((int32_t)(intptr_t)cmd, 0x100000, 0);
        ENC_KMSG("SyncCmdCache tag=%s req=%p core=%d cmd=%p w00=%08x w02=%08x w1b=%08x w1c=%08x w1d=%08x w1e=%08x w1f=%08x",
                 tag ? tag : "?", req, core_idx, cmd,
                 READ_S32(cmd, 0x00), READ_S32(cmd, 0x08), READ_S32(cmd, 0x6c),
                 READ_S32(cmd, 0x70), READ_S32(cmd, 0x74), READ_S32(cmd, 0x78),
                 READ_S32(cmd, 0x7c));
        ENC_KMSG("SyncCmdStatus tag=%s req=%p core=%d cmd=%p enc104=%08x enc108=%08x enc130=%08x enc134=%08x enc138=%08x enc13c=%08x enc140=%08x enc144=%08x",
                 tag ? tag : "?", req, core_idx, cmd,
                 READ_S32(cmd, 0x104), READ_S32(cmd, 0x108), READ_S32(cmd, 0x130),
                 READ_S32(cmd, 0x134), READ_S32(cmd, 0x138), READ_S32(cmd, 0x13c),
                 READ_S32(cmd, 0x140), READ_S32(cmd, 0x144));
        ENC_KMSG("SyncCmdEntropy tag=%s req=%p core=%d cmd=%p e1e4=%08x e1e8=%08x outc8=%08x outcc=%08x outf8=%08x outfc=%08x",
                 tag ? tag : "?", req, core_idx, cmd,
                 READ_S32(cmd, 0x1e4), READ_S32(cmd, 0x1e8), READ_S32(cmd, 0xc8),
                 READ_S32(cmd, 0xcc), READ_S32(cmd, 0xf8), READ_S32(cmd, 0xfc));
    }
}

static int32_t ScanStreamBufferRawEnd(const uint8_t *buf, int32_t size)
{
    int32_t pos;

    if (buf == NULL || size <= 0) {
        return 0;
    }

    pos = size;
    while (pos > 0 && buf[pos - 1] == 0) {
        --pos;
    }

    return pos;
}

static int32_t FindFirstNonZeroByte(const uint8_t *buf, int32_t start, int32_t size)
{
    int32_t pos;

    if (buf == NULL || size <= 0) {
        return -1;
    }

    if (start < 0) {
        start = 0;
    }
    if (start >= size) {
        return -1;
    }

    for (pos = start; pos < size; ++pos) {
        if (buf[pos] != 0) {
            return pos;
        }
    }

    return -1;
}

static void ProbeLiveT31AfterLaunch(void *ch, int32_t *req, int32_t core_idx, const char *tag)
{
    AL_EncCoreCtxCompat *core;
    AL_IpCtrl *ip;
    uint8_t *cmd;
    void *stream_meta;
    uint32_t core_base;
    uint32_t mask;
    uint32_t pending;
    uint32_t cl_addr;
    uint32_t status;
    uint32_t stat_8230;
    uint32_t stat_8234;
    uint32_t stat_8238;
    uint32_t cfg_8400;
    uint32_t cfg_8420;
    uint32_t cfg_8428;
    uint32_t cfg_85e4;
    int32_t raw_end = -1;
    int32_t first_nz = -1;

    if (ch == NULL || req == NULL || core_idx < 0) {
        return;
    }

    core = (AL_EncCoreCtxCompat *)((uint8_t *)READ_PTR(ch, 0x164) + core_idx * 0x44);
    cmd = READ_PTR(req, 0xa78 + core_idx * 4);
    stream_meta = READ_PTR(req, 0x318);
    if (core == NULL || core->ip_ctrl == NULL || core->ip_ctrl->vtable == NULL ||
        core->ip_ctrl->vtable->ReadRegister == NULL) {
        ENC_KMSG("encode1 probe-%s skip core_idx=%d core=%p cmd=%p",
                 tag ? tag : "?", core_idx, core, cmd);
        return;
    }

    if (cmd != NULL) {
        AL_DmaAlloc_FlushCache((int32_t)(intptr_t)cmd, 0x100000, 0);
    }
    if (stream_meta != NULL && READ_PTR(stream_meta, 8) != NULL && READ_S32(stream_meta, 0x10) > 0) {
        uint8_t *stream_base = (uint8_t *)READ_PTR(stream_meta, 8);
        int32_t stream_size = READ_S32(stream_meta, 0x10);
        int32_t stream_off = READ_S32(stream_meta, 0x14);

        raw_end = ScanStreamBufferRawEnd(stream_base, stream_size);
        first_nz = FindFirstNonZeroByte(stream_base, stream_off, stream_size);
    }

    ip = core->ip_ctrl;
    core_base = ((uint32_t)core->core_id) << 9;
    mask = (uint32_t)ip->vtable->ReadRegister(ip, 0x8014);
    pending = (uint32_t)ip->vtable->ReadRegister(ip, 0x8018);
    cl_addr = (uint32_t)ip->vtable->ReadRegister(ip, core_base + 0x83e0);
    status = (uint32_t)ip->vtable->ReadRegister(ip, core_base + 0x83f8);
    stat_8230 = (uint32_t)ip->vtable->ReadRegister(ip, core_base + 0x8230);
    stat_8234 = (uint32_t)ip->vtable->ReadRegister(ip, core_base + 0x8234);
    stat_8238 = (uint32_t)ip->vtable->ReadRegister(ip, core_base + 0x8238);
    cfg_8400 = (uint32_t)ip->vtable->ReadRegister(ip, 0x8400);
    cfg_8420 = (uint32_t)ip->vtable->ReadRegister(ip, 0x8420);
    cfg_8428 = (uint32_t)ip->vtable->ReadRegister(ip, 0x8428);
    cfg_85e4 = (uint32_t)ip->vtable->ReadRegister(ip, 0x85e4);

    ENC_KMSG("encode1 probe-%s core=%u mask=0x%08x irq=0x%08x cl=0x%08x status=0x%08x"
             " 8230=0x%08x 8234=0x%08x 8238=0x%08x cfg8400=0x%08x cfg8420=0x%08x cfg8428=0x%08x cfg85e4=0x%08x"
             " raw_end=%d first_nz=%d",
             tag ? tag : "?", (unsigned)core->core_id, mask, pending, cl_addr, status,
             stat_8230, stat_8234, stat_8238, cfg_8400, cfg_8420, cfg_8428, cfg_85e4,
             raw_end, first_nz);
    if (cmd != NULL) {
        ENC_KMSG("encode1 probe-%s cmd=%p w00=%08x w02=%08x w1b=%08x w1c=%08x w31=%08x w32=%08x w33=%08x"
                 " enc104=%08x enc108=%08x enc130=%08x enc134=%08x ent1e4=%08x ent1e8=%08x",
                 tag ? tag : "?", cmd,
                 READ_S32(cmd, 0x00), READ_S32(cmd, 0x08), READ_S32(cmd, 0x6c), READ_S32(cmd, 0x70),
                 READ_S32(cmd, 0xc4), READ_S32(cmd, 0xc8), READ_S32(cmd, 0xcc),
                 READ_S32(cmd, 0x104), READ_S32(cmd, 0x108), READ_S32(cmd, 0x130),
                 READ_S32(cmd, 0x134), READ_S32(cmd, 0x1e4), READ_S32(cmd, 0x1e8));
    }
}

int32_t OutputSkippedPicture(void *arg1, void *arg2, void *arg3)
{
    void *s3 = READ_PTR(arg2, 0x318);
    void *var_98 = &_gp;
    uint32_t byte_count = (uint32_t)(READ_S32(arg1, 0x3d7c) + 7) >> 3;
    uint8_t *dst = (uint8_t *)READ_PTR(s3, 8) + READ_S32(s3, 0x14);
    uint8_t *src = (uint8_t *)READ_PTR(arg1, 0x3d74);
    int32_t escaped = 0;

    (void)var_98;
    if (byte_count != 0U) {
        uint32_t cur = (uint32_t)*src;
        uint8_t *src_end = src + byte_count;
        uint8_t *dst_next = dst + 1;
        int32_t zero_run = 1;
        uint8_t *src_cur = src + 1;

        for (;;) {
            if (cur == 0U) {
                *dst = 0;
                if (src_cur == src_end) {
                    break;
                }

                if (zero_run == 2 && (((uint32_t)*src_cur & 0xfffffffcU) == 0U)) {
                    dst_next = dst + 2;
                    dst[1] = 3;
                    escaped += 1;
                    zero_run = 0;
                    dst = dst_next;
                }

                do {
                    cur = (uint32_t)*src_cur;
                    dst_next = dst + 1;
                    zero_run += 1;
                    src_cur += 1;
                } while (cur == 0U);
            }

            *dst = (uint8_t)cur;
            if (src_cur == src_end) {
                break;
            }

            zero_run = 0;
            dst = dst_next;
            cur = (uint32_t)*src_cur;
            dst_next = dst + 1;
            zero_run += 1;
            src_cur += 1;
            if (cur == 0U) {
                continue;
            }
        }
    }

    {
        uint32_t codec = (uint32_t)READ_U8(arg1, 0x1f);
        int32_t skipped_bytes = (int32_t)byte_count + escaped;
        int32_t header_bytes = 0;

        WRITE_S32(arg2, 0xb30, 1);
        WRITE_U16(arg2, 0xb24, 1);
        WRITE_U16(arg2, 0xb26, 1);
        WRITE_S32(arg2, 0xb98, 1);
        if (codec == 0U) {
            header_bytes = GenerateAvcSliceHeader(arg1, arg2, (uint8_t *)arg2 + 0x170, (uint8_t *)arg2 + 0x298,
                                                  READ_S32(s3, 0x14), 0);
            codec = (uint32_t)READ_U8(arg1, 0x1f);
        }

        if (codec == 1U) {
            int32_t slice_size = READ_S32(s3, 0x14);
            int16_t num_core = READ_S16(arg1, 0x40);
            uint8_t temp[0x10];

            WRITE_U16(arg2, 0xb24, READ_U16(arg1, 0x3c));
            WRITE_U16(arg2, 0xb26, (uint16_t)num_core);
            memset(temp, 0, sizeof(temp));
            WRITE_S32(temp, 8, skipped_bytes);
            header_bytes = GenerateHevcSliceHeader(arg1, arg2, (uint8_t *)arg2 + 0x170, (uint8_t *)arg2 + 0x298, temp,
                                                   slice_size, 0);
            codec = (uint32_t)READ_U8(arg1, 0x1f);
        }

        {
            int32_t tile_mb = READ_U16(arg2, 0x278) * READ_U16(arg2, 0x27a);
            int32_t qp = READ_S32(arg2, 0x28c);
            int32_t slice_type = READ_S32(arg2, 0x1a0);
            int32_t stream_size = READ_S32(arg1, 0x3d80);
            int32_t *desc = (int32_t *)((uint8_t *)READ_PTR(s3, 8) + READ_S32(s3, 0x18));
            int32_t result = header_bytes + skipped_bytes;

            desc[0] = READ_S32(s3, 0x14) - header_bytes;
            desc[1] = result;
            desc[2] = qp;
            desc[3] = slice_type;
            WRITE_S32(arg3, 8, skipped_bytes);
            WRITE_S32(arg3, 0xc, stream_size);
            WRITE_S32(arg3, 0x14, 0);
            WRITE_S32(arg3, 0x18, 0);
            WRITE_S32(arg3, 0x1c, 0);
            WRITE_S32(arg3, 4, tile_mb);
            WRITE_S32(arg3, 0x20, tile_mb);
            WRITE_S32(arg3, 0x24, 0);
            if (codec == 0U) {
                WRITE_S32(arg3, 0x28, tile_mb);
                WRITE_S32(arg3, 0x2c, 0);
            } else {
                WRITE_S32(arg3, 0x28, 0);
                WRITE_S32(arg3, 0x2c, 0);
            }
            return result;
        }
    }
}

int32_t CmdList_MergeMultiSliceEntropyStatus(void *arg1, void *arg2, void *arg3, void *arg4, uint8_t arg5, uint8_t arg6)
{
    int32_t t8 = (int32_t)(int8_t)arg5;
    int32_t t7 = READ_S32(arg2, 0xa6c);
    void *var_18 = &_gp;
    void *a0_1 = (uint8_t *)READ_PTR(arg2, ((t8 + 0x29c) << 2) + 8) + ((uint32_t)arg6 << 9);
    int32_t v1_3;
    uint8_t *t4_1;
    int32_t t6_1;
    int32_t t5_1;

    (void)arg1;
    (void)var_18;
    if (t7 <= 0) {
        v1_3 = READ_S32(a0_1, 0xcc);
        goto done;
    }

    t4_1 = (uint8_t *)arg2 + 0x84c;
    t6_1 = 0;
    t5_1 = 0;
    while (1) {
        int32_t t3_1 = READ_S32(t4_1, 0x80);
        uint8_t *v1_1 = t4_1;

        if (t3_1 > 0) {
            int32_t t0_1 = 0;
            int32_t t1_1 = 0;

            do {
                t0_1 += 1;
                t1_1 += (((uint32_t)READ_S32(v1_1, 4) ^ 1U) < 1U) ? 1 : 0;
                v1_1 = (uint8_t *)arg2 + ((intptr_t)v1_1 + t6_1 + 0x854 - (intptr_t)t4_1);
            } while (t0_1 != t3_1);

            if (t1_1 != 0) {
                v1_3 = READ_S32(a0_1, 0xfc);
                break;
            }
        }

        t5_1 += 1;
        t6_1 += 0x110;
        t4_1 += 0x110;
        if (t5_1 == t7) {
            v1_3 = READ_S32(a0_1, 0xcc);
            break;
        }
    }

done:
    {
        void *s0 = (uint8_t *)arg3 + t8 * 0x78;

        EntropyStatusRegsToSliceStatus(a0_1, s0, v1_3);
        return MergeEntropyStatus(arg4, s0);
    }
}

int32_t UpdateStatus(void *arg1, int32_t *arg2)
{
    if (READ_U8(arg1, 0x1de) != 0U) {
        WRITE_S32(arg1, 0xb34, arg2[0xd]);
    }

    {
        int32_t result = READ_S32(arg1, 0xb94);

        if (result != 0) {
            return result;
        }

        if ((uint32_t)arg2[2] != 0U) {
            WRITE_S32(arg1, 0xb94, 0x93);
            return 0x93;
        }

        if ((uint32_t)arg2[1] != 0U) {
            WRITE_S32(arg1, 0xb94, 0x88);
            return 0x88;
        }

        if ((uint32_t)arg2[0] != 0U) {
            WRITE_S32(arg1, 0xb94, 2);
            return 2;
        }

        return result;
    }
}

int32_t SetTileInfoIfNeeded(void *arg1, void *arg2, void *arg3, int32_t arg4)
{
    int32_t v0 = 1;

    if ((uint32_t)READ_U8(arg3, 0x1f) == 1U) {
        int32_t v0_2 = (READ_S32(arg3, 0x3c) < 2) ? 1 : 0;

        if (v0_2 == 0 && arg4 != 0) {
            if (READ_S32(arg2, 0x2c) != 0) {
                WRITE_S32(arg2, 0x40, READ_S32(arg1, 0x1ae8));
            }

            {
                uint32_t count = (uint32_t)READ_U16(arg2, 0x2e);

                v0 = (int32_t)(count + 0x11U);
                if (count == 0U) {
                    v0 = 1;
                }

                {
                    uint16_t *a0 = (uint16_t *)((uint8_t *)arg1 + 0x1b28);
                    uint32_t *i = (uint32_t *)((uint8_t *)arg2 + 0x44);

                    while ((uint8_t *)i != (uint8_t *)arg2 + ((uint32_t)v0 << 2)) {
                        *i++ = (uint32_t)*a0++;
                    }

                    return (int32_t)(intptr_t)i;
                }
            }
        }

        return v0_2;
    }

    return v0;
}

int32_t SetChannelSteps(void *arg1, void *arg2)
{
    void *var_28 = &_gp;
    int32_t v0_3;
    int32_t var_20;
    void *s1;
    int32_t result;

    (void)var_28;
    ENC_KMSG("SetChannelSteps entry ch=%p param=%p codec=%u entropy=%d slices=%d",
             arg1, arg2, (unsigned)READ_U8(arg2, 0x1f), READ_S32(arg2, 0x3c), READ_U16(arg2, 0x40));
    if ((uint32_t)READ_U8(arg2, 0x1f) != 0U || READ_S32(arg2, 0x3c) == 1) {
        getNoEntropyChannel(&var_20);
        v0_3 = var_20;
    } else {
        getAsyncEntropyChannel(&var_20);
        v0_3 = var_20;
    }

    s1 = (uint8_t *)arg1 + 0x18c0;
    WRITE_S32(arg1, 0xf0, v0_3);
    ENC_KMSG("SetChannelSteps selected fn=%p state=%p", (void *)(intptr_t)v0_3, s1);
    Rtos_Memset(s1, 0, 0x220);
    ENC_KMSG("SetChannelSteps pre-step-init fn=%p", (void *)(intptr_t)READ_S32(arg1, 0xf0));
    result = ((int32_t (*)(void *, void *))(intptr_t)READ_S32(arg1, 0xf0))(arg2, s1);
    WRITE_S32(arg1, 0x1ae0, result);
    ENC_KMSG("SetChannelSteps done result=%d step_state=0x%x", result, READ_S32(arg1, 0x1ae0));
    return result;
}

static int32_t GetStreamBuffers_part_72(void *arg1, void *arg2, void *arg3)
{
    uint32_t s2 = 1;
    int32_t result;
    int32_t *s7_1;
    int32_t fp_1;

    if (READ_S32(arg3, 0xc4) != 0) {
        s2 = (uint32_t)READ_U16(arg3, 0x40);
    }

    result = 1;
    if (READ_S32(arg3, 0xc4) == 0 || s2 != 0U) {
        s7_1 = (int32_t *)((uint8_t *)arg2 + 0x348);
        fp_1 = 0;
        while (1) {
            int32_t stream_entry[8];
            int32_t push_back_entry[8];
            uint32_t shift = (uint32_t)READ_U8(arg3, 0x4e);
            uint32_t max_size = (uint32_t)READ_U16(arg3, 0x40);
            uint32_t stream_rows = (uint32_t)READ_U8(arg3, 0x3c);
            uint32_t force_fixed_part = (uint32_t)READ_U8(arg3, 0x3e);
            int32_t part_size;
            int32_t result_1;

            result_1 = AL_StreamMngr_GetBuffer(arg1, stream_entry);
            result = result_1;
            if (result_1 == 0) {
                int32_t s7_2 = fp_1 - 1;

                if (fp_1 == 0) {
                    break;
                }

                {
                    int32_t *s5_1 = (int32_t *)((uint8_t *)arg2 + fp_1 * 0x28 + 0x320);

                    while (1) {
                        if (force_fixed_part != 0U) {
                            max_size = 0xc8;
                        }

                        part_size = (((((((1 << (shift & 0x1fU)) - 1) + (uint32_t)READ_U16(arg3, 6)) >>
                                         (shift & 0x1fU)) < max_size)
                                          ? max_size
                                          : ((((1 << (shift & 0x1fU)) - 1) + (uint32_t)READ_U16(arg3, 6)) >>
                                             (shift & 0x1fU))) *
                                         stream_rows +
                                     0x10)
                                    << 4;
                        part_size = ((part_size + 0x7f) >> 7) << 7;
                        part_size += s5_1[0];
                        s5_1[0] = part_size;
                        s5_1[2] = part_size;
                        if ((part_size & 3) != 0) {
                            __assert("pStreamInfo->iStreamPartOffset % 4 == 0",
                                     "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/ChannelMngr.c",
                                     0x5e5, "FreeReservedSpaceForStreamPart", &_gp);
                        }

                        if (s5_1[1] >= part_size) {
                            break;
                        }

                        push_back_entry[0] = s5_1[-1];
                        push_back_entry[1] = s5_1[-2];
                        push_back_entry[2] = part_size;
                        push_back_entry[3] = s5_1[1];
                        push_back_entry[4] = s5_1[-4];
                        push_back_entry[5] = s5_1[-3];
                        push_back_entry[6] = s5_1[4];
                        push_back_entry[7] = 0;
                        s5_1 -= 10;
                        s7_2 -= 1;
                        AL_StreamMngr_AddBufferBack(arg1, push_back_entry);
                        s5_1[8] = 0;
                        if (s7_2 == -1) {
                            return result;
                        }
                    }
                }

                return result;
            }

            ENC_KMSG("GetStreamBuffers raw lane=%d entry=[%08x %08x %08x %08x %08x %08x %08x %08x] cfg6=%u cfg3c=%u cfg3e=%u cfg40=%u cfg4e=%u cfg4f=%u",
                     fp_1,
                     stream_entry[0], stream_entry[1], stream_entry[2], stream_entry[3],
                     stream_entry[4], stream_entry[5], stream_entry[6], stream_entry[7],
                     (unsigned)READ_U16(arg3, 6), (unsigned)stream_rows,
                     (unsigned)force_fixed_part, (unsigned)READ_U16(arg3, 0x40),
                     (unsigned)shift, (unsigned)READ_U8(arg3, 0x4f));
            PushStreamBufferToAvpu(arg3, (uint32_t)stream_entry[0], "get-stream");
            s7_1[-1] = stream_entry[0];
            s7_1[-2] = stream_entry[1];
            s7_1[-4] = stream_entry[4];
            s7_1[-3] = stream_entry[5];
            s7_1[4] = stream_entry[6];
            s7_1[0] = stream_entry[2];
            s7_1[1] = stream_entry[3];
            s7_1[3] = stream_entry[3];

            if (force_fixed_part != 0U) {
                max_size = 0xc8;
            }

            part_size = (((((((1 << (shift & 0x1fU)) - 1) + (uint32_t)READ_U16(arg3, 6)) >> (shift & 0x1fU)) <
                           max_size)
                              ? max_size
                              : ((((1 << (shift & 0x1fU)) - 1) + (uint32_t)READ_U16(arg3, 6)) >> (shift & 0x1fU))) *
                             stream_rows +
                         0x10)
                        << 4;
            part_size = ((part_size + 0x7f) >> 7) << 7;
            if (part_size >= stream_entry[2]) {
                __assert("pStreamInfo->iMaxSize > iStreamPartSize",
                         "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/ChannelMngr.c", 0x5ed,
                         "ReserveSpaceForStreamPart", &_gp);
            }

            {
                int32_t v0_4 = stream_entry[2] - part_size;

                s7_1[0] = v0_4;
                s7_1[2] = v0_4;
                if ((v0_4 & 3) != 0) {
                    __assert("pStreamInfo->iStreamPartOffset % 4 == 0",
                             "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/ChannelMngr.c", 0x5f2,
                             "ReserveSpaceForStreamPart", &_gp);
                }

                if (stream_entry[3] >= v0_4) {
                    __assert("pStreamInfo->iOffset < pStreamInfo->iStreamPartOffset",
                             "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/ChannelMngr.c", 0x5f3,
                             "ReserveSpaceForStreamPart", &_gp);
                }

                ENC_KMSG("GetStreamBuffers lane_entry phys=0x%x virt=%p max=%d off=%d side0=%d side1=%d side2=%p part=%d part_off=%d",
                         stream_entry[0], (void *)(intptr_t)stream_entry[1], stream_entry[2], stream_entry[3],
                         stream_entry[4], stream_entry[5], (void *)(intptr_t)stream_entry[6],
                         part_size, v0_4);
                ENC_KMSG("GetStreamBuffers reserve lane=%d shift=%u min_lcu=%u max_cfg=%u rows=%u forced=%u part=%d part_off=%d limit=%d",
                         fp_1, shift,
                         (unsigned)((((1U << (shift & 0x1fU)) - 1U) + (uint32_t)READ_U16(arg3, 6)) >> (shift & 0x1fU)),
                         max_size, (unsigned)stream_rows, (unsigned)force_fixed_part,
                         part_size, v0_4, stream_entry[2]);
            }

            fp_1 += 1;
            s7_1 += 10;
            if (fp_1 >= (int32_t)s2) {
                return 1;
            }
        }
    }

    return result;
}

int32_t InitTracedBuffers(void *arg1, int32_t *arg2, void *arg3)
{
    int32_t *v1 = arg2;
    void *var_20 = &_gp;
    int32_t *i = (int32_t *)((uint8_t *)arg3 + 0x298);
    int32_t result;

    (void)var_20;
    do {
        v1[0] = i[0];
        v1[1] = i[1];
        v1[2] = i[2];
        v1[3] = i[3];
        i += 4;
        v1 += 4;
    } while ((uint8_t *)i != (uint8_t *)arg3 + 0x338);

    arg2[0x28] = READ_S32(arg3, 0x18);
    arg2[0x29] = READ_S32(arg3, 0x1c);
    arg2[0x2a] = AL_RefMngr_GetFrmBufTraceAddrs((uint8_t *)arg1 + 0x22c8, (char)READ_U8(arg3, 0x291));
    arg2[0x2b] = AL_RefMngr_GetFrmBufTraceAddrs((uint8_t *)arg1 + 0x22c8, (char)READ_U8(arg3, 0x292));
    arg2[0x2c] = AL_RefMngr_GetFrmBufTraceAddrs((uint8_t *)arg1 + 0x22c8, (char)READ_U8(arg3, 0x290));
    AL_RefMngr_GetMvBufAddr((uint8_t *)arg1 + 0x22c8, (char)READ_U8(arg3, 0x290), &arg2[0x2e]);
    AL_RefMngr_GetMvBufAddr((uint8_t *)arg1 + 0x22c8, (char)READ_U8(arg3, 0x293), &arg2[0x2d]);
    AL_IntermMngr_GetMapAddr((uint8_t *)arg1 + 0x2a54, READ_PTR(arg3, 0x838), &arg2[0x2f]);
    AL_IntermMngr_GetDataAddr((uint8_t *)arg1 + 0x2a54, READ_PTR(arg3, 0x838), &arg2[0x30]);

    {
        void *v0_4 = READ_PTR(arg3, 0x318);
        int32_t a0_7 = READ_S32(v0_4, 8);
        int32_t v1_2 = READ_S32(v0_4, 0x1c);

        result = READ_S32(v0_4, 0x10);
        arg2[0x34] = a0_7;
        arg2[0x35] = READ_S32(v0_4, 0xc);
        arg2[0x36] = result;
        arg2[0x37] = v1_2;
        return result;
    }
}

int32_t FillEncTrace(int32_t *arg1, void *arg2, void *arg3)
{
    int32_t t0 = READ_S32(arg3, 0x10);

    arg1[0] = READ_S32(arg2, 0);
    arg1[1] = t0;
    WRITE_U8(arg1, 0x10, READ_U8(arg2, 0x1ae5));
    WRITE_U8(arg1, 0x11, 0);
    WRITE_U8(arg1, 0x13, READ_U8(arg2, 0x3c));
    WRITE_U16(arg1, 0x14, READ_U16(arg2, 0x40));
    arg1[7] = READ_S32(arg3, 0xc50);
    return InitTracedBuffers(arg2, &arg1[8], arg3);
}

int32_t handleInputTraces(void *arg1, void *arg2, void *arg3, uint8_t arg4)
{
    int32_t cb = READ_S32(arg1, 0x1a9c);
    void *var_138 = &_gp;

    (void)var_138;
    if (cb != 0) {
        uint8_t trace[0x130];

        memset(trace, 0, sizeof(trace));
        FillEncTrace((int32_t *)trace, arg1, arg2);
        WRITE_S32(trace, 8, READ_S32(arg3, 8));
        WRITE_S32(trace, 0x0c, (int32_t)(intptr_t)((uint8_t *)arg2 + 0xa78));
        WRITE_S32(trace, 0x10, (int32_t)(intptr_t)((uint8_t *)arg2 + 0xab8));
        WRITE_U8(trace, 0x11, READ_U8(arg3, 0));
        WRITE_U8(trace, 0x12, arg4);
        WRITE_S32(trace, 0x18, 0);
        return ((int32_t (*)(void *, void *))(intptr_t)cb)(trace, READ_PTR(arg1, 0));
    }

    return (uint32_t)cb;
}

int32_t encode2(void *arg1)
{
    void *var_28 = &_gp;
    int32_t v0 = StaticFifo_Dequeue((uint8_t *)arg1 + 0x12a10);
    int32_t t0 = READ_S32((void *)(intptr_t)v0, 0xa68);
    int32_t *v0_1 = (int32_t *)(intptr_t)(v0 + 0x9e8);
    uint32_t a1 = (uint32_t)READ_U8(arg1, 0x1ae5);
    intptr_t a3 = 0x9f0 - (intptr_t)v0_1;
    int32_t a0_1 = 0;
    uint32_t a0_6;
    uint32_t v1_7;
    uint32_t s1;
    uint32_t s0_1;
    int32_t i;

    (void)var_28;
    ENC_KMSG("encode2 dequeued req=%p grp=%d slice_count=%d mod_count=%d core_off=%u core_tbl=%p",
             (void *)(intptr_t)v0, READ_S32((void *)(intptr_t)v0, 0xa70),
             READ_S32((void *)(intptr_t)v0, 0xa68), READ_S32((void *)(intptr_t)v0, 0x9dc),
             (unsigned)READ_U8(arg1, 0x1ae5), READ_PTR(arg1, 0x164));
    if (t0 > 0) {
        do {
            a0_1 += 1;
            *v0_1 += (int32_t)a1;
            v0_1 = (int32_t *)(intptr_t)(v0 + (intptr_t)v0_1 + a3);
        } while (a0_1 != t0);
    }

    {
        int32_t t0_1 = READ_S32((void *)(intptr_t)v0, 0x9dc);
        int32_t *v0_2 = (int32_t *)(intptr_t)(v0 + 0x95c);
        int32_t a0_2 = 0;
        intptr_t a3_1 = 0x964 - (intptr_t)v0_2;

        if (t0_1 > 0) {
            do {
                a0_2 += 1;
                *v0_2 += (int32_t)a1;
                v0_2 = (int32_t *)(intptr_t)(v0 + (intptr_t)v0_2 + a3_1);
            } while (a0_2 != t0_1);
        }
    }

    StaticFifo_Queue((StaticFifoCompat *)((uint8_t *)arg1 + READ_S32((void *)(intptr_t)v0, 0xa70) * 0x5c + 0x12a6c), v0);
    ENC_KMSG("encode2 queued-running req=%p fifo_off=0x%x",
             (void *)(intptr_t)v0, READ_S32((void *)(intptr_t)v0, 0xa70) * 0x5c + 0x12a6c);
    a0_6 = (uint32_t)READ_U16(arg1, 0x3c);
    v1_7 = (uint32_t)READ_U8((void *)(intptr_t)(v0 + READ_S32((void *)(intptr_t)v0, 0xa70) * 0x110 + 0x8d4), 0);
    s1 = 0;
    s0_1 = 0;
    i = ((int32_t)a0_6 < (int32_t)v1_7) ? 1 : 0;
    if (i != 0) {
        v1_7 = a0_6;
    }

    if ((int32_t)v1_7 > 0) {
        do {
            int32_t s1_2 = v0 + ((int32_t)s1 << 2);
            int32_t s1_4 = (int32_t)s0_1 * 0x44;
            uint32_t v0_13 = GetCoreFirstEnc2CmdOffset((char)READ_U8(arg1, 0x3c),
                                                       (char)READ_U16(arg1, 0x40),
                                                       (char)s0_1) << 9;
            int32_t cmd_phys = (int32_t)v0_13 + READ_S32((void *)(intptr_t)s1_2, 0xab8);
            int32_t cmd_virt = READ_S32((void *)(intptr_t)s1_2, 0xa78) + (int32_t)v0_13;
            void *core = (uint8_t *)READ_PTR(arg1, 0x164) + s1_4;

            ENC_KMSG("encode2 launch core=%u core_ptr=%p cmd_phys=0x%x cmd_virt=0x%x cmd_off=0x%x",
                     (unsigned)s0_1, core, cmd_phys, cmd_virt, (unsigned)v0_13);

            PrepareSourceConfigForLaunch(core, arg1, (void *)(intptr_t)v0, (uint32_t *)(uintptr_t)cmd_virt,
                                         "encode2-pre-cl");
            if ((unsigned)s0_1 == 0U) {
                RemapLiveT31Irq4(core, EndAvcEntropy, "encode2");
            }
            AL_EncCore_EnableEnc2Interrupt(core);
            AL_EncCore_Encode2(core, cmd_phys, cmd_virt);

            a0_6 = (uint32_t)READ_U16(arg1, 0x3c);
            v1_7 = (uint32_t)READ_U8((void *)(intptr_t)(v0 + READ_S32((void *)(intptr_t)v0, 0xa70) * 0x110 + 0x8d4), 0);
            s0_1 = (uint32_t)((uint8_t)s0_1 + 1U);
            if ((int32_t)a0_6 < (int32_t)v1_7) {
                v1_7 = a0_6;
            }

            i = ((int32_t)s0_1 < (int32_t)v1_7) ? 1 : 0;
            s1 = s0_1;
        } while (i != 0);
    }

    ENC_KMSG("encode2 exit req=%p launched=%u", (void *)(intptr_t)v0, (unsigned)s0_1);
    return i;
}

int32_t handleJpegInputTrace(void *arg1, void *arg2)
{
    int32_t result = READ_S32(arg1, 0x1a9c);
    void *var_130 = &_gp;

    (void)var_130;
    if (result == 0) {
        return result;
    }

    {
        uint8_t trace[0x128];

        memset(trace, 0, sizeof(trace));
        FillEncTrace((int32_t *)trace, arg1, arg2);
        WRITE_S32(trace, 0x10, (int32_t)(intptr_t)((uint8_t *)arg2 + 0xa78));
        WRITE_S32(trace, 0x14, (int32_t)(intptr_t)((uint8_t *)arg2 + 0xab8));
        WRITE_S32(trace, 0x18, 8);
        return ((int32_t (*)(void *, void *))(intptr_t)result)(trace, READ_PTR(arg1, 0));
    }
}

int32_t EndJpegEncoding(void *arg1)
{
    void *var_1a8 = &_gp;
    int32_t v0 = StaticFifo_Dequeue((uint8_t *)arg1 + 0x1780);
    void *core = READ_PTR(arg1, 0x164);
    uint8_t t0 = READ_U8((void *)(intptr_t)v0, 0x24);
    int32_t t1 = READ_S32(READ_PTR((void *)(intptr_t)v0, 0x318), 0x18);
    int32_t a2 = READ_S32((void *)(intptr_t)v0, 0x10);
    int32_t a3 = READ_S32((void *)(intptr_t)v0, 0x14);
    int32_t v0_4 = READ_S32((void *)(intptr_t)v0, 0x18);
    int32_t v1_1 = READ_S32((void *)(intptr_t)v0, 0x1c);
    int32_t t6 = READ_S32((void *)(intptr_t)v0, 0x40);
    int32_t t5 = READ_S32((void *)(intptr_t)v0, 0x30);
    int32_t t4 = READ_S32((void *)(intptr_t)v0, 0x34);
    int32_t t3 = READ_S32((void *)(intptr_t)v0, 0x48);
    int16_t t2 = READ_S16((void *)(intptr_t)v0, 4);
    uint8_t status[0x90];
    void *a1_1;
    uint32_t a1_2;
    int32_t *v0_7;
    int32_t cb;
    int32_t t9_3;
    void *s0_1;

    (void)var_1a8;
    WRITE_S32((void *)(intptr_t)v0, 0xa70, READ_S32((void *)(intptr_t)v0, 0xa70) + 1);
    AL_EncCore_TurnOffRAM(core, 1, 1, 0, 0);
    WRITE_U8((void *)(intptr_t)v0, 0xbad, READ_U8((void *)(intptr_t)v0, 0x3c));
    WRITE_S32((void *)(intptr_t)v0, 0xaf8, a2);
    WRITE_S32((void *)(intptr_t)v0, 0xafc, a3);
    WRITE_S32((void *)(intptr_t)v0, 0xb30, 1);
    WRITE_U8((void *)(intptr_t)v0, 0xba2, 1);
    WRITE_U8((void *)(intptr_t)v0, 0xba1, 1);
    WRITE_S32((void *)(intptr_t)v0, 0xb00, v0_4);
    WRITE_S32((void *)(intptr_t)v0, 0xb04, v1_1);
    WRITE_S32((void *)(intptr_t)v0, 0xb94, 0);
    WRITE_S32((void *)(intptr_t)v0, 0xb10, t6);
    WRITE_S32((void *)(intptr_t)v0, 0xb98, t5);
    WRITE_S32((void *)(intptr_t)v0, 0xb9c, t4);
    WRITE_U8((void *)(intptr_t)v0, 0xba0, t0 & 1U);
    WRITE_S32((void *)(intptr_t)v0, 0xba8, t3);
    WRITE_U16((void *)(intptr_t)v0, 0xba4, (uint16_t)t2);
    WRITE_S32((void *)(intptr_t)v0, 0xb2c, t1);
    memset(status, 0, sizeof(status));
    AL_EncCore_ReadStatusRegsJpeg(core, status);
    a1_1 = READ_PTR((void *)(intptr_t)v0, 0x318);
    a1_2 = (uint32_t)READ_U8(status, 1);
    v0_7 = (int32_t *)((uint8_t *)READ_PTR(a1_1, 8) + (READ_S32((void *)(intptr_t)v0, 0xb30) << 4) - 0x10 +
                       READ_S32(a1_1, 0x18));
    v0_7[0] = READ_S32(a1_1, 0x14);
    v0_7[1] = READ_S32(status, 8);
    v0_7[2] = -1;
    v0_7[3] = -1;
    if (a1_2 != 0U) {
        WRITE_S32((void *)(intptr_t)v0, 0xb94, 0x88);
    }

    Rtos_GetMutex(READ_PTR(arg1, 0x170));
    ((int32_t (*)(void *, void *, int32_t))(intptr_t)READ_S32(arg1, 0x144))((uint8_t *)arg1 + 0x128,
                                                                             (uint8_t *)(intptr_t)v0 + 0x20, 0);
    Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));

    cb = READ_S32(arg1, 0x1a9c);
    if (cb != 0) {
        uint8_t trace[0x1a0];

        JpegStatusToStatusRegs(status, (void *)(intptr_t)READ_S32((void *)(intptr_t)v0, 0xa78));
        memset(trace, 0, sizeof(trace));
        FillEncTrace((int32_t *)trace, arg1, (void *)(intptr_t)v0);
        WRITE_S32(trace, 0x10, READ_S32((void *)(intptr_t)v0, 0xab8));
        WRITE_S32(trace, 0x14, READ_S32((void *)(intptr_t)v0, 0xa78));
        WRITE_S32(trace, 0x18, 9);
        ((int32_t (*)(void *, void *))(intptr_t)cb)(trace, READ_PTR(arg1, 0));
    }

    ReleaseWorkBuffers(arg1, (void *)(intptr_t)v0);

    {
        int32_t v0_10 = (int32_t)(intptr_t)EndRequestsBuffer_Pop((uint8_t *)arg1 + 0x1604);
        int32_t *i = (int32_t *)(intptr_t)(v0 + 0xaf8);
        int32_t *a0_13;

        (*(void **)(void *)(intptr_t)v0_10) = READ_PTR((void *)(intptr_t)v0, 0x318);
        a0_13 = (int32_t *)(intptr_t)(v0_10 + 8);
        do {
            a0_13[0] = i[0];
            a0_13[1] = i[1];
            a0_13[2] = i[2];
            a0_13[3] = i[3];
            i += 4;
            a0_13 += 4;
        } while ((intptr_t)i != v0 + 0xbd8);
        StaticFifo_Queue((StaticFifoCompat *)((uint8_t *)arg1 + 0x12b24), v0_10);
    }

    Rtos_GetMutex(READ_PTR(arg1, 0x170));
    Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
    WRITE_S32(arg1, 0x35b4, READ_S32(arg1, 0x35b4) + 1);
    t9_3 = READ_S32(arg1, 0x1aa8);
    if (t9_3 != 0) {
        ((void (*)(void *, void *))(intptr_t)t9_3)(READ_PTR(arg1, 0x1aac), (uint8_t *)(intptr_t)v0 + 0x84c);
    }

    s0_1 = READ_PTR(READ_PTR((void *)(intptr_t)v0, 0x18), 0x24);
    if (s0_1 != 0) {
        int32_t t9_4 = READ_S32(s0_1, 0x434);

        if (t9_4 != 0 && READ_S32(s0_1, 0x438) != 0) {
            ((void (*)(void *, int32_t, int32_t))(intptr_t)t9_4)(READ_PTR(s0_1, 0x43c), READ_S32(s0_1, 0x438), 1);
        }

        {
            int32_t v0_14;
            int32_t v1_6;
            void *a0_19 = READ_PTR(s0_1, 0x428);

            v0_14 = Rtos_GetTime();
            v1_6 = Rtos_GetTime();
            WRITE_S32(s0_1, 0x410, v0_14);
            WRITE_S32(s0_1, 0x414, v1_6);
            WRITE_S32(a0_19, 0x160, READ_S32(status, 8));
            WRITE_U16(a0_19, 0x164, READ_U16(arg1, 0x80));
        }
    }

    return 1;
}

static void *SetChannelTraceCallBack_1a9c(void *arg1, void *arg2, void *arg3)
{
    (*(void **)((uint8_t *)arg1 + 0x1a9c)) = arg2;
    (*(void **)((uint8_t *)arg1 + 0x1aa0)) = arg3;
    return (void *)&_gp;
}

int32_t AL_EncChannel_ScheduleDestruction(void *arg1, void *arg2, void *arg3)
{
    void *var_10 = &_gp;

    (void)var_10;
    Rtos_GetMutex(READ_PTR(arg1, 0x170));
    WRITE_U16(arg1, 0x1ae6, 1);
    (*(void **)((uint8_t *)arg1 + 0x1a0c)) = arg2;
    (*(void **)((uint8_t *)arg1 + 0x1aa8)) = arg3;
    return Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
}

uint32_t AL_EncChannel_SetNumberOfCores(void *arg1)
{
    void *var_38 = &_gp;
    uint8_t var_30[0x30];
    uint32_t s2;
    int32_t v0_1;
    uint32_t result_1;
    uint32_t lo;
    uint32_t v0_3;
    uint32_t result;

    (void)var_38;
    memset(var_30, 0, sizeof(var_30));
    AL_CoreConstraintEnc_Init(var_30, READ_S32(arg1, 0x2c), (uint32_t)READ_U8(arg1, 0x1f));
    s2 = AL_CoreConstraintEnc_GetExpectedNumberOfCores(var_30, arg1);
    v0_1 = AL_CoreConstraintEnc_GetResources(var_30, READ_S32(arg1, 0x2c), (uint32_t)READ_U16(arg1, 4),
                                             (uint32_t)READ_U16(arg1, 6), (uint32_t)READ_U16(arg1, 0x74),
                                             (uint32_t)READ_U16(arg1, 0x76), s2);
    result_1 = (uint32_t)READ_U16(arg1, 0x1a88);
    lo = (uint32_t)READ_S32(arg1, 0x1a84) / result_1;
    if (result_1 == 0U || lo == 0U) {
        __assert("0", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/ChannelMngr.c", 0, "AL_EncChannel_SetNumberOfCores",
                 &_gp);
    }

    v0_3 = (uint32_t)((lo - 1 + (uint32_t)v0_1) / lo);
    if ((int32_t)v0_3 < (int32_t)s2) {
        v0_3 = s2;
    }

    result = v0_3 & 0xffU;
    if ((int32_t)result_1 < (int32_t)result) {
        result = result_1;
    }

    WRITE_U8(arg1, 0x3c, (uint8_t)result);
    return result;
}

uint32_t AL_EncChannel_ChannelCanBeLaunched(void *arg1)
{
    uint32_t result = (uint32_t)READ_U16(arg1, CH_GATE_OFF);
    StaticFifoCompat *lane0 = (StaticFifoCompat *)((uint8_t *)arg1 + 0x129b4);
    StaticFifoCompat *lane1 = (StaticFifoCompat *)((uint8_t *)arg1 + 0x12a10);

    ENC_KMSG("ChannelCanBeLaunched chctx=%p gate=0x%x active=%d need=%d flags40=%d q0=%p r/w/c=%d/%d/%d q1=%p r/w/c=%d/%d/%d",
             arg1, result, READ_S32(arg1, 0x2c), READ_S32(arg1, 0x30), READ_S32(arg1, 0x40),
             lane0, lane0->read_idx, lane0->write_idx, lane0->capacity,
             lane1, lane1->read_idx, lane1->write_idx, lane1->capacity);
    return result;
}

int32_t endOfInput(void *arg1)
{
    if (READ_U8(arg1, CH_STARTED_OFF) == 0U) {
        return 1;
    }

    {
        void *var_10 = &_gp;

        (void)var_10;
        return (AL_SrcReorder_GetWaitingSrcBufferCount((uint8_t *)arg1 + 0x178) > 0) ? 1 : 0;
    }
}

int32_t AL_EncChannel_GetBufResources(int32_t *arg1, void *arg2)
{
    void *var_20 = &_gp;
    int32_t v0 = AL_DPBConstraint_GetMaxDPBSize(arg2);
    int32_t s0 = v0 + 1;
    int32_t v0_2;
    int32_t s0_2;
    int32_t s3_1;

    (void)var_20;
    if ((uint32_t)READ_U8(arg2, 0x1f) == 1U) {
        s0 = v0;
    }

    v0_2 = AL_RefMngr_GetBufferSize((uint8_t *)arg2 + 0x22c8);
    s0_2 = s0 + ((READ_S32(arg2, 0x1adc) < 2) ? 0 : 1) + ((READ_S32(arg2, 0x2c) >> 6) & 1);
    if ((uint32_t)READ_U8(arg2, 0x1f) == 4U) {
        s0_2 = 0;
    }

    s3_1 = READ_S32(arg2, 0x1adc);
    arg1[3] = AL_IntermMngr_GetBufferSize((uint8_t *)arg2 + 0x2a54);
    arg1[0] = s0_2;
    arg1[1] = v0_2;
    arg1[2] = s3_1;
    if (arg1[2] <= 0 && READ_U8(arg2, 0x1f) != 4U && arg1[3] > 0) {
        arg1[2] = 1;
        ENC_KMSG("GetBufResources force-interm-buffer codec=%u size=%d",
                 (unsigned)READ_U8(arg2, 0x1f), arg1[3]);
    }
    return (int32_t)(intptr_t)arg1;
}

int32_t AL_EncChannel_PushRefBuffer(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5,
                                    int32_t arg6, void *arg7)
{
    (void)arg7;
    return AL_RefMngr_PushBuffer((uint8_t *)arg1 + 0x22c8, arg2, arg3, (uint32_t)arg4, arg5, arg6);
}

int32_t AL_EncChannel_PushStreamBuffer(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5,
                                       int32_t arg6, int32_t arg7, int32_t arg8, int32_t arg9)
{
    void *var_30 = &_gp;
    int32_t v0_1 = arg5 + arg6;
    int32_t stream_entry[8];
    int32_t a0_1;

    (void)var_30;
    if (arg4 < v0_1) {
        v0_1 = arg4;
    }

    if ((arg4 & 3) != 0) {
        return 0;
    }

    /* OEM passes a contiguous stack record into AL_StreamMngr_AddBuffer.
     * Recreate that record explicitly instead of relying on local-variable
     * layout, which is not a valid C ABI contract. */
    stream_entry[0] = arg2;
    stream_entry[1] = arg3;
    stream_entry[2] = v0_1;
    stream_entry[3] = arg5;
    stream_entry[4] = arg7;
    stream_entry[5] = arg8;
    stream_entry[6] = arg9;
    stream_entry[7] = 0;

    if ((uint32_t)READ_U8(arg1, 0x1f) == 4U) {
        a0_1 = READ_S32(arg1, 0x1aa4);
        if (a0_1 == 0) {
            a0_1 = READ_S32(arg1, 0x2a50);
        }
    } else {
        a0_1 = READ_S32(arg1, 0x2a50);
    }

    ENC_KMSG("PushStreamBuffer queue chctx=%p mgr=%p phys=0x%x virt=%p limit=%d size=%d side0=%d side1=%d side2=%p",
             arg1, (void *)(intptr_t)a0_1, (unsigned)stream_entry[0], (void *)(intptr_t)stream_entry[1],
             stream_entry[2], stream_entry[3], stream_entry[4], stream_entry[5],
             (void *)(intptr_t)stream_entry[6]);
    return AL_StreamMngr_AddBuffer((void *)(intptr_t)a0_1, stream_entry);
}

int32_t AL_EncChannel_PushIntermBuffer(void *arg1, int32_t arg2, int32_t arg3)
{
    void *var_28 = &_gp;
    AL_TIntermBufferCompat interm = { arg2, arg3 };
    int32_t v0;
    int32_t s2_1;
    int32_t ep1_size;

    (void)var_28;
    v0 = AL_IntermMngr_GetEp1Location((uint8_t *)arg1 + 0x2a54, &interm);
    s2_1 = READ_S32(arg1, 0x35c0);
    ep1_size = AL_GetAllocSizeEP1();
    ENC_KMSG("PushInterm chctx=%p in_phys=0x%x in_virt=0x%x ep1_dst=0x%x tpl=0x%x ep1_size=%d",
             arg1, arg2, arg3, v0, s2_1, ep1_size);
    if (s2_1 == 0) {
        ENC_KMSG("PushInterm memset dst=0x%x size=%d", v0, ep1_size);
        Rtos_Memset((void *)(intptr_t)v0, 0, ep1_size);
    } else {
        ENC_KMSG("PushInterm memcpy dst=0x%x src=0x%x size=%d", v0, s2_1, ep1_size);
        Rtos_Memcpy((void *)(intptr_t)v0, (void *)(intptr_t)s2_1, ep1_size);
    }

    ENC_KMSG("PushInterm post-copy addbuf phys=0x%x virt=0x%x", interm.addr, interm.location);
    return AL_IntermMngr_AddBuffer((uint8_t *)arg1 + 0x2a54, &interm);
}

int32_t AL_EncChannel_PushNewFrame(void *arg1, int32_t *arg2, int32_t *arg3, int32_t *arg4)
{
    void *var_e8 = &_gp;
    int32_t *i;
    uint8_t *s0_1;

    (void)var_e8;
    ENC_KMSG("PushNewFrame entry chctx=%p src=%p stream=%p meta=%p flags2c=0x%x eos=%u gate=0x%x freeze=%u",
             arg1, arg2, arg3, arg4, READ_S32(arg1, 0x2c), (unsigned)READ_U8(arg1, CH_STARTED_OFF),
             (unsigned)READ_U16(arg1, CH_GATE_OFF), (unsigned)READ_U8(arg1, CH_FREEZE_OFF));
    Rtos_GetMutex(READ_PTR(arg1, 0x170));
    if (READ_U8(arg1, CH_STARTED_OFF) != 0U) {
        ENC_KMSG("PushNewFrame reject-eos chctx=%p eos=%u flags2c=0x%x",
                 arg1, (unsigned)READ_U8(arg1, CH_STARTED_OFF), READ_S32(arg1, 0x2c));
        return Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
    }

    if (arg2 != 0 && arg3 != 0) {
        i = arg3;
    } else {
        i = arg3;
    }

    if (arg2 == 0 || arg3 == 0 || arg4 == 0) {
        s0_1 = 0;
        ENC_KMSG("PushNewFrame arm-only chctx=%p src=%p stream=%p meta=%p", arg1, arg2, arg3, arg4);
    } else {
        uint8_t src_buf[0xc8];

        /*
         * SrcReorder copies a fixed 0xc8-byte payload and later exposes the
         * command block at +0x48. Build that blob explicitly instead of
         * relying on decompiler-derived stack layout.
         */
        Rtos_Memset(src_buf, 0, sizeof(src_buf));
        Rtos_Memcpy(src_buf, arg4, 9 * sizeof(int32_t));
        Rtos_Memcpy(src_buf + 0x28, arg2, 8 * sizeof(int32_t));
        Rtos_Memcpy(src_buf + 0x48, arg3, 31 * sizeof(int32_t));
        s0_1 = src_buf;
        ENC_KMSG("PushNewFrame prepared chctx=%p pict=%d src0=0x%x src1=0x%x",
                 arg1, arg4[0], arg2[0], arg2[1]);
        ENC_KMSG("PushNewFrame packed meta0=%08x meta1=%08x meta2=%08x meta3=%08x src8=%08x src9=%08x src10=%08x src11=%08x",
                 READ_S32(src_buf, 0x00), READ_S32(src_buf, 0x04), READ_S32(src_buf, 0x08), READ_S32(src_buf, 0x0c),
                 READ_S32(src_buf, 0x48), READ_S32(src_buf, 0x4c), READ_S32(src_buf, 0x50), READ_S32(src_buf, 0x54));
        ENC_KMSG("PushNewFrame packed streammeta cmd=%08x meta0c=%08x meta10=%08x meta14=%08x meta18=%08x meta1c=%08x tailc0=%08x tailc4=%08x",
                 READ_S32(src_buf, 0x48), READ_S32(src_buf, 0x54), READ_S32(src_buf, 0x58), READ_S32(src_buf, 0x5c),
                 READ_S32(src_buf, 0x60), READ_S32(src_buf, 0x64), READ_S32(src_buf, 0xc0), READ_S32(src_buf, 0xc4));
    }

    {
        int32_t req = AddNewRequest((int32_t)(intptr_t)arg1);
        ENC_KMSG("PushNewFrame post-AddNewRequest chctx=%p req=%p srcbuf=%p pict=%d",
                 arg1, (void *)(intptr_t)req, s0_1,
                 req ? READ_S32((void *)(intptr_t)req, 0x20) : -1);
    }
    AL_SrcReorder_AddSrcBuffer((uint8_t *)arg1 + 0x178, s0_1);
    ENC_KMSG("PushNewFrame exit chctx=%p srcbuf=%p", arg1, s0_1);
    return Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
}

int32_t findCurCoreSlice(void *arg1, uint8_t arg2, uint8_t arg3)
{
    uint32_t i = (uint32_t)arg2;
    uint32_t a2 = (uint32_t)arg3;

    do {
        int32_t v1_2 = 1 << (i & 0x1fU);

        i += a2;
        if ((((uint32_t)v1_2 & (uint32_t)READ_S32(arg1, 0x840)) |
             ((uint32_t)(v1_2 >> 31) & (uint32_t)READ_S32(arg1, 0x844))) == 0U) {
            return (int32_t)(i - a2);
        }
    } while (i < 0x40U);

    __assert("curSlice <= 63", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/ChannelMngr.c",
             0xe83, "findCurCoreSlice", &_gp);
    return 0;
}

int32_t GetWPPOrSliceSizeOffset(void *arg1, void *arg2, int32_t arg3, int32_t arg4); /* forward decl, ported by T<N> later */
int32_t GetSliceSizeOffset(void *arg1, void *arg2, int32_t arg3, int32_t arg4,
                           void *arg5, int32_t arg6, int32_t arg7); /* forward decl */
int32_t GetWPPOffset(void *arg1, void *arg2, int32_t arg3, int32_t arg4,
                     void *arg5, int32_t arg6, int32_t arg7); /* forward decl */
int32_t UpdateCommand(void *arg1, void *arg2, void *arg3, int32_t arg4); /* forward decl */
int32_t AL_GetCompLcuSize(uint32_t arg1, uint32_t arg2, int32_t arg3, int32_t arg4); /* forward decl */
int32_t AL_EncCore_Reset(void *arg1); /* forward decl */
int32_t AL_EncCore_EnableInterrupts(void *arg1, uint8_t *arg2, char arg3, char arg4, char arg5); /* forward decl */
void AL_EncCore_TurnOnRAM(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5); /* forward decl */
void AL_EncCore_Encode1(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4); /* forward decl */
void AL_EncCore_ReadStatusRegsEnc(void *arg1, void *arg2); /* forward decl */
int32_t AL_EncCore_DisableEnc1Interrupt(void *arg1); /* forward decl */
int32_t AL_EncCore_DisableEnc2Interrupt(void *arg1); /* forward decl */
void AL_EncCore_ResetWPPCore0(void *arg1); /* forward decl */
int32_t AL_RefMngr_Flush(void *arg1); /* forward decl */
int32_t AL_RefMngr_GetMVBufferSize(void *arg1); /* forward decl */
void AL_RateCtrl_ExtractStatistics(void *arg1, void *arg2); /* forward decl */
int32_t AL_ModuleArray_AddModule(void *arg1, int32_t arg2, int32_t arg3); /* forward decl */
int32_t AL_SrcReorder_IsEosNext(void *arg1); /* forward decl */
int32_t AL_SrcReorder_IsAvailable(void *arg1, int32_t arg2); /* forward decl */
int32_t AL_SrcReorder_GetCommandAndMoveNext(void *arg1); /* forward decl */
void AL_ApplyNewGOPAndRCParams(void *arg1, void *arg2); /* forward decl */
void AL_ApplyGopCommands(void *arg1, void *arg2, int32_t arg3); /* forward decl */
void AL_ApplyGmvCommands(void *arg1, void *arg2); /* forward decl */
void AL_ApplyPictCommands(void *arg1, void *arg2, void *arg3); /* forward decl */
void *AL_SrcReorder_GetReadyCommand(void *arg1, int32_t arg2); /* forward decl */
void AL_RefMngr_SetRecResolution(void *arg1, int32_t arg2, int32_t arg3); /* forward decl */
int32_t AL_RefMngr_GetNewFrmBuffer(void *arg1); /* forward decl */
int32_t AL_IntermMngr_GetBuffer(void *arg1); /* forward decl */
void AL_IntermMngr_ReleaseBufferBack(void *arg1, void *arg2); /* forward decl */
int32_t AL_IntermMngr_GetEp1Addr(void *arg1, void *arg2, int32_t *arg3); /* forward decl */
void AL_GetLambda(int32_t arg1, int32_t arg2, int32_t arg3, uint32_t arg4, void *arg5, void *arg6,
                  uint32_t arg7); /* forward decl */
int32_t AL_IntermMngr_GetEp2Addr(void *arg1, void *arg2, int32_t *arg3); /* forward decl */
int32_t AL_IntermMngr_GetWppAddr(void *arg1, void *arg2, void *arg3); /* forward decl */
int32_t AL_StreamMngr_Init(void *arg1); /* forward decl */
void AL_StreamMngr_Deinit(void *arg1); /* forward decl */
int32_t AL_RefMngr_Init(void *arg1, void *arg2, int32_t arg3, int32_t arg4); /* forward decl */
void AL_RefMngr_Deinit(void *arg1); /* forward decl */
int32_t AL_IntermMngr_Init(void *arg1, void *arg2); /* forward decl */
void AL_IntermMngr_Deinit(void *arg1); /* forward decl */
void AL_SrcReorder_Init(void *arg1); /* forward decl */
void AL_SrcReorder_Deinit(void *arg1); /* forward decl */
int32_t AL_GopMngr_Init(void *arg1, void *arg2, int32_t arg3, int32_t arg4); /* forward decl */
void AL_GopMngr_Deinit(void *arg1); /* forward decl */
int32_t AL_RateCtrl_Init(void *arg1, void *arg2, int32_t arg3, int32_t arg4); /* forward decl */
void AL_RateCtrl_Deinit(void *arg1); /* forward decl */
void AL_GmvMngr_Init(void *arg1); /* forward decl */
void AL_RefMngr_EnableRecOut(void *arg1); /* forward decl */
int32_t AL_GetAllocSizeEP3(void); /* forward decl */
int32_t AL_GetAllocSizeSRD(uint32_t arg1, uint32_t arg2, uint32_t arg3); /* forward decl */
int32_t AlignedAlloc(void *arg1, const void *arg2, int32_t arg3, int32_t arg4, int32_t *arg5,
                     uint32_t *arg6); /* forward decl */
int32_t AL_HEVC_GenerateSkippedPicture(void); /* forward decl */
int32_t AL_AVC_GenerateSkippedPicture(void *arg1, int32_t arg2, int32_t arg3, uint32_t arg4); /* forward decl */
void SetCommandListBuffer(void *arg1, uint32_t arg2, uint32_t arg3, int32_t arg4, int32_t arg5); /* forward decl */
void JpegParamToCtrlRegs(void *arg1, void *arg2); /* forward decl */
void AL_EncCore_EncodeJpeg(void *arg1, void *arg2); /* forward decl */
int32_t embed_watermark(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5,
                        void *arg6, int32_t arg7, int32_t arg8, int32_t arg9); /* forward decl */
void AL_UpdateAutoQpCtrl(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5,
                         int32_t arg6, int32_t arg7, int32_t arg8, int32_t arg9, int32_t arg10); /* forward decl */
void AL_GmvMngr_UpdateGMVPoc(void *arg1, int32_t arg2, int32_t arg3); /* forward decl */
void AL_GmvMngr_GetGMV(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, void *arg6,
                       int32_t arg7, int32_t arg8, int32_t arg9); /* forward decl */
void SliceParamToCmdRegsEnc1(void *arg1, void *arg2, void *arg3, int32_t arg4); /* forward decl */
void SliceParamToCmdRegsEnc2(void *arg1, void *arg2); /* forward decl */

static int32_t NeedsHwRcScratch(const int32_t *channel_param)
{
    return (channel_param[0x1a] == 3) || (channel_param[0x27] != 0) || (channel_param[0x28] != 0) ||
           (channel_param[0x29] != 0);
}

static void FreeHwRcScratchBuffers(int32_t *channel)
{
    AL_TAllocator *dma_alloc = (AL_TAllocator *)READ_PTR(channel, 0x35ac);
    int i;

    if (dma_alloc == NULL || dma_alloc->vtable == NULL || dma_alloc->vtable->Free == NULL) {
        return;
    }

    for (i = 0; i < 3; ++i) {
        void *handle = READ_PTR(channel, 0x35c4 + i * 4);

        if (handle != NULL) {
            dma_alloc->vtable->Free(dma_alloc, handle);
            WRITE_PTR(channel, 0x35c4 + i * 4, NULL);
        }
        WRITE_PTR(channel, 0x35d0 + i * 4, NULL);
    }

    WRITE_S32(channel, 0x35dc, 0);
    WRITE_U8(channel, 0x35e0, 0);
}

static int32_t SetSourceBuffer_isra_74(void *arg1, int32_t *arg2, int32_t arg3, int32_t *arg4)
{
    int32_t *src = (int32_t *)(intptr_t)AL_SrcReorder_GetSrcBuffer((uint8_t *)arg1 + 0x178, arg3);

    arg4[0] = src[0];
    arg4[1] = src[1];
    arg4[2] = src[2];
    arg4[3] = src[3];
    arg4[4] = src[4];
    arg4[5] = src[5];
    arg4[6] = src[6];

    arg2[0] = src[10];
    arg2[1] = src[11];
    arg2[2] = src[12];
    arg2[3] = src[13];
    arg2[4] = src[14];
    arg2[5] = src[15];
    arg2[6] = src[16];
    arg2[7] = src[17];
    ENC_KMSG("SetSourceBuffer pict=%d src=%p req=%p io=[%08x %08x %08x %08x %08x %08x %08x] aux=[%08x %08x %08x %08x %08x %08x %08x %08x]",
             arg3, src, arg2,
             arg4[0], arg4[1], arg4[2], arg4[3], arg4[4], arg4[5], arg4[6],
             arg2[0], arg2[1], arg2[2], arg2[3], arg2[4], arg2[5], arg2[6], arg2[7]);
    return src[17];
}

static int32_t UpdateRateCtrl_constprop_83(int32_t *arg1, int32_t *arg2, int32_t *arg3, int32_t arg4, uint8_t arg5)
{
    int32_t rounded_bits = ((arg3[1] + 7) >> 3) << 3;
    int32_t status = 0;
    int32_t max_bits;
    int32_t produced;
    int32_t slice_budget;
    int32_t *rc = &arg1[0x3d];
    int32_t *frm = &arg2[8];

    if (arg4 == 0) {
        max_bits = arg1[3] << 3;
        if (arg2[0xc] != 7) {
            Rtos_GetMutex((void *)(uintptr_t)arg1[0x48]);
            ((void (*)(void *, void *, void *, int32_t, int32_t *))(intptr_t)arg1[0x3f])(rc, frm, arg3, max_bits, &status);
            Rtos_ReleaseMutex((void *)(uintptr_t)arg1[0x48]);
        } else {
            slice_budget = arg2[0xb2];
            goto final_update;
        }

        if ((uint32_t)arg5 != 0U && status < 0) {
            if ((arg1[0x24] & 8) != 0 && READ_U8(arg1, 0x3de0) == 0 && READ_U8(arg1, 0xc4) == 0 &&
                (arg2[9] & 1) == 0) {
                WRITE_U8(arg2, 0xb08, 1);
                AL_SrcReorder_Cancel(&arg1[0x5e], arg2[8]);
                ReleaseRefBuffers(arg1, arg2);
                Rtos_GetMutex((void *)(uintptr_t)arg1[0x5c]);
                ((void (*)(void *, void *))(intptr_t)arg1[0x50])(&arg1[0x4a], frm);
                Rtos_ReleaseMutex((void *)(uintptr_t)arg1[0x5c]);
                AL_SrcReorder_MarkSrcBufferAsUsed(&arg1[0x5e], arg2[8]);
                FillSliceParamFromPicParam(arg1, &arg2[0x5c], arg2);
                SetSourceBuffer_isra_74(arg1, arg2, arg2[8], &arg2[0xa6]);
                SetPictureReferences(arg1, arg2);
                SetPictureRefBuffers(arg1, arg2, arg1, arg2, (char)READ_U8(arg2, 0x290), &arg2[0xa6]);

                {
                    uint8_t old = READ_U8(arg2, 0x290);
                    uint8_t cur = (uint8_t)AL_RefMngr_GetFrmBuffer(&arg1[0x8b2], READ_U8(arg2, 0x291));
                    WRITE_U8(arg2, 0x290, cur);
                    if (old != 0xffU) {
                        AL_RefMngr_ReleaseFrmBuffer(&arg1[0x8b2], (char)old);
                    }
                }

                max_bits = OutputSkippedPicture(arg1, arg2, arg3) << 3;
                Rtos_GetMutex((void *)(uintptr_t)arg1[0x48]);
                ((void (*)(void *, void *, void *, int32_t, int32_t *))(intptr_t)arg1[0x3f])(rc, frm, arg3, max_bits, &status);
                Rtos_ReleaseMutex((void *)(uintptr_t)arg1[0x48]);
            }
        }
    } else {
        produced = READ_S32(arg3, 0x44) << 3;
        ENC_KMSG("UpdateRateCtrl arg4=1 frm=%p status=%p produced=%d rounded=%d type=%d skip=%u",
                 frm, arg3, produced, rounded_bits, arg2[0xc], (unsigned)READ_U8(arg2, 0xb08));

        if (produced >= rounded_bits) {
            rounded_bits = produced;
        }
        max_bits = rounded_bits;
        if (arg2[0xc] != 7) {
            ENC_KMSG("UpdateRateCtrl pre-lock rc=%p lock=%p cb3f=%p max_bits=%d",
                     rc, (void *)(uintptr_t)arg1[0x48], (void *)(intptr_t)arg1[0x3f], max_bits);
            Rtos_GetMutex((void *)(uintptr_t)arg1[0x48]);
            ENC_KMSG("UpdateRateCtrl post-lock rc=%p lock=%p", rc, (void *)(uintptr_t)arg1[0x48]);
            ((void (*)(void *, void *, void *, int32_t, int32_t *))(intptr_t)arg1[0x3f])(rc, frm, arg3, max_bits, &status);
            ENC_KMSG("UpdateRateCtrl post-cb3f rc=%p status=%d", rc, status);
            Rtos_ReleaseMutex((void *)(uintptr_t)arg1[0x48]);
            ENC_KMSG("UpdateRateCtrl post-unlock rc=%p", rc);
        } else {
            slice_budget = arg2[0xb2];
            goto final_update;
        }
    }

    if (status <= 0) {
        slice_budget = arg2[0xb2];
    } else {
        slice_budget = 8;
        if (status >= 8) {
            slice_budget = status;
        }
        arg2[0xb2] = slice_budget;
    }

final_update:
    if (READ_U8(arg1, 0x1f) == 0U && READ_U8(arg1, 0x3c) == 1U && max_bits <= 0 && slice_budget <= 0) {
        WRITE_U8(arg1, 0x3de0, 0);
        ENC_KMSG("UpdateRateCtrl final skip-zero-single-avc rc=%p max_bits=%d skip=%u slice_budget=%d",
                 rc, max_bits, (unsigned)READ_U8(arg2, 0xb08), slice_budget);
        return 0;
    }

    ENC_KMSG("UpdateRateCtrl final pre-lock rc=%p lock=%p cb40=%p max_bits=%d skip=%u slice_budget=%d",
             rc, (void *)(uintptr_t)arg1[0x48], (void *)(intptr_t)arg1[0x40], max_bits,
             (unsigned)READ_U8(arg2, 0xb08), slice_budget);
    Rtos_GetMutex((void *)(uintptr_t)arg1[0x48]);
    ENC_KMSG("UpdateRateCtrl final post-lock rc=%p", rc);
    WRITE_U8(arg1, 0x3de0, 0);
    ((void (*)(void *, void *, void *, int32_t, uint32_t, int32_t, void *))(intptr_t)arg1[0x40])(
        rc, frm, arg3, max_bits, (uint32_t)READ_U8(arg2, 0xb08), slice_budget << 3, &_gp);
    ENC_KMSG("UpdateRateCtrl final post-cb40 rc=%p", rc);
    Rtos_ReleaseMutex((void *)(uintptr_t)arg1[0x48]);
    ENC_KMSG("UpdateRateCtrl final post-unlock rc=%p", rc);
    return 0;
}

void OutputSlice(void *arg1, void *arg2, int32_t arg3, void *arg4)
{
    uint8_t *enc_stat_buf;
    uint8_t merged[0x78];
    size_t enc_stat_buf_size;
    int32_t i;
    uint32_t num_core;

    num_core = READ_U8(arg2, 0x1ee);
    if (num_core == 0U) {
        __assert("pReq->tSliceParam.NumCore > 0",
                 "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/ChannelMngr.c",
                 0x1a4, "CmdList_MergeMultiSliceStatus", &_gp);
    }

    enc_stat_buf_size = (size_t)num_core * 0x78U;
    enc_stat_buf = (uint8_t *)Rtos_Malloc(enc_stat_buf_size);
    if (enc_stat_buf == NULL) {
        __builtin_trap();
    }

    Rtos_Memset(enc_stat_buf, 0, enc_stat_buf_size);
    InitSliceStatus(merged);
    for (i = 0; i < (int32_t)num_core; ++i) {
        InitSliceStatus(enc_stat_buf + i * 0x78);
    }
    ENC_KMSG("OutputSlice entry req=%p slice=%d num_core=%u phases=%d",
             arg2, arg3, (unsigned)num_core, READ_S32(arg2, 0xa6c));

    {
        void *cmd_regs;
        void *cmd_regs_stream;
        void *stream_meta = READ_PTR(arg2, 0x318);
        int32_t cmd_index = arg3 << 9;
        int32_t stream_end_word = 0xc8;
        int32_t stream_end;

        if (READ_S32(arg2, 0x174) == 0) {
            cmd_regs = (uint8_t *)READ_PTR(arg2, 0xa78) + cmd_index;
        } else {
            uint32_t core_count = READ_U8(arg1, 0x3c);

            if (core_count == 0U) {
                __builtin_trap();
            }
            cmd_regs = (uint8_t *)READ_PTR(arg2, ((arg3 % (int32_t)core_count) + 0x29c) * 4 + 8) +
                       ((uint32_t)GetSliceEnc2CmdOffset(core_count, READ_U16(arg1, 0x40), arg3) << 9);
        }
        cmd_regs_stream = cmd_regs;

        CmdRegsEnc1ToSliceParam(cmd_regs, (uint8_t *)arg2 + 0x170, READ_S32(arg1, CH_CMD_TRACE_FLAGS_OFF));
        if (READ_S32(arg2, 0xa6c) <= 0) {
            stream_end = READ_S32(cmd_regs_stream, stream_end_word);
        } else {
            int32_t grp;

            for (grp = 0; grp < READ_S32(arg2, 0xa6c); ++grp) {
                uint8_t *base = (uint8_t *)arg2 + 0x84c + grp * 0x110;
                int32_t slice_cnt = READ_S32(base, 0x80);
                int32_t j;
                int32_t disabled = 0;

                for (j = 0; j < slice_cnt; ++j) {
                    disabled += (READ_S32(base, 4 + j * 8) ^ 1) == 0;
                }
                if (disabled != 0) {
                    uint32_t core_count = READ_U8(arg1, 0x3c);

                    if (core_count != 0U) {
                        cmd_regs_stream = (uint8_t *)READ_PTR(arg2, ((arg3 % (int32_t)core_count) + 0x29c) * 4 + 8) +
                                          ((uint32_t)GetSliceEnc2CmdOffset(core_count, READ_U16(arg1, 0x40), arg3) << 9);
                        stream_end_word = 0xf8;
                    }
                    break;
                }
            }
            stream_end = READ_S32(cmd_regs_stream, stream_end_word);
        }
        WRITE_S32(READ_PTR(arg2, 0x318), 0x14, stream_end);
        ENC_KMSG("OutputSlice stream-end req=%p slice=%d cmd=%p off=0x%x end=0x%x",
                 arg2, arg3, cmd_regs_stream, stream_end_word, stream_end);
        if (stream_end == 0 && stream_meta != NULL) {
            uint8_t *stream_base = (uint8_t *)READ_PTR(stream_meta, 8);
            int32_t stream_size = READ_S32(stream_meta, 0x10);
            int32_t stream_off = READ_S32(stream_meta, 0x14);
            int32_t raw_end = ScanStreamBufferRawEnd(stream_base, stream_size);
            int32_t first_nz = FindFirstNonZeroByte(stream_base, stream_off, stream_size);

            ENC_KMSG("OutputSlice stream-probe req=%p slice=%d base=%p size=%d off=%d raw_end=%d first_nz=%d",
                     arg2, arg3, stream_base, stream_size, stream_off, raw_end, first_nz);
            if (first_nz >= 0 && first_nz + 16 <= stream_size) {
                ENC_KMSG("OutputSlice stream-probe-bytes req=%p slice=%d @0x%x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                         arg2, arg3, first_nz,
                         stream_base[first_nz + 0], stream_base[first_nz + 1],
                         stream_base[first_nz + 2], stream_base[first_nz + 3],
                         stream_base[first_nz + 4], stream_base[first_nz + 5],
                         stream_base[first_nz + 6], stream_base[first_nz + 7],
                         stream_base[first_nz + 8], stream_base[first_nz + 9],
                         stream_base[first_nz + 10], stream_base[first_nz + 11],
                         stream_base[first_nz + 12], stream_base[first_nz + 13],
                         stream_base[first_nz + 14], stream_base[first_nz + 15]);
            }
        }
    }

    for (i = 0; i < (int32_t)num_core; ++i) {
        EncodingStatusRegsToSliceStatus((uint8_t *)READ_PTR(arg2, 0xa78 + i * 4) + (arg3 << 9), enc_stat_buf + i * 0x78);
        MergeEncodingStatus(merged, enc_stat_buf + i * 0x78);
    }

    if (READ_S32(arg2, 0xa6c) > 0) {
        int32_t grp;
        int32_t merged_any = 0;

        for (grp = 0; grp < READ_S32(arg2, 0xa6c); ++grp) {
            uint8_t *base = (uint8_t *)arg2 + 0x84c + grp * 0x110;
            int32_t slice_cnt = READ_S32(base, 0x80);
            int32_t j;
            int32_t disabled = 0;

            for (j = 0; j < slice_cnt; ++j) {
                disabled += (READ_S32(base, 4 + j * 8) ^ 1) == 0;
            }
            if (disabled != 0) {
                CmdList_MergeMultiSliceEntropyStatus(arg1, arg2, enc_stat_buf, merged,
                                                     (uint8_t)(arg3 % (int32_t)READ_U8(arg1, 0x3c)),
                                                     (uint8_t)GetSliceEnc2CmdOffset(READ_U8(arg1, 0x3c),
                                                                                    READ_U16(arg1, 0x40), arg3));
                merged_any = 1;
                break;
            }
        }

        if (merged_any == 0 && num_core != 0U) {
            for (i = 0; i < (int32_t)num_core; ++i) {
                CmdList_MergeMultiSliceEntropyStatus(arg1, arg2, enc_stat_buf, merged, (uint8_t)i, (uint8_t)arg3);
            }
        }
    }

    MergeEncodingStatus(arg4, merged);
    MergeEntropyStatus(arg4, merged);
    {
        void *stream_meta = READ_PTR(arg2, 0x318);
        int32_t section_count = READ_S32(arg2, 0xb30);
        int32_t byte_count = READ_S32(arg4, 4);
        int32_t header_bytes = 0;
        uint32_t codec = (uint32_t)READ_U8(arg1, 0x1f);

        if (codec == 0U) {
            uint32_t core_count = (uint32_t)READ_U8(arg1, 0x3c);

            if (core_count != 0U) {
                uint8_t *cmd = (uint8_t *)READ_PTR(arg2, (((arg3 % (int32_t)core_count) + 0x29c) << 2) + 8) +
                               ((uint32_t)GetSliceEnc2CmdOffset(core_count, READ_U16(arg1, 0x40), arg3) << 9);
                header_bytes = (int32_t)(READ_U16(cmd, 0x6e) & 0x03ffU);
            }
        }

        if (stream_meta != NULL) {
            uint8_t *stream_base = (uint8_t *)READ_PTR(stream_meta, 8);
            int32_t stream_size = READ_S32(stream_meta, 0x10);
            int32_t stream_end = READ_S32(stream_meta, 0x14);
            int32_t desc_off = READ_S32(stream_meta, 0x18);
            int32_t desc_pos = desc_off + (section_count << 4);
            uint32_t shift = (uint32_t)READ_U8(arg1, 0x4e);
            uint32_t min_lcu = ((((1U << (shift & 0x1fU)) - 1U) + (uint32_t)READ_U16(arg1, 6)) >>
                                (shift & 0x1fU));
            uint32_t max_cfg = (uint32_t)READ_U16(arg1, 0x40);
            uint32_t row_count = (uint32_t)READ_U8(arg1, 0x3c);
            uint32_t desc_tail;
            int32_t desc_limit;

            if (READ_U8(arg1, 0x3e) != 0U)
                max_cfg = 0xc8U;
            if (min_lcu > max_cfg)
                max_cfg = min_lcu;
            desc_tail = (((max_cfg * row_count + 0x10U) << 4) + 0x7fU) >> 7;
            desc_tail <<= 7;
            desc_limit = stream_size + (int32_t)desc_tail;

            if (header_bytes > stream_end) {
                ENC_KMSG("OutputSlice clamp-header req=%p slice=%d header=%d stream_end=%d",
                         arg2, arg3, header_bytes, stream_end);
                header_bytes = stream_end;
            }

            if (stream_base != NULL && section_count >= 0 && desc_pos >= 0 && desc_pos + 0x10 <= desc_limit) {
                int32_t *desc = (int32_t *)(void *)(stream_base + desc_pos);
                int32_t section_inc = 1;

                desc[0] = stream_end - header_bytes;
                desc[1] = byte_count + header_bytes;
                desc[2] = READ_S32(arg2, 0x28c);
                desc[3] = READ_S32(arg2, 0x1a0);
                if (codec != 0U)
                    section_inc = (int32_t)READ_U8(arg1, 0x3c);
                WRITE_S32(arg2, 0xb30, section_count + section_inc);
                WRITE_U16(arg2, 0xb24, 1);
                WRITE_U16(arg2, 0xb26, 1);
                ENC_KMSG("OutputSlice section-desc req=%p slice=%d idx=%d off=%d len=%d qp=%d type=%d cnt=%d stream_end=%d desc_off=%d bytes=%d header=%d",
                         arg2, arg3, section_count, desc[0], desc[1], desc[2], desc[3],
                         READ_S32(arg2, 0xb30), stream_end, desc_off, byte_count, header_bytes);
            } else {
                ENC_KMSG("OutputSlice section-skip req=%p slice=%d base=%p payload=%d tail=%u limit=%d desc_pos=%d count=%d",
                         arg2, arg3, stream_base, stream_size, desc_tail, desc_limit, desc_pos, section_count);
            }
        }
    }
    ENC_KMSG("OutputSlice exit req=%p slice=%d bytes=%d err=%u full=%u overflow=%u",
             arg2, arg3, READ_S32(arg4, 4),
             (unsigned)READ_U8(arg4, 0), (unsigned)READ_U8(arg4, 1), (unsigned)READ_U8(arg4, 2));
    Rtos_Free(enc_stat_buf);
}

int32_t TerminateRequest(void *arg1, int32_t *arg2, int32_t *arg3)
{
    int32_t i;
    int32_t mv_dst;
    int32_t mv_src;
    int32_t do_cb = 1;
    int32_t use_rc = 1;
    int32_t stats[4];

    (void)use_rc;
    if (READ_U8(arg1, 0x3c) != 0U) {
        for (i = 0; i < (int32_t)READ_U8(arg1, 0x3c); ++i) {
            WRITE_S32(arg1, 0x30 + i * 4, 0);
        }
    }

    WRITE_S32(arg1, 0x35bc, READ_S32(arg1, 0x35bc) + 1);
    SetTileInfoIfNeeded(arg1, &arg2[0x2be], arg1, READ_U8(arg2, 0x19d));
    WRITE_U16(arg2, 0xb28, READ_U16(arg2, 0x198));
    WRITE_U16(arg3, 0x3c, READ_U16(arg2, 0x198));
    WRITE_U8(arg2, 0xbad, READ_U8(arg2, 0x17e));

    if (READ_U8(arg1, 0x1f) != 0U || READ_U8(arg1, 0x3c) == 1U) {
        stats[0] = 1;
        UpdateRateCtrl_constprop_83(arg1, arg2, arg3, 0, 1);
        mv_dst = arg2[0xd6];
        do_cb = 1;
    } else {
        stats[0] = 1;
        mv_dst = arg2[0xd6];
        do_cb = 0;
    }

    if (mv_dst != 0) {
        mv_src = arg2[0xc7];
        if (mv_src != 0) {
            Rtos_Memcpy((void *)(intptr_t)mv_dst, (void *)(intptr_t)(mv_src + 0x100),
                        AL_RefMngr_GetMVBufferSize((uint8_t *)arg1 + 0x22c8) - 0x100);
        }
    }

    AL_RateCtrl_ExtractStatistics(arg3, &arg2[0x2ec]);
    if (do_cb != 0) {
        uint32_t skip = READ_U8(arg2, 0xb08);

        ((void (*)(void *, uint32_t, void *, void *, int32_t))(intptr_t)READ_S32(arg1, 0x14c))
            ((uint8_t *)arg1 + 0x128, skip, &arg2[8], arg3, stats[0]);
        ((void (*)(void *, void *, int32_t))(intptr_t)READ_S32(arg1, 0x144))((uint8_t *)arg1 + 0x128, &arg2[8], 0);
    }

    if (READ_U8(arg2, 0x1d3) != 0U) {
        if (READ_U8(arg2, 0x1a0) == 2U) {
            WRITE_U8(arg1, 0x35e0, 0);
        } else {
            uint64_t weighted = (uint64_t)((READ_S32(arg3, 0x2c) << 4) + (READ_S32(arg3, 0x30) << 6) +
                                           READ_S32(arg3, 0x24) + (READ_S32(arg3, 0x28) << 2));
            WRITE_U8(arg1, 0x35e0, ((weighted / 3U) < (uint64_t)READ_S32(arg3, 0x1c)) ? 1 : 0);
        }
    }

    return 1;
}

int32_t AL_EncChannel_GetNextFrameToOutput(void *arg1, int32_t *arg2)
{
    int32_t *entry;
    void *stream_meta;
    uint8_t end_evt[0x100];
    StaticFifoCompat *fifo = (StaticFifoCompat *)((uint8_t *)arg1 + 0x12b24);

    ENC_KMSG("GetNextFrameToOutput entry chctx=%p fifo=%p elems=%p r=%d w=%d cap=%d eos_field=%u done=%d flushed=%u",
             arg1, fifo, fifo->elems, fifo->read_idx, fifo->write_idx, fifo->capacity,
             (unsigned)READ_U8(arg1, CH_STARTED_OFF), READ_S32(arg1, 0x35b0), (unsigned)READ_U8(arg1, CH_DRAINED_OFF));
    Rtos_GetMutex(READ_PTR(arg1, 0x170));
    ENC_KMSG("GetNextFrameToOutput post-lock fifo r=%d w=%d cap=%d empty=%d",
             fifo->read_idx, fifo->write_idx, fifo->capacity, (int)StaticFifo_Empty(fifo));
    if (fifo->elems == NULL || fifo->capacity == 0) {
        Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
        ENC_KMSG("GetNextFrameToOutput skip uninit fifo");
        return 0;
    }
    if (StaticFifo_Empty((uint8_t *)arg1 + 0x12b24) == 0) {
        int32_t out_cb = READ_S32(arg1, CH_OUT_CB_OFF);
        int32_t out_ctx = READ_S32(arg1, CH_OUT_CTX_OFF);
        int32_t out_arg2 = (int32_t)(uint32_t)READ_U8(arg1, CH_OEM_CHANNEL_ID_OFF);

        entry = (int32_t *)(intptr_t)StaticFifo_Dequeue((uint8_t *)arg1 + 0x12b24);
        stream_meta = entry ? (void *)(intptr_t)entry[0] : NULL;
        ENC_KMSG("GetNextFrameToOutput dequeued entry=%p stream_meta=%p cb=%p cb_ctx=%p low48=%p low4c=%p",
                 entry, stream_meta, (void *)(intptr_t)out_cb, (void *)(intptr_t)out_ctx,
                 (void *)(intptr_t)READ_S32(arg1, 0x48), (void *)(intptr_t)READ_S32(arg1, 0x4c));
        memset(arg2, 0, 0x100);
        WRITE_S32(arg2, 0x00, out_cb);
        WRITE_S32(arg2, 0x04, out_ctx);
        WRITE_S32(arg2, 0x08, out_arg2);
        if (entry != NULL) {
            memcpy((uint8_t *)arg2 + 0x10, entry + 2, 0xe0);
        }
        if (stream_meta != NULL) {
            WRITE_S32(arg2, 0xf0, READ_S32(stream_meta, 0x00));
            WRITE_S32(arg2, 0xf4, READ_S32(stream_meta, 0x04));
        }
        WRITE_U8(arg2, 0xf8, 0);
        ENC_KMSG("GetNextFrameToOutput composed dst=%p cb=%p ctx=%p ignored=0x%x payload=%p tail0=0x%x tail1=0x%x flag=%u",
                 arg2, (void *)(intptr_t)READ_S32(arg2, 0x00), (void *)(intptr_t)READ_S32(arg2, 0x04),
                 READ_S32(arg2, 0x08), (uint8_t *)arg2 + 0x10,
                 READ_S32(arg2, 0xf0), READ_S32(arg2, 0xf4), (unsigned)READ_U8(arg2, 0xf8));
        Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
        ENC_KMSG("GetNextFrameToOutput return queued");
        return 1;
    }

    if (0 && READ_U8(arg1, CH_STARTED_OFF) != 0U && READ_S32(arg1, 0x35b0) == READ_S32(arg1, 0x35b4) &&
        READ_U8(arg1, CH_DRAINED_OFF) == 0U) {
        WRITE_U8(arg1, CH_DRAINED_OFF, 1);
        AL_RefMngr_Flush((uint8_t *)arg1 + 0x22c8);
        memset(end_evt, 0, sizeof(end_evt));
        WRITE_S32(end_evt, 0, READ_S32(arg1, 0x44));
        WRITE_S32(end_evt, 4, READ_S32(arg1, 0x48));
        WRITE_U32(end_evt, 8, READ_U8(arg1, 0x3c));
        WRITE_U8(end_evt, 0x100 - 0xf0, 1);
        memcpy(arg2, end_evt, 0xe0);
        Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
        ENC_KMSG("GetNextFrameToOutput return eos");
        return 1;
    }

    ProbeLane1WhileOutputEmpty(arg1);
    Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
    ENC_KMSG("GetNextFrameToOutput return empty");
    return 0;
}

int32_t handleOutputTraces(void *arg1, void *arg2, uint8_t arg3, char arg4)
{
    uint8_t trace[0x128];
    int32_t cb;

    cb = READ_S32(arg1, 0x1a9c);
    if (cb == 0) {
        return (int32_t)(intptr_t)((uint8_t *)arg2 + 0xa78);
    }
    ENC_KMSG("handleOutputTraces entry chctx=%p req=%p cur=%u kind=%d trace_cb=%p",
             arg1, arg2, (unsigned)arg3, (int)arg4, (void *)(intptr_t)cb);
    FillEncTrace((int32_t *)trace, arg1, arg2);
    ENC_KMSG("handleOutputTraces post-fill chctx=%p req=%p trace10=%u trace14=%u",
             arg1, arg2, (unsigned)READ_U8(trace, 0x10), (unsigned)READ_U16(trace, 0x14));
    WRITE_U8(trace, 0x116, arg3);
    WRITE_U8(trace, 0x117, (uint8_t)arg4);
    ENC_KMSG("handleOutputTraces pre-cb chctx=%p req=%p cb=%p arg0=0x%x",
             arg1, arg2, (void *)(intptr_t)cb, READ_S32(arg1, 0));
    {
        int32_t rc = ((int32_t (*)(void *, int32_t))(intptr_t)cb)(trace, READ_S32(arg1, 0));

        ENC_KMSG("handleOutputTraces post-cb chctx=%p req=%p rc=%d", arg1, arg2, rc);
        return rc;
    }
}

int32_t CommitSlice(void *arg1, void *arg2, int32_t *arg3, int32_t arg4, int32_t arg5, int32_t arg6)
{
    int32_t *slice_status = &arg3[0x2f6 / 4];
    int32_t cur;

    if (READ_U8(arg3, 0x174) == 0U) {
        cur = READ_U16(arg3, 0x848);
        {
            int32_t bit = 1 << (cur & 0x1f);

            if ((cur & 0x20) != 0) {
                arg3[0x211] |= bit;
            } else {
                arg3[0x210] |= bit;
            }
        }
        WRITE_U16(arg3, 0x848, (uint16_t)(cur + 1));
        if (cur >= READ_U16(arg1, 0x40)) {
            __assert("iCurSliceIndex >= 0 && iCurSliceIndex < pChParam->uNumSlices",
                     "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/ChannelMngr.c",
                     0xf04, "CommitSlice", &_gp);
        }
    } else {
        cur = READ_U8(arg3, arg4 + 0x84a) - 1;
        {
            int32_t bit = 1 << (cur & 0x1f);

            if ((cur & 0x20) != 0) {
                arg3[0x211] |= bit;
            } else {
                arg3[0x210] |= bit;
            }
        }
        WRITE_U16(arg3, 0x848, (uint16_t)(READ_U16(arg3, 0x848) + 1));
    }

    arg3[0xc6] = READ_S32(arg3, 0x338 + cur * 0x28);
    arg3[0x2ea] = arg3[0x12];
    arg3[0x2e5] = 0;
    arg3[0x2c4] = arg3[0x10];
    arg3[0x2cb] = arg3[0xd4 + cur * 0xa];
    arg3[0x2e6] = arg3[0xc];
    arg3[0x2e7] = arg3[0xd];
    WRITE_U8(arg3, 0xba0, READ_U8(arg3, 0x24) & 1U);
    WRITE_U16(arg3, 0xba4, READ_U16(arg3, 2));
    WRITE_U8(arg3, 0xbac, READ_U8(arg3, 0x3c));
    WRITE_U8(arg3, 0xba1, 0);
    WRITE_U8(arg3, 0xba2, 0);

    InitSliceStatus(slice_status);
    WRITE_U8(arg3, 0xba1, 1);
    OutputSlice(arg1, arg3, cur, slice_status);
    UpdateStatus(arg3, slice_status);
    SetTileInfoIfNeeded(arg1, &arg3[0x2be], arg1, READ_U8(arg3, 0x19d));
    WRITE_U8(arg3, 0xbad, READ_U8(arg3, 0x17e));
    WRITE_U16(arg3, 0xb28, READ_U16(arg3, 0x198));
    arg3[0x2c0] = arg3[6];
    arg3[0x2c1] = arg3[7];

    if (cur == READ_U16(arg1, 0x40) - 1) {
        StaticFifo_Dequeue(arg2);
        arg3[0x29c] += 1;
        WRITE_U8(arg3, 0xba2, 1);
        TerminateRequest(arg1, arg3, slice_status);
        ReleaseWorkBuffers(arg1, arg3);
    }

    {
        int32_t *evt = (int32_t *)(intptr_t)EndRequestsBuffer_Pop((uint8_t *)arg1 + 0x1604);

        evt[0] = arg3[0xc6];
        memcpy(evt + 2, &arg3[0x2be], (uint8_t *)slice_status - (uint8_t *)&arg3[0x2be]);
        StaticFifo_Queue((StaticFifoCompat *)((uint8_t *)arg1 + 0x12b24), (int32_t)(intptr_t)evt);
    }

    handleOutputTraces(arg1, arg3, (uint8_t)cur, 7);
    if (cur == READ_U16(arg1, 0x40) - 1) {
        WRITE_S32(arg1, 0x35b4, READ_S32(arg1, 0x35b4) + 1);
    }

    if (READ_U8(arg3, 0x174) == 0U) {
        int32_t next = cur + 1;
        int32_t bit = 1 << (next & 0x1f);

        if (((cur + 1) & 0x20) != 0) {
            if ((arg3[0x211] & bit) != 0) {
                return ((READ_U16(arg1, 0x40) - 1) == cur) ? 1 : 0;
            }
        } else if ((arg3[0x210] & bit) != 0) {
            return ((READ_U16(arg1, 0x40) - 1) == cur) ? 1 : 0;
        }
    }

    return ((READ_U16(arg1, 0x40) - 1) == cur) ? 1 : 0;
}

uint32_t adjustSubframeNumSlices(void *arg1)
{
    uint32_t num_core;
    uint32_t num_slices;

    if (READ_S32(arg1, 0xc4) == 0) {
        __assert("pChParam->bSubframeLatency",
                 "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/ChannelMngr.c",
                 0x8e9, "adjustSubframeNumSlices", &_gp);
    }

    if (READ_U8(arg1, 0x1f) != 0U) {
        num_slices = READ_U16(arg1, 0x40);
        if (num_slices > 0x20U) {
            num_slices = 0x20U;
        }
        WRITE_U16(arg1, 0x40, (uint16_t)num_slices);
        return num_slices;
    }

    num_core = READ_U8(arg1, 0x3c);
    if (num_core == 0U) {
        __builtin_trap();
    }
    num_slices = ((READ_U16(arg1, 0x40) + num_core - 1U) / num_core) * num_core;
    {
        uint32_t max_slices = (0x20U / num_core) * num_core;

        if (num_slices > max_slices) {
            num_slices = max_slices;
        }
    }
    WRITE_U16(arg1, 0x40, (uint16_t)num_slices);
    return num_slices;
}

int32_t AL_EncChannel_CheckAndAdjustParam(void *arg1)
{
    int32_t flags = READ_S32(arg1, 0x10);
    int32_t ch_flags = READ_S32(arg1, 0x30);
    uint32_t lcu = READ_U8(arg1, 0x4e);
    uint32_t codec = READ_U8(arg1, 0x1f);
    uint32_t slice_per_core;
    uint32_t num_core;
    int32_t width = READ_U16(arg1, 6);

    if ((flags & 0xd) != 8) {
        return 0x90;
    }
    if ((ch_flags & 1) != 0 && READ_U8(arg1, 0xc4) != 0) {
        return 0x90;
    }
    if (((flags >> 4) & 0xd) != 8) {
        return 0x90;
    }
    if (((uint32_t)flags >> 8 & 0xfU) >= 4U) {
        return 0x90;
    }
    if (lcu >= 7U) {
        return 0x90;
    }
    if (lcu < READ_U8(arg1, 0x4f) || READ_U8(arg1, 0x4f) < 3U) {
        return 0x90;
    }
    if (codec == 4U) {
        if (READ_U16(arg1, 4) >= 0x4001U) {
            return 0x90;
        }
        if (READ_U8(arg1, 0xc4) != 0) {
            return 0x90;
        }
    } else if (codec == 0U) {
        /* AVC-specific: stock reads num_core + *(arg1+0x3e). If num_core >= 2
         * AND *(arg1+0x3e) != 0, FAIL. Stock path via 0x695b8. */
        if ((int32_t)READ_U8(arg1, 0x3c) >= 2 && READ_U16(arg1, 0x3e) != 0U) {
            return 0x90;
        }
    }
    if (READ_U32(arg1, 0xc8) >= 4U && READ_U32(arg1, 0xc8) != 0x80U) {
        return 0x90;
    }

    num_core = READ_U8(arg1, 0x3c);
    if (num_core == 1U) {
        ch_flags &= ~2;
        WRITE_S32(arg1, 0x30, ch_flags);
    } else if (READ_S32(arg1, 0xc4) == 0) {
        WRITE_S32(arg1, 0x30, ch_flags | 2);
    }

    slice_per_core = READ_U16(arg1, 0x40);
    if ((READ_S32(arg1, 0x30) & 1) != 0) {
        int32_t need_lcus = (int32_t)(slice_per_core * num_core);
        int32_t lcu_px = 1 << (lcu & 0x1f);

        if (width < need_lcus * lcu_px) {
            int32_t total = lcu_px * (int32_t)num_core;
            uint32_t new_slices;

            if (total == 0) {
                __builtin_trap();
            }
            new_slices = (uint32_t)(width / total);
            WRITE_U16(arg1, 0x40, (uint16_t)new_slices);
            if (new_slices != slice_per_core) {
                adjustSubframeNumSlices(arg1);
                return 3;
            }
        }
    }

    return 0;
}

int32_t AL_EncChannel_Init(int32_t *arg1, int32_t *arg2, void *arg3, uint8_t arg4, uint8_t arg5, int32_t arg6,
                           void *arg7, void *arg8, int32_t *arg9, int32_t arg10, int32_t arg11, int32_t arg12,
                           uint8_t arg13)
{
    AL_TAllocator *dma_alloc = (AL_TAllocator *)arg8;
    void *cmdlist_handle = NULL;
    void *cmdlist_mutex = NULL;
    intptr_t cmdlist_vaddr;
    intptr_t cmdlist_paddr;
    int32_t cmdlist_alloc_size;
    uint32_t cmdlist_align;
    uint32_t cmdlist_size;
    int32_t result;
    uint32_t width = READ_U16(arg2, 4);
    uint32_t lcu = READ_U8(arg2, 0x4e);
    uint32_t height = READ_U16(arg2, 6);
    int32_t dpb_size;
    int32_t max_ref;
    int32_t rc_mode = -1;

    ENC_KMSG("AL_EncChannel_Init entry ctx=%p chParam=%p alloc=%p dma=%p codec=%u size=%ux%u gop=%d fps=%u/%u",
             arg1, arg2, arg7, arg8, (unsigned)READ_U8(arg2, 0x1f), width, height, arg2[0x2a],
             (unsigned)READ_U16(arg2, 0x3a), (unsigned)READ_U16(arg2, 0x38));
    SetChannelSteps(arg1, arg2);
    ENC_KMSG("AL_EncChannel_Init post-SetChannelSteps step_fn=%p step_state=%d",
             (void *)(intptr_t)READ_S32(arg1, 0xf0), READ_S32(arg1, 0x1ae0));
    arg1[0xd6e] = arg11;
    arg1[0xd6b] = (int32_t)(intptr_t)arg8;
    arg1[0xd70] = arg10;
    ENC_KMSG("AL_EncChannel_Init pre-SrcReorder ctx=%p reorder=%p", arg1, &arg1[0x5e]);
    AL_SrcReorder_Init(&arg1[0x5e]);
    ENC_KMSG("AL_EncChannel_Init post-SrcReorder reorder=%p", &arg1[0x5e]);
    ENC_KMSG("AL_EncChannel_Init pre-GopMngr_Init gop=%p alloc=%p gopMode=%d", &arg1[0x4a], arg7, arg2[0x2a]);
    result = AL_GopMngr_Init(&arg1[0x4a], arg7, arg2[0x2a], 0);
    ENC_KMSG("AL_EncChannel_Init post-GopMngr_Init result=%d gop=%p cb0=%p cb1=%p mutex=%p",
             result, &arg1[0x4a], (void *)(intptr_t)arg1[0x4a], (void *)(intptr_t)arg1[0x4b],
             READ_PTR(arg1, 0x170));
    if (result == 0) {
        AL_SrcReorder_Deinit(&arg1[0x5e]);
        AL_GopMngr_Deinit(&arg1[0x4a]);
        return 0;
    }

    ENC_KMSG("AL_EncChannel_Init pre-GopMngr-setup1 cb=%p", (void *)(intptr_t)arg1[0x4a]);
    ((void (*)(void *, uint32_t, int32_t, uint32_t, uint32_t))(intptr_t)arg1[0x4a])(
        &arg1[0x4a], READ_U8(arg2, 0x1f), arg2[3], (width + (1U << (lcu & 0x1fU)) - 1U) >> (lcu & 0x1fU),
        (height + (1U << (lcu & 0x1fU)) - 1U) >> (lcu & 0x1fU));
    ENC_KMSG("AL_EncChannel_Init post-GopMngr-setup1");
    ENC_KMSG("AL_EncChannel_Init pre-GopMngr-setup2 cb=%p", (void *)(intptr_t)arg1[0x4b]);
    ((void (*)(void *, void *, uint32_t))(intptr_t)arg1[0x4b])(&arg1[0x4a], &arg2[0x2a], READ_U8(arg2, 0x1f));
    ENC_KMSG("AL_EncChannel_Init post-GopMngr-setup2");

    ENC_KMSG("AL_EncChannel_Init pre-StreamMngr_Init stream=%p", &arg1[0xa94]);
    if (AL_StreamMngr_Init(&arg1[0xa94]) == 0) {
        AL_SrcReorder_Deinit(&arg1[0x5e]);
        AL_GopMngr_Deinit(&arg1[0x4a]);
        return 0;
    }
    ENC_KMSG("AL_EncChannel_Init post-StreamMngr_Init stream=%p streamCtx=%p", &arg1[0xa94],
             (void *)(intptr_t)arg1[0xa94]);

    ENC_KMSG("AL_EncChannel_Init pre-maxref-lock mutex=%p", READ_PTR(arg1, 0x170));
    Rtos_GetMutex(READ_PTR(arg1, 0x170));
    ENC_KMSG("AL_EncChannel_Init post-maxref-lock mutex=%p cb=%p", READ_PTR(arg1, 0x170),
             (void *)(intptr_t)arg1[0x4c]);
    max_ref = ((int32_t (*)(void *, uint32_t))(intptr_t)arg1[0x4c])(&arg1[0x4a], READ_U8(arg2, 0x1f));
    ENC_KMSG("AL_EncChannel_Init post-maxref-cb max_ref=%d", max_ref);
    Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
    ENC_KMSG("AL_EncChannel_Init post-maxref-unlock");
    if (READ_U8(arg2, 0x1f) == 4U) {
        max_ref = 0;
    }

    dpb_size = AL_DPBConstraint_GetMaxDPBSize(arg2);
    ENC_KMSG("AL_EncChannel_Init pre-RefMngr_Init dpb=%d max_ref=%d", dpb_size, max_ref);
    {
        int32_t ref_count = dpb_size + 1;

        if (READ_U8(arg2, 0x1f) == 1U) {
            ref_count = dpb_size;
        }
        if (AL_RefMngr_Init(&arg1[0x8b2], arg2, max_ref, ref_count) == 0) {
            AL_StreamMngr_Deinit((void *)(intptr_t)arg1[0xa94]);
            AL_SrcReorder_Deinit(&arg1[0x5e]);
            AL_GopMngr_Deinit(&arg1[0x4a]);
            return 0;
        }
        ENC_KMSG("AL_EncChannel_Init post-RefMngr_Init ref=%p ref_count=%d", &arg1[0x8b2], ref_count);
    }

    ENC_KMSG("AL_EncChannel_Init pre-GmvMngr_Init gmv=%p", &arg1[0xad4]);
    AL_GmvMngr_Init(&arg1[0xad4]);
    ENC_KMSG("AL_EncChannel_Init post-GmvMngr_Init gmv=%p", &arg1[0xad4]);
    arg2[0xa] = (((arg2[0xa] & 0xffffff0f) | (max_ref << 4)) & 0xfffff0ff) | (max_ref << 8);
    ENC_KMSG("AL_EncChannel_Init pre-param-copy dst=%p src=%p", arg1, arg2);
    memcpy(arg1, arg2, 0xf0);
    ENC_KMSG("AL_EncChannel_Init post-param-copy flags=0x%x", arg1[0xb]);
    if ((arg1[0xb] & 0x40) != 0) {
        AL_RefMngr_EnableRecOut(&arg1[0x8b2]);
        ENC_KMSG("AL_EncChannel_Init post-EnableRecOut");
    }
    ENC_KMSG("AL_EncChannel_Init pre-IntermMngr_Init interm=%p", &arg1[0xa95]);
    if (AL_IntermMngr_Init(&arg1[0xa95], arg2) == 0) {
        AL_RefMngr_Deinit(&arg1[0x8b2]);
        AL_StreamMngr_Deinit((void *)(intptr_t)arg1[0xa94]);
        AL_SrcReorder_Deinit(&arg1[0x5e]);
        AL_GopMngr_Deinit(&arg1[0x4a]);
        return 0;
    }
    ENC_KMSG("AL_EncChannel_Init post-IntermMngr_Init interm=%p mutex=%p", &arg1[0xa95],
             READ_PTR(&arg1[0xa95], 0xf8));

    if ((READ_U16(arg2, 0x76) != 0U) && (READ_U16(arg2, 0x74) != 0U) && (READ_U8(arg2, 0x78) != 0U)) {
        switch (arg2[0x1a]) {
            case 0: rc_mode = 2; break;
            case 1: rc_mode = 0; break;
            case 2: rc_mode = 1; break;
            case 3: rc_mode = 4; break;
            case 4: rc_mode = 8; break;
            case 8: rc_mode = 9; break;
            case 0x3f: rc_mode = 5; break;
            default: rc_mode = -1; break;
        }
        ENC_KMSG("AL_EncChannel_Init rc_gate w=%u h=%u codec_mode=%u rc_sel=%d rc74=0x%x rc76=0x%x rc78=0x%x",
                 width, height, (unsigned)READ_U8(arg2, 0x1f), rc_mode,
                 (unsigned)READ_U16(arg2, 0x74), (unsigned)READ_U16(arg2, 0x76),
                 (unsigned)READ_U8(arg2, 0x78));
        result = AL_RateCtrl_Init(&arg1[0x3d], arg7, rc_mode, 0);
        if (result == 0) {
            AL_IntermMngr_Deinit(&arg1[0xa95]);
            AL_RefMngr_Deinit(&arg1[0x8b2]);
            AL_StreamMngr_Deinit((void *)(intptr_t)arg1[0xa94]);
            AL_SrcReorder_Deinit(&arg1[0x5e]);
            AL_GopMngr_Deinit(&arg1[0x4a]);
            return 0;
        }
        ENC_KMSG("AL_EncChannel_Init rc_ready init=%p cfg=%p mutex=%p state=%p",
                 (void *)(intptr_t)arg1[0x3d], (void *)(intptr_t)arg1[0x3e], READ_PTR(arg1, 0x120),
                 READ_PTR(arg1, 0x11c));
        ENC_KMSG("AL_EncChannel_Init rc_init_call rc=%p width=%u height=%u",
                 &arg1[0x3d], width, height);
        ((void (*)(void *, uint32_t, uint32_t))(intptr_t)arg1[0x3d])(&arg1[0x3d], width, height);
        ENC_KMSG("AL_EncChannel_Init rc_init_done rc=%p", &arg1[0x3d]);
        Rtos_GetMutex(READ_PTR(arg1, 0x120));
        void *rc_attr = &arg2[0x1a];
        void *gop_attr = &arg2[0x2a];
        ENC_KMSG("AL_EncChannel_Init rc_cfg_call rc=%p rcAttr=%p gop=%p",
                 &arg1[0x3d], rc_attr, gop_attr);
        ((void (*)(void *, void *, void *))(intptr_t)arg1[0x3e])(&arg1[0x3d], rc_attr, gop_attr);
        ENC_KMSG("AL_EncChannel_Init rc_cfg_done rc=%p", &arg1[0x3d]);
        Rtos_ReleaseMutex(READ_PTR(arg1, 0x120));
    }

    WRITE_S32(arg1, 0x164, (int32_t)(intptr_t)arg3);
    ResetChannelParam(arg1);
    SetTileOffsets(arg1);
    WRITE_U8(arg1, 0x3c, arg5);
    WRITE_U8(arg1, 0x3d, arg4);
    WRITE_S32(arg1, 0x2c, arg6);
    WRITE_U8(arg1, 0x3f, READ_U8(arg2, 0xf));
    WRITE_S32(arg1, 0x1a80, arg12);
    WRITE_S32(arg1, 0x1a84, arg6);
    WRITE_U16(arg1, 0x1a88, (uint16_t)READ_U8(arg2, 0xf));
    WRITE_U8(arg1, 0x44, READ_U8(arg2, 0));
    WRITE_U8(arg1, CH_OEM_CHANNEL_ID_OFF, arg5);
    WRITE_U8(arg1, CH_OEM_CORE_BASE_OFF, arg4);
    WRITE_S32(arg1, CH_OUT_CB_OFF, arg9[0]);
    WRITE_S32(arg1, CH_OUT_CTX_OFF, arg9[1]);
    WRITE_S32(arg1, CH_DESTROY_CB_OFF, 0);
    WRITE_S32(arg1, CH_DESTROY_CTX_OFF, 0);
    WRITE_S32(arg1, CH_FRAME_DONE_CB_OFF, arg9[2]);
    WRITE_S32(arg1, CH_FRAME_DONE_CTX_OFF, arg9[3]);
    WRITE_S32(arg1, CH_CMD_TRACE_FLAGS_OFF, 0);
    WRITE_U8(arg1, CH_STARTED_OFF, 0);
    WRITE_U8(arg1, CH_DRAINED_OFF, 0);
    WRITE_U16(arg1, CH_GATE_OFF, 1);
    WRITE_U8(arg1, CH_FREEZE_OFF, 0);
    WRITE_U8(arg1, 0x58, 0);
    WRITE_U8(arg1, 0x59, 0);
    InitMERange((int32_t)(intptr_t)arg1, arg2);
    ENC_KMSG("AL_EncChannel_Init cb-tail out=%p out_ctx=%p done=%p done_ctx=%p low48=0x%x low4c=0x%x ch_id=%u",
             (void *)(intptr_t)READ_S32(arg1, CH_OUT_CB_OFF),
             (void *)(intptr_t)READ_S32(arg1, CH_OUT_CTX_OFF),
             (void *)(intptr_t)READ_S32(arg1, CH_FRAME_DONE_CB_OFF),
             (void *)(intptr_t)READ_S32(arg1, CH_FRAME_DONE_CTX_OFF),
             READ_S32(arg1, 0x48), READ_S32(arg1, 0x4c),
             (unsigned)READ_U8(arg1, CH_OEM_CHANNEL_ID_OFF));
    WRITE_PTR(arg1, 0x35c4, NULL);
    WRITE_PTR(arg1, 0x35c8, NULL);
    WRITE_PTR(arg1, 0x35cc, NULL);
    WRITE_PTR(arg1, 0x35d0, NULL);
    WRITE_PTR(arg1, 0x35d4, NULL);
    WRITE_PTR(arg1, 0x35d8, NULL);
    WRITE_S32(arg1, 0x35dc, 0);
    WRITE_U8(arg1, 0x35e0, 0);
    if (NeedsHwRcScratch(arg2) != 0) {
        int32_t ep3_size = AL_GetAllocSizeEP3();
        int i;

        for (i = 0; i < 3; ++i) {
            void *ep3_handle;
            intptr_t ep3_vaddr;
            int32_t ep3_alloc_size;
            uint32_t ep3_align;

            ep3_handle = (void *)(intptr_t)AlignedAlloc(dma_alloc, "ep3", ep3_size, 0x20, &ep3_alloc_size, &ep3_align);
            if (ep3_handle == NULL) {
                ENC_KMSG("AL_EncChannel_Init hwrc ep3 alloc failed idx=%d size=%d", i, ep3_size);
                FreeHwRcScratchBuffers(arg1);
                AL_RateCtrl_Deinit((uint8_t *)arg1 + 0xf4);
                AL_IntermMngr_Deinit(&arg1[0xa95]);
                AL_RefMngr_Deinit(&arg1[0x8b2]);
                AL_StreamMngr_Deinit((void *)(intptr_t)arg1[0xa94]);
                AL_SrcReorder_Deinit(&arg1[0x5e]);
                AL_GopMngr_Deinit(&arg1[0x4a]);
                return 0;
            }

            WRITE_PTR(arg1, 0x35c4 + i * 4, ep3_handle);
            ep3_vaddr = dma_alloc->vtable->GetVirtualAddr(dma_alloc, ep3_handle);
            if (ep3_vaddr == 0) {
                ENC_KMSG("AL_EncChannel_Init hwrc ep3 map failed idx=%d handle=%p", i, ep3_handle);
                FreeHwRcScratchBuffers(arg1);
                AL_RateCtrl_Deinit((uint8_t *)arg1 + 0xf4);
                AL_IntermMngr_Deinit(&arg1[0xa95]);
                AL_RefMngr_Deinit(&arg1[0x8b2]);
                AL_StreamMngr_Deinit((void *)(intptr_t)arg1[0xa94]);
                AL_SrcReorder_Deinit(&arg1[0x5e]);
                AL_GopMngr_Deinit(&arg1[0x4a]);
                return 0;
            }

            Rtos_Memset((void *)ep3_vaddr, 0, (size_t)ep3_alloc_size);
            WRITE_PTR(arg1, 0x35d0 + i * 4, (void *)(ep3_vaddr + ep3_align));
        }

        WRITE_S32(arg1, 0x35dc, 0);
        WRITE_U8(arg1, 0x35e0, 1);
        ENC_KMSG("AL_EncChannel_Init hwrc ep3 ready size=%d ch=%p buf0=%p buf1=%p buf2=%p",
                 ep3_size, arg1, READ_PTR(arg1, 0x35d0), READ_PTR(arg1, 0x35d4), READ_PTR(arg1, 0x35d8));
        ENC_KMSG("AL_EncChannel_Init pre-InitHwRC_Content ch=%p", arg1);
        InitHwRC_Content(arg1, arg2);
        ENC_KMSG("AL_EncChannel_Init post-InitHwRC_Content ch=%p", arg1);
    }
    ENC_KMSG("AL_EncChannel_Init pre-cmdlist-mutex ch=%p", arg1);
    cmdlist_mutex = Rtos_CreateMutex();
    ENC_KMSG("AL_EncChannel_Init post-cmdlist-mutex ch=%p mutex=%p", arg1, cmdlist_mutex);
    if (cmdlist_mutex == NULL) {
        ENC_KMSG("AL_EncChannel_Init cmdlist mutex alloc failed ch=%p", arg1);
        FreeHwRcScratchBuffers(arg1);
        AL_RateCtrl_Deinit((uint8_t *)arg1 + 0xf4);
        AL_IntermMngr_Deinit(&arg1[0xa95]);
        AL_RefMngr_Deinit(&arg1[0x8b2]);
        AL_StreamMngr_Deinit((void *)(intptr_t)arg1[0xa94]);
        AL_SrcReorder_Deinit(&arg1[0x5e]);
        AL_GopMngr_Deinit(&arg1[0x4a]);
        return 0;
    }
    WRITE_PTR(arg1, 0x170, cmdlist_mutex);

    cmdlist_size = (uint32_t)READ_U16(arg2, 0x40) * (uint32_t)READ_U8(arg2, 0x3c) * 0x2600U;
    ENC_KMSG("AL_EncChannel_Init cmdlist size=%u cores=%u blocks=%u",
             (unsigned)cmdlist_size, (unsigned)READ_U8(arg2, 0x3c), (unsigned)READ_U16(arg2, 0x40));
    if (cmdlist_size != 0U) {
        cmdlist_handle = (void *)(intptr_t)AlignedAlloc(dma_alloc, "cmdlist", (int32_t)cmdlist_size, 0x20,
                                                        &cmdlist_alloc_size, &cmdlist_align);
        if (cmdlist_handle == NULL) {
            ENC_KMSG("AL_EncChannel_Init cmdlist alloc failed size=%u cores=%u blocks=%u",
                     (unsigned)cmdlist_size, (unsigned)READ_U8(arg2, 0x3c), (unsigned)READ_U16(arg2, 0x40));
            Rtos_DeleteMutex(cmdlist_mutex);
            WRITE_PTR(arg1, 0x170, NULL);
            FreeHwRcScratchBuffers(arg1);
            AL_RateCtrl_Deinit((uint8_t *)arg1 + 0xf4);
            AL_IntermMngr_Deinit(&arg1[0xa95]);
            AL_RefMngr_Deinit(&arg1[0x8b2]);
            AL_StreamMngr_Deinit((void *)(intptr_t)arg1[0xa94]);
            AL_SrcReorder_Deinit(&arg1[0x5e]);
            AL_GopMngr_Deinit(&arg1[0x4a]);
            return 0;
        }

        cmdlist_vaddr = dma_alloc->vtable->GetVirtualAddr(dma_alloc, cmdlist_handle);
        cmdlist_paddr = dma_alloc->vtable->GetPhysicalAddr(dma_alloc, cmdlist_handle);
        if (cmdlist_vaddr == 0 || cmdlist_paddr == 0) {
            ENC_KMSG("AL_EncChannel_Init cmdlist map failed handle=%p v=0x%lx p=0x%lx align=%d",
                     cmdlist_handle, (unsigned long)cmdlist_vaddr, (unsigned long)cmdlist_paddr,
                     (unsigned)cmdlist_align);
            dma_alloc->vtable->Free(dma_alloc, cmdlist_handle);
            Rtos_DeleteMutex(cmdlist_mutex);
            WRITE_PTR(arg1, 0x170, NULL);
            FreeHwRcScratchBuffers(arg1);
            AL_RateCtrl_Deinit((uint8_t *)arg1 + 0xf4);
            AL_IntermMngr_Deinit(&arg1[0xa95]);
            AL_RefMngr_Deinit(&arg1[0x8b2]);
            AL_StreamMngr_Deinit((void *)(intptr_t)arg1[0xa94]);
            AL_SrcReorder_Deinit(&arg1[0x5e]);
            AL_GopMngr_Deinit(&arg1[0x4a]);
            return 0;
        }

        Rtos_Memset((void *)(cmdlist_vaddr + cmdlist_align), 0, (size_t)cmdlist_alloc_size);
        SetCommandListBuffer((void *)(intptr_t)(arg1 + 0xb08), (uint32_t)READ_U8(arg2, 0x3c),
                             (uint32_t)READ_U16(arg2, 0x40), (int32_t)(cmdlist_vaddr + cmdlist_align),
                             (int32_t)(cmdlist_paddr + cmdlist_align));
        WRITE_PTR(arg1, 0x35a8, cmdlist_handle);
        ENC_KMSG("AL_EncChannel_Init cmdlist ready handle=%p v=0x%lx p=0x%lx align=%u count=%d next=%d cores=%u blocks=%u size=%u",
                 cmdlist_handle, (unsigned long)(cmdlist_vaddr + cmdlist_align),
                 (unsigned long)(cmdlist_paddr + cmdlist_align), (unsigned)cmdlist_align,
                 READ_S32((uint8_t *)arg1 + 0x2c20, 0x980), READ_S32((uint8_t *)arg1 + 0x2c20, 0x984),
                 (unsigned)READ_U8(arg2, 0x3c), (unsigned)READ_U16(arg2, 0x40), (unsigned)cmdlist_size);
    }
    ENC_KMSG("EncChannel_Init final chctx=%p 0x2c=%d 0x3c=%u 0x3d=%u 0x40=%u legacy78=%u legacy79=%u legacy7c=%u",
             arg1, READ_S32(arg1, 0x2c), (unsigned)READ_U8(arg1, 0x3c), (unsigned)READ_U8(arg1, 0x3d),
             (unsigned)READ_U16(arg1, 0x40), (unsigned)READ_U8(arg1, 0x78), (unsigned)READ_U8(arg1, 0x79),
             (unsigned)READ_U16(arg1, 0x7c));
    (void)arg12;
    return result;
}

int32_t AL_EncChannel_DeInit(void *arg1)
{
    int32_t *alloc = (int32_t *)READ_PTR(arg1, 0x35ac);
    uint32_t core_count = READ_U8(arg1, 0x3c);
    int32_t i;
    int32_t cb = READ_S32(arg1, 0x58);

    ENC_KMSG("EncChannel_DeInit chctx=%p alloc=%p core_count=%u rc_fn=%p rc_alloc=%p rc_state=%p rc_mutex=%p stream=%p destroy_flag=0x%x",
             arg1, alloc, (unsigned)core_count,
             READ_PTR((uint8_t *)arg1 + 0xf4, 0x20),
             READ_PTR((uint8_t *)arg1 + 0xf4, 0x24),
             READ_PTR((uint8_t *)arg1 + 0xf4, 0x28),
             READ_PTR((uint8_t *)arg1 + 0xf4, 0x2c),
             READ_PTR(arg1, 0x2a50), READ_U16(arg1, 0x1ae6));

    AL_RateCtrl_Deinit((uint8_t *)arg1 + 0xf4);
    AL_GopMngr_Deinit((uint8_t *)arg1 + 0x128);
    AL_IntermMngr_Deinit((uint8_t *)arg1 + 0x2a54);
    AL_RefMngr_Deinit((uint8_t *)arg1 + 0x22c8);
    AL_StreamMngr_Deinit(READ_PTR(arg1, 0x2a50));
    AL_SrcReorder_Deinit((uint8_t *)arg1 + 0x178);
    WRITE_S32(arg1, 0x55, 0);
    WRITE_U16(arg1, 0x50, 0);
    WRITE_U8(arg1, 0x3c, 0xff);
    WRITE_S32(arg1, 0x48, 0);
    WRITE_S32(arg1, 0x4c, 0);
    ResetChannelParam(arg1);
    (*((void (**)(void *, int32_t))(*alloc + 8)))(alloc, READ_S32(arg1, 0x35a8));
    (*((void (**)(void *, int32_t))(*alloc + 8)))(alloc, READ_S32(arg1, 0x3d70));
    for (i = 0; i < 3; ++i) {
        (*((void (**)(void *, int32_t))(*alloc + 8)))(alloc, READ_S32(arg1, 0x35c4 + i * 4));
    }
    Rtos_DeleteMutex(READ_PTR(arg1, 0x170));
    if (READ_S32(arg1, 0x3d64) != 0) {
        (*((void (**)(void *, int32_t))(*alloc + 8)))(alloc, READ_S32(arg1, 0x3d64));
        WRITE_S32(arg1, 0x3d64, 0);
        WRITE_S32(arg1, 0x3d68, 0);
        WRITE_S32(arg1, 0x3d6c, 0);
    }
    if (cb != 0) {
        return ((int32_t (*)(int32_t, uint32_t))(intptr_t)cb)(READ_S32(arg1, 0x54), core_count);
    }
    return 0;
}

int32_t AL_EncChannel_ListModulesNeeded(void *arg1, void *arg2)
{
    int32_t lane;
    int32_t order_idx;
    int32_t lane_order[2] = { 0, 1 };
    StaticFifoCompat *fifo_base;

    ENC_KMSG("ListModulesNeeded entry chctx=%p freeze=%u out_count=%d",
             arg1, (unsigned)READ_U8(arg1, CH_FREEZE_OFF), READ_S32(arg2, 0x80));
    if (READ_U8(arg1, CH_FREEZE_OFF) != 0U) {
        ENC_KMSG("ListModulesNeeded early-freeze chctx=%p", arg1);
        return (int32_t)(intptr_t)arg1;
    }

    Rtos_GetMutex(READ_PTR(arg1, 0x170));
    fifo_base = (StaticFifoCompat *)((uint8_t *)arg1 + 0x129b4);
    if (StaticFifo_Front((StaticFifoCompat *)((uint8_t *)arg1 + 0x12a10)) != 0) {
        lane_order[0] = 1;
        lane_order[1] = 0;
        ENC_KMSG("ListModulesNeeded priority phase1 front=%p",
                 (void *)(intptr_t)StaticFifo_Front((StaticFifoCompat *)((uint8_t *)arg1 + 0x12a10)));
    }

    for (order_idx = 0; order_idx != 2; ++order_idx) {
        lane = lane_order[order_idx];
        StaticFifoCompat *fifo = (StaticFifoCompat *)((uint8_t *)fifo_base + lane * 0x5c);
        int32_t *req = (int32_t *)(intptr_t)StaticFifo_Front(fifo);

        ENC_KMSG("ListModulesNeeded lane=%d fifo=%p front=%p read=%d write=%d cap=%d",
                 lane, fifo, req, fifo->read_idx, fifo->write_idx, fifo->capacity);

        if (req != 0) {
            ENC_KMSG("ListModulesNeeded lane=%d req=%p prep=%u armed=%u pict=%d type=%d lane_mods=%d lane_slices=%d",
                     lane, req, (unsigned)READ_U8(req, 0xa74), (unsigned)READ_U8(req, 0xa75),
                     READ_S32(req, 0x20), READ_S32(req, 0x30),
                     READ_S32(req, lane * 0x110 + 0x8cc), READ_S32(req, lane * 0x110 + 0x958));
            if (READ_U8(req, 0xa74) != 0U) {
                int32_t *grp = (int32_t *)((uint8_t *)req + lane * 0x110 + 0x84c);
                int32_t i;

                for (i = 0; i < READ_S32(req, lane * 0x110 + 0x8cc); ++i) {
                    ENC_KMSG("ListModulesNeeded lane=%d add core=%d mod=%d idx=%d",
                             lane, READ_U8(arg1, 0x3d) + grp[0], grp[1], i);
                    AL_ModuleArray_AddModule(arg2, READ_U8(arg1, 0x3d) + grp[0], grp[1]);
                    grp += 2;
                }
                ENC_KMSG("ListModulesNeeded lane=%d prepared-count=%d", lane, READ_S32(arg2, 0x80));
            } else {
                int32_t pict_id = READ_S32(req, 0x20);

                if (READ_U8(req, 0xa75) == 0U) {
                    ENC_KMSG("ListModulesNeeded lane=%d prepare req=%p pict=%d", lane, req, pict_id);
                    if (AL_SrcReorder_IsEosNext((uint8_t *)arg1 + 0x178) != 0 &&
                        AL_SrcReorder_GetWaitingSrcBufferCount((uint8_t *)arg1 + 0x178) == 0) {
                        Rtos_GetMutex(READ_PTR(arg1, 0x170));
                        ((void (*)(void *, void *))(intptr_t)READ_S32(arg1, 0x134))((uint8_t *)arg1 + 0x128,
                                                                                     (uint8_t *)req + 0x20);
                        Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
                    }
                    if (READ_S32(arg1, 0x22b8) != READ_S32(arg1, 0x22bc)) {
                        while (AL_SrcReorder_IsAvailable((uint8_t *)arg1 + 0x178, pict_id) == 0) {
                            int32_t wait = AL_SrcReorder_GetWaitingSrcBufferCount((uint8_t *)arg1 + 0x178);
                            int32_t eos = AL_SrcReorder_IsEosNext((uint8_t *)arg1 + 0x178);
                            void *cmd = (void *)(intptr_t)AL_SrcReorder_GetCommandAndMoveNext((uint8_t *)arg1 + 0x178);

                            if (cmd != 0) {
                                AL_ApplyNewGOPAndRCParams(arg1, cmd);
                                AL_ApplyGopCommands((uint8_t *)arg1 + 0x128, cmd, wait);
                                AL_ApplyGmvCommands((uint8_t *)arg1 + 0x2b50, cmd);
                                if (eos != 0) {
                                    Rtos_GetMutex(READ_PTR(arg1, 0x170));
                                    ((void (*)(void *, int32_t))(intptr_t)READ_S32(arg1, 0x150))
                                        ((uint8_t *)arg1 + 0x128, wait);
                                    Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
                                }
                            }
                            Rtos_GetMutex(READ_PTR(arg1, 0x170));
                            ((void (*)(void *, void *))(intptr_t)READ_S32(arg1, 0x134))((uint8_t *)arg1 + 0x128,
                                                                                         (uint8_t *)req + 0x20);
                            Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
                        }
                    }
                    if (READ_S32(req, 0x24) >= 0 && READ_S32(req, 0x30) != 8) {
                        void *ready = AL_SrcReorder_GetReadyCommand((uint8_t *)arg1 + 0x178, pict_id);

                        ENC_KMSG("ListModulesNeeded lane=%d ready=%p pict=%d", lane, ready, pict_id);
                        if (ready != 0 && (READ_S32(ready, 0) & 0x200) != 0) {
                            int32_t check_rc;

                            ENC_KMSG("ListModulesNeeded lane=%d ready-dynres req=%p w=%d h=%d core_hint=%u flags=0x%x",
                                     lane, req, READ_S32(ready, 0x68), READ_S32(ready, 0x6c),
                                     (unsigned)READ_U8(ready, 0x70), READ_S32(ready, 0));
                            WRITE_U16(arg1, 4, (uint16_t)READ_S32(ready, 0x68));
                            WRITE_U16(arg1, 6, (uint16_t)READ_S32(ready, 0x6c));
                            WRITE_U8(arg1, 0x44, READ_U8(ready, 0x70));
                            ENC_KMSG("ListModulesNeeded lane=%d before-set-cores chctx=%p size=%ux%u core_hint=%u",
                                     lane, arg1, (unsigned)READ_U16(arg1, 4), (unsigned)READ_U16(arg1, 6),
                                     (unsigned)READ_U8(arg1, 0x44));
                            AL_EncChannel_SetNumberOfCores(arg1);
                            ENC_KMSG("ListModulesNeeded lane=%d after-set-cores cores=%u core_base=%u active=%d need=%d",
                                     lane, (unsigned)READ_U8(arg1, 0x3c), (unsigned)READ_U8(arg1, 0x3d),
                                     READ_S32(arg1, 0x2c), READ_S32(arg1, 0x30));
                            SetChannelSteps(arg1, arg1);
                            ENC_KMSG("ListModulesNeeded lane=%d after-set-steps step4e=%u step4f=%u fmt=%u",
                                     lane, (unsigned)READ_U8(arg1, 0x4e), (unsigned)READ_U8(arg1, 0x4f),
                                     (unsigned)READ_U8(arg1, 0x1f));
                            check_rc = AL_EncChannel_CheckAndAdjustParam(arg1);
                            ENC_KMSG("ListModulesNeeded lane=%d check-adjust rc=0x%x", lane, check_rc);
                            if ((uint32_t)check_rc >= 0x80U) {
                                __assert("!AL_IS_ERROR_CODE(eErrorCode)",
                                         "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/ChannelMngr.c",
                                         0xb62, "SetInputResolution", &_gp);
                            }
                            SetTileOffsets(arg1);
                            ENC_KMSG("ListModulesNeeded lane=%d after-tile-offsets", lane);
                            InitMERange((int32_t)(intptr_t)arg1, arg1);
                            ENC_KMSG("ListModulesNeeded lane=%d after-init-me-range", lane);
                            AL_RefMngr_SetRecResolution((uint8_t *)arg1 + 0x22c8, READ_U16(arg1, 4), READ_U16(arg1, 6));
                            ENC_KMSG("ListModulesNeeded lane=%d after-set-rec-resolution", lane);
                            ((void (*)(void *, uint32_t, uint32_t))(intptr_t)READ_S32(arg1, 0xf4))
                                ((uint8_t *)arg1 + 0xf4, READ_U16(arg1, 4), READ_U16(arg1, 6));
                            ENC_KMSG("ListModulesNeeded lane=%d after-hw-fn", lane);
                            InitHwRC_Content(arg1, arg1);
                            ENC_KMSG("ListModulesNeeded lane=%d after-init-hwrc", lane);
                        }
                    }
                    WRITE_U8(arg1, CH_FREEZE_OFF, 0);
                    ENC_KMSG("ListModulesNeeded lane=%d before-mark-used pict=%d", lane, pict_id);
                    AL_SrcReorder_MarkSrcBufferAsUsed((uint8_t *)arg1 + 0x178, pict_id);
                    ENC_KMSG("ListModulesNeeded lane=%d after-mark-used pict=%d", lane, pict_id);
                    if (READ_U8(arg1, 0x1f) != 4U) {
                        int32_t rec = AL_RefMngr_GetNewFrmBuffer((uint8_t *)arg1 + 0x22c8);

                        if (rec == 0xff) {
                            ENC_KMSG("ListModulesNeeded lane=%d no-rec-buffer pict=%d", lane, pict_id);
                            req = 0;
                            continue;
                        }

                        WRITE_S32(req, 0x838,
                                  AL_IntermMngr_GetBuffer((uint8_t *)arg1 + 0x2a54));
                        if (READ_PTR(req, 0x838) == NULL) {
                            ENC_KMSG("ListModulesNeeded lane=%d no-interm-buffer pict=%d rec=%d",
                                     lane, pict_id, rec);
                            AL_RefMngr_ReleaseFrmBuffer((uint8_t *)arg1 + 0x22c8, (char)rec);
                            req = 0;
                            continue;
                        }

                        WRITE_S32(req, 0x318, (int32_t)(intptr_t)((uint8_t *)req + 0x338));
                        {
                            int32_t stream_ok = GetStreamBuffers_part_72(READ_PTR(arg1, 0x2a50), req, arg1);

                            ENC_KMSG("ListModulesNeeded lane=%d post-get-stream-buffers pict=%d req=%p ok=%d stream=%p",
                                     lane, pict_id, req, stream_ok, READ_PTR(req, 0x318));
                            if (stream_ok == 0) {
                            ENC_KMSG("ListModulesNeeded lane=%d no-stream-buffers pict=%d rec=%d interm=%p",
                                     lane, pict_id, rec, READ_PTR(req, 0x838));
                            AL_IntermMngr_ReleaseBufferBack((uint8_t *)arg1 + 0x2a54, READ_PTR(req, 0x838));
                            WRITE_S32(req, 0x838, 0);
                            AL_RefMngr_ReleaseFrmBuffer((uint8_t *)arg1 + 0x22c8, (char)rec);
                            req = 0;
                            continue;
                            }
                        }

                        ENC_KMSG("ListModulesNeeded lane=%d before-set-source-buffer pict=%d req=%p srcslot=%p",
                                 lane, pict_id, req, &req[0xa6]);
                        SetSourceBuffer_isra_74(arg1, req, pict_id, &req[0xa6]);
                        ENC_KMSG("ListModulesNeeded lane=%d after-set-source-buffer pict=%d req=%p srcY=0x%x srcUV=0x%x",
                                 lane, pict_id, req, READ_S32(req, 0x298), READ_S32(req, 0x29c));
                        {
                            ENC_KMSG("ListModulesNeeded lane=%d before-get-src-buffer pict=%d reorder=%p",
                                     lane, pict_id, (uint8_t *)arg1 + 0x178);
                            int32_t *src = (int32_t *)(intptr_t)AL_SrcReorder_GetSrcBuffer((uint8_t *)arg1 + 0x178,
                                                                                            pict_id);

                            ENC_KMSG("ListModulesNeeded lane=%d after-get-src-buffer pict=%d src=%p",
                                     lane, pict_id, src);
                            if (src != NULL) {
                                WRITE_S32(req, 0x2b4, src[7]);
                                WRITE_S32(req, 0x2b8, src[8]);
                                WRITE_S32(req, 0x2bc, 0);
                                ENC_KMSG("ListModulesNeeded lane=%d copied-src-stream pict=%d ep2phys=0x%x ep2virt=0x%x",
                                         lane, pict_id, READ_S32(req, 0x2b4), READ_S32(req, 0x2b8));
                            }
                        }
                        ENC_KMSG("ListModulesNeeded lane=%d before-get-ep1 pict=%d interm=%p",
                                 lane, pict_id, READ_PTR(req, 0x838));
                        WRITE_S32(req, 0x2fc,
                                  AL_IntermMngr_GetEp1Addr((uint8_t *)arg1 + 0x2a54,
                                                           READ_PTR(req, 0x838), &req[0xca]));
                        ENC_KMSG("ListModulesNeeded lane=%d after-get-ep1 pict=%d ep1phys=0x%x ep1virt=0x%x",
                                 lane, pict_id, READ_S32(req, 0x2fc), READ_S32(req, 0x328));
                        if ((READ_S32(req, 0x2b8) | READ_S32(req, 0x2bc)) == 0 ||
                            READ_S32(req, 0x2b4) == 0) {
                            int32_t ep2_virt = 0;

                            ENC_KMSG("ListModulesNeeded lane=%d before-fallback-ep2 pict=%d interm=%p",
                                     lane, pict_id, READ_PTR(req, 0x838));
                            WRITE_S32(req, 0x2b4,
                                      AL_IntermMngr_GetEp2Addr((uint8_t *)arg1 + 0x2a54,
                                                               READ_PTR(req, 0x838), &ep2_virt));
                            WRITE_S32(req, 0x2b8, ep2_virt);
                            WRITE_S32(req, 0x2bc, 0);
                            ENC_KMSG("ListModulesNeeded lane=%d after-fallback-ep2 pict=%d ep2phys=0x%x ep2virt=0x%x",
                                     lane, pict_id, READ_S32(req, 0x2b4), READ_S32(req, 0x2b8));
                        }
                        WRITE_S32(req, 0x300, 0);
                        WRITE_S32(req, 0x324, 0);
                        WRITE_S32(req, 0x304, 0);
                        WRITE_S32(req, 0x308, 0);
                        if ((READ_U32(arg1, 0x1c) >> 0x18) == 0U && READ_U8(arg1, 0x3c) >= 2U) {
                            ENC_KMSG("ListModulesNeeded lane=%d before-get-map-data pict=%d interm=%p",
                                     lane, pict_id, READ_PTR(req, 0x838));
                            WRITE_S32(req, 0x304,
                                      AL_IntermMngr_GetMapAddr((uint8_t *)arg1 + 0x2a54,
                                                               READ_PTR(req, 0x838), 0));
                            WRITE_S32(req, 0x308,
                                      AL_IntermMngr_GetDataAddr((uint8_t *)arg1 + 0x2a54,
                                                                READ_PTR(req, 0x838), 0));
                            WRITE_S32(req, 0x2f8,
                                      AL_IntermMngr_GetWppAddr((uint8_t *)arg1 + 0x2a54,
                                                               READ_PTR(req, 0x838), &req[0xc8]));
                            ENC_KMSG("ListModulesNeeded lane=%d after-get-map-data pict=%d map=0x%x data=0x%x wpp=0x%x wppvirt=0x%x",
                                     lane, pict_id, READ_S32(req, 0x304), READ_S32(req, 0x308),
                                     READ_S32(req, 0x2f8), READ_S32(req, 0x320));
                        }
                        ENC_KMSG("ListModulesNeeded lane=%d before-set-picture-ref-bufs pict=%d req=%p rec=%d",
                                 lane, pict_id, req, rec);
                        SetPictureRefBuffers(arg1, req, arg1, req, (char)rec, &req[0xa6]);
                        ENC_KMSG("ListModulesNeeded lane=%d after-set-picture-ref-bufs pict=%d req=%p",
                                 lane, pict_id, req);
                        ENC_KMSG("ListModulesNeeded lane=%d prepared-bufs pict=%d rec=%d interm=%p stream=%p srcY=0x%x srcUV=0x%x ep1=0x%x ep2=0x%x stream_off=%d stream_part=%d",
                                 lane, pict_id, rec, READ_PTR(req, 0x838), READ_PTR(req, 0x318),
                                 READ_S32(req, 0x298), READ_S32(req, 0x29c),
                                 READ_S32(req, 0x2fc), READ_S32(req, 0x2b4),
                                 READ_PTR(req, 0x318) ? READ_S32(READ_PTR(req, 0x318), 0x0c) : 0,
                                 READ_PTR(req, 0x318) ? READ_S32(READ_PTR(req, 0x318), 0x14) : 0);
                    }
                    if (READ_S32(req, 0x30) == 2 && READ_U8(arg1, 0x2d4) != 0U) {
                        Rtos_GetMutex(READ_PTR(arg1, 0x170));
                        ((void (*)(void *))(intptr_t)READ_S32(arg1, 0x158))((uint8_t *)arg1 + 0x128);
                        Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
                    }
                    {
                        int32_t *pict_src = (int32_t *)(intptr_t)AL_SrcReorder_GetSrcBuffer((uint8_t *)arg1 + 0x178, pict_id);
                        int32_t *pict_cmd = pict_src ? pict_src + 0x12 : NULL;

                        ENC_KMSG("ListModulesNeeded lane=%d before-apply-pict pict=%d src=%p cmd=%p",
                                 lane, pict_id, pict_src, pict_cmd);
                        AL_ApplyPictCommands(arg1, pict_cmd, (void *)(intptr_t)pict_id);
                    }
                    ENC_KMSG("ListModulesNeeded lane=%d after-apply-pict pict=%d", lane, pict_id);
                    if (READ_U8(arg1, 0x1f) == 0U && READ_U8(arg1, 0x3c) > 1U &&
                        READ_PTR(arg1, 0x138) != (void *)(intptr_t)rc_Iol) {
                        ENC_KMSG("ListModulesNeeded lane=%d repair-update-fn old=%p new=%p",
                                 lane, READ_PTR(arg1, 0x138), (void *)(intptr_t)rc_Iol);
                        WRITE_S32(arg1, 0x138, (int32_t)(intptr_t)rc_Iol);
                    }
                    ENC_KMSG("ListModulesNeeded lane=%d before-update-fn req=%p fn=%p req20=%p w0=0x%x w1=0x%x w2=0x%x w3=0x%x",
                             lane, req, READ_PTR(arg1, 0x138), (uint8_t *)req + 0x20,
                             READ_S32(req, 0x20), READ_S32(req, 0x24), READ_S32(req, 0x28), READ_S32(req, 0x2c));
                    Rtos_GetMutex(READ_PTR(arg1, 0x170));
                    ((void (*)(void *, void *))(intptr_t)READ_S32(arg1, 0x138))((uint8_t *)arg1 + 0x128,
                                                                                 (uint8_t *)req + 0x20);
                    Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
                    ENC_KMSG("ListModulesNeeded lane=%d after-update-fn pict=%d w8=%08x w9=%08x wa=%08x wb=%08x wc=%08x wd=%08x w12=%08x",
                             lane, pict_id,
                             READ_S32(req, 0x20), READ_S32(req, 0x24), READ_S32(req, 0x28),
                             READ_S32(req, 0x2c), READ_S32(req, 0x30), READ_S32(req, 0x34),
                             READ_S32(req, 0x48));
                    if (READ_U8(arg1, 0x1f) != 4U) {
                        ENC_KMSG("ListModulesNeeded lane=%d before-set-picture-refs req=%p", lane, req);
                        SetPictureReferences(arg1, req);
                        ENC_KMSG("ListModulesNeeded lane=%d after-set-picture-refs req=%p", lane, req);
                    }
                    WRITE_U8(req, 0xa74, 1);
                    WRITE_U8(req, 0xa75, 1);
                    {
                        int32_t *grp = (int32_t *)((uint8_t *)req + lane * 0x110 + 0x84c);
                        int32_t i;
                        int32_t mod_count = READ_S32(req, lane * 0x110 + 0x8cc);

                        for (i = 0; i < mod_count; ++i) {
                            ENC_KMSG("ListModulesNeeded lane=%d add-prepared core=%d mod=%d idx=%d",
                                     lane, READ_U8(arg1, 0x3d) + grp[0], grp[1], i);
                            AL_ModuleArray_AddModule(arg2, READ_U8(arg1, 0x3d) + grp[0], grp[1]);
                            grp += 2;
                        }
                    }
                    ENC_KMSG("ListModulesNeeded lane=%d prepared req=%p a74=%u a75=%u lane_mods=%d lane_slices=%d out_count=%d",
                             lane, req, (unsigned)READ_U8(req, 0xa74), (unsigned)READ_U8(req, 0xa75),
                             READ_S32(req, lane * 0x110 + 0x8cc), READ_S32(req, lane * 0x110 + 0x958),
                             READ_S32(arg2, 0x80));
                }
            }
        } else {
            ENC_KMSG("ListModulesNeeded lane=%d empty", lane);
        }

        if (req != 0) {
            ENC_KMSG("ListModulesNeeded lane=%d done out_count=%d req_a74=%u req_a75=%u lane_mods=%d lane_slices=%d",
                     lane, READ_S32(arg2, 0x80), (unsigned)READ_U8(req, 0xa74), (unsigned)READ_U8(req, 0xa75),
                     READ_S32(req, lane * 0x110 + 0x8cc), READ_S32(req, lane * 0x110 + 0x958));
        } else {
            ENC_KMSG("ListModulesNeeded lane=%d done out_count=%d", lane, READ_S32(arg2, 0x80));
        }

        if (lane == 1 && READ_S32(arg2, 0x80) > 0) {
            ENC_KMSG("ListModulesNeeded priority phase1 satisfied out_count=%d", READ_S32(arg2, 0x80));
            break;
        }
    }

    Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
    ENC_KMSG("ListModulesNeeded exit chctx=%p out_count=%d", arg1, READ_S32(arg2, 0x80));
    return 0;
}

int32_t encode1(void *arg1)
{
    int32_t *ch = arg1;
    int32_t *req;
    int32_t core;
    int32_t pict_id;
    uint32_t core_offset = READ_U8(ch, 0x3d);

    Rtos_GetMutex(READ_PTR(ch, 0x170));
    req = (int32_t *)(intptr_t)StaticFifo_Dequeue((uint8_t *)ch + 0x129b4);
    for (core = 0; core < READ_S32(req, 0x958); ++core) {
        WRITE_S32(req, 0x8d8 + core * 8, READ_S32(req, 0x8d8 + core * 8) + core_offset);
    }
    for (core = 0; core < READ_S32(req, 0x8cc); ++core) {
        WRITE_S32(req, 0x84c + core * 8, READ_S32(req, 0x84c + core * 8) + core_offset);
    }
    StaticFifo_Queue((StaticFifoCompat *)((uint8_t *)ch + 0x12a6c + READ_S32(req, 0xa70) * 0x5c),
                     (int32_t)(intptr_t)req);
    ENC_KMSG("encode1 dequeued req=%p grp=%d slice_count=%d mod_count=%d core_off=%u",
             req, READ_S32(req, 0xa70), READ_S32(req, 0x958), READ_S32(req, 0x8cc),
             (unsigned)core_offset);
    WRITE_U8(req, 0x182, (READ_U8(ch, 0x1f) == 0U && LIVE_T31_FORCE_DUAL_PUSH_AVC) ? 1U :
                          ((READ_U8(ch, 0x1f) == 0U) ? (READ_U8(ch, 0x3c) < 2U) : 1U));
    WRITE_S32(ch, 0x30, 0);
    WRITE_U8(req, 0x170, 2);
    WRITE_U8(req, 0x171, READ_U8(ch, 0x50));
    WRITE_U8(req, 0x172, READ_U8(ch, 0x13c));
    WRITE_U8(req, 0x174, READ_U8(ch, 0x1f));
    WRITE_U8(req, 0x173, READ_U8(ch, 0x4e));
    WRITE_U16(req, 0x17a, (READ_U16(ch, 4) + 7U) >> 3);
    WRITE_U16(req, 0x17c, (READ_U16(ch, 6) + 7U) >> 3);
    WRITE_U16(req, 0x278, (READ_U16(ch, 4) + (1U << (READ_U8(ch, 0x4e) & 0x1fU)) - 1U) >> (READ_U8(ch, 0x4e) & 0x1fU));
    WRITE_U16(req, 0x27a, (READ_U16(ch, 6) + (1U << (READ_U8(ch, 0x4e) & 0x1fU)) - 1U) >> (READ_U8(ch, 0x4e) & 0x1fU));
    pict_id = READ_S32(req, 0x20);
    if (READ_PTR(req, 0x318) == NULL) {
        int32_t *src = (int32_t *)(intptr_t)AL_SrcReorder_GetSrcBuffer((uint8_t *)ch + 0x178, pict_id);
        if (src != NULL) {
            WRITE_S32(req, 0x318, (int32_t)(intptr_t)((uint8_t *)src + 0x48));
        }
        ENC_KMSG("encode1 req source pict=%d src=%p cmd=%p", pict_id, src, READ_PTR(req, 0x318));
    }
    if (READ_U8(ch, 0x1f) != 4U) {
        WRITE_S32(req, 0x318, (int32_t)(intptr_t)((uint8_t *)req + 0x338));
    }
    ENC_KMSG("encode1 pre-FillSliceParam req=%p mode=%u chroma=%u cores=%u dual=%u ch4e=%u ch4f=%u pre170=%u pre171=%u pre172=%u pre173=%u pre174=%u w8=%08x w9=%08x wa=%08x wb=%08x wc=%08x wd=%08x w12=%08x",
             req, (unsigned)READ_U8(ch, 0x1f), (unsigned)READ_U8(ch, 0x4), (unsigned)READ_U8(ch, 0x3c),
             (unsigned)READ_U8(req, 0x182),
             (unsigned)READ_U8(ch, 0x4e), (unsigned)READ_U8(ch, 0x4f),
             (unsigned)READ_U8(req, 0x170), (unsigned)READ_U8(req, 0x171), (unsigned)READ_U8(req, 0x172),
             (unsigned)READ_U8(req, 0x173), (unsigned)READ_U8(req, 0x174),
             READ_S32(req, 0x20), READ_S32(req, 0x24), READ_S32(req, 0x28),
             READ_S32(req, 0x2c), READ_S32(req, 0x30), READ_S32(req, 0x34),
             READ_S32(req, 0x48));
    FillSliceParamFromPicParam(ch, (uint8_t *)req + 0x170, req);
    ENC_KMSG("encode1 post-FillSliceParam req=%p slice_type=%u pic_order=%d cmd318=%p s170=%u s171=%u s172=%u s173=%u s174=%u",
             req, (unsigned)READ_U8(req, 0x170), READ_S32(req, 0x184), READ_PTR(req, 0x318),
             (unsigned)READ_U8(req, 0x170), (unsigned)READ_U8(req, 0x171), (unsigned)READ_U8(req, 0x172),
             (unsigned)READ_U8(req, 0x173), (unsigned)READ_U8(req, 0x174));
#if LIVE_T31_VERBOSE_REQ_WINDOW
    ENC_KMSG("encode1 req-win req=%p 298=%08x 29c=%08x 2a0=%08x 2a4=%08x 2a8=%08x 2ac=%08x 2b0=%08x 2b4=%08x",
             req,
             READ_S32(req, 0x298), READ_S32(req, 0x29c), READ_S32(req, 0x2a0), READ_S32(req, 0x2a4),
             READ_S32(req, 0x2a8), READ_S32(req, 0x2ac), READ_S32(req, 0x2b0), READ_S32(req, 0x2b4));
    ENC_KMSG("encode1 req-win2 req=%p 2c0=%08x 2c4=%08x 2c8=%08x 2d0=%08x 2d4=%08x 2d8=%08x 2e0=%08x 2e4=%08x 2e8=%08x",
             req,
             READ_S32(req, 0x2c0), READ_S32(req, 0x2c4), READ_S32(req, 0x2c8), READ_S32(req, 0x2d0),
             READ_S32(req, 0x2d4), READ_S32(req, 0x2d8), READ_S32(req, 0x2e0), READ_S32(req, 0x2e4),
             READ_S32(req, 0x2e8));
    ENC_KMSG("encode1 req-win3 req=%p 2f0=%08x 2f4=%08x 2f8=%08x 2fc=%08x 300=%08x 304=%08x 308=%08x 30c=%08x 310=%08x 314=%08x",
             req,
             READ_S32(req, 0x2f0), READ_S32(req, 0x2f4), READ_S32(req, 0x2f8), READ_S32(req, 0x2fc),
             READ_S32(req, 0x300), READ_S32(req, 0x304), READ_S32(req, 0x308), READ_S32(req, 0x30c),
             READ_S32(req, 0x310), READ_S32(req, 0x314));
    ENC_KMSG("encode1 req-win3b req=%p 2bc=%08x 2cc=%08x 2d8=%08x 2dc=%08x 2ec=%08x 320=%08x 324=%08x 328=%08x 32c=%08x",
             req,
             READ_S32(req, 0x2bc), READ_S32(req, 0x2cc), READ_S32(req, 0x2d8), READ_S32(req, 0x2dc),
             READ_S32(req, 0x2ec), READ_S32(req, 0x320), READ_S32(req, 0x324), READ_S32(req, 0x328),
             READ_S32(req, 0x32c));
    ENC_KMSG("encode1 req-win4 req=%p 315=%02x 318=%p meta0c=%08x meta10=%08x meta14=%08x meta18=%08x meta1c=%08x",
             req, (unsigned)READ_U8(req, 0x315), READ_PTR(req, 0x318),
             READ_PTR(req, 0x318) ? READ_S32(READ_PTR(req, 0x318), 0x0c) : 0,
             READ_PTR(req, 0x318) ? READ_S32(READ_PTR(req, 0x318), 0x10) : 0,
             READ_PTR(req, 0x318) ? READ_S32(READ_PTR(req, 0x318), 0x14) : 0,
             READ_PTR(req, 0x318) ? READ_S32(READ_PTR(req, 0x318), 0x18) : 0,
             READ_PTR(req, 0x318) ? READ_S32(READ_PTR(req, 0x318), 0x1c) : 0);
#endif
    ENC_KMSG("encode1 pre-UpdateCommand req=%p slice=%p cmd1[0]=0x%x cmd1[1]=0x%x cmd2[0]=0x%x cmd2[1]=0x%x",
             req, (uint8_t *)req + 0x170,
             READ_S32(req, 0xa78), READ_S32(req, 0xa7c),
             READ_S32(req, 0xab8), READ_S32(req, 0xabc));
    UpdateCommand(ch, req, (uint8_t *)req + 0x170, 0);
    ENC_KMSG("encode1 post-UpdateCommand req=%p cmd318=%p cmd1[0]=0x%x",
             req, READ_PTR(req, 0x318), READ_S32(req, 0xa78));
    if (LIVE_T31_FORCE_SINGLE_CORE_AVC && READ_U8(ch, 0x1f) == 0U && READ_U8(ch, 0x3c) > 1U) {
        ENC_KMSG("encode1 force-single-core-avc req=%p ch_cores=%u mods=%d slices=%d pending0=%d",
                 req, (unsigned)READ_U8(ch, 0x3c), READ_S32(req, 0x8cc),
                 READ_S32(req, 0x958), READ_S32(req, 0x8d0));
        WRITE_U8(ch, 0x3c, 1);
        WRITE_S32(req, 0x8cc, 1);
        WRITE_S32(req, 0x958, 1);
        WRITE_S32(req, 0x8d0, 1);
        WRITE_U8(req, 0x1ee, 1);
    }
    {
        uint8_t *slice = (uint8_t *)req + 0x170;
        void *src_meta = READ_PTR(req, 0x318);
        int32_t slice_idx = 0;

        ENC_KMSG("encode1 materialize-entry req=%p slice=%p meta=%p cores=%u",
                 req, slice, src_meta, (unsigned)READ_U8(ch, 0x3c));
        for (core = 0; core < (int32_t)READ_U8(ch, 0x3c); ++core) {
            uint8_t *cmd_regs = (uint8_t *)(intptr_t)READ_S32(req, 0xa78 + core * 4);
            uint32_t enc2_off = GetCoreFirstEnc2CmdOffset((char)READ_U8(ch, 0x3c),
                                                          (char)READ_U16(ch, 0x40),
                                                          (char)core) << 9;
            uint8_t *cmd_regs_enc2 = cmd_regs != NULL ? cmd_regs + enc2_off : NULL;
            uint8_t slice_core[SLICE_PARAM_COPY_SIZE];
            int32_t stream_off = 0;
            int32_t stream_avail = 0;
            int32_t stream_part_limit = 0;
            int32_t core_stream_off = 0;
            int32_t core_stream_avail = 0;
            int32_t avc_header_bytes = 0;
            int32_t slice_off = 0;

            if (cmd_regs == NULL) {
                continue;
            }

            PrepareSliceParamForCore(ch, req, slice, (uint32_t)core, slice_core);
            memset(cmd_regs, 0, 0x200);

            if (src_meta != NULL) {
                stream_part_limit = READ_S32(src_meta, 0x10);
                stream_off = READ_S32(src_meta, 0x14);
                stream_avail = stream_part_limit - stream_off;
                if (stream_avail < 0) {
                    stream_avail = 0;
                }
                stream_avail &= ~0x1f;
            }
            if (READ_U8(ch, 0x1f) == 0U && READ_U8(ch, 0x3c) == 1U && src_meta != NULL) {
                int32_t legacy_part = ComputeLiveT31StreamPartOffset(ch, READ_S32(src_meta, 0x10));

                if (legacy_part > stream_off) {
                    ENC_KMSG("encode1 legacy-stream-part core=%d size=%d old_part=%d hdr=%d old_avail=%d new_part=%d new_avail=%d",
                             core, READ_S32(src_meta, 0x10), stream_part_limit, stream_off,
                             stream_avail, legacy_part, (legacy_part - stream_off) & ~0x1f);
                    stream_part_limit = legacy_part;
                    stream_avail = (legacy_part - stream_off) & ~0x1f;
                }
            }
            core_stream_off = stream_off;
            core_stream_avail = stream_avail;
            if (stream_avail > 0 && READ_U8(ch, 0x3c) > 1U) {
                int32_t core_count = (int32_t)READ_U8(ch, 0x3c);
                int32_t part = (stream_avail / core_count) & ~0x1f;

                if (part > 0) {
                    core_stream_off = stream_off + part * core;
                    core_stream_avail = (core + 1 == core_count) ? (stream_avail - part * core) : part;
                    core_stream_avail &= ~0x1f;
                }
            }

            ENC_KMSG("encode1 materialize-core core=%d cmd=%p meta10=%08x meta14=%08x stream_part_off=%d stream_part_avail=%d",
                     core, cmd_regs,
                     src_meta ? READ_S32(src_meta, 0x10) : 0,
                     src_meta ? READ_S32(src_meta, 0x14) : 0,
                     core_stream_off, core_stream_avail);
            if (READ_U8(ch, 0x1f) == 0U && READ_U8(ch, 0x3c) == 1U) {
                slice_off = 0;
                ENC_KMSG("encode1 materialize-off-single-avc core=%d cmd=%p slice_off=%d",
                         core, cmd_regs, slice_off);
            } else {
                slice_off = GetWPPOrSliceSizeOffset((uint8_t *)ch, slice, core, (uint8_t)slice_idx);
                ENC_KMSG("encode1 materialize-off core=%d cmd=%p slice_off=%d", core, cmd_regs, slice_off);
            }

            WRITE_S32(cmd_regs, 0x80, READ_S32(req, 0x298));
            WRITE_S32(cmd_regs, 0x84, READ_S32(req, 0x29c));
            WRITE_S32(cmd_regs, 0x88, (READ_S32(cmd_regs, 0x88) & 0xfffc0000) | (READ_S32(req, 0x2a4) & 0x3ffff));
            WRITE_S32(cmd_regs, 0x88, (READ_S32(cmd_regs, 0x88) & 0x7fffffff) | ((READ_S32(req, 0x2a0) & 1) << 31));
            WRITE_S32(cmd_regs, 0x88, (READ_S32(cmd_regs, 0x88) & 0x87ffffff) | ((READ_U8(req, 0x2a8) & 0x7) << 27));
            WRITE_S32(cmd_regs, 0x88, (READ_S32(cmd_regs, 0x88) & 0xf807ffff) | (READ_U8(req, 0x2a9) << 19));
            WRITE_S32(cmd_regs, 0x8c, READ_S32(req, 0x2b4));
            WRITE_S32(cmd_regs, 0x90, READ_S32(req, 0x2e0));
            WRITE_S32(cmd_regs, 0x94, READ_S32(req, 0x2e4));
            WRITE_S32(cmd_regs, 0x98, (READ_S32(cmd_regs, 0x98) & 0xfffc0000) | (READ_S32(req, 0x310) & 0x3ffff));
            WRITE_S32(cmd_regs, 0x98, (READ_S32(cmd_regs, 0x98) & 0x7fffffff) | ((READ_U8(req, 0x30c) & 1) << 31));
            WRITE_S32(cmd_regs, 0x98, (READ_S32(cmd_regs, 0x98) & 0xf807ffff) | (READ_U8(req, 0x315) << 19));
            WRITE_S32(cmd_regs, 0x98, (READ_S32(cmd_regs, 0x98) & 0x8fffffff) | ((READ_U8(req, 0x314) & 0x7) << 28));
            WRITE_S32(cmd_regs, 0x9c, READ_S32(req, 0x2fc));
            WRITE_S32(cmd_regs, 0xa0, READ_S32(req, 0x2c0));
            WRITE_S32(cmd_regs, 0xa4, READ_S32(req, 0x2c4));
            WRITE_S32(cmd_regs, 0xa8, READ_S32(req, 0x2d0));
            WRITE_S32(cmd_regs, 0xac, READ_S32(req, 0x2d4));
            if (READ_S32(req, 0x2f0) != 0) {
                WRITE_S32(cmd_regs, 0xb0, READ_S32(req, 0x2f0) + 0x100);
            }
            WRITE_S32(cmd_regs, 0xb4, READ_S32(req, 0x300));
            WRITE_S32(cmd_regs, 0xb8, READ_S32(req, 0x2f4) + 0x100);
            WRITE_S32(cmd_regs, 0xbc, READ_S32(req, 0x2f8) + slice_off);
            WRITE_S32(cmd_regs, 0xc0, src_meta ? READ_S32(src_meta, 0x0c) : 0);
            WRITE_S32(cmd_regs, 0xc4, src_meta ? stream_part_limit : 0);
            WRITE_S32(cmd_regs, 0xc8, src_meta ? core_stream_off : 0);
            WRITE_S32(cmd_regs, 0xcc, core_stream_avail);
            WRITE_S32(cmd_regs, 0xd0, READ_S32(req, 0x304));
            WRITE_S32(cmd_regs, 0xd4, READ_S32(req, 0x308));
            WRITE_S32(cmd_regs, 0xd8, 0);
            WRITE_S32(cmd_regs, 0xdc, READ_S32(req, 0x2e8));
            WRITE_S32(cmd_regs, 0xe0, READ_S32(req, 0x2c8));
            WRITE_S32(cmd_regs, 0xe4, READ_S32(req, 0x2d8));
            if (READ_U8(ch, 0x1f) == 0U) {
                /* The inline AVC entropy path inside CL_PUSH=2 consumes the same
                 * late address window that the standalone Enc2 CL uses. Mirror
                 * it into the base Enc1 command block so the embedded entropy
                 * stage sees map/data/stream pointers on the first IDR frame. */
                WRITE_S32(cmd_regs, 0xe8, READ_S32(req, 0x304));
                WRITE_S32(cmd_regs, 0xec, READ_S32(req, 0x308));
                WRITE_S32(cmd_regs, 0xf0, src_meta ? READ_S32(src_meta, 0x0c) : 0);
                WRITE_S32(cmd_regs, 0xf4, src_meta ? stream_part_limit : 0);
                WRITE_S32(cmd_regs, 0xf8, core_stream_off);
                WRITE_S32(cmd_regs, 0xfc, core_stream_avail);
                /* The inline entropy stage also reads the embedded stream block
                 * at cmd[0x50..0x57]. Keep it in sync with cmd[0x30..0x37]
                 * so CL_PUSH=2 carries the same stream and intermediate-buffer
                 * tuple in both places. */
                WRITE_S32(cmd_regs, 0x140, READ_S32(cmd_regs, 0xc0));
                WRITE_S32(cmd_regs, 0x144, READ_S32(cmd_regs, 0xc4));
                WRITE_S32(cmd_regs, 0x148, READ_S32(cmd_regs, 0xc8));
                WRITE_S32(cmd_regs, 0x14c, READ_S32(cmd_regs, 0xcc));
                WRITE_S32(cmd_regs, 0x150, READ_S32(cmd_regs, 0xd0));
                WRITE_S32(cmd_regs, 0x154, READ_S32(cmd_regs, 0xd4));
                WRITE_S32(cmd_regs, 0x158, READ_S32(cmd_regs, 0xd8));
                WRITE_S32(cmd_regs, 0x15c, READ_S32(cmd_regs, 0xdc));
            }

            ENC_KMSG("encode1 materialize-prepack core=%d cmd=%p c0=%08x c4=%08x c8=%08x cc=%08x",
                     core, cmd_regs,
                     READ_S32(cmd_regs, 0xc0), READ_S32(cmd_regs, 0xc4),
                     READ_S32(cmd_regs, 0xc8), READ_S32(cmd_regs, 0xcc));
            ENC_KMSG("encode1 prepack-meta core=%d req=%p srcblk14=%08x srcblk18=%08x srcblk34=%08x srcblk44=%08x srcblk54=%08x srcblk94=%08x",
                     core, req,
                     READ_S32(req, 0x2ac), READ_S32(req, 0x2b0), READ_S32(req, 0x2cc),
                     READ_S32(req, 0x2dc), READ_S32(req, 0x2ec), READ_S32(req, 0x32c));
            ENC_KMSG("encode1 pre-enc1-pack core=%d slice=%p start=%d end=%d span=%u lcu=%ux%u",
                     core, slice_core,
                     READ_S32(slice_core, 0x3c), READ_S32(slice_core, 0x44), (unsigned)READ_U16(slice_core, 0x4c),
                     (unsigned)READ_U16(slice_core, 0x108), (unsigned)READ_U16(slice_core, 0x10a));
            SliceParamToCmdRegsEnc1(slice_core, cmd_regs, (uint8_t *)req + 0x298, READ_U8(ch, 0x4f));
            PatchLiveT31AvcIdrEnc1Control(ch, req, cmd_regs);
            if (READ_U8(ch, 0x1f) == 0U) {
                avc_header_bytes = PrewriteLiveT31AvcHeaders(ch, req, src_meta, core_stream_off,
                                                             core == 0, core, READ_S32(slice_core, 0x3c),
                                                             (READ_U32(cmd_regs, 0x00) & 0x00000400U) != 0U,
                                                             (READ_U32(cmd_regs, 0x00) & 0x00000400U) != 0U);
                if (avc_header_bytes > 0 && avc_header_bytes < core_stream_avail) {
                    core_stream_off += avc_header_bytes;
                    core_stream_avail = (core_stream_avail - avc_header_bytes) & ~0x1f;
                    WRITE_S32(cmd_regs, 0xc8, core_stream_off);
                    WRITE_S32(cmd_regs, 0xcc, core_stream_avail);
                    WRITE_S32(cmd_regs, 0xf8, core_stream_off);
                    WRITE_S32(cmd_regs, 0xfc, core_stream_avail);
                    WRITE_S32(cmd_regs, 0x148, core_stream_off);
                    WRITE_S32(cmd_regs, 0x14c, core_stream_avail);
                    ENC_KMSG("encode1 avc-prewrite-payload core=%d header=%d payload_off=%d payload_avail=%d",
                             core, avc_header_bytes, core_stream_off, core_stream_avail);
                }
                RepairStandaloneAvcEnc2Slice(ch, slice_core);
                if (avc_header_bytes > 0 && avc_header_bytes <= 0x3ff) {
                    WRITE_U16(slice_core, 0x78, (uint16_t)avc_header_bytes);
                }
                SliceParamToCmdRegsEnc2(slice_core, cmd_regs);
                ENC_KMSG("encode1 materialized-inline-enc2 core=%d cmd=%p sp74=%04x sp78=%04x spf8=%02x sp100=%08x w1b=%08x w1c=%08x w1d=%08x w1e=%08x w1f=%08x",
                         core, cmd_regs,
                         (unsigned)READ_U16(slice_core, 0x74), (unsigned)READ_U16(slice_core, 0x78),
                         (unsigned)READ_U8(slice_core, 0xf8), (unsigned)READ_U32(slice_core, 0x100),
                         READ_S32(cmd_regs, 0x6c), READ_S32(cmd_regs, 0x70),
                         READ_S32(cmd_regs, 0x74), READ_S32(cmd_regs, 0x78),
                         READ_S32(cmd_regs, 0x7c));
            }
            ENC_KMSG("encode1 post-enc1-pack core=%d cmd=%p w0=%08x w1=%08x w2=%08x w3=%08x w0a=%08x w0b=%08x",
                     core, cmd_regs,
                     READ_S32(cmd_regs, 0x00), READ_S32(cmd_regs, 0x04),
                     READ_S32(cmd_regs, 0x08), READ_S32(cmd_regs, 0x0c),
                     READ_S32(cmd_regs, 0x28), READ_S32(cmd_regs, 0x2c));
            if (READ_U8(ch, 0x1f) == 0U && cmd_regs_enc2 != NULL && READ_S32(req, 0xa6c) > 1 &&
                READ_U8(req, 0x164) != 0U) {
                uint8_t slice_enc2[SLICE_PARAM_COPY_SIZE];
                uint32_t slice_cmd_fc;
                uint32_t slice_live_fc;
                uint32_t slice_final_fc;

                memcpy(slice_enc2, slice_core, sizeof(slice_enc2));
                CmdRegsEnc1ToSliceParam(cmd_regs, slice_enc2, READ_S32(ch, CH_CMD_TRACE_FLAGS_OFF));
                slice_cmd_fc = READ_U32(slice_enc2, 0xfc);
                slice_live_fc = READ_U32(slice_core, 0xfc);
                slice_final_fc = slice_cmd_fc;
                if (slice_final_fc == 0U) {
                    slice_final_fc = slice_live_fc;
                }
                WRITE_S32(slice_enc2, 0x3c, READ_S32(slice_core, 0x3c));
                WRITE_S32(slice_enc2, 0x44, READ_S32(slice_core, 0x44));
                WRITE_U16(slice_enc2, 0x4c, READ_U16(slice_core, 0x4c));
                WRITE_S32(slice_enc2, 0x54, READ_S32(slice_core, 0x54));
                WRITE_U16(slice_enc2, 0x108, READ_U16(slice_core, 0x108));
                WRITE_U16(slice_enc2, 0x10a, READ_U16(slice_core, 0x10a));
                WRITE_U32(slice_enc2, 0xfc, slice_final_fc);
                ENC_KMSG("encode1 enc2-fc core=%d live=%u cmd=%u final=%u",
                         core, slice_live_fc, slice_cmd_fc, slice_final_fc);
                RepairStandaloneAvcEnc2Slice(ch, slice_enc2);
                if (avc_header_bytes > 0 && avc_header_bytes <= 0x3ff) {
                    WRITE_U16(slice_enc2, 0x78, (uint16_t)avc_header_bytes);
                }
                if (cmd_regs_enc2 != cmd_regs) {
                    Rtos_Memset(cmd_regs_enc2, 0, 0x200);
                    memcpy(cmd_regs_enc2, cmd_regs, 0x6c);
                    memcpy(cmd_regs_enc2 + 0x80, cmd_regs + 0x80, 0x68);
                } else {
                    ENC_KMSG("encode1 materialize-enc2 alias core=%d cmd=%p", core, cmd_regs_enc2);
                }
                WRITE_S32(cmd_regs_enc2, 0xe8, READ_S32(req, 0x304));
                WRITE_S32(cmd_regs_enc2, 0xec, READ_S32(req, 0x308));
                WRITE_S32(cmd_regs_enc2, 0xf0, src_meta ? READ_S32(src_meta, 0x0c) : 0);
                WRITE_S32(cmd_regs_enc2, 0xf4, src_meta ? stream_part_limit : 0);
                WRITE_S32(cmd_regs_enc2, 0xf8, core_stream_off);
                WRITE_S32(cmd_regs_enc2, 0xfc, core_stream_avail);
                SliceParamToCmdRegsEnc2(slice_enc2, cmd_regs_enc2);
                ENC_KMSG("encode1 materialized-enc2 core=%d cmd=%p map=%08x data=%08x stream=%08x max=%08x off=%08x avail=%08x sp74=%04x sp78=%04x spf8=%02x sp100=%08x w1b=%08x w1c=%08x w1d=%08x w1e=%08x w1f=%08x",
                         core, cmd_regs_enc2,
                         READ_S32(cmd_regs_enc2, 0xe8), READ_S32(cmd_regs_enc2, 0xec),
                         READ_S32(cmd_regs_enc2, 0xf0), READ_S32(cmd_regs_enc2, 0xf4),
                         READ_S32(cmd_regs_enc2, 0xf8), READ_S32(cmd_regs_enc2, 0xfc),
                         (unsigned)READ_U16(slice_enc2, 0x74), (unsigned)READ_U16(slice_enc2, 0x78),
                         (unsigned)READ_U8(slice_enc2, 0xf8), (unsigned)READ_U32(slice_enc2, 0x100),
                         READ_S32(cmd_regs_enc2, 0x6c), READ_S32(cmd_regs_enc2, 0x70),
                         READ_S32(cmd_regs_enc2, 0x74), READ_S32(cmd_regs_enc2, 0x78),
                         READ_S32(cmd_regs_enc2, 0x7c));
            } else if (READ_U8(ch, 0x1f) == 0U && READ_S32(req, 0xa6c) > 1) {
                ENC_KMSG("encode1 skip-standalone-enc2 core=%d refs=%u cmd=%p enc2_cmd=%p",
                         core, (unsigned)READ_U8(req, 0x164), cmd_regs, cmd_regs_enc2);
            }
            ENC_KMSG("encode1 materialized core=%d cmd=%p slice_off=%d stream_off=%d stream_avail=%d start=%d end=%d span=%u w0=%08x w1=%08x w2=%08x w3=%08x",
                     core, cmd_regs, slice_off, core_stream_off, core_stream_avail,
                     READ_S32(slice_core, 0x3c), READ_S32(slice_core, 0x44), (unsigned)READ_U16(slice_core, 0x4c),
                     READ_S32(cmd_regs, 0x00), READ_S32(cmd_regs, 0x04),
                     READ_S32(cmd_regs, 0x08), READ_S32(cmd_regs, 0x0c));
        }
    }
    ENC_KMSG("encode1 pre-handleInputTraces req=%p", req);
    handleInputTraces(ch, req, (uint8_t *)req + 0x170, 0);
    ENC_KMSG("encode1 post-handleInputTraces req=%p", req);
    ENC_KMSG("encode1 pre-TurnOnRAM cores=%u", (unsigned)READ_U8(ch, 0x3c));
    AL_EncCore_TurnOnRAM(READ_PTR(ch, 0x164), READ_U8(ch, 0x1f), READ_U8(ch, 0x3c), 0, 0);
    ENC_KMSG("encode1 post-TurnOnRAM");
    ENC_KMSG("encode1 pre-EnableInterrupts cores=%u core_tbl=%p",
             (unsigned)READ_U8(ch, 0x3c), (uint8_t *)ch + 0x3c);
    if (READ_U8(ch, 0x3c) > 1) {
        void *core1_live = (uint8_t *)READ_PTR(ch, 0x164) + 0x44;

        RemapLiveT31Irq4(core1_live, EndEncoding, "encode1");
    } else if (READ_U8(ch, 0x1f) == 0U) {
        RemapLiveT31Irq4(READ_PTR(ch, 0x164), EndEncoding, "encode1-core0");
    }
    AL_EncCore_EnableInterrupts(READ_PTR(ch, 0x164), (uint8_t *)ch + 0x3c, 0, 1, 0);
    if (READ_U8(ch, 0x1f) == 0U && READ_U8(ch, 0x3c) == 1U) {
        ApplyLiveT31Core0LegacyIrqMask(READ_PTR(ch, 0x164), "encode1");
    }
    ENC_KMSG("encode1 post-EnableInterrupts");
    /* Multi-core launches keep EndEncoding blocked until every core command has
     * been submitted. The live T31 path is forced to single-core AVC, where the
     * IRQ can arrive immediately and must not deadlock on this channel mutex. */
    {
        int unlock_before_launch = (READ_U8(ch, 0x1f) == 0U && READ_U8(ch, 0x3c) == 1U);

        ENC_KMSG("encode1 launch mutex-held req=%p unlock_before_launch=%d", req, unlock_before_launch);
        if (unlock_before_launch) {
            ENC_KMSG("encode1 unlock-before-launch req=%p mutex=%p", req, READ_PTR(ch, 0x170));
            Rtos_ReleaseMutex(READ_PTR(ch, 0x170));
        }
    for (core = 0; core < (int32_t)READ_U8(ch, 0x3c); ++core) {
        void *enc1 = (uint8_t *)READ_PTR(ch, 0x164) + core * 0x44;
        int32_t cmd1 = READ_S32(req, 0xa78 + core * 4);
        int32_t cmd2 = READ_S32(req, 0xab8 + core * 4);
        void *req18 = READ_PTR(req, 0x18);
        void *req18_24 = req18 ? READ_PTR(req18, 0x24) : NULL;
        int inline_avc_enc2 = (READ_U8(ch, 0x1f) == 0U && READ_S32(req, 0xa6c) > 1 &&
                               READ_U8(req, 0x164) == 0U);
        int launch_enc2 = (READ_U8(req, 0x182) != 0U);

        ENC_KMSG("encode1 launch core=%d enc=%p cmd1=0x%x cmd2=0x%x dual=%u inline_avc_enc2=%d",
                 core, enc1, cmd1, cmd2, (unsigned)launch_enc2, inline_avc_enc2);
        ENC_KMSG("encode1 launch-ctx core=%d req18=%p req18+24=%p req18_24_428=%08x req18_24_42c=%08x req18_24_430=%08x req18_24_434=%08x req18_24_43c=%p",
                 core, req18, req18_24,
                 req18_24 ? READ_S32(req18_24, 0x428) : 0,
                 req18_24 ? READ_S32(req18_24, 0x42c) : 0,
                 req18_24 ? READ_S32(req18_24, 0x430) : 0,
                 req18_24 ? READ_S32(req18_24, 0x434) : 0,
                 req18_24 ? READ_PTR(req18_24, 0x43c) : NULL);
        if (READ_U8(ch, 0x1f) == 0U) {
            PrepareSourceConfigForLaunch(enc1, ch, req, (uint32_t *)(uintptr_t)cmd1, "pre-cl");
        }
        AL_EncCore_Encode1(enc1, cmd2, cmd1, launch_enc2 ? cmd2 : 0);
        if (!unlock_before_launch) {
            PrepareSourceConfigForLaunch(enc1, ch, req, (uint32_t *)(uintptr_t)cmd1, "post-cl");
        }
        if (!unlock_before_launch && LIVE_T31_DELAYED_POST_CL_PROBE && READ_U8(ch, 0x1f) == 0U && core == 0) {
            ProbeLiveT31AfterLaunch(ch, req, core, "post-cl+0ms");
            Rtos_Sleep(10);
            ProbeLiveT31AfterLaunch(ch, req, core, "post-cl+10ms");
            Rtos_Sleep(100);
            ProbeLiveT31AfterLaunch(ch, req, core, "post-cl+110ms");
        }
    }
        if (unlock_before_launch) {
            ENC_KMSG("encode1 launch-unlocked-return req=%p", req);
            return 1;
        }
    }
    return Rtos_ReleaseMutex(READ_PTR(ch, 0x170));
}

int32_t AL_EncChannel_Encode(void *arg1, void *arg2)
{
    int32_t (*workers[2])(void *) = { encode1, encode2 };
    StaticFifoCompat *running = (StaticFifoCompat *)((uint8_t *)arg1 + 0x129b4);
    int lane;
    int32_t result = 0;

    ENC_KMSG("EncChannel_Encode entry chctx=%p modules=%p codec=%u cores=%u core_base=%u",
             arg1, arg2, (unsigned)READ_U8(arg1, 0x1f), (unsigned)READ_U8(arg1, 0x3c), (unsigned)READ_U8(arg1, 0x3d));
    if (READ_U8(arg1, 0x1f) != 4U) {
        int i;
        uint8_t *core = (uint8_t *)READ_PTR(arg1, 0x164);

        for (i = 0; i < (int)READ_U8(arg1, 0x3c); ++i) {
            AL_EncCore_Reset(core + i * 0x44);
        }

        for (lane = 0; lane != 2; ++lane) {
            int32_t active = 0;
            int32_t *front = (int32_t *)(intptr_t)StaticFifo_Front(running);

            if (READ_S32(arg2, 0x80) > 0) {
                int32_t *slot = (int32_t *)arg2;
                int i;

                for (i = 0; i < READ_S32(arg2, 0x80); ++i) {
                    active += (lane == READ_S32(slot, 4));
                    slot += 2;
                }
            }
            if (active != 0 && front != 0) {
                int32_t ofs = READ_S32(front, 0xa70) * 0x110;
                int32_t slice_count = READ_S32(front, ofs + 0x958);
                int32_t i;
                int32_t match = 0;

                for (i = 0; i < slice_count; ++i) {
                    match += (lane == READ_S32(front, ofs + 0x8dc + i * 8));
                }
                if (match != 0) {
                    ENC_KMSG("EncChannel_Encode lane=%d active=%d slice_match=%d front=%p", lane, active, match, front);
                    result = workers[lane](arg1);
                    ENC_KMSG("EncChannel_Encode worker lane=%d result=%d", lane, result);
                }
            }
            running = (StaticFifoCompat *)((uint8_t *)running + 0x5c);
        }
        ENC_KMSG("EncChannel_Encode exit result=%d", result);
        return result;
    }

    Rtos_GetMutex(READ_PTR(arg1, 0x170));
    {
        int32_t *req = (int32_t *)(intptr_t)StaticFifo_Dequeue((uint8_t *)arg1 + 0x129b4);
        int i;

        for (i = 0; i < READ_S32(req, 0x958); ++i) {
            WRITE_S32(req, 0x8d8 + i * 8, READ_S32(req, 0x8d8 + i * 8) + READ_U8(arg1, 0x3d));
        }
        for (i = 0; i < READ_S32(req, 0x8cc); ++i) {
            WRITE_S32(req, 0x84c + i * 8, READ_S32(req, 0x84c + i * 8) + READ_U8(arg1, 0x3d));
        }
        StaticFifo_Queue((StaticFifoCompat *)((uint8_t *)arg1 + 0x12a6c), (int32_t)(intptr_t)req);
        WRITE_S32(req, 0x318, (int32_t)(intptr_t)((uint8_t *)req + 0x338));
        memset((void *)(intptr_t)READ_S32(req, 0xa78), 0, 0x200);
        JpegParamToCtrlRegs(arg1, (void *)(intptr_t)READ_S32(req, 0xa78));
        WRITE_S32((void *)(intptr_t)READ_S32(req, 0xa78), 0x10, READ_S32(req, 0x298));
        WRITE_S32((void *)(intptr_t)READ_S32(req, 0xa78), 0x14, READ_S32(req, 0x29c));
        WRITE_S32((void *)(intptr_t)READ_S32(req, 0xa78), 0x18, READ_S32(req, 0x2fc));
        WRITE_S32((void *)(intptr_t)READ_S32(req, 0xa78), 0x1c, READ_S32(READ_PTR(req, 0x318), 0xc));
        WRITE_S32((void *)(intptr_t)READ_S32(req, 0xa78), 0x20, READ_S32(READ_PTR(req, 0x318), 0x10));
        WRITE_S32((void *)(intptr_t)READ_S32(req, 0xa78), 0x24, READ_S32(READ_PTR(req, 0x318), 0x14));
        WRITE_S32((void *)(intptr_t)READ_S32(req, 0xa78), 0x28,
                  READ_S32(READ_PTR(req, 0x318), 0x10) - READ_S32(READ_PTR(req, 0x318), 0x14));
        handleJpegInputTrace(arg1, req);
        AL_EncCore_TurnOnRAM(READ_PTR(arg1, 0x164), 4, 1, 0, 0);
        AL_EncCore_SetJpegInterrupt(READ_PTR(arg1, 0x164));
        AL_EncCore_EncodeJpeg(READ_PTR(arg1, 0x164), (void *)(intptr_t)READ_S32(req, 0xa78));
    }
    return Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
}

int32_t AL_EncChannel_EndEncoding(void *arg1, uint8_t arg2, int32_t arg3)
{
    ENC_KMSG("EndEncoding entry chctx=%p core=%u lane=%d jpeg=%u gate=%u",
             arg1, (unsigned)arg2, arg3, (unsigned)READ_U8(arg1, 0x1f), (unsigned)READ_U8(arg1, 0xc4));
    if (READ_U8(arg1, 0x1f) == 4U) {
        EndJpegEncoding(arg1);
        return 1;
    }

    if (READ_U8(arg1, 0xc4) != 0U) {
        Rtos_GetMutex(READ_PTR(arg1, 0x170));
        {
            void *fifo = getFifoRunning(arg1, arg3);

            if (fifo == 0) {
                ENC_KMSG("EndEncoding no-fifo chctx=%p core=%u lane=%d", arg1, (unsigned)arg2, arg3);
                Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
                return 0;
            }

            {
                int32_t *req = (int32_t *)(intptr_t)StaticFifo_Front(fifo);
                int32_t *core = (int32_t *)((uint8_t *)req + arg2);
                uint32_t done = READ_U8(core, 0x84a) + 1U;

                ENC_KMSG("EndEncoding req=%p core_slot=%p done=%u req174=%u req848=%u core_base=%u core_count=%u",
                         req, core, (unsigned)done, (unsigned)READ_U8(req, 0x174),
                         (unsigned)READ_U16(req, 0x848), (unsigned)READ_U8(arg1, 0x3d),
                         (unsigned)READ_U8(arg1, 0x3c));

                WRITE_U8(core, 0x84a, (uint8_t)done);
                WRITE_S32(req, req[0x29c] * 0x11 + arg3 + 0x234, READ_S32(req, req[0x29c] * 0x11 + arg3 + 0x234) - 1);
                if (READ_U8(req, 0x174) == 0U) {
                    uint32_t num_core = READ_U8(arg1, 0x3c);
                    uint8_t start = READ_U8(arg1, 0x3d);

                    if (num_core == 0U) {
                        __builtin_trap();
                    }
                    if ((uint32_t)arg2 == (uint32_t)(start + (READ_U16(req, 0x848) % num_core))) {
                        int32_t committed = CommitSlice(arg1, fifo, req, arg2, 0, arg3);

                        ENC_KMSG("EndEncoding commit-single req=%p core=%u lane=%d committed=%d",
                                 req, (unsigned)arg2, arg3, committed);

                        Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
                        if (committed == 0) {
                            return 0;
                        }
                        if (READ_S32(arg1, 0x50) != 0) {
                            ((void (*)(int32_t, void *))(intptr_t)READ_S32(arg1, 0x50))
                                (READ_S32(arg1, 0x54), (uint8_t *)req + (req[0x29c] - 1) * 0x110 + 0x84c);
                        }
                        return 1;
                    }
                } else {
                    int32_t ready = 0;
                    int32_t *base = (int32_t *)((uint8_t *)req + READ_U8(arg1, 0x3d) + 0x84a);

                    if (READ_S32(base, 0) >= (int32_t)done &&
                        (READ_U8(arg1, 0x3c) == 1U || READ_S32(base, 1) >= (int32_t)done)) {
                        ready = 1;
                    }
                    if (ready != 0) {
                        int32_t committed = CommitSlice(arg1, fifo, req, arg2, 0, arg3);

                        ENC_KMSG("EndEncoding commit-ready req=%p core=%u lane=%d committed=%d done=%u",
                                 req, (unsigned)arg2, arg3, committed, (unsigned)done);

                        Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
                        if (committed == 0) {
                            return 0;
                        }
                        if (READ_S32(arg1, 0x50) != 0) {
                            ((void (*)(int32_t, void *))(intptr_t)READ_S32(arg1, 0x50))
                                (READ_S32(arg1, 0x54), (uint8_t *)req + (req[0x29c] - 1) * 0x110 + 0x84c);
                        }
                        return 1;
                    }
                }

                if (READ_U8(req, 0x174) == 0U) {
                    int32_t bit = 1 << (findCurCoreSlice(req, (uint8_t)(arg2 - READ_U8(arg1, 0x3d)),
                                                         (uint8_t)READ_U8(arg1, 0x3c)) & 0x1f);
                    req[0x210] |= bit;
                    req[0x211] |= bit >> 31;
                    ENC_KMSG("EndEncoding mark-pending req=%p bit=0x%x mask0=0x%x mask1=0x%x",
                             req, bit, req[0x210], req[0x211]);
                }
            }
        }
        Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
        return 0;
    }

    Rtos_GetMutex(READ_PTR(arg1, 0x170));
    {
        void *fifo = getFifoRunning(arg1, arg3);

        if (fifo != 0) {
            int32_t *req = (int32_t *)(intptr_t)StaticFifo_Front(fifo);
            int32_t req_phase = READ_S32(req, 0xa70);
            int32_t pending_off = 0x8d0 + ((req_phase * 0x44 + arg3) << 2);
            int32_t pending = READ_S32(req, pending_off) - 1;

            WRITE_S32(req, pending_off, pending);
            ENC_KMSG("EndEncoding non-gate req=%p lane=%d phase=%d pending=%d a6c=%d",
                     req, arg3, req_phase, pending, READ_S32(req, 0xa6c));

            if (pending <= 0) {
                void *core_arr = READ_PTR(arg1, 0x164);
                int32_t req_phase_next;
                int32_t done_cb;
                int32_t force_status;
                int32_t active_cores;
                int32_t core_idx;
                uint8_t slice_status[0x78];
                int32_t *evt;
                int32_t *i;
                int32_t *dst;
                int32_t slice_count;
                int32_t slice_idx;

                StaticFifo_Dequeue(fifo);
                req_phase_next = req_phase + 1;
                WRITE_S32(req, 0xa70, req_phase_next);
                ClearCompletedPhaseRunState(arg1, req, req_phase);

                AL_EncCore_TurnOffRAM(READ_PTR(arg1, 0x164), 0, READ_U8(arg1, 0x3c), 0, 0);

                WRITE_S32(req, 0xaf8, READ_S32(req, 0x10));
                WRITE_S32(req, 0xafc, READ_S32(req, 0x14));
                WRITE_S32(req, 0xb00, READ_S32(req, 0x18));
                WRITE_S32(req, 0xb04, READ_S32(req, 0x1c));
                WRITE_S32(req, 0xb94, 0);
                WRITE_S32(req, 0xb10, READ_S32(req, 0x40));
                WRITE_S32(req, 0xb98, READ_S32(req, 0x30));
                WRITE_S32(req, 0xb9c, READ_S32(req, 0x34));
                WRITE_U8(req, 0xba0, READ_U8(req, 0x24) & 1U);
                WRITE_U16(req, 0xba4, READ_U16(req, 4));
                WRITE_S32(req, 0xba8, READ_S32(req, 0x48));
                WRITE_U8(req, 0xbac, READ_U8(req, 0x3c));
                WRITE_U8(req, 0xba1, 1);
                WRITE_U8(req, 0xba2, 1);
                WRITE_S32(req, 0xb2c, READ_S32(READ_PTR(req, 0x318), 0x18));

                if (req_phase_next < READ_S32(req, 0xa6c) &&
                    !(READ_U8(arg1, 0x1f) == 0U && req_phase == 0 && READ_U8(req, 0x164) == 0U)) {
                    void *core_arr_164 = READ_PTR(arg1, 0x164);
                    void *core_arr_168 = READ_PTR(arg1, 0x168);

                    ENC_KMSG("EndEncoding non-gate phase-advance req=%p next=%d total=%d core164=%p core168=%p active=%u",
                             req, req_phase_next, READ_S32(req, 0xa6c), core_arr_164, core_arr_168,
                             (unsigned)READ_U8(req, 0x1ee));
                    StaticFifo_Queue((StaticFifoCompat *)((uint8_t *)arg1 + req_phase_next * 0x5c + 0x129b4),
                                     (int32_t)(intptr_t)req);
                    ENC_KMSG("EndEncoding non-gate queued req=%p fifo_off=0x%x",
                             req, req_phase_next * 0x5c + 0x129b4);
                    SyncCompletedCmdListCache(arg1, req, "phase-advance");
                    InitSliceStatus(slice_status);
                    active_cores = (int32_t)READ_U8(req, 0x1ee);
                    core_arr = core_arr_164;
                    for (core_idx = 0; core_idx < active_cores; ++core_idx) {
                        ENC_KMSG("EndEncoding non-gate read-status req=%p core_idx=%d core=%p",
                                 req, core_idx, (uint8_t *)core_arr + core_idx * 0x44);
                        AL_EncCore_ReadStatusRegsEnc((uint8_t *)core_arr + core_idx * 0x44, slice_status);
                    }
                    ENC_KMSG("EndEncoding non-gate read-status-done req=%p rc_bytes=%d status1=%d status2=%d",
                             req, READ_S32(slice_status, 4), READ_S32(slice_status, 0), READ_S32(slice_status, 8));
                    force_status = 0;
                    if ((READ_S32(arg1, 0x90) & 8) != 0 &&
                        READ_U8(arg1, 0x3de0) == 0U &&
                        READ_U8(arg1, 0xc4) == 0U) {
                        force_status = 1;
                    }
                    ENC_KMSG("EndEncoding non-gate pre-rc req=%p force_status=%d skip=%u",
                             req, force_status, (unsigned)READ_U8(req, 0xb08));
                    UpdateRateCtrl_constprop_83(arg1, req, (int32_t *)slice_status, 1, 1);
                    ENC_KMSG("EndEncoding non-gate post-rc req=%p rc_bytes=%d slice_budget=%d skip=%u",
                             req, READ_S32(slice_status, 4), READ_S32(req, 0x2c8 * 4), (unsigned)READ_U8(req, 0xb08));
                    ENC_KMSG("EndEncoding non-gate pre-cb14c req=%p cb=%p",
                             req, (void *)(intptr_t)READ_S32(arg1, 0x14c));
                    ((void (*)(void *, uint32_t, void *, void *, int32_t))(intptr_t)READ_S32(arg1, 0x14c))
                        ((uint8_t *)arg1 + 0x128, READ_U8(req, 0xb08), (uint8_t *)req + 0x20, slice_status,
                         force_status);
                    ENC_KMSG("EndEncoding non-gate post-cb14c req=%p", req);
                    ENC_KMSG("EndEncoding non-gate pre-cb144 req=%p cb=%p",
                             req, (void *)(intptr_t)READ_S32(arg1, 0x144));
                    ((void (*)(void *, void *, int32_t))(intptr_t)READ_S32(arg1, 0x144))
                        ((uint8_t *)arg1 + 0x128, (uint8_t *)req + 0x20, 0);
                    ENC_KMSG("EndEncoding non-gate post-cb144 req=%p", req);
                    ENC_KMSG("EndEncoding non-gate pre-store req=%p", req);
                    StorePicture((int32_t)(intptr_t)((uint8_t *)arg1 + 0x22c8), req);
                    ENC_KMSG("EndEncoding non-gate post-store req=%p", req);
                    ENC_KMSG("EndEncoding non-gate pre-output-trace req=%p", req);
                    handleOutputTraces(arg1, req, (uint8_t)(READ_U8(arg1, 0x3c) - 1U), 3);
                    ENC_KMSG("EndEncoding non-gate post-output-trace req=%p", req);
                    DisableCompletedPhaseInterrupts(arg1, req, req_phase);
                    done_cb = READ_S32(arg1, 0x1aa8);
                    Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
                    ENC_KMSG("EndEncoding non-gate queued-next-phase req=%p next=%d total=%d rc_bytes=%d skip=%u",
                             req, req_phase_next, READ_S32(req, 0xa6c), READ_S32(slice_status, 4),
                             (unsigned)READ_U8(req, 0xb08));
                    if (done_cb != 0) {
                        ((void (*)(void *, void *))(intptr_t)done_cb)
                            (READ_PTR(arg1, 0x1aac),
                             (uint8_t *)req + (READ_S32(req, 0xa70) - 1) * 0x110 + 0x84c);
                    }
                    return 1;
                }

                if (req_phase_next < READ_S32(req, 0xa6c) &&
                    READ_U8(arg1, 0x1f) == 0U && req_phase == 0 && READ_U8(req, 0x164) == 0U) {
                    ENC_KMSG("EndEncoding inline-avc final-output req=%p next=%d total=%d refs=%u",
                             req, req_phase_next, READ_S32(req, 0xa6c), (unsigned)READ_U8(req, 0x164));
                    WRITE_S32(req, 0xa6c, req_phase_next);
                }

                DisableCompletedPhaseInterrupts(arg1, req, req_phase);

                SyncCompletedCmdListCache(arg1, req, "final-output");
                InitSliceStatus(slice_status);
                if (READ_S32(req, 0x30) == 7) {
                    ENC_KMSG("EndEncoding final skipped-output req=%p phase=%d", req, req_phase);
                    OutputSkippedPicture(arg1, req, slice_status);
                } else {
                    slice_count = READ_U16(arg1, 0x40);
                    ENC_KMSG("EndEncoding final slices req=%p phase=%d count=%d", req, req_phase, slice_count);
                    for (slice_idx = 0; slice_idx < slice_count; ++slice_idx) {
                        ENC_KMSG("EndEncoding final pre-slice req=%p slice=%d", req, slice_idx);
                        OutputSlice(arg1, req, slice_idx, slice_status);
                        ENC_KMSG("EndEncoding final post-slice req=%p slice=%d bytes=%d err=%u full=%u overflow=%u",
                                 req, slice_idx, READ_S32(slice_status, 4),
                                 (unsigned)READ_U8(slice_status, 0),
                                 (unsigned)READ_U8(slice_status, 1),
                                 (unsigned)READ_U8(slice_status, 2));
                    }
                }
                ENC_KMSG("EndEncoding final pre-status req=%p", req);
                UpdateStatus(req, (int32_t *)slice_status);
                ENC_KMSG("EndEncoding final post-status req=%p status=%d rc_bytes=%d",
                         req, READ_S32(req, 0xb94), READ_S32(slice_status, 4));
                handleOutputTraces(arg1, req, (uint8_t)(READ_U8(arg1, 0x3c) - 1U),
                                   (READ_S32(req, 0xa6c) >= 2) ? 5 : 7);
                ENC_KMSG("EndEncoding final pre-terminate req=%p", req);
                TerminateRequest(arg1, req, (int32_t *)slice_status);
                ENC_KMSG("EndEncoding final post-terminate req=%p", req);
                ENC_KMSG("EndEncoding final pre-release req=%p", req);
                ReleaseWorkBuffers(arg1, req);
                ENC_KMSG("EndEncoding final post-release req=%p", req);

                ENC_KMSG("EndEncoding final pre-pop req=%p", req);
                evt = (int32_t *)(intptr_t)EndRequestsBuffer_Pop((uint8_t *)arg1 + 0x1604);
                evt[0] = (int32_t)(intptr_t)READ_PTR(req, 0x318);
                i = &req[0xaf8 / 4];
                dst = evt + 2;
                while ((uint8_t *)i != (uint8_t *)req + 0xbd8) {
                    dst[0] = i[0];
                    dst[1] = i[1];
                    dst[2] = i[2];
                    dst[3] = i[3];
                    i += 4;
                    dst += 4;
                }
                ENC_KMSG("EndEncoding final evt req=%p cb=%p arg1=0x%x arg2=0x%x arg3=0x%x arg4=0x%x stream=%p evt=%p",
                         req, (void *)(intptr_t)READ_S32(req, 0x10),
                         READ_S32(req, 0x14), READ_S32(req, 0x18),
                         READ_S32(req, 0x1c), READ_S32(req, 0x48),
                         READ_PTR(req, 0x318), evt);
                ENC_KMSG("EndEncoding final pre-queue req=%p evt=%p", req, evt);
                StaticFifo_Queue((StaticFifoCompat *)((uint8_t *)arg1 + 0x12b24), (int32_t)(intptr_t)evt);
                WRITE_S32(arg1, 0x35b4, READ_S32(arg1, 0x35b4) + 1);
                done_cb = READ_S32(arg1, 0x1aa8);
                Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
                ENC_KMSG("EndEncoding non-gate queued-output req=%p evt=%p phase=%d", req, evt, req_phase_next);
                if (done_cb != 0) {
                    ((void (*)(void *, void *))(intptr_t)done_cb)(READ_PTR(arg1, 0x1aac), (uint8_t *)req + 0x84c);
                }
                return 1;
            }
        }
    }
    Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
    return 0;
}

int32_t AL_EncChannel_SetTraceCallBack(void *arg1, int32_t arg2, int32_t arg3)
{
    WRITE_S32(arg1, CH_TRACE_CB_OFF, arg2);
    WRITE_S32(arg1, CH_TRACE_CTX_OFF, arg3);
    return 0;
}
