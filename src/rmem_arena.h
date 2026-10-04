#ifndef OPENIMP_RMEM_ARENA_H
#define OPENIMP_RMEM_ARENA_H

/*
 * Allocator for the reserved DMA arena (/dev/rmem or the /dev/mem window).
 *
 * The arena used to be a bump allocator whose frees were no-ops, so every
 * FrameSource disable/enable cycle (day/night switch, last client leaving)
 * leaked its VBM pools until EnableChn failed. This keeps a sorted table of
 * live extents and places a request in the smallest gap that fits (best
 * fit), which reuses the pools a disable/enable cycle frees at the same
 * addresses.
 *
 * Pure bookkeeping, no locking: the caller serializes (g_alloc_mutex).
 */

#include <stddef.h>
#include <stdint.h>

#define RMEM_ARENA_PAGE        4096u
#define RMEM_ARENA_MAX_EXTENTS 256

typedef struct {
    size_t off;
    size_t len;                 /* page-rounded */
} RmemExtent;

typedef struct {
    size_t size;                /* arena bytes */
    size_t used;                /* bytes in live extents */
    size_t high_water;          /* end of the highest extent ever placed */
    size_t peak_used;           /* highest value 'used' ever reached */
    int count;
    RmemExtent ext[RMEM_ARENA_MAX_EXTENTS];
} RmemArena;

static inline size_t rmem_arena_round(size_t size)
{
    return (size + (RMEM_ARENA_PAGE - 1u)) & ~(size_t)(RMEM_ARENA_PAGE - 1u);
}

static inline void rmem_arena_init(RmemArena *a, size_t size)
{
    a->size = size;
    a->used = 0;
    a->high_water = 0;
    a->peak_used = 0;
    a->count = 0;
}

/* Largest free gap, for diagnostics. */
static inline size_t rmem_arena_largest_gap(const RmemArena *a)
{
    size_t prev_end = 0, best = 0;

    for (int i = 0; i < a->count; i++) {
        if (a->ext[i].off - prev_end > best)
            best = a->ext[i].off - prev_end;
        prev_end = a->ext[i].off + a->ext[i].len;
    }
    if (a->size > prev_end && a->size - prev_end > best)
        best = a->size - prev_end;
    return best;
}

/* Free bytes behind the highest extent: the only gap a larger arena grows. */
static inline size_t rmem_arena_tail_gap(const RmemArena *a)
{
    size_t end = a->count ? a->ext[a->count - 1].off + a->ext[a->count - 1].len : 0;

    return a->size > end ? a->size - end : 0;
}

/* Returns 0 and the page-aligned offset, or -1 if nothing fits. */
static inline int rmem_arena_alloc(RmemArena *a, size_t size, size_t *off_out)
{
    size_t len, prev_end = 0, best_off = 0, best_gap = (size_t)-1;
    int best_idx = -1;

    if (size == 0 || a->count >= RMEM_ARENA_MAX_EXTENTS)
        return -1;
    len = rmem_arena_round(size);
    if (len < size)
        return -1;
    for (int i = 0; i <= a->count; i++) {
        size_t end = i < a->count ? a->ext[i].off : a->size;
        size_t gap = end > prev_end ? end - prev_end : 0;

        if (gap >= len && gap < best_gap) {
            best_gap = gap;
            best_off = prev_end;
            best_idx = i;
        }
        if (i < a->count)
            prev_end = a->ext[i].off + a->ext[i].len;
    }
    if (best_idx < 0)
        return -1;
    for (int i = a->count; i > best_idx; i--)
        a->ext[i] = a->ext[i - 1];
    a->ext[best_idx].off = best_off;
    a->ext[best_idx].len = len;
    a->count++;
    a->used += len;
    if (a->used > a->peak_used)
        a->peak_used = a->used;
    if (best_off + len > a->high_water)
        a->high_water = best_off + len;
    *off_out = best_off;
    return 0;
}

/* Like rmem_arena_alloc, but for long-lived buffers: place the request at
 * the top end of the highest free gap that fits, so it stays clear of the
 * pools that are freed and re-created from the bottom. */
static inline int rmem_arena_alloc_top(RmemArena *a, size_t size,
                                       size_t *off_out)
{
    size_t len, prev_end = 0, best_off = 0;
    int best_idx = -1;

    if (size == 0 || a->count >= RMEM_ARENA_MAX_EXTENTS)
        return -1;
    len = rmem_arena_round(size);
    if (len < size)
        return -1;
    for (int i = 0; i <= a->count; i++) {
        size_t end = i < a->count ? a->ext[i].off : a->size;

        if (end > prev_end && end - prev_end >= len) {
            best_off = end - len;
            best_idx = i;
        }
        if (i < a->count)
            prev_end = a->ext[i].off + a->ext[i].len;
    }
    if (best_idx < 0)
        return -1;
    for (int i = a->count; i > best_idx; i--)
        a->ext[i] = a->ext[i - 1];
    a->ext[best_idx].off = best_off;
    a->ext[best_idx].len = len;
    a->count++;
    a->used += len;
    if (a->used > a->peak_used)
        a->peak_used = a->used;
    if (best_off + len > a->high_water)
        a->high_water = best_off + len;
    *off_out = best_off;
    return 0;
}

/* Frees the extent starting at off; -1 if there is none (double free). */
static inline int rmem_arena_free(RmemArena *a, size_t off, size_t *len_out)
{
    for (int i = 0; i < a->count; i++) {
        if (a->ext[i].off != off)
            continue;
        if (len_out)
            *len_out = a->ext[i].len;
        a->used -= a->ext[i].len;
        for (int j = i; j + 1 < a->count; j++)
            a->ext[j] = a->ext[j + 1];
        a->count--;
        return 0;
    }
    return -1;
}

#endif
