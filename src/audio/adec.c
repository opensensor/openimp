#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <imp/imp_audio.h>

typedef struct {
    int32_t channel;
    int32_t enabled;
    uint8_t mutex_storage[0x1c];
    int32_t payload_type;
    int32_t buf_size;
    int32_t unknown_0x2c;
    int32_t value_ptr;
    int32_t *buf;
    int32_t ao_buf_size;
} AudioDencChannel;

typedef struct {
    int32_t registered;
    int32_t payload_type;
    char name[16];
    uintptr_t open_decoder;
    uintptr_t decode_frm;
    uintptr_t get_frm_info;
    uintptr_t close_decoder;
} AudioDencMethod;

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

int32_t IMP_Log_Get_Option(void); /* forward decl, ported by T<N> later */
void imp_log_fun(int32_t level, int32_t option, int32_t type, ...); /* forward decl, ported by T<N> later */
int32_t _ao_get_buf_size(void); /* forward decl, ported by T<N> later */
int32_t *audio_buf_alloc(int32_t arg1, int32_t arg2, int32_t arg3); /* forward decl, ported by T<N> later */
int32_t audio_buf_free(int32_t *arg1); /* forward decl, ported by T<N> later */
int32_t audio_buf_get_node(int32_t *arg1, int32_t arg2); /* forward decl, ported by T<N> later */
int32_t audio_buf_try_get_node(int32_t *arg1, int32_t arg2); /* forward decl, ported by T<N> later */
int32_t audio_buf_put_node(int32_t *arg1, int32_t arg2, int32_t arg3); /* forward decl, ported by T<N> later */
int32_t audio_buf_node_get_data(void *arg1); /* forward decl, ported by T<N> later */
int32_t audio_buf_clear(int32_t *arg1); /* forward decl, ported by T<N> later */
void _setLeftPart32(uint32_t leftpart); /* forward decl, ported by T<N> later */
uint32_t _setRightPart32(uint32_t rightpart); /* forward decl, ported by T<N> later */
extern char _gp;

int32_t g711a_decode(int16_t *arg1, uint8_t *arg2, int32_t arg3); /* forward decl, ported by T<N> later */
int32_t g711u_decode(int16_t *arg1, uint8_t *arg2, int32_t arg3); /* forward decl, ported by T<N> later */
int32_t adpcm_decoder(char *arg1, int16_t *arg2, int32_t arg3, AdpcmState *arg4); /* forward decl, ported by T<N> later */
G726State *g726_init(G726State *arg1, int32_t arg2); /* forward decl, ported by T<N> later */
int32_t g726_decode(G726State *arg1, int16_t *arg2, uint8_t *arg3, int32_t arg4); /* forward decl, ported by T<N> later */

static AudioDencMethod audioDencMethod[11];
static AudioDencChannel audioDencChn[6];
static AdpcmState state_5028;
static G726State g726_state_5036;
static int32_t tmpCount_5037;

static pthread_mutex_t *audio_denc_lock(AudioDencChannel *channel)
{
    return (pthread_mutex_t *)(void *)&channel->mutex_storage[0];
}

int32_t _adpcm_get_frm_info(void)
{
    return 0;
}

int32_t _adpcm_close_decoder(void)
{
    return 0;
}

int32_t _adpcm_open_decoder(void)
{
    return 0;
}

int32_t _g711a_open_decoder(void)
{
    return 0;
}

int32_t _g711a_get_frm_info(void)
{
    return 0;
}

int32_t _g711u_open_decoder(void)
{
    return 0;
}

int32_t _g711u_get_frm_info(void)
{
    return 0;
}

int32_t _g726_open_decoder(void)
{
    return 0;
}

int32_t _g726_get_frm_info(void)
{
    return 0;
}

int32_t _g726_close_decoder(void)
{
    return 0;
}

int32_t _g711a_close_decoder(void)
{
    return 0;
}

int32_t _g711u_close_decoder(void)
{
    return 0;
}

int32_t _adec_g711a2pcm(uint8_t *arg1, int16_t *arg2, int32_t arg3)
{
    if (arg1 != 0 || arg2 != 0 || arg3 != 0) {
        return g711a_decode(arg2, arg1, arg3);
    }

    imp_log_fun(6, IMP_Log_Get_Option(), 2, "adec",
                "/home/user/git/proj/sdk-lv3/src/imp/audio/adec.c", 0x26, "_adec_g711a2pcm",
                "Error, empty data or transmit failed, exit !\n", &_gp);
    return -1;
}

int32_t _g711a_decode_frm(int32_t arg1, uint8_t *arg2, int32_t arg3, int16_t *arg4, int32_t *arg5)
{
    (void)arg1;
    *arg5 = _adec_g711a2pcm(arg2, arg4, arg3);
    return 0;
}

int32_t _adec_g711u2pcm(uint8_t *arg1, int16_t *arg2, int32_t arg3)
{
    if (arg1 != 0 || arg2 != 0 || arg3 != 0) {
        return g711u_decode(arg2, arg1, arg3);
    }

    imp_log_fun(6, IMP_Log_Get_Option(), 2, "adec",
                "/home/user/git/proj/sdk-lv3/src/imp/audio/adec.c", 0x34, "_adec_g711u2pcm",
                "Error, empty data or transmit failed, exit !\n", &_gp);
    return -1;
}

int32_t _g711u_decode_frm(int32_t arg1, uint8_t *arg2, int32_t arg3, int16_t *arg4, int32_t *arg5)
{
    (void)arg1;
    *arg5 = _adec_g711u2pcm(arg2, arg4, arg3);
    return 0;
}

int32_t _adec_adpcm2pcm(char *arg1, int16_t *arg2, int32_t arg3)
{
    if (arg1 == 0 && arg2 == 0 && arg3 == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "adec",
                    "/home/user/git/proj/sdk-lv3/src/imp/audio/adec.c", 0x40, "_adec_adpcm2pcm",
                    "Error,empty data or transmit failed,exit!\n", &_gp);
        return -1;
    }

    adpcm_decoder(arg1, arg2, arg3 << 1, &state_5028);
    return arg3 << 2;
}

int32_t _adpcm_decode_frm(int32_t arg1, char *arg2, int32_t arg3, int16_t *arg4, int32_t *arg5)
{
    (void)arg1;
    if (arg2 != 0 && arg4 != 0 && arg5 != 0) {
        *arg5 = _adec_adpcm2pcm(arg2, arg4, arg3);
        return 0;
    }

    imp_log_fun(6, IMP_Log_Get_Option(), 2, "adec",
                "/home/user/git/proj/sdk-lv3/src/imp/audio/adec.c", 0xbc, "_adpcm_decode_frm",
                "empty data,adpcm decode failed\n", &_gp);
    return -1;
}

int32_t _adec_g726_2pcm(uint8_t *arg1, int16_t *arg2, int32_t arg3)
{
    if (arg1 == 0 && arg2 == 0 && arg3 == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "adec",
                    "/home/user/git/proj/sdk-lv3/src/imp/audio/adec.c", 0x4e, "_adec_g726_2pcm",
                    "Error,empty data or transmit failed,exit!\n", &_gp);
        return -1;
    }

    if (tmpCount_5037 == 0) {
        g726_init(&g726_state_5036, 0x3e80);
        tmpCount_5037 += 1;
    }

    return g726_decode(&g726_state_5036, arg2, arg1, arg3) << 1;
}

int32_t _g726_decode_frm(int32_t arg1, uint8_t *arg2, int32_t arg3, int16_t *arg4, int32_t *arg5)
{
    (void)arg1;
    if (arg2 == 0 && arg4 == 0 && arg3 == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "adec",
                    "/home/user/git/proj/sdk-lv3/src/imp/audio/adec.c", 0xa3, "_g726_decode_frm",
                    "Error,empty data or transmit failed,exit !\n", &_gp);
        return -1;
    }

    *arg5 = _adec_g726_2pcm(arg2, arg4, arg3);
    return 0;
}

int32_t IMP_ADEC_CreateChn(int32_t arg1, IMPAudioDecChnAttr *arg2)
{
    const char *var_34_1;
    int32_t v0_9;
    int32_t v1_5;

    if (arg1 >= 6) {
        v0_9 = IMP_Log_Get_Option();
        var_34_1 = "Invalid Audio Denc Channel.\n";
        v1_5 = 0xe1;
        imp_log_fun(6, v0_9, 2, "adec", "/home/user/git/proj/sdk-lv3/src/imp/audio/adec.c",
                    v1_5, "IMP_ADEC_CreateChn", var_34_1);
        return -1;
    }

    {
        AudioDencChannel *s3_1 = &audioDencChn[arg1];
        int32_t s5_1;

        if (s3_1->enabled == 1) {
            v0_9 = IMP_Log_Get_Option();
            var_34_1 = "AeChn is already enabled.\n";
            v1_5 = 0xe6;
            imp_log_fun(6, v0_9, 2, "adec", "/home/user/git/proj/sdk-lv3/src/imp/audio/adec.c",
                        v1_5, "IMP_ADEC_CreateChn", var_34_1);
            return -1;
        }

        s3_1->channel = arg1;
        s3_1->payload_type = arg2->type;
        s3_1->buf_size = arg2->bufSize;
        s3_1->value_ptr = (int32_t)(uintptr_t)arg2->value;

        s5_1 = _ao_get_buf_size();
        if (s5_1 <= 0) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "adec",
                        "/home/user/git/proj/sdk-lv3/src/imp/audio/adec.c", 0xf1,
                        "IMP_ADEC_CreateChn", "ao_buf size error %d\n", s5_1);
            return -1;
        }

        pthread_mutex_init(audio_denc_lock(s3_1), 0);
        pthread_mutex_lock(audio_denc_lock(s3_1));
        if (s3_1->buf == 0) {
            int32_t *v0_6 = audio_buf_alloc(s3_1->buf_size, s5_1 + 8, 0);
            s3_1->buf = v0_6;
            s3_1->ao_buf_size = s5_1;
            if (v0_6 == 0) {
                imp_log_fun(6, IMP_Log_Get_Option(), 2, "adec",
                            "/home/user/git/proj/sdk-lv3/src/imp/audio/adec.c", 0xfa,
                            "IMP_ADEC_CreateChn", "adec_buf alloc error\n", s5_1);
                pthread_mutex_unlock(audio_denc_lock(s3_1));
                return -1;
            }
        }
        pthread_mutex_unlock(audio_denc_lock(s3_1));

        strncpy(audioDencMethod[1].name, "PT_G711A", 9);
        audioDencMethod[1].open_decoder = (uintptr_t)_g711a_open_decoder;
        audioDencMethod[1].decode_frm = (uintptr_t)_g711a_decode_frm;
        audioDencMethod[1].get_frm_info = (uintptr_t)_g711a_get_frm_info;
        audioDencMethod[1].close_decoder = (uintptr_t)_g711a_close_decoder;
        audioDencMethod[2].payload_type = 2;
        audioDencMethod[2].open_decoder = (uintptr_t)_g711u_open_decoder;
        audioDencMethod[1].payload_type = 1;
        audioDencMethod[2].decode_frm = (uintptr_t)_g711u_decode_frm;
        audioDencMethod[2].get_frm_info = (uintptr_t)_g711u_get_frm_info;
        audioDencMethod[2].close_decoder = (uintptr_t)_g711u_close_decoder;
        audioDencMethod[2].registered = 1;
        strncpy(audioDencMethod[2].name, "PT_G711U", 8);
        audioDencMethod[3].payload_type = 3;
        audioDencMethod[3].registered = 1;
        strncpy(audioDencMethod[3].name, "PT_G726", 8);
        audioDencMethod[3].open_decoder = (uintptr_t)_g726_open_decoder;
        audioDencMethod[3].decode_frm = (uintptr_t)_g726_decode_frm;
        audioDencMethod[3].get_frm_info = (uintptr_t)_g726_get_frm_info;
        audioDencMethod[3].close_decoder = (uintptr_t)_g726_close_decoder;
        audioDencMethod[5].payload_type = 5;
        audioDencMethod[5].registered = 1;
        strncpy(audioDencMethod[5].name, "PT_ADPCM", 8);
        audioDencMethod[5].open_decoder = (uintptr_t)_adpcm_open_decoder;
        audioDencMethod[5].decode_frm = (uintptr_t)_adpcm_decode_frm;
        audioDencMethod[5].get_frm_info = (uintptr_t)_adpcm_get_frm_info;
        audioDencMethod[5].close_decoder = (uintptr_t)_adpcm_close_decoder;

        {
            int32_t v1_1 = s3_1->payload_type;
            int32_t a1 = audioDencMethod[v1_1].registered;

            if (a1 == 0) {
                imp_log_fun(6, IMP_Log_Get_Option(), 2, "adec",
                            "/home/user/git/proj/sdk-lv3/src/imp/audio/adec.c", 0x126,
                            "IMP_ADEC_CreateChn", "encoder [%d] is not register\n",
                            s3_1->payload_type);
                return -1;
            }

            if (audioDencMethod[v1_1].open_decoder != 0) {
                ((int32_t (*)(void *, void *))(uintptr_t)audioDencMethod[v1_1].open_decoder)(0, 0);
            }
        }

        s3_1->enabled = 1;
        return 0;
    }
}

int32_t IMP_ADEC_DestroyChn(int32_t arg1)
{
    if (arg1 >= 6) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "adec",
                    "/home/user/git/proj/sdk-lv3/src/imp/audio/adec.c", 0x135,
                    "IMP_ADEC_DestroyChn", "Invalid Audio Enc Channel.\n", &_gp);
        return -1;
    }

    pthread_mutex_lock(audio_denc_lock(&audioDencChn[arg1]));
    {
        int32_t *a0_2 = audioDencChn[arg1].buf;
        audioDencChn[arg1].enabled = 0;
        if (a0_2 != 0) {
            audio_buf_free(a0_2);
        }
        audioDencChn[arg1].buf = 0;
    }
    pthread_mutex_unlock(audio_denc_lock(&audioDencChn[arg1]));

    {
        int32_t result = pthread_mutex_destroy(audio_denc_lock(&audioDencChn[arg1]));
        if (result == 0) {
            return 0;
        }

        imp_log_fun(6, IMP_Log_Get_Option(), 2, "adec",
                    "/home/user/git/proj/sdk-lv3/src/imp/audio/adec.c", 0x141,
                    "IMP_ADEC_DestroyChn", "err: destroy adec chn lock\n", &_gp);
        return result;
    }
}

int32_t IMP_ADEC_SendStream(int32_t arg1, IMPAudioStream *arg2, IMPBlock arg3)
{
    const char *var_2c_1;
    int32_t v0_5;
    int32_t v1_3;

    if (arg1 >= 6) {
        v0_5 = IMP_Log_Get_Option();
        var_2c_1 = "Invalid Audio Enc Channel.\n";
        v1_3 = 0x14c;
        imp_log_fun(6, v0_5, 2, "adec", "/home/user/git/proj/sdk-lv3/src/imp/audio/adec.c",
                    v1_3, "IMP_ADEC_SendStream", var_2c_1, &_gp);
        return -1;
    }

    {
        AudioDencChannel *s3_1 = &audioDencChn[arg1];

        (void)arg3;

        if (s3_1->enabled == 1) {
            int32_t v0_1;

            while (1) {
                pthread_mutex_lock(audio_denc_lock(s3_1));
                v0_1 = audio_buf_get_node(s3_1->buf, 0);
                pthread_mutex_unlock(audio_denc_lock(s3_1));
                if (v0_1 != 0) {
                    break;
                }
                usleep(0x3e8);
            }

            {
                int32_t *s2_3 = (int32_t *)(uintptr_t)audio_buf_node_get_data((void *)(uintptr_t)v0_1);
                uintptr_t t9_1 = audioDencMethod[s3_1->payload_type].decode_frm;

                *(int32_t *)((char *)s2_3 + 4) = s3_1->ao_buf_size;
                if (t9_1 != 0) {
                    ((int32_t (*)(int32_t, uint8_t *, int32_t, int16_t *, int32_t *, int32_t *))(uintptr_t)t9_1)(
                        0, (uint8_t *)arg2->stream, arg2->len, (int16_t *)&s2_3[2],
                        (int32_t *)((char *)s2_3 + 4), 0);
                }
                *s2_3 = v0_1;
                pthread_mutex_lock(audio_denc_lock(s3_1));
                audio_buf_put_node(s3_1->buf, v0_1, 1);
                pthread_mutex_unlock(audio_denc_lock(s3_1));
                return 0;
            }
        }

        v0_5 = IMP_Log_Get_Option();
        var_2c_1 = "AdChn is not enabled.\n";
        v1_3 = 0x151;
        imp_log_fun(6, v0_5, 2, "adec", "/home/user/git/proj/sdk-lv3/src/imp/audio/adec.c",
                    v1_3, "IMP_ADEC_SendStream", var_2c_1, &_gp);
        return -1;
    }
}

int32_t IMP_ADEC_PollingStream(int32_t arg1, uint32_t arg2)
{
    const char *var_2c_1;
    int32_t v0_6;
    int32_t v1_1;

    if (arg1 >= 6) {
        v0_6 = IMP_Log_Get_Option();
        var_2c_1 = "Invalid Audio Enc Channel.\n";
        v1_1 = 0x170;
        imp_log_fun(6, v0_6, 2, "adec", "/home/user/git/proj/sdk-lv3/src/imp/audio/adec.c",
                    v1_1, "IMP_ADEC_PollingStream", var_2c_1);
        return -1;
    }

    if (audioDencChn[arg1].enabled == 1) {
        int32_t s0_1 = 0;

        while (1) {
            int32_t v0_1;

            pthread_mutex_lock(audio_denc_lock(&audioDencChn[arg1]));
            v0_1 = audio_buf_try_get_node(audioDencChn[arg1].buf, 1);
            pthread_mutex_unlock(audio_denc_lock(&audioDencChn[arg1]));
            if (v0_1 != 0) {
                return 0;
            }

            if ((uint32_t)s0_1 >= arg2) {
                break;
            }

            s0_1 += 0x14;
            usleep(0x4e20);
        }

        imp_log_fun(6, IMP_Log_Get_Option(), 2, "adec",
                    "/home/user/git/proj/sdk-lv3/src/imp/audio/adec.c", 0x180,
                    "IMP_ADEC_PollingStream", "%s %d Audio DEC polling timeout.\n",
                    "IMP_ADEC_PollingStream", 0x180, &_gp);
        return -1;
    }

    v0_6 = IMP_Log_Get_Option();
    var_2c_1 = "AdChn is not enabled.\n";
    v1_1 = 0x175;
    imp_log_fun(6, v0_6, 2, "adec", "/home/user/git/proj/sdk-lv3/src/imp/audio/adec.c",
                v1_1, "IMP_ADEC_PollingStream", var_2c_1);
    return -1;
}

int32_t IMP_ADEC_GetStream(int32_t arg1, IMPAudioStream *arg2, IMPBlock arg3)
{
    const char *var_24_1;
    int32_t v0_6;
    int32_t v1_2;

    if (arg1 >= 6) {
        v0_6 = IMP_Log_Get_Option();
        var_24_1 = "Invalid Audio Enc Channel.\n";
        v1_2 = 0x18f;
        imp_log_fun(6, v0_6, 2, "adec", "/home/user/git/proj/sdk-lv3/src/imp/audio/adec.c",
                    v1_2, "IMP_ADEC_GetStream", var_24_1, &_gp);
        return -1;
    }

    if (audioDencChn[arg1].enabled == 1) {
        while (1) {
            int32_t v0_1;

            pthread_mutex_lock(audio_denc_lock(&audioDencChn[arg1]));
            v0_1 = audio_buf_get_node(audioDencChn[arg1].buf, 1);
            pthread_mutex_unlock(audio_denc_lock(&audioDencChn[arg1]));
            if (v0_1 != 0) {
                int32_t v0_2 = audio_buf_node_get_data((void *)(uintptr_t)v0_1);
                int32_t v1_1 = *(int32_t *)(uintptr_t)(v0_2 + 4);

                arg2->stream = (uint32_t *)(uintptr_t)(v0_2 + 8);
                arg2->len = v1_1;
                return 0;
            }

            usleep(0x3e8);
            if (arg3 == 0) {
                return 0;
            }
        }
    }

    v0_6 = IMP_Log_Get_Option();
    var_24_1 = "AdChn is not enabled.\n";
    v1_2 = 0x193;
    imp_log_fun(6, v0_6, 2, "adec", "/home/user/git/proj/sdk-lv3/src/imp/audio/adec.c",
                v1_2, "IMP_ADEC_GetStream", var_24_1, &_gp);
    return -1;
}

int32_t IMP_ADEC_ReleaseStream(int32_t arg1, IMPAudioStream *arg2)
{
    const char *var_1c;
    int32_t v0_3;
    int32_t v1_1;

    if (arg1 >= 6) {
        v0_3 = IMP_Log_Get_Option();
        var_1c = "Invalid Audio Enc Channel.\n";
        v1_1 = 0x1ab;
        imp_log_fun(6, v0_3, 2, "adec", "/home/user/git/proj/sdk-lv3/src/imp/audio/adec.c",
                    v1_1, "IMP_ADEC_ReleaseStream", var_1c, &_gp);
        return -1;
    }

    if (audioDencChn[arg1].enabled == 1) {
        int32_t s2 = (int32_t)(uintptr_t)arg2->stream;

        pthread_mutex_lock(audio_denc_lock(&audioDencChn[arg1]));
        audio_buf_put_node(audioDencChn[arg1].buf, *(int32_t *)(uintptr_t)(s2 - 8), 0);
        pthread_mutex_unlock(audio_denc_lock(&audioDencChn[arg1]));
        return 0;
    }

    v0_3 = IMP_Log_Get_Option();
    var_1c = "AdChn is not enabled.\n";
    v1_1 = 0x1af;
    imp_log_fun(6, v0_3, 2, "adec", "/home/user/git/proj/sdk-lv3/src/imp/audio/adec.c",
                v1_1, "IMP_ADEC_ReleaseStream", var_1c, &_gp);
    return -1;
}

int32_t IMP_ADEC_ClearChnBuf(int32_t arg1)
{
    const char *var_1c;
    int32_t v0_3;
    int32_t v1_1;

    if (arg1 >= 6) {
        v0_3 = IMP_Log_Get_Option();
        var_1c = "Invalid Audio Enc Channel.\n";
        v1_1 = 0x1be;
        imp_log_fun(6, v0_3, 2, "adec", "/home/user/git/proj/sdk-lv3/src/imp/audio/adec.c",
                    v1_1, "IMP_ADEC_ClearChnBuf", var_1c, &_gp);
        return -1;
    }

    if (audioDencChn[arg1].enabled == 1) {
        pthread_mutex_lock(audio_denc_lock(&audioDencChn[arg1]));
        audio_buf_clear(audioDencChn[arg1].buf);
        pthread_mutex_unlock(audio_denc_lock(&audioDencChn[arg1]));
        return 0;
    }

    v0_3 = IMP_Log_Get_Option();
    var_1c = "AdChn is not enabled.\n";
    v1_1 = 0x1c2;
    imp_log_fun(6, v0_3, 2, "adec", "/home/user/git/proj/sdk-lv3/src/imp/audio/adec.c",
                v1_1, "IMP_ADEC_ClearChnBuf", var_1c, &_gp);
    return -1;
}

int32_t IMP_ADEC_RegisterDecoder(int32_t *arg1, IMPAudioDecDecoder *arg2)
{
    int32_t a2 = 6;
    AudioDencMethod *v1 = &audioDencMethod[6];
    int32_t result;

    while (1) {
        result = v1->registered;
        if (result == 0) {
            break;
        }
        v1 = &v1[1];
        a2 += 1;
        if (a2 == 0xb) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "adec",
                        "/home/user/git/proj/sdk-lv3/src/imp/audio/adec.c", 0x1d4,
                        "IMP_ADEC_RegisterDecoder",
                        "You have reached the maximum amount of registration.\n", &_gp);
            return -1;
        }
    }

    {
        AudioDencMethod *v1_3 = &audioDencMethod[a2];
        int32_t i;

        uint32_t *src = (uint32_t *)(void *)arg2;

        for (i = 0; i < 9; i += 1) {
            _setLeftPart32(src[i]);
            ((uint32_t *)&v1_3->payload_type)[i] = _setRightPart32(src[i]);
        }
        v1_3->registered = 1;
        *arg1 = a2;
        return result;
    }
}

int32_t IMP_ADEC_UnRegisterDecoder(int32_t *arg1)
{
    int32_t v0 = *arg1;

    if ((uint32_t)(v0 - 6) >= 6) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "adec",
                    "/home/user/git/proj/sdk-lv3/src/imp/audio/adec.c", 0x1e2,
                    "IMP_ADEC_UnRegisterDecoder", "Invalid handle\n", &_gp);
        return -1;
    }

    audioDencMethod[v0].registered = 0;
    return 0;
}
