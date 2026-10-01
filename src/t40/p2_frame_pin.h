/*
 * p2_frame_pin.h -- shared ownership of one capture frame between the AVC
 * encoder thread that dequeued it and the JPEG channels it lends it to.
 *
 * The frame goes back to the FrameSource only when the last holder lets go:
 * the AVC channel after its picture completed (or failed), each JPEG channel
 * after its snapshot encode returned. A frame that was never lent has no
 * entry and is released at once, as before.
 *
 * Header-only (static inline) so the host tests exercise exactly this code.
 * The caller supplies the lock-protected table; the release callback runs
 * outside the lock.
 */
#ifndef P2_FRAME_PIN_H
#define P2_FRAME_PIN_H

#include <pthread.h>
#include <stddef.h>

#define P2_FRAME_PIN_SLOTS 8

typedef struct {
    void *frame;
    int source_channel;
    int refs;               /* holders still using the frame, >= 2 if live */
} P2FramePinSlot;

typedef struct {
    pthread_mutex_t lock;
    P2FramePinSlot slots[P2_FRAME_PIN_SLOTS];
} P2FramePinTable;

#define P2_FRAME_PIN_TABLE_INITIALIZER { PTHREAD_MUTEX_INITIALIZER, {{0}} }

/* The current holder lends the frame to one more user. Returns 0, or -1
 * when the table is full (the caller then must not lend it). */
static inline int p2_frame_pin_lend(P2FramePinTable *table,
                                    int source_channel, void *frame)
{
    P2FramePinSlot *free_slot = NULL;
    int i;

    if (!frame)
        return -1;
    pthread_mutex_lock(&table->lock);
    for (i = 0; i < P2_FRAME_PIN_SLOTS; i++) {
        P2FramePinSlot *slot = &table->slots[i];

        if (slot->frame == frame && slot->source_channel == source_channel) {
            slot->refs++;
            pthread_mutex_unlock(&table->lock);
            return 0;
        }
        if (!slot->frame && !free_slot)
            free_slot = slot;
    }
    if (!free_slot) {
        pthread_mutex_unlock(&table->lock);
        return -1;
    }
    free_slot->frame = frame;
    free_slot->source_channel = source_channel;
    free_slot->refs = 2;    /* the lender and the borrower */
    pthread_mutex_unlock(&table->lock);
    return 0;
}

/* A holder is done with the frame. Calls release(source_channel, frame)
 * and returns its result when this was the last holder (or the frame was
 * never lent); otherwise returns 0 and leaves the frame to the others. */
static inline int p2_frame_pin_put(P2FramePinTable *table,
                                   int source_channel, void *frame,
                                   int (*release)(int, void *))
{
    int i;

    if (!frame)
        return -1;
    pthread_mutex_lock(&table->lock);
    for (i = 0; i < P2_FRAME_PIN_SLOTS; i++) {
        P2FramePinSlot *slot = &table->slots[i];

        if (slot->frame != frame || slot->source_channel != source_channel)
            continue;
        if (--slot->refs > 0) {
            pthread_mutex_unlock(&table->lock);
            return 0;
        }
        slot->frame = NULL;
        slot->source_channel = 0;
        slot->refs = 0;
        break;
    }
    pthread_mutex_unlock(&table->lock);
    return release(source_channel, frame);
}

#endif /* P2_FRAME_PIN_H */
