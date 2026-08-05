#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

typedef int32_t (*get_pcm_fn)(int32_t, int32_t, int32_t, int32_t);
typedef int32_t (*get_max_nal_fn)(int32_t, int32_t, int32_t, int32_t,
                                  int32_t, int32_t, int32_t);
typedef int32_t (*imp_alloc_fn)(void *, int32_t, const char *);

static get_pcm_fn real_get_pcm;
static get_max_nal_fn real_get_max_nal;
static imp_alloc_fn real_imp_alloc;

static void trace_line(const char *format, ...)
{
    FILE *stream;
    va_list args;

    stream = fopen("/tmp/t31-stream-size-trace.log", "a");
    if (!stream)
        return;
    fprintf(stream, "pid=%ld ", (long)getpid());
    va_start(args, format);
    vfprintf(stream, format, args);
    va_end(args);
    fputc('\n', stream);
    fclose(stream);
}

int32_t GetPcmVclNalSize(int32_t width, int32_t height, int32_t format,
                         int32_t bit_depth)
{
    int32_t result;

    if (!real_get_pcm)
        real_get_pcm = (get_pcm_fn)dlsym(RTLD_NEXT, "GetPcmVclNalSize");
    if (!real_get_pcm)
        return -1;
    result = real_get_pcm(width, height, format, bit_depth);
    trace_line("GetPcmVclNalSize w=%d h=%d format=%d depth=%d -> 0x%x",
               width, height, format, bit_depth, (unsigned int)result);
    return result;
}

int32_t AL_GetMaxNalSize(int32_t codec, int32_t width, int32_t height,
                         int32_t format, int32_t bit_depth, int32_t level,
                         int32_t profile)
{
    int32_t result;

    if (!real_get_max_nal)
        real_get_max_nal =
            (get_max_nal_fn)dlsym(RTLD_NEXT, "AL_GetMaxNalSize");
    if (!real_get_max_nal)
        return -1;
    result = real_get_max_nal(codec, width, height, format, bit_depth,
                              level, profile);
    trace_line("AL_GetMaxNalSize codec=%d w=%d h=%d format=%d depth=%d "
               "level=%d profile=%d -> 0x%x",
               codec, width, height, format, bit_depth, level, profile,
               (unsigned int)result);
    return result;
}

int32_t IMP_Alloc(void *info, int32_t size, const char *tag)
{
    int32_t result;

    if (!real_imp_alloc)
        real_imp_alloc = (imp_alloc_fn)dlsym(RTLD_NEXT, "IMP_Alloc");
    if (!real_imp_alloc)
        return -1;
    result = real_imp_alloc(info, size, tag);
    trace_line("IMP_Alloc info=%p size=0x%x tag=%s -> %d",
               info, (unsigned int)size, tag ? tag : "(null)", result);
    return result;
}
