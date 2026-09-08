/* SPDX-License-Identifier: LGPL-2.1-or-later */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include "../../src/t40/openimp_core_lease.h"

static OpenIMPCoreLease core = OPENIMP_CORE_LEASE_INITIALIZER;
static int main_owner, sub_owner;

static void *completion(void *arg)
{
    openimp_core_release(&core, arg);
    return NULL;
}

static void *cycles(void *owner)
{
    for (int i = 0; i < 10000; ++i) {
        assert(!openimp_core_acquire(&core, owner, 2000));
        assert(core.owner == owner);
        openimp_core_release(&core, owner);
    }
    return NULL;
}

int main(void)
{
    pthread_t a, b;
    assert(openimp_core_acquire(&core, NULL, 0) == -EINVAL);
    assert(!openimp_core_acquire(&core, &main_owner, 0));
    assert(openimp_core_acquire(&core, &main_owner, 0) == -EAGAIN);
    assert(openimp_core_acquire(&core, &main_owner, 2) == -ETIMEDOUT);
    assert(openimp_core_acquire(&core, &sub_owner, 0) == -EAGAIN);
    assert(openimp_core_acquire(&core, &sub_owner, 2) == -ETIMEDOUT);
    assert(core.owner == &main_owner); /* timeout cannot revoke DMA ownership */
    openimp_core_release(&core, &sub_owner);
    assert(core.owner == &main_owner);
    /* A published packet may be consumed before its IRQ epilogue releases
     * the lease. A serial next Submit must wait, not report recursive entry.
     * Repeat the handoff with the SAME codec token across distinct threads. */
    for (int i = 0; i < 100; ++i) {
        assert(!pthread_create(&a, NULL, completion, &main_owner));
        assert(!openimp_core_acquire(&core, &main_owner, 2000));
        assert(!pthread_join(a, NULL));
        assert(core.owner == &main_owner);
    }
    assert(!pthread_create(&a, NULL, completion, &main_owner));
    assert(!openimp_core_acquire(&core, &sub_owner, 2000));
    assert(!pthread_join(a, NULL));
    openimp_core_release(&core, &main_owner); /* stale completion is harmless */
    assert(core.owner == &sub_owner);
    openimp_core_release(&core, &sub_owner);
    assert(!pthread_create(&a, NULL, cycles, &main_owner));
    assert(!pthread_create(&b, NULL, cycles, &sub_owner));
    assert(!pthread_join(a, NULL));
    assert(!pthread_join(b, NULL));
    assert(core.owner == NULL);
    puts("core lease: same-codec completion tail, timeout retention, stale owner, 20000 handoffs PASS");
    return 0;
}
