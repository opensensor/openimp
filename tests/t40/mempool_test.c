/* IMP_System_MemPoolRequest argument rules (host). */
#include <assert.h>
#include <stdio.h>
#include "../../src/t40/openimp_p3_compat.c"
int main(void)
{
    assert(IMP_System_MemPoolRequest(-1, 4096, "x") == -1);
    assert(IMP_System_MemPoolRequest(32, 4096, "x") == -1);
    assert(IMP_System_MemPoolRequest(5, 0, "x") == -1);
    assert(IMP_System_MemPoolRequest(5, 8 << 20, "encpool") == 0);
    assert(IMP_System_MemPoolRequest(5, 8 << 20, "again") == -1);
    assert(IMP_System_MemPoolRequest(6, 4096, NULL) == 0);
    puts("mempool_test OK");
    return 0;
}
