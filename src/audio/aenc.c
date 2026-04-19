#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "imp/imp_audio.h"

typedef struct {
    int16_t valprev;
    int8_t index;
    int8_t pad;
} AdpcmState;

typedef struct {
    int32_t bitstream;
    int32_t residue;
} BitstreamState;

typedef struct G726State G726State;

struct G726State {
    int32_t rate;
    int32_t code_size;
    int32_t yl;
    int16_t yu;
    int16_t dms;
    int16_t dml;
    int16_t ap;
    int16_t a[2];
    int16_t b[6];
    int16_t pk[2];
    int16_t dq[6];
    int16_t sr[2];
    int8_t td;
    uint8_t pad_39[3];
    BitstreamState bs;
    int32_t (*encoder)(G726State *state, int32_t sample);
    int32_t (*decoder)(G726State *state, char code);
};

typedef struct {
    int32_t registered;
    int32_t type;
    int32_t max_frm_len;
    char name[0x10];
    void *open_encoder;
    void *encoder_frm;
    void *close_encoder;
} AudioEncMethod;

typedef struct {
    int32_t channel;
    int32_t enabled;
    int32_t pad_08;
    int32_t type;
    int32_t buf_size;
    uint32_t *value;
    int32_t *audio_buf;
    int32_t ai_buf_size;
    pthread_mutex_t mutex;
} AudioEncChannel;

int32_t IMP_Log_Get_Option(void); /* forward decl, ported by T<N> later */
int32_t imp_log_fun(int32_t level, int32_t option, int32_t type, ...); /* forward decl, ported by T<N> later */
int32_t _ai_get_buf_size(void); /* forward decl, ported by T<N> later */
int32_t *audio_buf_alloc(int32_t arg1, int32_t arg2, int32_t arg3); /* forward decl, ported by T<N> later */
int32_t audio_buf_free(int32_t *arg1); /* forward decl, ported by T<N> later */
int32_t audio_buf_get_node(int32_t *arg1, int32_t arg2); /* forward decl, ported by T<N> later */
int32_t audio_buf_try_get_node(int32_t *arg1, int32_t arg2); /* forward decl, ported by T<N> later */
int32_t audio_buf_put_node(int32_t *arg1, int32_t arg2, int32_t arg3); /* forward decl, ported by T<N> later */
int32_t audio_buf_node_get_data(void *arg1); /* forward decl, ported by T<N> later */
int32_t g711a_encode(uint8_t *arg1, int16_t *arg2, int32_t arg3); /* forward decl, ported by T<N> later */
int32_t g711u_encode(uint8_t *arg1, int16_t *arg2, int32_t arg3); /* forward decl, ported by T<N> later */
int32_t g726_encode(G726State *arg1, uint8_t *arg2, int16_t *arg3, int32_t arg4); /* forward decl, ported by T<N> later */
G726State *g726_init(G726State *arg1, int32_t arg2); /* forward decl, ported by T<N> later */
int32_t adpcm_coder(int16_t *arg1, char *arg2, int32_t arg3, AdpcmState *arg4); /* forward decl, ported by T<N> later */
void _setLeftPart32(uint32_t leftpart); /* forward decl, ported by T<N> later */
uint32_t _setRightPart32(uint32_t rightpart); /* forward decl, ported by T<N> later */

static G726State g726_state_5044;
static uint32_t tmpCount_5043;
static AdpcmState state_5035;
static AudioEncMethod audioEncMethod[11];
static AudioEncChannel audioEncChn[6];

int32_t _adpcm_open_encoder(void)
{
    return 0;
}

int32_t _adpcm_encode_close(void)
{
    return 0;
}

int32_t _g726_open_encoder(void)
{
    return 0;
}

int32_t _g711a_open_encoder(void)
{
    return 0;
}

int32_t _g711u_open_encoder(void)
{
    return 0;
}

int32_t _g711u_close_encoder(void)
{
    return 0;
}

int32_t _g726_close_encoder(void)
{
    return 0;
}

int32_t _g711a_close_encoder(void)
{
    return 0;
}

int32_t _aenc_pcm2g711a(int32_t arg1, int32_t arg2, int32_t arg3)
{
    uint32_t v0;

    if (arg1 == 0 && arg2 == 0) {
        v0 = (uint32_t)arg3 >> 0x1f;
        if (arg3 == 0) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "aenc",
                "/home/user/git/proj/sdk-lv3/src/imp/audio/aenc.c", 0x30,
                "_aenc_pcm2g711a", "Error, empty data or transmit failed, exit !\n");
            return -1;
        }
    } else {
        v0 = (uint32_t)arg3 >> 0x1f;
    }

    return g711a_encode((uint8_t *)(uintptr_t)arg2, (int16_t *)(uintptr_t)arg1, (int32_t)(v0 + (uint32_t)arg3) >> 1);
}

int32_t _g711a_encode_frm(int32_t arg1, void *arg2, int32_t arg3, int32_t *arg4)
{
    (void)arg1;
    *arg4 = _aenc_pcm2g711a((int32_t)(intptr_t)((IMPAudioFrame *)arg2)->virAddr, arg3, ((IMPAudioFrame *)arg2)->len);
    return 0;
}

int32_t _aenc_pcm2g711u(int32_t arg1, int32_t arg2, int32_t arg3)
{
    uint32_t v0;

    if (arg1 == 0 && arg2 == 0) {
        v0 = (uint32_t)arg3 >> 0x1f;
        if (arg3 == 0) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "aenc",
                "/home/user/git/proj/sdk-lv3/src/imp/audio/aenc.c", 0x3e,
                "_aenc_pcm2g711u", "Error, empty data or transmit failed, exit !\n");
            return -1;
        }
    } else {
        v0 = (uint32_t)arg3 >> 0x1f;
    }

    return g711u_encode((uint8_t *)(uintptr_t)arg2, (int16_t *)(uintptr_t)arg1, (int32_t)(v0 + (uint32_t)arg3) >> 1);
}

int32_t _g711u_encode_frm(int32_t arg1, void *arg2, int32_t arg3, int32_t *arg4)
{
    (void)arg1;
    *arg4 = _aenc_pcm2g711u((int32_t)(intptr_t)((IMPAudioFrame *)arg2)->virAddr, arg3, ((IMPAudioFrame *)arg2)->len);
    return 0;
}

int32_t _aenc_pcm2adpcm(int32_t arg1, int32_t arg2, int32_t arg3)
{
    if (arg1 != 0 || arg2 != 0 || arg3 != 0) {
        return adpcm_coder((int16_t *)(uintptr_t)arg1, (char *)(uintptr_t)arg2,
            (((uint32_t)arg3 >> 0x1f) + arg3) >> 1, &state_5035);
    }

    imp_log_fun(6, IMP_Log_Get_Option(), 2, "aenc",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/aenc.c", 0x4b,
        "_aenc_pcm2adpcm", "Error,empty data or trasmit failed,exit !\n");
    return -1;
}

int32_t _adpcm_encode_frm(int32_t arg1, void *arg2, int32_t arg3, int32_t *arg4)
{
    (void)arg1;
    if (arg2 == 0 && arg3 == 0 && arg4 == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "aenc",
            "/home/user/git/proj/sdk-lv3/src/imp/audio/aenc.c", 0xbc,
            "_adpcm_encode_frm", "Error, empty data or transmit failed, exit !\n");
        return -1;
    }

    *arg4 = _aenc_pcm2adpcm((int32_t)(intptr_t)((IMPAudioFrame *)arg2)->virAddr, arg3, ((IMPAudioFrame *)arg2)->len);
    return 0;
}

int32_t _aenc_pcm2g726(int32_t arg1, int32_t arg2, int32_t arg3)
{
    uint32_t tmpCount_5043_1;

    if (arg1 == 0 && arg2 == 0) {
        tmpCount_5043_1 = tmpCount_5043;
        if (arg3 == 0) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "aenc",
                "/home/user/git/proj/sdk-lv3/src/imp/audio/aenc.c", 0x5a,
                "_aenc_pcm2g726", "Error,empty data or transmit failed,exit!\n");
            return -1;
        }
    } else {
        tmpCount_5043_1 = tmpCount_5043;
    }

    if (tmpCount_5043_1 != 0) {
        return g726_encode(&g726_state_5044, (uint8_t *)(uintptr_t)arg2,
            (int16_t *)(uintptr_t)arg1, (((uint32_t)arg3 >> 0x1f) + arg3) >> 1);
    }

    g726_init(&g726_state_5044, 0x3e80);
    tmpCount_5043 += 1;
    return g726_encode(&g726_state_5044, (uint8_t *)(uintptr_t)arg2,
        (int16_t *)(uintptr_t)arg1, (((uint32_t)arg3 >> 0x1f) + arg3) >> 1);
}

int32_t _g726_encode_frm(int32_t arg1, void *arg2, int32_t arg3, int32_t *arg4)
{
    (void)arg1;
    if (arg2 == 0 && arg3 == 0 && arg4 == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "aenc",
            "/home/user/git/proj/sdk-lv3/src/imp/audio/aenc.c", 0xa8,
            "_g726_encode_frm", "Error,empty data or transmit failed,exit !\n");
        return -1;
    }

    *arg4 = _aenc_pcm2g726((int32_t)(intptr_t)((IMPAudioFrame *)arg2)->virAddr, arg3, ((IMPAudioFrame *)arg2)->len);
    return 0;
}

int32_t IMP_AENC_CreateChn(int32_t arg1, IMPAudioEncChnAttr *arg2)
{
    const char *var_34_1;
    int32_t v0_9;
    int32_t v1_5;

    if (arg1 >= 6) {
        v0_9 = IMP_Log_Get_Option();
        var_34_1 = "Invalid Audio Enc Channel.\n";
        v1_5 = 0xd3;
        imp_log_fun(6, v0_9, 2, "aenc",
            "/home/user/git/proj/sdk-lv3/src/imp/audio/aenc.c",
            v1_5, "IMP_AENC_CreateChn", var_34_1);
        return -1;
    }

    {
        int32_t s2_1 = arg1 << 3;
        int32_t s1_1 = arg1 << 6;
        AudioEncChannel *chan = (AudioEncChannel *)((char *)audioEncChn + (s1_1 - s2_1));

        if (chan->enabled != 1) {
            int32_t a3_1 = arg2->type;
            int32_t a2_2 = arg2->bufSize;
            int32_t v1 = (int32_t)(intptr_t)arg2->value;
            int32_t s5_1;
            int32_t var_30 = 0;
            int32_t s4_1;

            chan->channel = arg1;
            chan->type = a3_1;
            chan->buf_size = a2_2;
            chan->value = (uint32_t *)(intptr_t)v1;
            s5_1 = _ai_get_buf_size();

            if (s5_1 <= 0) {
                s4_1 = s1_1 - s2_1;
                var_30 = s5_1;
                s5_1 = 0x320;
                imp_log_fun(6, IMP_Log_Get_Option(), 2, "aenc",
                    "/home/user/git/proj/sdk-lv3/src/imp/audio/aenc.c", 0xe2,
                    "IMP_AENC_CreateChn", "ai_buf size error %d\n", var_30);
            } else {
                s4_1 = s1_1 - s2_1;
            }

            pthread_mutex_init(&((AudioEncChannel *)((char *)audioEncChn + s4_1))->mutex, 0);
            pthread_mutex_lock(&((AudioEncChannel *)((char *)audioEncChn + s4_1))->mutex);

            if (((AudioEncChannel *)((char *)audioEncChn + s4_1))->audio_buf == 0) {
                int32_t *v0_6 = audio_buf_alloc(((AudioEncChannel *)((char *)audioEncChn + s4_1))->buf_size, s5_1 + 8, 0);
                ((AudioEncChannel *)((char *)audioEncChn + s4_1))->audio_buf = v0_6;
                ((AudioEncChannel *)((char *)audioEncChn + s4_1))->ai_buf_size = s5_1;
                if (v0_6 == 0) {
                    imp_log_fun(6, IMP_Log_Get_Option(), 2, "aenc",
                        "/home/user/git/proj/sdk-lv3/src/imp/audio/aenc.c", 0xeb,
                        "IMP_AENC_CreateChn", "aenc_buf alloc error\n", var_30);
                    pthread_mutex_unlock(&((AudioEncChannel *)((char *)audioEncChn + s4_1))->mutex);
                    return -1;
                }
            }

            pthread_mutex_unlock(&((AudioEncChannel *)((char *)audioEncChn + s4_1))->mutex);
            __builtin_strncpy(audioEncMethod[1].name, "PT_G711A", 9);
            audioEncMethod[1].open_encoder = _g711a_open_encoder;
            audioEncMethod[1].encoder_frm = _g711a_encode_frm;
            __builtin_strncpy(audioEncMethod[2].name, "PT_G", 4);
            audioEncMethod[1].close_encoder = _g711a_close_encoder;
            audioEncMethod[1].max_frm_len = 2;
            audioEncMethod[2].type = 0;
            audioEncMethod[2].open_encoder = _g711u_open_encoder;
            audioEncMethod[2].encoder_frm = _g711u_encode_frm;
            __builtin_strncpy(audioEncMethod[3].name, "PT_G", 4);
            audioEncMethod[2].close_encoder = _g711u_close_encoder;
            audioEncMethod[2].max_frm_len = 3;
            *(int32_t *)audioEncMethod[3].name = 0x363237;
            audioEncMethod[1].type = 1;
            audioEncMethod[3].open_encoder = _g726_open_encoder;
            audioEncMethod[0].registered = 1;
            __builtin_strncpy(&audioEncMethod[2].name[4], "711U", 4);
            audioEncMethod[1].registered = 1;
            audioEncMethod[1].max_frm_len = 0x1000;
            audioEncMethod[2].max_frm_len = 0x1000;
            audioEncMethod[3].max_frm_len = 0x1000;
            audioEncMethod[3].encoder_frm = _g726_encode_frm;
            audioEncMethod[3].close_encoder = _g726_close_encoder;

            {
                int32_t v1_1 = chan->type;
                int32_t a1 = *(int32_t *)((char *)audioEncMethod + v1_1 * 0x28);

                audioEncMethod[5].type = 5;
                __builtin_strncpy(audioEncMethod[5].name, "PT_A", 4);
                audioEncMethod[5].max_frm_len = 0;
                audioEncMethod[5].open_encoder = _adpcm_open_encoder;
                audioEncMethod[2].registered = 1;
                __builtin_strncpy(&audioEncMethod[5].name[4], "DPCM", 4);
                audioEncMethod[4].registered = 1;
                audioEncMethod[5].encoder_frm = _adpcm_encode_frm;
                audioEncMethod[5].max_frm_len = 0x1000;
                audioEncMethod[5].close_encoder = _adpcm_encode_close;

                if (a1 == 0) {
                    imp_log_fun(6, IMP_Log_Get_Option(), 2, "aenc",
                        "/home/user/git/proj/sdk-lv3/src/imp/audio/aenc.c", 0x117,
                        "IMP_AENC_CreateChn", "encoder [%d] is not register\n", chan->type);
                    return -1;
                }

                {
                    int32_t (*t9_1)(void *attr, void *enc) =
                        (int32_t (*)(void *, void *))*(void **)((char *)audioEncMethod + v1_1 * 0x28 + 0x1c);

                    if (t9_1 != 0) {
                        t9_1(0, 0);
                    }
                }

                chan->enabled = 1;
                return 0;
            }
        }

        v0_9 = IMP_Log_Get_Option();
        var_34_1 = "AeChn is already enabled.\n";
        v1_5 = 0xd8;
        imp_log_fun(6, v0_9, 2, "aenc",
            "/home/user/git/proj/sdk-lv3/src/imp/audio/aenc.c",
            v1_5, "IMP_AENC_CreateChn", var_34_1);
        return -1;
    }
}

int32_t IMP_AENC_DestroyChn(int32_t arg1)
{
    if (arg1 >= 6) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "aenc",
            "/home/user/git/proj/sdk-lv3/src/imp/audio/aenc.c", 0x126,
            "IMP_AENC_DestroyChn", "Invalid Audio Enc Channel.\n");
        return -1;
    }

    {
        int32_t s4_1 = arg1 << 3;
        int32_t s0_1 = arg1 << 6;
        AudioEncChannel *chan = (AudioEncChannel *)((char *)audioEncChn + (s0_1 - s4_1));
        int32_t (*t9_1)(void *enc) =
            (int32_t (*)(void *))*(void **)((char *)audioEncMethod + chan->type * 0x28 + 0x24);

        if (t9_1 != 0) {
            t9_1(0);
        }

        {
            int32_t s3_1 = s0_1 - s4_1;
            int32_t *a0_1;
            int32_t result;

            pthread_mutex_lock(&((AudioEncChannel *)((char *)audioEncChn + s3_1))->mutex);
            a0_1 = ((AudioEncChannel *)((char *)audioEncChn + s3_1))->audio_buf;
            ((AudioEncChannel *)((char *)audioEncChn + s3_1))->enabled = 0;
            if (a0_1 != 0) {
                audio_buf_free(a0_1);
            }
            ((AudioEncChannel *)((char *)audioEncChn + (s0_1 - s4_1)))->audio_buf = 0;
            pthread_mutex_unlock(&((AudioEncChannel *)((char *)audioEncChn + s3_1))->mutex);
            result = pthread_mutex_destroy(&((AudioEncChannel *)((char *)audioEncChn + s3_1))->mutex);
            if (result == 0) {
                return 0;
            }

            imp_log_fun(6, IMP_Log_Get_Option(), 2, "aenc",
                "/home/user/git/proj/sdk-lv3/src/imp/audio/aenc.c", 0x137,
                "IMP_AENC_DestroyChn", "err: destroy aenc chn lock\n");
            return result;
        }
    }
}

int32_t IMP_AENC_SendFrame(int32_t arg1, IMPAudioFrame *arg2)
{
    const char *var_2c_1;
    int32_t v0_4;
    int32_t v1_3;

    if (arg1 >= 6) {
        v0_4 = IMP_Log_Get_Option();
        var_2c_1 = "Invalid Audio Enc Channel.\n";
        v1_3 = 0x142;
    } else {
        int32_t s5_1 = arg1 << 3;
        int32_t s3_1 = arg1 << 6;
        int32_t s0_1 = s3_1 - s5_1;

        if (((AudioEncChannel *)((char *)audioEncChn + s0_1))->enabled == 1) {
            int32_t v0_1;

            while (1) {
                pthread_mutex_lock(&((AudioEncChannel *)((char *)audioEncChn + s0_1))->mutex);
                v0_1 = audio_buf_get_node(((AudioEncChannel *)((char *)audioEncChn + s0_1))->audio_buf, 0);
                pthread_mutex_unlock(&((AudioEncChannel *)((char *)audioEncChn + s0_1))->mutex);
                if (v0_1 != 0) {
                    break;
                }
                usleep(0x3e8);
            }

            {
                int32_t v0_2 = audio_buf_node_get_data((void *)(intptr_t)v0_1);
                int32_t (*t9_1)(int32_t arg1_, void *arg2_, int32_t arg3_, int32_t *arg4_) =
                    (int32_t (*)(int32_t, void *, int32_t, int32_t *))*(void **)((char *)audioEncMethod + ((AudioEncChannel *)((char *)audioEncChn + s0_1))->type * 0x28 + 0x20);

                *(int32_t *)((char *)(intptr_t)v0_2 + 4) = ((AudioEncChannel *)((char *)audioEncChn + s0_1))->ai_buf_size;
                if (t9_1 != 0) {
                    t9_1(0, arg2, v0_2 + 8, (int32_t *)((char *)(intptr_t)v0_2 + 4));
                }
                *(int32_t *)(intptr_t)v0_2 = v0_1;
                pthread_mutex_lock(&((AudioEncChannel *)((char *)audioEncChn + s0_1))->mutex);
                audio_buf_put_node(((AudioEncChannel *)((char *)audioEncChn + (s3_1 - s5_1)))->audio_buf, v0_1, 1);
                pthread_mutex_unlock(&((AudioEncChannel *)((char *)audioEncChn + s0_1))->mutex);
                return 0;
            }
        }

        v0_4 = IMP_Log_Get_Option();
        var_2c_1 = "AeChn is not enabled.\n";
        v1_3 = 0x147;
    }

    imp_log_fun(6, v0_4, 2, "aenc",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/aenc.c", v1_3,
        "IMP_AENC_SendFrame", var_2c_1);
    return -1;
}

int32_t IMP_AENC_PollingStream(int32_t arg1, uint32_t arg2)
{
    const char *var_2c_1;
    int32_t v0_7;
    int32_t v1_1;

    if (arg1 >= 6) {
        v0_7 = IMP_Log_Get_Option();
        var_2c_1 = "Invalid Audio Enc Channel.\n";
        v1_1 = 0x165;
    } else {
        int32_t a0_1 = arg1 * 0x38;

        if (((AudioEncChannel *)((char *)audioEncChn + a0_1))->enabled == 1) {
            int32_t s0_1 = 0;

            while (1) {
                int32_t v0_2;

                pthread_mutex_lock(&((AudioEncChannel *)((char *)audioEncChn + a0_1))->mutex);
                v0_2 = audio_buf_try_get_node(((AudioEncChannel *)((char *)audioEncChn + a0_1))->audio_buf, 1);
                pthread_mutex_unlock(&((AudioEncChannel *)((char *)audioEncChn + a0_1))->mutex);
                if (v0_2 != 0) {
                    return 0;
                }
                if ((uint32_t)s0_1 >= arg2) {
                    break;
                }
                s0_1 += 0x14;
                usleep(0x4e20);
            }

            imp_log_fun(6, IMP_Log_Get_Option(), 2, "aenc",
                "/home/user/git/proj/sdk-lv3/src/imp/audio/aenc.c", 0x176,
                "IMP_AENC_PollingStream", "%s %d aenc polling timeout.\n",
                "IMP_AENC_PollingStream", 0x176);
            return -1;
        }

        v0_7 = IMP_Log_Get_Option();
        var_2c_1 = "AeChn is not enabled.\n";
        v1_1 = 0x16a;
    }

    imp_log_fun(6, v0_7, 2, "aenc",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/aenc.c", v1_1,
        "IMP_AENC_PollingStream", var_2c_1);
    return -1;
}

int32_t IMP_AENC_GetStream(int32_t arg1, IMPAudioStream *arg2, IMPBlock arg3)
{
    const char *var_24_1;
    int32_t v0_7;
    int32_t v1_2;

    if (arg1 >= 6) {
        v0_7 = IMP_Log_Get_Option();
        var_24_1 = "Invalid Audio Enc Channel.\n";
        v1_2 = 0x185;
    } else {
        int32_t a0_1 = arg1 * 0x38;

        if (((AudioEncChannel *)((char *)audioEncChn + a0_1))->enabled == 1) {
            while (1) {
                int32_t v0_2;

                pthread_mutex_lock(&((AudioEncChannel *)((char *)audioEncChn + a0_1))->mutex);
                v0_2 = audio_buf_get_node(((AudioEncChannel *)((char *)audioEncChn + a0_1))->audio_buf, 1);
                pthread_mutex_unlock(&((AudioEncChannel *)((char *)audioEncChn + a0_1))->mutex);
                if (v0_2 != 0) {
                    int32_t v0_3 = audio_buf_node_get_data((void *)(intptr_t)v0_2);
                    int32_t v1_1 = *(int32_t *)((char *)(intptr_t)v0_3 + 4);

                    arg2->stream = (uint32_t *)(intptr_t)(v0_3 + 8);
                    arg2->len = v1_1;
                    return 0;
                }
                usleep(0x3e8);
                if (arg3 == 0) {
                    return 0;
                }
            }
        }

        v0_7 = IMP_Log_Get_Option();
        var_24_1 = "AeChn is not enabled.\n";
        v1_2 = 0x18a;
    }

    imp_log_fun(6, v0_7, 2, "aenc",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/aenc.c", v1_2,
        "IMP_AENC_GetStream", var_24_1);
    return -1;
}

int32_t IMP_AENC_ReleaseStream(int32_t arg1, IMPAudioStream *arg2)
{
    const char *var_1c;
    int32_t v0_2;
    int32_t v1_1;

    if (arg1 >= 6) {
        v0_2 = IMP_Log_Get_Option();
        var_1c = "Invalid Audio Enc Channel.\n";
        v1_1 = 0x1a3;
    } else {
        int32_t a0_1 = arg1 * 0x38;

        if (((AudioEncChannel *)((char *)audioEncChn + a0_1))->enabled == 1) {
            int32_t s2 = (int32_t)(intptr_t)arg2->stream;

            pthread_mutex_lock(&((AudioEncChannel *)((char *)audioEncChn + a0_1))->mutex);
            audio_buf_put_node(((AudioEncChannel *)((char *)audioEncChn + a0_1))->audio_buf,
                *(int32_t *)(uintptr_t)(s2 - 8), 0);
            pthread_mutex_unlock(&((AudioEncChannel *)((char *)audioEncChn + a0_1))->mutex);
            return 0;
        }

        v0_2 = IMP_Log_Get_Option();
        var_1c = "AeChn is not enabled.\n";
        v1_1 = 0x1a8;
    }

    imp_log_fun(6, v0_2, 2, "aenc",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/aenc.c", v1_1,
        "IMP_AENC_ReleaseStream", var_1c);
    return -1;
}

int32_t IMP_AENC_RegisterEncoder(int32_t *arg1, IMPAudioEncEncoder *arg2)
{
    int32_t a2 = 6;
    int32_t *v1 = (int32_t *)&audioEncMethod[6];
    int32_t result;
    uint32_t *src = (uint32_t *)(void *)arg2;
    uint32_t *dst;

    while (1) {
        result = *v1;
        v1 = &v1[0xa];
        if (result == 0) {
            break;
        }
        a2 += 1;
        if (a2 == 0xb) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "aenc",
                "/home/user/git/proj/sdk-lv3/src/imp/audio/aenc.c", 0x1bd,
                "IMP_AENC_RegisterEncoder",
                "You have reached the maximum amount of registration.\n");
            return -1;
        }
    }

    {
        int32_t t4 = a2 << 3;
        int32_t t5 = a2 << 5;

        dst = (uint32_t *)((char *)audioEncMethod + t4 + t5);
        _setLeftPart32(src[0]);
        _setLeftPart32(src[1]);
        _setLeftPart32(src[2]);
        _setLeftPart32(src[3]);
        dst[0] = _setRightPart32(src[0]);
        dst[1] = _setRightPart32(src[1]);
        dst[2] = _setRightPart32(src[2]);
        dst[3] = _setRightPart32(src[3]);
        src = &src[4];
        dst = &dst[4];
        while (src != &src[8]) {
            _setLeftPart32(src[0]);
            _setLeftPart32(src[1]);
            _setLeftPart32(src[2]);
            _setLeftPart32(src[3]);
            dst[0] = _setRightPart32(src[0]);
            dst[1] = _setRightPart32(src[1]);
            dst[2] = _setRightPart32(src[2]);
            dst[3] = _setRightPart32(src[3]);
            src = &src[4];
            dst = &dst[4];
        }
        _setLeftPart32(*src);
        *dst = _setRightPart32(*src);
        *(int32_t *)((char *)audioEncMethod + t4 + t5) = 1;
        *arg1 = a2;
        return result;
    }
}

int32_t IMP_AENC_UnRegisterEncoder(int32_t *arg1)
{
    int32_t v0 = *arg1;

    if ((uint32_t)(v0 - 6) >= 6) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "aenc",
            "/home/user/git/proj/sdk-lv3/src/imp/audio/aenc.c", 0x1cb,
            "IMP_AENC_UnRegisterEncoder", "Invalid handle\n");
        return -1;
    }

    *(int32_t *)((char *)audioEncMethod + v0 * 0x28) = 0;
    return 0;
}
