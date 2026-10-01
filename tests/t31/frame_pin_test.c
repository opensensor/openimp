/* Host test for the capture-frame sharing between the AVC channel and the
 * JPEG channels it lends a frame to (src/t40/p2_frame_pin.h). */
#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <pthread.h>
#include <stdio.h>

#include "t40/p2_frame_pin.h"

static int release_calls;
static int last_channel;

static int fake_release(int channel, void *frame)
{
    __atomic_store_n(&last_channel, channel, __ATOMIC_RELAXED);
    __atomic_fetch_add(&release_calls, 1, __ATOMIC_RELAXED);
    __atomic_fetch_add((int *)frame, 1, __ATOMIC_RELAXED);
    return 0;
}

static P2FramePinTable table = P2_FRAME_PIN_TABLE_INITIALIZER;
static int frames[P2_FRAME_PIN_SLOTS];

static void *jpeg_thread(void *arg)
{
    int i;

    (void)arg;
    for (i = 0; i < P2_FRAME_PIN_SLOTS; i++)
        assert(p2_frame_pin_put(&table, 0, &frames[i], fake_release) == 0);
    return NULL;
}

int main(void)
{
    int frame_a = 0, frame_b = 0;
    pthread_t thread;
    int i;

    /* Never lent: released at once, as before. */
    assert(p2_frame_pin_put(&table, 1, &frame_a, fake_release) == 0);
    assert(frame_a == 1 && release_calls == 1 && last_channel == 1);

    /* Lent to one JPEG channel, AVC done first. */
    frame_a = 0;
    assert(p2_frame_pin_lend(&table, 0, &frame_a) == 0);
    assert(p2_frame_pin_put(&table, 0, &frame_a, fake_release) == 0);
    assert(frame_a == 0);
    assert(p2_frame_pin_put(&table, 0, &frame_a, fake_release) == 0);
    assert(frame_a == 1);

    /* JPEG done first, then AVC. */
    frame_a = 0;
    assert(p2_frame_pin_lend(&table, 0, &frame_a) == 0);
    assert(p2_frame_pin_put(&table, 0, &frame_a, fake_release) == 0);
    assert(frame_a == 0);
    assert(p2_frame_pin_put(&table, 0, &frame_a, fake_release) == 0);
    assert(frame_a == 1);

    /* Two JPEG channels, and the same pointer on another source channel
     * is a different frame. */
    frame_a = 0;
    frame_b = 0;
    assert(p2_frame_pin_lend(&table, 0, &frame_a) == 0);
    assert(p2_frame_pin_lend(&table, 0, &frame_a) == 0);
    assert(p2_frame_pin_lend(&table, 2, &frame_b) == 0);
    assert(p2_frame_pin_put(&table, 0, &frame_a, fake_release) == 0);
    assert(p2_frame_pin_put(&table, 0, &frame_a, fake_release) == 0);
    assert(frame_a == 0);
    assert(p2_frame_pin_put(&table, 0, &frame_a, fake_release) == 0);
    assert(frame_a == 1);
    assert(p2_frame_pin_put(&table, 2, &frame_b, fake_release) == 0);
    assert(frame_b == 0);
    assert(p2_frame_pin_put(&table, 2, &frame_b, fake_release) == 0);
    assert(frame_b == 1 && last_channel == 2);

    /* Full table: lending is refused, nothing is pinned. */
    for (i = 0; i < P2_FRAME_PIN_SLOTS; i++)
        assert(p2_frame_pin_lend(&table, 0, &frames[i]) == 0);
    assert(p2_frame_pin_lend(&table, 0, &frame_a) == -1);
    for (i = 0; i < P2_FRAME_PIN_SLOTS; i++) {
        assert(p2_frame_pin_put(&table, 0, &frames[i], fake_release) == 0);
        assert(p2_frame_pin_put(&table, 0, &frames[i], fake_release) == 0);
        assert(frames[i] == 1);
        frames[i] = 0;
    }

    /* Concurrent holders (AVC and JPEG threads): a lent frame is released
     * exactly once, after both. */
    for (int round = 0; round < 200; round++) {
        for (i = 0; i < P2_FRAME_PIN_SLOTS; i++)
            assert(p2_frame_pin_lend(&table, 0, &frames[i]) == 0);
        assert(pthread_create(&thread, NULL, jpeg_thread, NULL) == 0);
        for (i = 0; i < P2_FRAME_PIN_SLOTS; i++)
            assert(p2_frame_pin_put(&table, 0, &frames[i], fake_release) == 0);
        assert(pthread_join(thread, NULL) == 0);
        for (i = 0; i < P2_FRAME_PIN_SLOTS; i++) {
            assert(frames[i] == 1);
            frames[i] = 0;
        }
    }
    for (i = 0; i < P2_FRAME_PIN_SLOTS; i++)
        assert(table.slots[i].frame == NULL);

    puts("T31 frame-pin tests passed");
    return 0;
}
