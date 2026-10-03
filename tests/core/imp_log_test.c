/* Host test: imp_log_fun/IMP_Log_Get_Option. Build: see Makefile-free one-liner
 *   cc -I src tests/core/imp_log_test.c src/core/imp_log.c -o /tmp/imp_log_test */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "imp_log_fun.h"

int main(void)
{
    char out[256] = {0};
    int fd[2], save = dup(2);

    if (pipe(fd)) return 1;
    dup2(fd[1], 2);
    imp_log_fun(6, 1, 2, "Device", "/a/b/device.c", 0x1d, "alloc_device", "bad %d %s\n", 7, "x");
    imp_log_fun(3, 1, 2, "Device", "/a/b/device.c", 1, "f", "hidden\n");
    fflush(stderr);
    dup2(save, 2);
    close(fd[1]);
    read(fd[0], out, sizeof(out) - 1);
    if (!strstr(out, "[Device] device.c:29 alloc_device: bad 7 x\n") || strstr(out, "hidden")) {
        printf("FAIL: %s", out);
        return 1;
    }
    /* syslog is opt-in: default is stderr only */
    if (IMP_Log_Get_Option() != 1) return 1;
    imp_log_fun(6, 1, 2, "T", "f.c", 1, "f", "");
    puts("imp_log_test OK");
    return 0;
}
