/* T40 IMP_AI/AO_Set/GetDigitalGain (vendor 1.3.1): raw codec value 0..255
 * through AMIC_AI_SET_VOLUME / AMIC_SPK_SET_VOLUME, clamped, kept even when
 * the ioctl fails, Get from the kept value.  The driver ioctl is faked. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>

int64_t IMP_System_GetTimeStamp(void) { return 0; }
static unsigned long fake_req;
static uint32_t fake_words[3];
static int fake_calls, fake_fail;

static int fake_ioctl(int fd, unsigned long request, long arg);
#define ioctl(fd, req, arg) fake_ioctl((fd), (req), (long)(arg))
#define _Static_assert(...) extern int audio_dgain_test_unused
#include "../../src/t40/openimp_p3_audio.c"
#undef _Static_assert
#undef ioctl

static int fake_ioctl(int fd, unsigned long request, long arg)
{
    (void)fd;
    fake_req = request;
    memcpy(fake_words, (const void *)arg, sizeof(fake_words));
    fake_calls++;
    return fake_fail ? -1 : 0;
}

int main(void)
{
    int g = -5;

    p3_audio.fd = 3;
    /* AI: register value, clamped */
    assert(IMP_AI_SetDigitalGain(0, 0, 300) == 0);
    assert((uint32_t)fake_req == (uint32_t)AMIC_AI_SET_VOLUME && (fake_req & 0xffffu) == 0x505bu);
    assert(fake_words[0] == 1 && fake_words[1] == 255 && fake_words[2] == 255);
    assert(IMP_AI_GetDigitalGain(0, 0, &g) == 0 && g == 255);
    assert(IMP_AI_SetDigitalGain(0, 0, -4) == 0 && fake_words[1] == 0);
    assert(IMP_AI_SetDigitalGain(0, 0, 150) == 0 && fake_words[1] == 150);
    /* vendor checks */
    fake_calls = 0;
    assert(IMP_AI_SetDigitalGain(2, 0, 1) == -1);
    assert(IMP_AI_SetDigitalGain(0, 1, 1) == -1);
    assert(IMP_AI_GetDigitalGain(0, 0, NULL) == -1);
    assert(fake_calls == 0);
    /* a failing ioctl is only logged */
    fake_fail = 1;
    assert(IMP_AI_SetDigitalGain(0, 0, 77) == 0);
    fake_fail = 0;
    assert(IMP_AI_GetDigitalGain(0, 0, &g) == 0 && g == 77);
    /* AO: needs the device enabled */
    assert(IMP_AO_SetDigitalGain(0, 0, 100) == -1);
    p3_audio.ao_enabled = 1;
    assert(IMP_AO_SetDigitalGain(0, 0, 100) == 0);
    assert((uint32_t)fake_req == (uint32_t)AMIC_SPK_SET_VOLUME && (fake_req & 0xffffu) == 0x5059u);
    assert(fake_words[1] == 100);
    assert(IMP_AO_GetDigitalGain(0, 0, &g) == 0 && g == 100);
    assert(IMP_AO_SetDigitalGain(1, 0, 1) == -1 && IMP_AO_SetDigitalGain(0, 1, 1) == -1);
    puts("T40 audio digital gain: all checks passed");
    return 0;
}
