#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

typedef void (*destroy_fn)(void *);

int main(int argc, char **argv)
{
    const char *openimp_library =
        argc > 1 ? argv[1] : "/tmp/libimp-openimp-t40.so";
    void *oem_handle;
    void *recovered_handle;
    destroy_fn destroy_move;
    destroy_fn destroy_base_move;
    void *move;
    void *base_move;

    oem_handle = dlopen("/usr/lib/libimp.so", RTLD_LAZY | RTLD_LOCAL);
    if (!oem_handle) {
        printf("OEM dlopen failed: %s\n", dlerror());
        return 2;
    }
    recovered_handle = dlopen(openimp_library, RTLD_LAZY | RTLD_LOCAL);
    if (!recovered_handle) {
        printf("recovered dlopen failed: %s\n", dlerror());
        return 3;
    }
    destroy_move = (destroy_fn)dlsym(
        recovered_handle, "IMP_IVS_DestroyMoveInterface");
    destroy_base_move = (destroy_fn)dlsym(
        recovered_handle, "IMP_IVS_DestroyBaseMoveInterface");
    if (!destroy_move || !destroy_base_move) {
        printf("dlsym failed: %s\n", dlerror());
        return 4;
    }

    move = malloc(64);
    base_move = malloc(64);
    if (!move || !base_move)
        return 5;
    destroy_move(move);
    destroy_base_move(base_move);
    destroy_move(NULL);
    destroy_base_move(NULL);
    printf("PASS IVS destroy/free and null paths\n");
    return 0;
}
