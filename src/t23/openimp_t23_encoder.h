#ifndef OPENIMP_T23_ENCODER_H
#define OPENIMP_T23_ENCODER_H

#include <stdint.h>

#include <imp/imp_encoder.h>

/* What src/t40/openimp_p2_encoder.c shares with the T23 encoder extras. */
typedef struct {
    int created;
    int receiving;
    int codec_type;                 /* IMP_ENC_TYPE_* */
    void *codec;                    /* AL codec, NULL until created */
    IMPEncoderCHNAttr *attr;        /* the channel's live attribute */
} OpenIMPT23P2View;

typedef int (*OpenIMPT23P2Fn)(OpenIMPT23P2View *view, void *arg);

/* Run fn with the channel locked; -1 for an invalid channel. */
int openimp_t23_p2_call(int channel, OpenIMPT23P2Fn fn, void *arg);

/* Forward a channel setting to the running Helix encoder (the OEM
 * i264e_set_param behind each IMP_Encoder_Set* call).  No-ops for
 * channels that are not H.264 or not created; -1 when the encoder
 * refused the value. */
int openimp_t23_enc_push_rc(void *codec, int codec_type,
                            const IMPEncoderAttrRcMode *mode);
int openimp_t23_enc_push_fps(void *codec, int codec_type,
                             const IMPEncoderFrmRate *rate);
int openimp_t23_enc_push_gop(void *codec, int codec_type, int gop);
int openimp_t23_enc_push_color2grey(void *codec, int codec_type,
                                    const IMPEncoderColor2GreyCfg *cfg);
int openimp_t23_enc_push_roi(void *codec, int codec_type,
                             const IMPEncoderROICfg *cfg);
int openimp_t23_enc_push_denoise(void *codec, int codec_type,
                                 const IMPEncoderAttrDenoise *cfg,
                                 int created_enable);
int openimp_t23_enc_push_mbrc(void *codec, int codec_type, int enable);
int openimp_t23_enc_push_superframe(void *codec, int codec_type,
                                    const IMPEncoderSuperFrmCfg *cfg);
int openimp_t23_enc_push_h264trans(void *codec, int codec_type,
                                   const IMPEncoderH264TransCfg *cfg);
int openimp_t23_enc_push_qpgmode(void *codec, int codec_type,
                                 const IMPEncoderQpgMode *mode);
/* OEM frame-loss threshold in bytes (Setframelossthd/MaxPictureSize). */
int openimp_t23_enc_set_lossthd(int channel, uint32_t bytes);

#endif
