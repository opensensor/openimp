#include <stdint.h>

extern char _gp;

int32_t AL_Common_Encoder_Create(int32_t arg1); /* forward decl, ported by T<N> later */
int32_t AL_Common_Encoder_Destroy(int32_t **arg1); /* forward decl, ported by T<N> later */
int32_t AL_Common_Encoder_CreateChannel(int32_t *arg1, int32_t arg2, int32_t arg3,
                                        int32_t *arg4); /* forward decl, ported by T<N> later */
int32_t AL_Common_Encoder_GetRecPicture(int32_t *arg1, int32_t *arg2,
                                        int32_t arg3); /* forward decl, ported by T<N> later */
int32_t AL_Common_Encoder_ReleaseRecPicture(int32_t *arg1, int32_t *arg2,
                                            int32_t arg3); /* forward decl, ported by T<N> later */
int32_t AL_Common_Encoder_PutStreamBuffer(int32_t *arg1, void *arg2, int32_t arg3); /* forward decl, ported by T<N> later */
int32_t AL_Common_Encoder_Process(int32_t *arg1, int32_t arg2, int32_t arg3,
                                  int32_t arg4); /* forward decl, ported by T<N> later */
int32_t AL_Common_Encoder_SetBitRate(int32_t *arg1, int32_t arg2, int32_t arg3,
                                     int32_t arg4); /* forward decl, ported by T<N> later */
int32_t AL_Common_Encoder_SetFrameRate(int32_t *arg1, uint8_t arg2,
                                       int16_t arg3); /* forward decl, ported by T<N> later */
int32_t AL_Common_Encoder_SetQP(int32_t *arg1, int16_t arg2); /* forward decl, ported by T<N> later */
int32_t AL_Common_Encoder_SetQPBounds(int32_t *arg1, int16_t arg2,
                                      int16_t arg3); /* forward decl, ported by T<N> later */
int32_t AL_Common_Encoder_SetQPIPDelta(int32_t *arg1, int16_t arg2); /* forward decl, ported by T<N> later */
int32_t AL_Common_Encoder_SetQPPBDelta(int32_t *arg1, int16_t arg2); /* forward decl, ported by T<N> later */
int32_t AL_Common_Encoder_SetInputResolution(int32_t *arg1, int32_t arg2,
                                             int32_t arg3); /* forward decl, ported by T<N> later */
int32_t AL_Common_Encoder_SetLoopFilterBetaOffset(int32_t *arg1,
                                                  int8_t arg2); /* forward decl, ported by T<N> later */
int32_t AL_Common_Encoder_SetLoopFilterTcOffset(int32_t *arg1,
                                                int8_t arg2); /* forward decl, ported by T<N> later */
uint32_t *AL_Common_Encoder_SetTraceMode(int32_t *arg1, int32_t arg2, int32_t arg3,
                                         char arg4); /* forward decl, ported by T<N> later */
int32_t (*AL_CreateAvcEncoder(int32_t (**arg1)(void)))(void); /* forward decl, ported by T<N> later */
int32_t AL_CreateHevcEncoder(int32_t (**arg1)()); /* forward decl, ported by T<N> later */
int32_t (*AL_CreateJpegEncoder(int32_t (**arg1)()))(); /* forward decl, ported by T<N> later */

int32_t AL_Encoder_Create(int32_t **arg1, int32_t arg2, int32_t arg3, uint8_t *arg4, int32_t arg5, int32_t arg6)
{
    int32_t result = 0x80;

    (void)_gp;
    if (arg4 != 0) {
        int32_t *v0 = (int32_t *)(intptr_t)AL_Common_Encoder_Create(arg3);

        *arg1 = v0;
        if (v0 == 0)
            return 0x87;

        {
            uint32_t v0_1 = arg4[0x1f];
            int32_t (**s2_1)(void) = (int32_t (**)(void))(intptr_t)*v0;

            if (v0_1 == 1) {
                AL_CreateHevcEncoder((int32_t (**)())s2_1);
                v0_1 = arg4[0x1f];
            }
            if (v0_1 == 0) {
                AL_CreateAvcEncoder(s2_1);
                v0_1 = arg4[0x1f];
            }
            if (v0_1 == 4)
                AL_CreateJpegEncoder((int32_t (**)())s2_1);

            result = AL_Common_Encoder_CreateChannel((int32_t *)(intptr_t)v0, arg2, arg3, (int32_t *)(void *)arg4);
            if ((uint32_t)result >= 0x80U) {
                AL_Common_Encoder_Destroy((int32_t **)(intptr_t)v0);
                *arg1 = 0;
                return result;
            }
            if (arg5 != 0) {
                *(int32_t *)((char *)s2_1 + 0xe0d4) = arg5; /* +0xe0d4 callback override */
                *(int32_t *)((char *)s2_1 + 0xe0d8) = arg6; /* +0xe0d8 callback cookie */
            }
            return result;
        }
    }

    return result;
}

int32_t AL_Encoder_GetRecPicture(int32_t *arg1, int32_t *arg2)
{
    return AL_Common_Encoder_GetRecPicture(arg1, arg2, 0);
}

int32_t AL_Encoder_ReleaseRecPicture(int32_t *arg1, int32_t *arg2)
{
    return AL_Common_Encoder_ReleaseRecPicture(arg1, arg2, 0);
}

int32_t AL_Encoder_PutStreamBuffer(int32_t *arg1, void *arg2, int32_t arg3)
{
    (void)arg3;
    return AL_Common_Encoder_PutStreamBuffer(arg1, arg2, 0);
}

int32_t AL_Encoder_Process(int32_t *arg1, int32_t arg2, int32_t arg3)
{
    return AL_Common_Encoder_Process(arg1, arg2, arg3, 0);
}

int32_t AL_Encoder_SetBitRate(int32_t *arg1, int32_t arg2, int32_t arg3)
{
    return AL_Common_Encoder_SetBitRate(arg1, arg2, arg3, 0);
}

int32_t AL_Encoder_SetFrameRate(int32_t *arg1, int32_t arg2, int32_t arg3)
{
    return AL_Common_Encoder_SetFrameRate(arg1, (uint8_t)arg2, (int16_t)arg3);
}

int32_t AL_Encoder_SetQP(int32_t *arg1, int32_t arg2)
{
    return AL_Common_Encoder_SetQP(arg1, (int16_t)arg2);
}

int32_t AL_Encoder_SetQPBounds(int32_t *arg1, int32_t arg2, int32_t arg3)
{
    return AL_Common_Encoder_SetQPBounds(arg1, (int16_t)arg2, (int16_t)arg3);
}

int32_t AL_Encoder_SetQPIPDelta(int32_t *arg1, int32_t arg2)
{
    return AL_Common_Encoder_SetQPIPDelta(arg1, (int16_t)arg2);
}

int32_t AL_Encoder_SetQPPBDelta(int32_t *arg1, int32_t arg2)
{
    return AL_Common_Encoder_SetQPPBDelta(arg1, (int16_t)arg2);
}

int32_t AL_Encoder_SetInputResolution(int32_t *arg1, int32_t arg2, int32_t arg3)
{
    return AL_Common_Encoder_SetInputResolution(arg1, arg2, arg3);
}

int32_t AL_Encoder_SetLoopFilterBetaOffset(int32_t *arg1, int32_t arg2)
{
    return AL_Common_Encoder_SetLoopFilterBetaOffset(arg1, (int8_t)arg2);
}

int32_t AL_Encoder_SetLoopFilterTcOffset(int32_t *arg1, int32_t arg2)
{
    return AL_Common_Encoder_SetLoopFilterTcOffset(arg1, (int8_t)arg2);
}

uint32_t *AL_Encoder_SetTraceMode(int32_t *arg1, int32_t arg2, int32_t arg3, int32_t arg4)
{
    return AL_Common_Encoder_SetTraceMode(arg1, arg2, arg3, (char)arg4);
}
