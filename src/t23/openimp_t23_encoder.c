/* T23 1.3.0 encoder channel extras.
 *
 * In the OEM libimp every IMP_Encoder_Set* that changes coding becomes an
 * i264e_set_param(encoder, id, value) on the channel's H.264 encoder; here
 * that encoder is the OEM YUV session inside openimp-t23-helixd, so the
 * value is converted to the OEM i264e layout and sent through the Helix
 * bridge, which also remembers it for worker (re)starts.  Values the OEM
 * channel thread uses per picture (initial QP, GDR) are sent as frame
 * controls.  As in the OEM, these setters do nothing for JPEG channels.
 *
 * Settings that the OEM keeps on the channel for its own buffer and frame
 * scheduling logic - frame reuse mode, frame-loss threshold, fisheye
 * flag, pool sizes, multi-section mode, read-buffer sharing, pad frames -
 * are validated and stored like the OEM does, but have no effect on this
 * stack (documented per function).
 *
 * OPENIMP_T23_ENC_PARAMS=0 keeps the settings local (nothing is sent to
 * the encoder), for comparison on a device. */

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <imp/imp_encoder.h>

#include "codec.h"
#include "imp_log_int.h"
#include "t23/openimp_t23_encoder.h"
#include "t23/openimp_t23_helix_bridge.h"

#define T23_ENC_CHANNELS 8

extern T23HelixBridge *OpenIMP_T23_CodecBridge(void *codec);

/* ---- per-channel state the P2 channel does not keep ------------------- */

typedef struct {
    IMPEncoderCropCfg crop;
    IMPEncoderAttrFrmUsed frm_used;
    uint32_t loss_threshold;        /* bytes */
    int fisheye;
    int rd_buf_share;
    int change_ref;
    int pad_enable;
    uint8_t pad_attr[32];
    IMPEncoderGDRCfg gdr;
    T23HelixFrameCtl frame_ctl;
} T23EncExtra;

static pthread_mutex_t extra_lock = PTHREAD_MUTEX_INITIALIZER;
static T23EncExtra extras[T23_ENC_CHANNELS];
static int pool_size;
static int multi_section_mode;
static int multi_section_size;
static int multi_section_count;

static int valid_channel(int channel)
{
    return channel >= 0 && channel < T23_ENC_CHANNELS;
}

static int params_enabled(void)
{
    static int state;

    if (!state) {
        const char *v = getenv("OPENIMP_T23_ENC_PARAMS");

        state = v && v[0] == '0' && v[1] == '\0' ? -1 : 1;
        if (state < 0)
            IMP_LOG_INFO("Encoder", "T23: encoder parameters stay local "
                         "(OPENIMP_T23_ENC_PARAMS=0)");
    }
    return state > 0;
}

/* ---- pushing values into the Helix encoder ----------------------------- */

static T23HelixBridge *h264_bridge(void *codec, int codec_type)
{
    if (!codec || codec_type != IMP_ENC_TYPE_AVC || !params_enabled())
        return NULL;
    return OpenIMP_T23_CodecBridge(codec);
}

static int push(void *codec, int codec_type, uint32_t id, uint32_t key,
                const void *data, uint32_t size)
{
    T23HelixBridge *bridge = h264_bridge(codec, codec_type);

    if (!bridge)
        return 0;
    return OpenIMP_T23_HelixSetParam(bridge, id, key, data, size) == 0
               ? 0 : -1;
}

/* OEM IMP_Encoder_SetChnAttrRcMode: IMPEncoderAttrRcMode -> 56-byte i264e
 * rate-control parameter. */
typedef struct {
    uint32_t reserved;
    uint32_t mode;
    uint32_t fixed_qp;
    uint32_t max_qp;
    uint32_t min_qp;
    uint32_t cbr_bitrate;
    int32_t bias_level;
    uint32_t frame_qp_step;
    uint32_t gop_qp_step;
    uint8_t adaptive_mode;
    uint8_t gop_relation;
    uint8_t pad[2];
    uint32_t static_time;
    uint32_t max_bitrate;
    uint32_t change_pos;
    uint32_t quality_level;
} T23I264eRc;
_Static_assert(sizeof(T23I264eRc) == 56, "i264e rate control parameter");

static int rc_to_i264e(const IMPEncoderAttrRcMode *mode, T23I264eRc *rc)
{
    memset(rc, 0, sizeof(*rc));
    rc->mode = (uint32_t)mode->rcMode;
    switch (mode->rcMode) {
    case IMP_ENC_RC_MODE_FIXQP:
        rc->fixed_qp = mode->attrH264FixQp.qp;
        return 0;
    case IMP_ENC_RC_MODE_CBR:
        rc->max_qp = mode->attrH264Cbr.maxQp;
        rc->min_qp = mode->attrH264Cbr.minQp;
        rc->cbr_bitrate = mode->attrH264Cbr.outBitRate;
        rc->bias_level = mode->attrH264Cbr.iBiasLvl;
        rc->frame_qp_step = mode->attrH264Cbr.frmQPStep;
        rc->gop_qp_step = mode->attrH264Cbr.gopQPStep;
        rc->adaptive_mode = mode->attrH264Cbr.adaptiveMode;
        rc->gop_relation = mode->attrH264Cbr.gopRelation;
        return 0;
    case IMP_ENC_RC_MODE_VBR:
    case IMP_ENC_RC_MODE_SMART:
        rc->max_qp = mode->attrH264Vbr.maxQp;
        rc->min_qp = mode->attrH264Vbr.minQp;
        rc->static_time = mode->attrH264Vbr.staticTime;
        rc->max_bitrate = mode->attrH264Vbr.maxBitRate;
        rc->bias_level = mode->attrH264Vbr.iBiasLvl;
        rc->change_pos = mode->attrH264Vbr.changePos;
        rc->quality_level = mode->attrH264Vbr.qualityLvl;
        rc->frame_qp_step = mode->attrH264Vbr.frmQPStep;
        rc->gop_qp_step = mode->attrH264Vbr.gopQPStep;
        rc->gop_relation = mode->attrH264Vbr.gopRelation;
        return 0;
    default:
        return -1;
    }
}

int openimp_t23_enc_push_rc(void *codec, int codec_type,
                            const IMPEncoderAttrRcMode *mode)
{
    T23I264eRc rc;

    if (!mode || rc_to_i264e(mode, &rc) != 0)
        return codec_type == IMP_ENC_TYPE_AVC && codec ? -1 : 0;
    return push(codec, codec_type, T23_I264E_RC, 0, &rc, sizeof(rc));
}

int openimp_t23_enc_push_fps(void *codec, int codec_type,
                             const IMPEncoderFrmRate *rate)
{
    uint32_t fps[2];

    if (!rate)
        return 0;
    fps[0] = rate->frmRateNum;
    fps[1] = rate->frmRateDen;
    return push(codec, codec_type, T23_I264E_FPS, 0, fps, sizeof(fps));
}

int openimp_t23_enc_push_gop(void *codec, int codec_type, int gop)
{
    int32_t value = gop;

    return push(codec, codec_type, T23_I264E_GOP, 0, &value, sizeof(value));
}

int openimp_t23_enc_push_color2grey(void *codec, int codec_type,
                                    const IMPEncoderColor2GreyCfg *cfg)
{
    uint32_t enable = cfg && cfg->enable ? 1u : 0u;

    return push(codec, codec_type, T23_I264E_COLOR2GREY, 0, &enable,
                sizeof(enable));
}

/* OEM: corners in macroblocks, sorted, signed /16 */
static uint8_t mb_of(int pixel)
{
    return (uint8_t)(pixel / 16);
}

int openimp_t23_enc_push_roi(void *codec, int codec_type,
                             const IMPEncoderROICfg *cfg)
{
    /* IMPRect here is the OEM {p0.x, p0.y, p1.x, p1.y} in x/y/w/h names */
    int x0 = cfg->rect.x, y0 = cfg->rect.y;
    int x1 = cfg->rect.width, y1 = cfg->rect.height;
    uint8_t roi[12];

    memset(roi, 0, sizeof(roi));
    memcpy(roi, &cfg->u32Index, sizeof(uint32_t));
    roi[4] = (uint8_t)cfg->bEnable;
    roi[5] = (uint8_t)cfg->bRelatedQp;
    roi[6] = (uint8_t)cfg->s32Qp;
    roi[7] = mb_of(x0 < x1 ? x0 : x1);
    roi[8] = mb_of(x0 < x1 ? x1 : x0);
    roi[9] = mb_of(y0 < y1 ? y0 : y1);
    roi[10] = mb_of(y0 < y1 ? y1 : y0);
    return push(codec, codec_type, T23_I264E_ROI, cfg->u32Index, roi,
                sizeof(roi));
}

int openimp_t23_enc_push_denoise(void *codec, int codec_type,
                                 const IMPEncoderAttrDenoise *cfg,
                                 int created_enable)
{
    int32_t dn[3];

    dn[0] = created_enable ? cfg->dnType : 0;
    dn[1] = cfg->dnIQp;
    dn[2] = cfg->dnPQp;
    return push(codec, codec_type, T23_I264E_DENOISE, 0, dn, sizeof(dn));
}

int openimp_t23_enc_push_mbrc(void *codec, int codec_type, int enable)
{
    uint32_t value = enable ? 1u : 0u;

    return push(codec, codec_type, T23_I264E_MBRC, 0, &value, sizeof(value));
}

int openimp_t23_enc_push_superframe(void *codec, int codec_type,
                                    const IMPEncoderSuperFrmCfg *cfg)
{
    uint32_t value[5];

    value[0] = (uint32_t)cfg->superFrmMode;
    value[1] = cfg->superIFrmBitsThr;
    value[2] = cfg->superPFrmBitsThr;
    value[3] = cfg->superBFrmBitsThr;
    value[4] = (uint32_t)cfg->rcPriority;
    return push(codec, codec_type, T23_I264E_SUPER_FRAME, 0, value,
                sizeof(value));
}

int openimp_t23_enc_push_h264trans(void *codec, int codec_type,
                                   const IMPEncoderH264TransCfg *cfg)
{
    int32_t value = cfg->chroma_qp_index_offset;

    return push(codec, codec_type, T23_I264E_H264_TRANS, 0, &value,
                sizeof(value));
}

int openimp_t23_enc_push_qpgmode(void *codec, int codec_type,
                                 const IMPEncoderQpgMode *mode)
{
    int32_t value = (int32_t)*mode;

    return push(codec, codec_type, T23_I264E_QPG_MODE, 0, &value,
                sizeof(value));
}

/* ---- channel callbacks ---------------------------------------------------
 * Every new entry point validates like the OEM, then runs under the P2
 * channel lock (openimp_t23_p2_call) so the codec stays alive. */

typedef struct {
    int channel;
    const void *in;
    void *out;
    int value;
} T23Call;

static int is_h264(const OpenIMPT23P2View *v)
{
    return v->created && v->codec && v->codec_type == IMP_ENC_TYPE_AVC;
}

static int call(int channel, OpenIMPT23P2Fn fn, const void *in, void *out,
                int value)
{
    T23Call c;

    c.channel = channel;
    c.in = in;
    c.out = out;
    c.value = value;
    return openimp_t23_p2_call(channel, fn, &c);
}

static int get_param(OpenIMPT23P2View *v, uint32_t id, uint32_t key,
                     void *data, uint32_t size)
{
    T23HelixBridge *bridge = h264_bridge(v->codec, v->codec_type);

    return bridge ? OpenIMP_T23_HelixGetParam(bridge, id, key, data, size)
                  : 1;
}

/* crop: OEM i264e order {enable, x, w, y, h} */
static int set_crop_cb(OpenIMPT23P2View *v, void *arg)
{
    T23Call *c = arg;
    const IMPEncoderCropCfg *cfg = c->in;
    uint32_t value[5];

    if (!v->created || cfg->x + cfg->w > v->attr->encAttr.picWidth ||
        cfg->y + cfg->h > v->attr->encAttr.picHeight)
        return -1;
    if (!is_h264(v))
        return 0;
    value[0] = cfg->enable ? 1u : 0u;
    value[1] = cfg->x;
    value[2] = cfg->w;
    value[3] = cfg->y;
    value[4] = cfg->h;
    if (push(v->codec, v->codec_type, T23_I264E_CROP, 0, value,
             sizeof(value)) != 0)
        return -1;
    pthread_mutex_lock(&extra_lock);
    extras[c->channel].crop = *cfg;
    pthread_mutex_unlock(&extra_lock);
    return 0;
}

int IMP_Encoder_SetChnCrop(int channel, const IMPEncoderCropCfg *cfg)
{
    if (!valid_channel(channel) || !cfg || ((cfg->x | cfg->y | cfg->w |
                                             cfg->h) & 1u))
        return -1;
    return call(channel, set_crop_cb, cfg, NULL, 0);
}

static int get_crop_cb(OpenIMPT23P2View *v, void *arg)
{
    T23Call *c = arg;
    IMPEncoderCropCfg *cfg = c->out;
    uint32_t value[5];
    int ret;

    memset(cfg, 0, sizeof(*cfg));
    if (!is_h264(v))
        return 0;
    memset(value, 0, sizeof(value));
    ret = get_param(v, T23_I264E_CROP, 0, value, sizeof(value));
    if (ret < 0)
        return -1;
    if (ret > 0) {
        pthread_mutex_lock(&extra_lock);
        *cfg = extras[c->channel].crop;
        pthread_mutex_unlock(&extra_lock);
        return 0;
    }
    cfg->enable = value[0] != 0;
    cfg->x = value[1];
    cfg->w = value[2];
    cfg->y = value[3];
    cfg->h = value[4];
    return 0;
}

int IMP_Encoder_GetChnCrop(int channel, IMPEncoderCropCfg *cfg)
{
    if (!valid_channel(channel) || !cfg)
        return -1;
    return call(channel, get_crop_cb, NULL, cfg, 0);
}

static int set_hskip_cb(OpenIMPT23P2View *v, void *arg)
{
    T23Call *c = arg;
    const IMPEncoderAttrHSkip *attr = c->in;

    if (is_h264(v)) {
        if (attr->skipType > v->attr->rcAttr.attrHSkip.maxHSkipType)
            return -1;
        if (push(v->codec, v->codec_type, T23_I264E_HSKIP, 0, attr,
                 sizeof(*attr)) != 0)
            return -1;
        /* native encoder: the IDR period in GOPs (maxSameSceneCnt for the
         * skip types N1X and H1M, as CreateChn); the OEM
         * i264e_reconfig_hskip_set -> i264e_idr_reconfig codes an IDR and
         * restarts the rate control, so does the Helix encoder on the
         * change (OpenIMP_T30_HelixReconfigure) */
        if (v->codec) {
            int ok = attr->skipType == IMP_Encoder_STYPE_N1X ||
                     attr->skipType == IMP_Encoder_STYPE_H1M_FALSE ||
                     attr->skipType == IMP_Encoder_STYPE_H1M_TRUE;

            (void)AL_Codec_Encode_SetSameSceneGops(
                v->codec, ok && attr->maxSameSceneCnt > 0
                              ? (uint32_t)attr->maxSameSceneCnt : 0u);
        }
    }
    /* OEM: the channel attribute follows (also for idle channels) */
    v->attr->rcAttr.attrHSkip.hSkipAttr = *attr;
    return 0;
}

int IMP_Encoder_SetChnHSkip(int channel, const IMPEncoderAttrHSkip *attr)
{
    if (!valid_channel(channel) || !attr)
        return -1;
    return call(channel, set_hskip_cb, attr, NULL, 0);
}

static int get_hskip_cb(OpenIMPT23P2View *v, void *arg)
{
    T23Call *c = arg;
    IMPEncoderAttrHSkip *attr = c->out;
    int ret;

    memset(attr, 0, sizeof(*attr));
    if (!is_h264(v))
        return 0;
    ret = get_param(v, T23_I264E_HSKIP, 0, attr, sizeof(*attr));
    if (ret > 0)
        *attr = v->attr->rcAttr.attrHSkip.hSkipAttr;
    return ret < 0 ? -1 : 0;
}

int IMP_Encoder_GetChnHSkip(int channel, IMPEncoderAttrHSkip *attr)
{
    if (!valid_channel(channel) || !attr)
        return -1;
    return call(channel, get_hskip_cb, NULL, attr, 0);
}

static int black_enhance_cb(OpenIMPT23P2View *v, void *arg)
{
    T23Call *c = arg;
    uint32_t value = c->value ? 1u : 0u;

    if (!is_h264(v))
        return 0;
    if (push(v->codec, v->codec_type, T23_I264E_BLACK_ENHANCE, 0, &value,
             sizeof(value)) != 0)
        return -1;
    v->attr->rcAttr.attrHSkip.hSkipAttr.bBlackEnhance = (int)value;
    return 0;
}

int IMP_Encoder_SetChnHSkipBlackEnhance(int channel, const int enable)
{
    if (!valid_channel(channel))
        return -1;
    return call(channel, black_enhance_cb, NULL, NULL, enable);
}

static int change_ref_cb(OpenIMPT23P2View *v, void *arg)
{
    T23Call *c = arg;
    uint32_t value = c->value ? 1u : 0u;

    if (!v->created)
        return -1;
    if (!is_h264(v))
        return 0;
    if (push(v->codec, v->codec_type, T23_I264E_CHANGE_REF, 0, &value,
             sizeof(value)) != 0)
        return -1;
    pthread_mutex_lock(&extra_lock);
    extras[c->channel].change_ref = (int)value;
    pthread_mutex_unlock(&extra_lock);
    return 0;
}

int IMP_Encoder_SetChangeRef(int channel, int enable)
{
    if (!valid_channel(channel))
        return -1;
    return call(channel, change_ref_cb, NULL, NULL, enable);
}

static int get_change_ref_cb(OpenIMPT23P2View *v, void *arg)
{
    T23Call *c = arg;

    /* OEM 1.3.0 quirk kept as is: a created channel reports an error
     * (its getter has no i264e read-back); an idle one reports 0. */
    if (v->created)
        return -1;
    *(int *)c->out = 0;
    return 0;
}

int IMP_Encoder_GetChangeRef(int channel, int *enable)
{
    if (!valid_channel(channel) || !enable)
        return -1;
    return call(channel, get_change_ref_cb, NULL, enable, 0);
}

/* OEM prototype (not in the 1.3.0 header): the trigger level travels as a
 * raw 32-bit word (float bits in the OEM's own caller) plus a mode word. */
static int rc_trig_cb(OpenIMPT23P2View *v, void *arg)
{
    const uint32_t *value = ((T23Call *)arg)->in;

    if (!is_h264(v))
        return 0;
    return push(v->codec, v->codec_type, T23_I264E_RC_TRIG, 0, value,
                2 * sizeof(uint32_t));
}

int IMP_Encoder_SetChnRcTrigLevel(int channel, uint32_t level, int mode)
{
    uint32_t value[2];

    if (!valid_channel(channel))
        return -1;
    value[0] = level;
    value[1] = (uint32_t)mode;
    return call(channel, rc_trig_cb, value, NULL, 0);
}

static int gop_size_cb(OpenIMPT23P2View *v, void *arg)
{
    IMPEncoderGOPSizeCfg *cfg = ((T23Call *)arg)->out;
    int32_t gop = 0;
    int ret;

    if (!is_h264(v))
        return 0;
    ret = get_param(v, T23_I264E_GOP, 0, &gop, sizeof(gop));
    if (ret < 0)
        return -1;
    cfg->gopsize = ret == 0 ? gop : (int)v->attr->rcAttr.maxGop;
    return 0;
}

int IMP_Encoder_GetGOPSize(int channel, IMPEncoderGOPSizeCfg *cfg)
{
    if (!valid_channel(channel) || !cfg)
        return -1;
    return call(channel, gop_size_cb, NULL, cfg, 0);
}

/* ---- picture controls: initial QP and GDR ------------------------------- */

static int send_frame_ctl(OpenIMPT23P2View *v, int channel)
{
    T23HelixBridge *bridge = h264_bridge(v->codec, v->codec_type);
    T23HelixFrameCtl ctl;

    pthread_mutex_lock(&extra_lock);
    ctl = extras[channel].frame_ctl;
    extras[channel].frame_ctl.gdr_request = 0;
    pthread_mutex_unlock(&extra_lock);
    if (!bridge)
        return 0;
    return OpenIMP_T23_HelixSetFrameCtl(bridge, &ctl) == 0 ? 0 : -1;
}

static int init_qp_cb(OpenIMPT23P2View *v, void *arg)
{
    T23Call *c = arg;
    T23HelixBridge *bridge;

    if (!v->created)
        return -1;
    /* OEM: only before the first frame was encoded */
    bridge = h264_bridge(v->codec, v->codec_type);
    if (bridge && bridge->worker_pid > 0)
        return -1;
    pthread_mutex_lock(&extra_lock);
    extras[c->channel].frame_ctl.init_qp = c->value;
    pthread_mutex_unlock(&extra_lock);
    return send_frame_ctl(v, c->channel);
}

int IMP_Encoder_SetChnInitQP(int channel, uint32_t qp)
{
    if (!valid_channel(channel) || qp < 1u || qp > 51u)
        return -1;
    return call(channel, init_qp_cb, NULL, NULL, (int)qp);
}

static int set_gdr_cb(OpenIMPT23P2View *v, void *arg)
{
    T23Call *c = arg;
    const IMPEncoderGDRCfg *cfg = c->in;
    T23EncExtra *e = &extras[c->channel];

    if (!v->created)
        return -1;
    pthread_mutex_lock(&extra_lock);
    if (cfg->enable) {
        e->gdr.enable = true;
        e->gdr.gdrCycle = cfg->gdrCycle >= 3 && cfg->gdrCycle < 0x10000
                              ? cfg->gdrCycle : 120;
        e->gdr.gdrFrames = cfg->gdrFrames >= 2 && cfg->gdrFrames <= 10
                               ? cfg->gdrFrames : 6;
    } else {
        /* (the OEM 1.3.0 ignores disable; turning GDR off is honoured) */
        e->gdr.enable = false;
    }
    e->frame_ctl.gdr_enable = e->gdr.enable;
    e->frame_ctl.gdr_cycle = e->gdr.gdrCycle;
    e->frame_ctl.gdr_frames = e->gdr.gdrFrames;
    pthread_mutex_unlock(&extra_lock);
    return send_frame_ctl(v, c->channel);
}

int IMP_Encoder_SetGDRCfg(int channel, const IMPEncoderGDRCfg *cfg)
{
    if (!valid_channel(channel) || !cfg)
        return -1;
    return call(channel, set_gdr_cb, cfg, NULL, 0);
}

static int get_gdr_cb(OpenIMPT23P2View *v, void *arg)
{
    T23Call *c = arg;

    if (!v->created)
        return -1;
    if (c->out) {
        pthread_mutex_lock(&extra_lock);
        *(IMPEncoderGDRCfg *)c->out = extras[c->channel].gdr;
        pthread_mutex_unlock(&extra_lock);
    }
    return 0;
}

int IMP_Encoder_GetGDRCfg(int channel, IMPEncoderGDRCfg *cfg)
{
    if (!valid_channel(channel))
        return -1;
    return call(channel, get_gdr_cb, NULL, cfg, 0);
}

static int request_gdr_cb(OpenIMPT23P2View *v, void *arg)
{
    T23Call *c = arg;

    if (!v->created)
        return -1;
    pthread_mutex_lock(&extra_lock);
    extras[c->channel].frame_ctl.gdr_request = 1;
    extras[c->channel].frame_ctl.gdr_frames = c->value;
    extras[c->channel].gdr.gdrFrames = c->value;
    pthread_mutex_unlock(&extra_lock);
    return send_frame_ctl(v, c->channel);
}

int IMP_Encoder_RequestGDR(int channel, int frames)
{
    if (!valid_channel(channel) || frames < 2 || frames > 10)
        return -1;
    return call(channel, request_gdr_cb, NULL, NULL, frames);
}

/* ---- channel bookkeeping (stored like the OEM, no effect here) --------- */

/* The OEM channel thread reuses or skips input frames by this mode when
 * the source is slower than the encoder; OpenIMP paces every channel at
 * its frame rate instead, so the mode is only kept. */
int IMP_Encoder_SetChnFrmUsedMode(int channel,
                                  const IMPEncoderAttrFrmUsed *attr)
{
    if (!valid_channel(channel) || !attr)
        return -1;
    pthread_mutex_lock(&extra_lock);
    extras[channel].frm_used = *attr;
    pthread_mutex_unlock(&extra_lock);
    return 0;
}

int IMP_Encoder_GetChnFrmUsedMode(int channel, IMPEncoderAttrFrmUsed *attr)
{
    if (!valid_channel(channel) || !attr)
        return -1;
    pthread_mutex_lock(&extra_lock);
    *attr = extras[channel].frm_used;
    pthread_mutex_unlock(&extra_lock);
    return 0;
}

static int created_cb(OpenIMPT23P2View *v, void *arg)
{
    (void)arg;
    return v->created ? 0 : -1;
}

/* The OEM compares each encoded frame with this threshold (bytes) to
 * re-encode oversized frames at a higher QP; the Helix session has no such
 * hook, so the threshold is kept for the getters. */
int openimp_t23_enc_set_lossthd(int channel, uint32_t bytes)
{
    if (!valid_channel(channel) ||
        openimp_t23_p2_call(channel, created_cb, NULL) != 0)
        return -1;
    pthread_mutex_lock(&extra_lock);
    extras[channel].loss_threshold = bytes;
    pthread_mutex_unlock(&extra_lock);
    return 0;
}

int IMP_Encoder_Setframelossthd(int channel, uint32_t threshold)
{
    return openimp_t23_enc_set_lossthd(channel, threshold);
}

int IMP_Encoder_Getframelossthd(int channel, uint32_t *threshold)
{
    if (!valid_channel(channel) || !threshold ||
        openimp_t23_p2_call(channel, created_cb, NULL) != 0)
        return -1;
    pthread_mutex_lock(&extra_lock);
    *threshold = extras[channel].loss_threshold;
    pthread_mutex_unlock(&extra_lock);
    return 0;
}

int IMP_Encoder_GetChnMaxPictureSize(int channel, uint32_t *maximum_i,
                                     uint32_t *maximum_p)
{
    uint32_t threshold;

    if (!maximum_i || !maximum_p ||
        IMP_Encoder_Getframelossthd(channel, &threshold) != 0)
        return -1;
    /* OEM: one shared threshold, bytes -> kbit for both */
    *maximum_i = threshold / 128u;
    *maximum_p = threshold / 128u;
    return 0;
}

static int not_created_cb(OpenIMPT23P2View *v, void *arg)
{
    (void)arg;
    return v->created ? -1 : 0;
}

/* OEM: a creation-time i264e option (fisheye lens motion search); the OEM
 * YUV session the Helix worker uses has no way to pass it, so it is kept
 * for the getter only. */
int IMP_Encoder_SetFisheyeEnableStatus(int channel, int enable)
{
    if (!valid_channel(channel) ||
        openimp_t23_p2_call(channel, not_created_cb, NULL) != 0)
        return -1;
    pthread_mutex_lock(&extra_lock);
    extras[channel].fisheye = enable != 0;
    pthread_mutex_unlock(&extra_lock);
    return 0;
}

int IMP_Encoder_GetFisheyeEnableStatus(int channel, int *enable)
{
    if (!valid_channel(channel) || !enable)
        return -1;
    pthread_mutex_lock(&extra_lock);
    *enable = extras[channel].fisheye;
    pthread_mutex_unlock(&extra_lock);
    return 0;
}

/* OEM: before creation; CreateChn turns it into i264e param[52] (default
 * 1): one shared reference/reconstruction ring per channel (hwicodec cfg
 * +0x2c -> BUF_SHARE_CFG) and the rate controller re-encodes only IDR
 * pictures.  Kept only: the native Helix encoder follows
 * OPENIMP_REF_SHARE (unset = on, as the OEM default). */
int IMP_Encoder_SetRdBufShare(int channel, int enable)
{
    if (!valid_channel(channel) ||
        openimp_t23_p2_call(channel, not_created_cb, NULL) != 0)
        return -1;
    pthread_mutex_lock(&extra_lock);
    extras[channel].rd_buf_share = enable != 0;
    pthread_mutex_unlock(&extra_lock);
    return 0;
}

/* OEM: size of the encoder's rmem pool.  The Helix worker allocates its
 * buffers from the OEM VBM in its own process, so this is only checked. */
int IMP_Encoder_SetPoolSize(int size)
{
    if (size <= 0)
        return -1;
    pool_size = size;
    return 0;
}

/* OEM: split the encoder rmem into `cnt` sections of `size` KiB (mode 1/2)
 * before the encoder starts; mode 0 is refused, larger modes fall back to
 * 1.  Kept only, for the same reason as SetPoolSize. */
int IMP_Encoder_SetMultiSectionMode(int mode, int size, int cnt)
{
    if (mode == 0 || size <= 0 || cnt <= 0)
        return -1;
    multi_section_mode = mode < 3 ? mode : 1;
    multi_section_size = size << 10;
    multi_section_count = cnt;
    return 0;
}

/* Undocumented OEM pad-frame input: frames handed in by the application
 * are encoded in place of captured ones.  The attribute is stored like the
 * OEM's; frames are refused - the OEM error path: the release callback gets
 * the frame back and -1 is returned - because the Helix path encodes
 * FrameSource frames only. */
static int pad_attr_cb(OpenIMPT23P2View *v, void *arg)
{
    T23Call *c = arg;

    if (!is_h264(v))
        return 0;
    pthread_mutex_lock(&extra_lock);
    extras[c->channel].pad_enable = c->value != 0;
    if (c->value && c->in)
        memcpy(extras[c->channel].pad_attr, c->in,
               sizeof(extras[c->channel].pad_attr));
    pthread_mutex_unlock(&extra_lock);
    return 0;
}

int IMP_Encoder_SetPadFrameAttr(int channel, int enable, const void *attr)
{
    if (!valid_channel(channel))
        return -1;
    return call(channel, pad_attr_cb, attr, NULL, enable);
}

int IMP_Encoder_PadFrame(int channel, void *frame, void *priv,
                         void (*release)(void *frame, void *priv))
{
    (void)channel;
    if (release)
        release(frame, priv);
    return -1;
}

/* OEM: NCU denoise exists on T30-class SoCs only (cpu id 3..5); on T23
 * both calls log and fail. */
int IMP_Encoder_EnableAllNCUDenoise(void)
{
    return -1;
}

int IMP_Encoder_DisableAllNCUDenoise(void)
{
    return -1;
}
