#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef int (*set_default_fn)(void *, uint32_t, uint32_t, uint16_t, uint16_t,
                              uint32_t, uint32_t, uint32_t, int32_t, int32_t,
                              uint32_t);

struct test_case {
    const char *name;
    uint32_t profile;
    uint32_t rc_mode;
    uint32_t bitrate;
};

int main(int argc, char **argv)
{
    static const struct test_case cases[] = {
        { "avc-cbr", 0x00000064, 1, 3000000 },
        { "avc-vbr", 0x00000064, 2, 2000000 },
        { "hevc-capped-vbr", 0x01000001, 4, 1800000 },
        { "hevc-capped-quality", 0x01000001, 8, 1500000 },
        { "jpeg-fixqp", 0x04000000, 0, 0 },
    };
    void *oem_handle;
    void *recovered_handle;
    set_default_fn oem_fn;
    set_default_fn recovered_fn;
    unsigned char oem_attr[256];
    unsigned char recovered_attr[256];
    unsigned int case_index;
    const char *openimp_library =
        argc > 1 ? argv[1] : "/tmp/libimp-openimp-t40.so";

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
    oem_fn = (set_default_fn)dlsym(oem_handle, "IMP_Encoder_SetDefaultParam");
    recovered_fn = (set_default_fn)dlsym(
        recovered_handle, "IMP_Encoder_SetDefaultParam");
    if (!oem_fn || !recovered_fn) {
        printf("dlsym failed: %s\n", dlerror());
        return 4;
    }

    for (case_index = 0; case_index < sizeof(cases) / sizeof(cases[0]);
         case_index++) {
        int oem_rc;
        int recovered_rc;
        unsigned int offset;

        memset(oem_attr, 0xa5, sizeof(oem_attr));
        memset(recovered_attr, 0xa5, sizeof(recovered_attr));
        oem_rc = oem_fn(oem_attr, cases[case_index].profile,
                        cases[case_index].rc_mode, 1920, 1080,
                        30, 1, 30, 2, -1, cases[case_index].bitrate);
        recovered_rc = recovered_fn(recovered_attr, cases[case_index].profile,
                                    cases[case_index].rc_mode, 1920, 1080,
                                    30, 1, 30, 2, -1,
                                    cases[case_index].bitrate);
        if (oem_rc != recovered_rc || memcmp(oem_attr, recovered_attr, 112) != 0) {
            for (offset = 0; offset < 112; offset++) {
                if (oem_attr[offset] != recovered_attr[offset])
                    break;
            }
            printf("FAIL %s rc=%d/%d first_diff=%u oem=%02x recovered=%02x\n",
                   cases[case_index].name, oem_rc, recovered_rc, offset,
                   offset < 112 ? oem_attr[offset] : 0,
                   offset < 112 ? recovered_attr[offset] : 0);
            return 10 + case_index;
        }
        printf("PASS %s rc=%d\n", cases[case_index].name, recovered_rc);
    }

    return 0;
}
