/* T41/T40 IMP_AO_SendFrame with software mute from two threads: the shared
 * silence buffer must not race. The
 * driver ioctl is faked, no audio device is touched. */
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>


int64_t IMP_System_GetTimeStamp(void) { return 0; }
static long fake_bytes;
static int fake_nonzero;
static pthread_mutex_t fake_lock = PTHREAD_MUTEX_INITIALIZER;

static int fake_ioctl(int fd, unsigned long request, long arg);
#define ioctl(fd, req, arg) fake_ioctl((fd), (req), (long)(arg))
/* the target ABI asserts (32-bit) do not hold on the 64-bit host */
#define _Static_assert(...) extern int p3_ao_mute_test_unused
#include "../../src/t40/openimp_p3_audio.c"
#undef _Static_assert
#undef ioctl

static int fake_ioctl(int fd, unsigned long request, long arg)
{
    const P3AudioOutputStream *s = (const P3AudioOutputStream *)arg;
    const unsigned char *d = s->data;
    uint32_t i;

    (void)fd;
    (void)request;
    pthread_mutex_lock(&fake_lock);
    fake_bytes += s->size;
    for (i = 0; i < s->size; i++)
        if (d[i])
            fake_nonzero++;
    pthread_mutex_unlock(&fake_lock);
    return 0;
}

static void *worker(void *arg)
{
    unsigned char *src = malloc(8192);
    IMPAudioFrame frame;
    int i, bad = 0;

    memset(src, 0x55, 8192);
    memset(&frame, 0, sizeof(frame));
    frame.virAddr = (uint32_t *)src;
    for (i = 0; i < 20000; i++) {
        /* growing and shrinking sizes force the realloc path */
        frame.len = 64 + (i * 37 + (int)(intptr_t)arg * 1000) % 8000;
        if (IMP_AO_SendFrame(0, 0, &frame, BLOCK) != 0)
            bad++;
    }
    free(src);
    return (void *)(intptr_t)bad;
}

int main(void)
{
    pthread_t t[2];
    void *r[2];

    p3_audio.fd = 3;
    p3_audio.ao_enabled = 1;
    p3_audio.ao_channel_enabled = 1;
    p3_audio.ao_attr.samplerate = 16000;
    assert(IMP_AO_SetVolMute(0, 0, 1) == 0);
    pthread_create(&t[0], NULL, worker, (void *)1);
    pthread_create(&t[1], NULL, worker, (void *)2);
    pthread_join(t[0], &r[0]);
    pthread_join(t[1], &r[1]);
    assert(r[0] == NULL && r[1] == NULL);
    assert(fake_bytes > 0);
    assert(fake_nonzero == 0);              /* only silence reached the driver */
    printf("p3_ao_mute_test: ok (%ld bytes of silence)\n", fake_bytes);
    return 0;
}
