/* Host stand-in for openimp-t23-helixd: speaks the bridge IPC and reports
 * how each frame arrived, so helix_zero_copy_test can check the bridge. */
#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "t23/openimp_t23_helix_ipc.h"

#define FAKE_UNREACHABLE_PHYS 0xdead0000u

static int io_all(int fd, void *buffer, size_t size, int writing)
{
    unsigned char *bytes = buffer;
    size_t done = 0u;

    while (done < size) {
        ssize_t n = writing ? write(fd, bytes + done, size - done)
                            : read(fd, bytes + done, size - done);

        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return -1;
        done += (size_t)n;
    }
    return 0;
}

int main(int argc, char **argv)
{
    int fd, shared_fd;
    size_t shared_size;
    unsigned char *shared;
    uint32_t init_flags = 0u;

    if (argc != 4)
        return 2;
    fd = atoi(argv[1]);
    shared_fd = atoi(argv[2]);
    shared_size = (size_t)strtoul(argv[3], NULL, 10);
    shared = mmap(NULL, shared_size, PROT_READ | PROT_WRITE, MAP_SHARED,
                  shared_fd, 0);
    if (shared == MAP_FAILED)
        return 2;
    for (;;) {
        T23HelixIpcRequest request;
        T23HelixIpcResponse response;

        if (io_all(fd, &request, sizeof(request), 0) != 0)
            return 1;
        memset(&response, 0, sizeof(response));
        response.magic = T23_HELIX_IPC_MAGIC;
        response.version = T23_HELIX_IPC_VERSION;
        response.command = request.command;
        if (request.version != T23_HELIX_IPC_VERSION) {
            response.status = -EPROTO;
        } else if (request.command == T23_HELIX_COMMAND_INIT) {
            init_flags = request.flags;
        } else if (request.command == T23_HELIX_COMMAND_ENCODE) {
            /* out: [kind, init_flags, value(4)] */
            unsigned char *out = shared + request.input_capacity;
            uint32_t value = 0u;
            uint32_t i;

            if (request.input_physical == FAKE_UNREACHABLE_PHYS) {
                response.status = -EFAULT;
            } else {
                if (request.input_physical) {
                    out[0] = 'P';
                    value = request.input_physical;
                } else {
                    out[0] = 'C';
                    for (i = 0; i < request.input_size; i++)
                        value = value * 31u + shared[i];
                }
                out[1] = (unsigned char)init_flags;
                memcpy(out + 2, &value, sizeof(value));
                response.output_offset = request.input_capacity;
                response.output_length = 6u;
            }
        }
        if (io_all(fd, &response, sizeof(response), 1) != 0)
            return 1;
        if (request.command == T23_HELIX_COMMAND_EXIT)
            return 0;
    }
}
