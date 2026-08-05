#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef int32_t (*get_pcm_fn)(int32_t, int32_t, int32_t, int32_t);
typedef int32_t (*get_max_nal_fn)(int32_t, int32_t, int32_t, int32_t,
                                  int32_t, int32_t, int32_t);

static void *must_sym(void *handle, const char *name)
{
    void *result = dlsym(handle, name);

    if (!result) {
        fprintf(stderr, "dlsym(%s): %s\n", name, dlerror());
        exit(1);
    }
    return result;
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "/lib/libimp.so";
    static const int depths[] = { 8, 10, 12 };
    static const int levels[] = { 9, 10, 11, 12, 13, 20, 21, 22, 30, 31,
                                  32, 40, 41, 42, 50, 51, 52, 60, 61 };
    static const int profiles[] = { 66, 77, 88, 100 };
    void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    get_pcm_fn get_pcm;
    get_max_nal_fn get_max_nal;
    unsigned int fmt;
    unsigned int depth_idx;
    unsigned int level_idx;
    unsigned int profile_idx;

    if (!handle) {
        fprintf(stderr, "dlopen(%s): %s\n", path, dlerror());
        return 1;
    }
    get_pcm = (get_pcm_fn)must_sym(handle, "GetPcmVclNalSize");
    get_max_nal = (get_max_nal_fn)must_sym(handle, "AL_GetMaxNalSize");

    for (fmt = 0; fmt < 4; ++fmt) {
        for (depth_idx = 0; depth_idx < sizeof(depths) / sizeof(depths[0]);
             ++depth_idx) {
            int pcm = get_pcm(640, 360, (int)fmt, depths[depth_idx]);

            printf("pcm fmt=%u depth=%d size=0x%x\n",
                   fmt, depths[depth_idx], (unsigned int)pcm);
        }
    }

    for (fmt = 0; fmt < 4; ++fmt) {
        for (level_idx = 0;
             level_idx < sizeof(levels) / sizeof(levels[0]); ++level_idx) {
            for (profile_idx = 0;
                 profile_idx < sizeof(profiles) / sizeof(profiles[0]);
                 ++profile_idx) {
                int size = get_max_nal(0, 640, 360, (int)fmt, 8,
                                       levels[level_idx],
                                       profiles[profile_idx]);

                if (size >= 0x20000 && size <= 0x30000)
                    printf("max fmt=%u depth=8 level=%d profile=%d "
                           "size=0x%x\n",
                           fmt, levels[level_idx], profiles[profile_idx],
                           (unsigned int)size);
            }
        }
    }

    dlclose(handle);
    return 0;
}
