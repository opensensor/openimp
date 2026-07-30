#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define OPENIMP_ALL_ROOTS 0x3fU

typedef int (*system_fn)(void);
typedef int64_t (*timestamp_fn)(void);
typedef const char *(*cpu_info_fn)(void);
typedef int (*state_fn)(uint32_t *, uint32_t *, int32_t *, int32_t *);

static int maps_have_oem_libimp(void)
{
    char line[512];
    FILE *maps = fopen("/proc/self/maps", "r");

    if (!maps)
        return -1;
    while (fgets(line, sizeof(line), maps)) {
        char *name = strstr(line, "/libimp.so");
        if (name && (name[10] == '\0' || name[10] == '\n' ||
                     name[10] == ' ' || name[10] == '\r')) {
            fclose(maps);
            return 1;
        }
    }
    fclose(maps);
    return 0;
}

static int read_state(state_fn get_state, int expected_initialized,
                      uint32_t expected_roots, uint32_t *generation)
{
    uint32_t roots = 0;
    int32_t main_process = -1;
    int32_t cpu_id = -1;
    int initialized = get_state(generation, &roots, &main_process, &cpu_id);

    if (initialized != expected_initialized || roots != expected_roots ||
        main_process != expected_initialized || cpu_id != 22) {
        fprintf(stderr,
                "state mismatch: init=%d roots=0x%08x main=%d cpu=%d\n",
                initialized, roots, main_process, cpu_id);
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *library = argc > 1 ? argv[1] : "/tmp/libimp-t40-p0.so";
    void *handle;
    system_fn system_init;
    system_fn system_exit;
    timestamp_fn get_timestamp;
    cpu_info_fn get_cpu_info;
    state_fn get_state;
    uint32_t generation1 = 0;
    uint32_t generation2 = 0;
    uint32_t generation3 = 0;
    uint32_t stress_generation = 0;
    unsigned long stress_cycles = argc > 2 ? strtoul(argv[2], NULL, 0) : 100;
    unsigned long cycle;
    int64_t timestamp1;
    int64_t timestamp2;
    int maps_result;

    handle = dlopen(library, RTLD_LAZY | RTLD_LOCAL);
    if (!handle) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return 1;
    }

    system_init = (system_fn)dlsym(handle, "IMP_System_Init");
    system_exit = (system_fn)dlsym(handle, "IMP_System_Exit");
    get_timestamp = (timestamp_fn)dlsym(handle, "IMP_System_GetTimeStamp");
    get_cpu_info = (cpu_info_fn)dlsym(handle, "IMP_System_GetCPUInfo");
    get_state = (state_fn)dlsym(handle, "OpenIMP_P0_GetState");
    if (!system_init || !system_exit || !get_timestamp || !get_cpu_info ||
        !get_state) {
        fprintf(stderr, "required symbol missing: %s\n", dlerror());
        return 2;
    }

    maps_result = maps_have_oem_libimp();
    if (maps_result != 0) {
        fprintf(stderr, "OEM libimp map check failed: %d\n", maps_result);
        return 3;
    }
    if (system_init() != 0)
        return 4;
    if (read_state(get_state, 1, OPENIMP_ALL_ROOTS, &generation1) != 0)
        return 5;
    if (generation1 == 0)
        return 6;

    if (system_init() != 0)
        return 7;
    if (read_state(get_state, 1, OPENIMP_ALL_ROOTS, &generation2) != 0)
        return 8;
    if (generation2 != generation1) {
        fprintf(stderr, "init was not idempotent: %u -> %u\n",
                generation1, generation2);
        return 9;
    }

    timestamp1 = get_timestamp();
    usleep(20000);
    timestamp2 = get_timestamp();
    if (timestamp1 < 0 || timestamp2 <= timestamp1) {
        fprintf(stderr, "timestamp did not advance: %lld -> %lld\n",
                (long long)timestamp1, (long long)timestamp2);
        return 10;
    }
    if (strcmp(get_cpu_info(), "T40-XP") != 0)
        return 11;

    if (system_exit() != 0)
        return 12;
    if (read_state(get_state, 0, 0, &generation2) != 0)
        return 13;
    if (system_exit() != 0)
        return 14;

    if (system_init() != 0)
        return 15;
    if (read_state(get_state, 1, OPENIMP_ALL_ROOTS, &generation3) != 0)
        return 16;
    if (generation3 != generation1 + 1) {
        fprintf(stderr, "reinit generation mismatch: %u -> %u\n",
                generation1, generation3);
        return 17;
    }
    if (system_exit() != 0)
        return 18;

    for (cycle = 0; cycle < stress_cycles; cycle++) {
        if (system_init() != 0)
            return 20;
        if (read_state(get_state, 1, OPENIMP_ALL_ROOTS,
                       &stress_generation) != 0)
            return 21;
        if (stress_generation != generation3 + cycle + 1) {
            fprintf(stderr,
                    "stress generation mismatch at %lu: expected %lu got %u\n",
                    cycle, (unsigned long)generation3 + cycle + 1,
                    stress_generation);
            return 22;
        }
        if (system_exit() != 0)
            return 23;
        if (read_state(get_state, 0, 0, &stress_generation) != 0)
            return 24;
    }

    maps_result = maps_have_oem_libimp();
    if (maps_result != 0) {
        fprintf(stderr, "OEM libimp appeared during probe: %d\n", maps_result);
        return 19;
    }

    printf("OPENIMP_P0_PASS generation=%u roots=0x%08x cpu=T40-XP "
           "timestamp_delta_us=%lld stress_cycles=%lu oem_mapped=0\n",
           stress_generation, OPENIMP_ALL_ROOTS,
           (long long)(timestamp2 - timestamp1), stress_cycles);
    dlclose(handle);
    return 0;
}
