/* tpctl: control the test pattern of a running process (timps, roitest, ...) that loaded libimp with
 * OPENIMP_TEST_PATTERN_CTL=<socket path> (default /tmp/openimp_tp_ctl).
 *   tpctl get
 *   tpctl set <0..4> [step=N] [logo=0..1] [noise=N] [obj_size=N] [obj_contrast=N] [obj_speed=N]
 *                    [obj_cell=X,Y] [obj_on_ms=N] [obj_period_ms=N] [script=PATH]
 * Prints the reply (the state after the call); exit 0 on "ok", 1 on error, 2 no answer.
 * Build: gcc -O2 -o tpctl tpctl.c (or the mipsel toolchain).  See include/openimp_testpattern.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>

int main(int argc, char **argv)
{
    struct sockaddr_un srv, me;
    struct timeval tv = { 3, 0 };
    char cmd[512] = "", rep[1024];
    const char *path = getenv("OPENIMP_TEST_PATTERN_CTL");
    ssize_t n;
    int fd, i;

    if (argc < 2) {
        fprintf(stderr, "usage: %s get | set <mode 0..4 (4 = sensor pattern)> [key=value ...]\n", argv[0]);
        return 1;
    }
    for (i = 1; i < argc; i++) {
        if (strlen(cmd) + strlen(argv[i]) + 2 >= sizeof cmd)
            return 1;
        strcat(cmd, argv[i]);
        strcat(cmd, " ");
    }
    fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0)
        return 2;
    memset(&me, 0, sizeof me);
    me.sun_family = AF_UNIX;                       /* abstract name: no file left behind */
    snprintf(me.sun_path + 1, sizeof me.sun_path - 1, "tpctl.%d", (int)getpid());
    if (bind(fd, (struct sockaddr *)&me, sizeof(sa_family_t) + 1 + strlen(me.sun_path + 1)) < 0)
        return 2;
    memset(&srv, 0, sizeof srv);
    srv.sun_family = AF_UNIX;
    snprintf(srv.sun_path, sizeof srv.sun_path, "%s", path && path[0] ? path : "/tmp/openimp_tp_ctl");
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    if (sendto(fd, cmd, strlen(cmd), 0, (struct sockaddr *)&srv, sizeof srv) < 0) {
        perror("tpctl: send");
        return 2;
    }
    n = recv(fd, rep, sizeof rep - 1, 0);
    if (n < 0) {
        fprintf(stderr, "tpctl: no answer\n");
        return 2;
    }
    rep[n] = 0;
    puts(rep);
    return strncmp(rep, "ok", 2) ? 1 : 0;
}
