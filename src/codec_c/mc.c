#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

/* forward decl, ported by T55 later */
void c_copy_frame_nv12_to_nv12(int32_t *arg1, int32_t *arg2, int32_t *arg3, int32_t *arg4,
                               size_t arg5, int32_t arg6);
/* forward decl, ported by T55 later */
int32_t c_copy_frame_i420_to_nv12(int32_t *arg1, int32_t *arg2, int32_t *arg3, int32_t *arg4,
                                  size_t arg5, int32_t arg6);
/* forward decl, ported by T55 later */
void c_copy_frame_nv21_to_nv12(int32_t *arg1, int32_t *arg2, int32_t *arg3, int32_t *arg4,
                               size_t arg5, int32_t arg6);
/* forward decl, ported by T55 later */
int32_t c_copy_frame_t420_to_nv12(int32_t *arg1, int32_t *arg2, int32_t *arg3, int32_t *arg4,
                                  int32_t arg5, int32_t arg6);
/* forward decl, ported by T55 later */
int32_t c_copy_frame_nv12_to_t420(int32_t *arg1, int32_t *arg2, int32_t *arg3, int32_t *arg4,
                                  int32_t arg5, int32_t arg6);
/* forward decl, ported by T55 later */
int32_t c_copy_frame_i420_to_t420(int32_t *arg1, int32_t *arg2, int32_t *arg3, int32_t *arg4,
                                  int32_t arg5, int32_t arg6);
/* forward decl, ported by T55 later */
int32_t c_copy_frame_nv21_to_t420(int32_t *arg1, int32_t *arg2, int32_t *arg3, int32_t *arg4,
                                  int32_t arg5, int32_t arg6);
/* forward decl, ported by T55 later */
int32_t c_copy_frame_t420_to_t420(void **arg1, int32_t *arg2, int32_t *arg3, int32_t *arg4,
                                  int32_t arg5, int32_t arg6);
/* forward decl, ported by T57 later */
int32_t c_log(int32_t level, const char *fmt, ...);

static int32_t data_1172f4 = 0;
static int32_t data_1172f8 = 0;
static int32_t data_1172fc = 0;
static int32_t data_117300 = 0;
static int32_t data_117304 = 0;
static uint8_t data_117308 = 0;
static int32_t data_117310 = 0;
static int32_t data_11730c = -1;
static int32_t kmmc = 0;

static const int32_t CSWTCH_45[6] = {5, 0, 0, 4, 6, 7};
static const int32_t CSWTCH_44[6] = {1, 0, 0, 0, 2, 3};

int32_t c_mc_init(void (**arg1)(int32_t *arg1, int32_t *arg2, int32_t *arg3, int32_t *arg4,
                                size_t arg5, int32_t arg6))
{
    *arg1 = c_copy_frame_nv12_to_nv12;
    arg1[1] = (void (*)(int32_t *, int32_t *, int32_t *, int32_t *, size_t, int32_t))c_copy_frame_i420_to_nv12;
    arg1[2] = c_copy_frame_nv21_to_nv12;
    arg1[3] = (void (*)(int32_t *, int32_t *, int32_t *, int32_t *, size_t, int32_t))c_copy_frame_t420_to_nv12;
    arg1[4] = (void (*)(int32_t *, int32_t *, int32_t *, int32_t *, size_t, int32_t))c_copy_frame_nv12_to_t420;
    arg1[5] = (void (*)(int32_t *, int32_t *, int32_t *, int32_t *, size_t, int32_t))c_copy_frame_i420_to_t420;
    arg1[6] = (void (*)(int32_t *, int32_t *, int32_t *, int32_t *, size_t, int32_t))c_copy_frame_nv21_to_t420;
    arg1[7] = (void (*)(int32_t *, int32_t *, int32_t *, int32_t *, size_t, int32_t))c_copy_frame_t420_to_t420;
    return 0;
}

int32_t c_mc_type(int32_t arg1, int32_t arg2)
{
    int32_t v1 = (uint32_t)(arg2 - 1) < 6 ? 1 : 0;
    int32_t result;

    if (arg1 == 4) {
        result = 0;
        if (v1 != 0) {
            return CSWTCH_44[arg2 - 1];
        }
    } else {
        result = 0;
        if (v1 != 0) {
            return CSWTCH_45[arg2 - 1];
        }
    }

    return result;
}

int32_t get_kmem_info(void)
{
    int32_t var_14 = 0;
    int32_t var_18 = 0;
    char buf[0x200];
    int32_t *haystack = (int32_t *)buf;
    char *str = &buf[4];

    memset(str, 0, 0x1fc);

    FILE *stream = fopen("/proc/cmdline", "r");
    if (stream == NULL) {
        c_log(0, "%s open file (%s) error\n", "get_kmem_info", "/proc/cmdline");
        return -1;
    }

    size_t v0 = fread(haystack, 1, 0x200, stream);
    char *str_1 = NULL;
    if (v0 != 0) {
        str_1 = strstr((char *)haystack, "rmem");
    }

    if (v0 == 0 || str_1 == NULL) {
        c_log(0, "%s fread (%s) error\n", "get_kmem_info", "/proc/cmdline");
    } else {
        int32_t v0_2 = (int32_t)(*(strchr(str_1, 0x40) - 1));
        int32_t a2_3;

        if (v0_2 == 0x4d) {
            sscanf(str_1, "rmem=%dM@%x", &var_18, &var_14);
            a2_3 = var_18 << 0x14;
            var_18 = a2_3;
label_32db8:
            {
                int32_t a3_3 = var_14;
                data_1172f4 = a3_3;
                data_1172f8 = a2_3;
                if (a3_3 != 0 && a2_3 != 0) {
                    c_log(2, "CMD Line Rmem Size:%x, Addr:0x%08x\n");
                    fclose(stream);
                    return 0;
                }
                c_log(0, "CMD Line Rmem Size:%d, Addr:0x%08x is invalide\n");
            }
        } else if (v0_2 == 0x4b) {
            sscanf(str_1, "rmem=%dK@%x", &var_18, &var_14);
            a2_3 = var_18 << 0xa;
            var_18 = a2_3;
            goto label_32db8;
        }
    }

    fclose(stream);
    return -1;
}

int32_t kmmc_init(void)
{
    if (get_kmem_info() < 0) {
        c_log(0, "get kmem info failed\n");
        return -1;
    }

    int32_t v0_1 = open("/dev/rmem", 2);
    data_11730c = v0_1;
    if (v0_1 <= 0) {
        c_log(0, "open %s failed\n", "/dev/rmem");
        return -1;
    }

    int32_t v0_2 = (int32_t)(intptr_t)mmap(0, data_1172f8, 3, 1, v0_1, data_1172f4);
    kmmc = v0_2;
    if (v0_2 <= 0) {
        c_log(0, "mmap failed\n");
        close(data_11730c);
        return -1;
    }

    c_log(2, "kmem_vaddr=%x, kmem_paddr=%x, kmem_length=%x\n", v0_2, data_1172f4, data_1172f8);
    {
        int32_t kmmc_1 = kmmc;
        int32_t v1_3 = data_1172f4;
        data_117304 = data_1172f8;
        data_117308 = 1;
        data_1172fc = kmmc_1;
        data_117300 = v1_3;
        data_117310 = 0;
    }
    return 0;
}

int32_t *kmmc_deinit(void)
{
    int32_t *kmmc_1 = (int32_t *)0x110000;

    if (data_117308 != 0 && data_117310 == 0) {
        int32_t a0_1 = data_11730c;
        kmmc_1 = (int32_t *)(intptr_t)kmmc;
        if (a0_1 > 0) {
            if ((int32_t)(intptr_t)kmmc_1 > 0) {
                int32_t a1_1 = data_1172f8;
                if (a1_1 != 0) {
                    munmap(kmmc_1, a1_1);
                    a0_1 = data_11730c;
                }
            }
            close(a0_1);
        }
        data_11730c = -1;
        data_117308 = 0;
        kmmc_1 = &data_1172fc;
        data_1172fc = 0;
        data_117300 = 0;
        data_117304 = 0;
    }

    return kmmc_1;
}

int32_t c_kalloc(int32_t arg1, int32_t arg2)
{
    int32_t v0_4;

    if (data_117308 == 0) {
        v0_4 = kmmc_init();
        if (data_117308 == 0 && v0_4 < 0) {
            return 0;
        }
    }

    {
        int32_t a3_1 = data_1172fc;
        int32_t result = (arg2 - 1 + a3_1) & -arg2;
        int32_t v1_1 = data_117304 - (result - a3_1);
        if ((uint32_t)arg1 < (uint32_t)v1_1) {
            int32_t a0 = data_117310 + 1;
            data_1172fc = result + arg1;
            data_117304 = v1_1 - arg1;
            data_117310 = a0;
            return result;
        }
        return 0;
    }
}

int32_t c_kfree(void)
{
    int32_t result = data_117310;
    if (result > 0) {
        result -= 1;
        data_117310 = result;
        if (result == 0) {
            return (int32_t)(intptr_t)kmmc_deinit();
        }
    }
    return result;
}

int32_t c_kvirt_to_kphys(int32_t arg1)
{
    int32_t kmmc_1 = kmmc;
    int32_t a1_1;
    int32_t a2;

    if (arg1 < kmmc_1) {
        a1_1 = data_1172f8;
        a2 = kmmc_1 + a1_1;
    } else {
        a1_1 = data_1172f8;
        a2 = kmmc_1 + a1_1;
        if ((uint32_t)a2 >= (uint32_t)arg1) {
            return arg1 - kmmc_1 + data_1172f4;
        }
    }

    c_log(0,
          "%s:vaddr input error:vaddr=%x, kmmc.base.vaddr=%x, kmmc.base.size=%x, "
          "kmmc.base.vaddr + kmmc.base.size=%x\n",
          "c_kvirt_to_kphys", arg1, kmmc_1, a1_1, a2);
    return 0;
}

int32_t c_kphys_to_kvirt(int32_t arg1)
{
    int32_t v0 = data_1172f4;
    int32_t v1 = data_1172f8;
    int32_t a2_1;

    if (arg1 < v0) {
        a2_1 = v0 + v1;
    } else {
        a2_1 = v0 + v1;
        if ((uint32_t)a2_1 >= (uint32_t)arg1) {
            return arg1 - v0 + kmmc;
        }
    }

    c_log(0,
          "%s:paddr input error:paddr=%x, kmmc.base.paddr=%x, kmmc.base.size=%x, "
          "kmmc.base.paddr + kmmc.base.size=%x\n",
          "c_kphys_to_kvirt", arg1, v0, v1, a2_1);
    return 0;
}
