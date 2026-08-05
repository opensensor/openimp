#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint32_t (*get_target_size_fn)(void *, int32_t *, int32_t);
typedef int32_t (*preprocess_hwrc_fn)(int32_t *, int32_t *, int32_t,
                                     int32_t, uint32_t *);

static void dump_words(const uint32_t *words, size_t byte_count)
{
    size_t word_count = byte_count / sizeof(*words);
    size_t offset;

    for (offset = 0; offset < word_count; offset += 8u) {
        size_t column;

        printf("%04zx:", offset * sizeof(*words));
        for (column = 0; column < 8u && offset + column < word_count;
             ++column)
            printf(" %08x", words[offset + column]);
        putchar('\n');
    }
}

int main(int argc, char **argv)
{
    const char *library = argc > 1 ? argv[1] : "/lib/libimp.so";
    uint32_t bitrate = argc > 2 ? (uint32_t)strtoul(argv[2], NULL, 0)
                                : 1000000u;
    uint8_t rate_storage[0x80];
    int32_t target_params[8];
    uint32_t output[0x1420u / sizeof(uint32_t)];
    get_target_size_fn get_target_size;
    preprocess_hwrc_fn preprocess_hwrc;
    int32_t *rate = (int32_t *)rate_storage;
    void *handle;
    uint32_t target;
    int32_t result;

    memset(rate_storage, 0, sizeof(rate_storage));
    memset(target_params, 0, sizeof(target_params));
    memset(output, 0xa5, sizeof(output));

    /*
     * Select the static AVC curve and arrange GetTargetSize's fixed-rate
     * branch so that its intermediate target is the requested bitrate:
     * (time_scale * bitrate) / (num_units_in_tick * 1000), followed by the
     * OEM function's 95-percent reduction.
     */
    rate[0] = 3;
    *(uint16_t *)(rate_storage + 0x0cu) = 1u;
    *(uint16_t *)(rate_storage + 0x0eu) = 1000u;
    *(uint32_t *)(rate_storage + 0x10u) = bitrate;
    target_params[0] = 8;

    handle = dlopen(library, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        fprintf(stderr, "dlopen(%s): %s\n", library, dlerror());
        return 1;
    }

    get_target_size =
        (get_target_size_fn)dlsym(handle, "GetTargetSize");
    preprocess_hwrc =
        (preprocess_hwrc_fn)dlsym(handle, "PreprocessHwRateCtrl");
    if (!get_target_size || !preprocess_hwrc) {
        fprintf(stderr, "missing HWRC exports: %s\n", dlerror());
        dlclose(handle);
        return 2;
    }

    target = get_target_size(rate, target_params, 0);
    result = preprocess_hwrc(rate, target_params, 1, 0, output);
    printf("library=%s bitrate=%u target=%u result=%d\n",
           library, bitrate, target, result);
    dump_words(output, 0x400u);

    dlclose(handle);
    return result < 0 ? 3 : 0;
}
