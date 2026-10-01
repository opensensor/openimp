#define _GNU_SOURCE
#include "openimp_t23_helix_ipc.h"

#include <dlfcn.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <syslog.h>
#include <unistd.h>

#include <imp/imp_common.h>
#include <imp/imp_encoder.h>

/* Private entry points exported by Ingenic's T23 libimp. */
extern int EncoderInit(void);
extern int EncoderExit(void);
extern int IMP_FlushCache(void *address, uint32_t size, int direction);
/* IMP_Encoder_Yuv* and IMP_Encoder_Vbm* are declared by imp/imp_encoder.h */

#define T23_HELIX_PAGE_SIZE 4096u
#define T23_CACHE_WBACK 1

typedef struct {
    int socket_fd;
    unsigned char *shared;
    size_t shared_size;
    void *encoder;
    unsigned char *input_buffer;
    unsigned char *output_buffer;
    uint32_t input_physical;
    uint32_t input_size;
    uint32_t input_capacity;
    uint32_t output_capacity;
    uint32_t width;
    uint32_t height;
    int subsystem_ready;
} T23HelixWorker;

_Static_assert(sizeof(T23EncoderYuvIn) == 0x3c,
               "T23 YUV encoder input ABI mismatch");
_Static_assert(sizeof(T23EncoderYuvOut) == 0x08,
               "T23 YUV encoder output ABI mismatch");

static int read_all(int fd, void *buffer, size_t size)
{
    unsigned char *bytes = buffer;
    size_t consumed = 0u;

    while (consumed < size) {
        ssize_t count = read(fd, bytes + consumed, size - consumed);

        if (count < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (!count)
            return -1;
        consumed += (size_t)count;
    }
    return 0;
}

static int write_all(int fd, const void *buffer, size_t size)
{
    const unsigned char *bytes = buffer;
    size_t written = 0u;

    while (written < size) {
        ssize_t count = write(fd, bytes + written, size - written);

        if (count < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (!count)
            return -1;
        written += (size_t)count;
    }
    return 0;
}

static void worker_release(T23HelixWorker *worker)
{
    if (!worker)
        return;
    if (worker->input_buffer)
        IMP_Encoder_VbmFree(worker->input_buffer);
    if (worker->output_buffer)
        IMP_Encoder_VbmFree(worker->output_buffer);
    if (worker->encoder)
        IMP_Encoder_YuvExit(worker->encoder);
    if (worker->subsystem_ready)
        EncoderExit();
    worker->input_buffer = NULL;
    worker->output_buffer = NULL;
    worker->encoder = NULL;
    worker->subsystem_ready = 0;
}

static int worker_alloc_input(T23HelixWorker *worker)
{
    intptr_t physical;

    if (worker->input_buffer)
        return 0;
    worker->input_buffer =
        IMP_Encoder_VbmAlloc(worker->input_capacity, T23_HELIX_PAGE_SIZE);
    if (!worker->input_buffer)
        return -ENOMEM;
    physical = IMP_Encoder_VbmV2P((intptr_t)(uintptr_t)worker->input_buffer);
    if (physical <= 0 || (uintptr_t)physical > UINT32_MAX)
        return -EFAULT;
    memset(worker->input_buffer, 0, worker->input_capacity);
    if (IMP_FlushCache(worker->input_buffer, worker->input_capacity,
                       T23_CACHE_WBACK) != 0)
        return -EIO;
    worker->input_physical = (uint32_t)(uintptr_t)physical;
    return 0;
}

/*
 * The frame in place: the OEM kmem allocator maps the whole reserved
 * memory (/dev/rmem) linearly, and OpenIMP's frame pools live in the same
 * reserved memory, so IMP_Encoder_VbmP2V gives this process's address of
 * the frame. The OEM YuvEncode takes the luma/chroma addresses from
 * virAddr and translates them back with V2P for the core, so both ends of
 * the frame must translate back to the address OpenIMP sent.
 */
static uint32_t worker_map_input(uint32_t physical, uint32_t size)
{
    intptr_t virt;
    intptr_t last;

    if (!physical || !size || physical > UINT32_MAX - size)
        return 0u;
    virt = IMP_Encoder_VbmP2V((intptr_t)physical);
    if (virt <= 0)
        return 0u;
    last = IMP_Encoder_VbmV2P(virt + (intptr_t)size - 1);
    if (IMP_Encoder_VbmV2P(virt) != (intptr_t)physical ||
        last != (intptr_t)(physical + size - 1u))
        return 0u;
    return (uint32_t)virt;
}

static int worker_init(T23HelixWorker *worker,
                       const T23HelixIpcRequest *request)
{
    uint64_t required;

    if (!worker || !request || request->width == 0u ||
        request->height == 0u || request->input_size == 0u ||
        request->input_size > request->input_capacity ||
        request->input_capacity % T23_HELIX_PAGE_SIZE != 0u ||
        request->output_capacity == 0u ||
        request->output_capacity % T23_HELIX_PAGE_SIZE != 0u)
        return -EINVAL;
    required = (uint64_t)request->input_capacity + request->output_capacity;
    if (required > worker->shared_size)
        return -EINVAL;
    if (EncoderInit() != 0)
        return -EIO;
    worker->subsystem_ready = 1;
    if (IMP_Encoder_YuvInit(&worker->encoder, (int)request->width,
                            (int)request->height,
                            (T23EncoderYuvIn *)&request->encoder_input) != 0 ||
        !worker->encoder)
        return -EIO;

    worker->output_buffer =
        IMP_Encoder_VbmAlloc(request->output_capacity, T23_HELIX_PAGE_SIZE);
    if (!worker->output_buffer)
        return -ENOMEM;
    memset(worker->output_buffer, 0, request->output_capacity);
    if (IMP_FlushCache(worker->output_buffer, request->output_capacity,
                       T23_CACHE_WBACK) != 0)
        return -EIO;
    worker->input_size = request->input_size;
    worker->input_capacity = request->input_capacity;
    /* Zero-copy sessions read the frames in place; the input copy buffer
     * (a frame-sized VBM allocation) is only made for a copied frame. */
    if (!(request->flags & T23_HELIX_INIT_ZERO_COPY)) {
        int status = worker_alloc_input(worker);

        if (status != 0)
            return status;
    }

    worker->output_capacity = request->output_capacity;
    worker->width = request->width;
    worker->height = request->height;
    syslog(LOG_NOTICE,
           "openimp/T23 helper: Helix ready %ux%u input=%u output=%u",
           worker->width, worker->height, worker->input_size,
           worker->output_capacity);
    return 0;
}

static int worker_encode(T23HelixWorker *worker,
                         const T23HelixIpcRequest *request,
                         T23HelixIpcResponse *response)
{
    IMPFrameInfo frame;
    T23EncoderYuvOut output;
    uintptr_t output_begin;
    uintptr_t buffer_begin;
    uintptr_t buffer_end;
    unsigned char *shared_output;

    if (!worker || !request || !response || !worker->encoder ||
        request->width != worker->width ||
        request->height != worker->height ||
        request->input_size != worker->input_size)
        return -EINVAL;

    memset(&frame, 0, sizeof(frame));
    if (request->input_physical) {
        /* OpenIMP holds the frame until our response and wrote back its
         * CPU view; the core reads it from memory. */
        uint32_t virt = worker_map_input(request->input_physical,
                                         worker->input_size);

        if (!virt)
            return -EFAULT;     /* OpenIMP falls back to copying */
        frame.phyAddr = request->input_physical;
        frame.virAddr = virt;
        frame.direct_phyAddr = request->input_physical;
    } else {
        int status = worker_alloc_input(worker);

        if (status != 0)
            return status;
        memcpy(worker->input_buffer, worker->shared, worker->input_size);
        if (IMP_FlushCache(worker->input_buffer, worker->input_size,
                           T23_CACHE_WBACK) != 0)
            return -EIO;
        frame.phyAddr = worker->input_physical;
        frame.virAddr = (uint32_t)(uintptr_t)worker->input_buffer;
        frame.direct_phyAddr = worker->input_physical;
    }
    frame.index = -1;
    frame.pool_idx = -1;
    frame.width = worker->width;
    frame.height = worker->height;
    frame.pixfmt = request->pixel_format;
    frame.size = worker->input_size;
    frame.timeStamp = request->timestamp;

    output.outAddr = worker->output_buffer;
    output.outLen = worker->output_capacity;
    if (IMP_Encoder_YuvEncode(worker->encoder, frame, &output) != 0 ||
        !output.outAddr || !output.outLen)
        return -EIO;

    buffer_begin = (uintptr_t)worker->output_buffer;
    buffer_end = buffer_begin + worker->output_capacity;
    output_begin = (uintptr_t)output.outAddr;
    if (output_begin < buffer_begin || output_begin > buffer_end ||
        output.outLen > buffer_end - output_begin)
        return -EOVERFLOW;

    shared_output = worker->shared + worker->input_capacity;
    memcpy(shared_output, output.outAddr, output.outLen);
    __sync_synchronize();
    response->output_offset = worker->input_capacity;
    response->output_length = output.outLen;
    return 0;
}

static int parse_fd(const char *text)
{
    char *end = NULL;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno || !end || *end || value < 0 || value > 0x7fffffffL)
        return -1;
    return (int)value;
}

static int parse_size(const char *text, size_t *size)
{
    char *end = NULL;
    unsigned long value;

    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno || !end || *end || !value || value > UINT32_MAX)
        return -1;
    *size = (size_t)value;
    return 0;
}

int main(int argc, char **argv)
{
    T23HelixWorker worker;
    int shared_fd;
    int result = 1;

    if (argc != 4)
        return 2;
    memset(&worker, 0, sizeof(worker));
    worker.socket_fd = parse_fd(argv[1]);
    shared_fd = parse_fd(argv[2]);
    if (worker.socket_fd < 0 || shared_fd < 0 ||
        parse_size(argv[3], &worker.shared_size) != 0)
        return 2;
    worker.shared = mmap(NULL, worker.shared_size, PROT_READ | PROT_WRITE,
                         MAP_SHARED, shared_fd, 0);
    close(shared_fd);
    if (worker.shared == MAP_FAILED)
        return 2;

    signal(SIGPIPE, SIG_IGN);
    openlog("openimp-t23-helixd", LOG_PID, LOG_DAEMON);
    /* This worker must run on the OEM libimp.  OpenIMP exports the same
     * IMP_Encoder_Yuv* entry points and would serve them by spawning
     * another worker, so refuse to run on it (e.g. when
     * /opt/openimp-t23/libimp.so was overwritten with OpenIMP). */
    if (dlsym(RTLD_DEFAULT, "OpenIMP_P0_GetState")) {
        syslog(LOG_ERR, "openimp/T23 helper: libimp.so is OpenIMP, not "
                        "the OEM library; refusing to run");
        munmap(worker.shared, worker.shared_size);
        close(worker.socket_fd);
        closelog();
        return 3;
    }
    for (;;) {
        T23HelixIpcRequest request;
        T23HelixIpcResponse response;
        int status;

        if (read_all(worker.socket_fd, &request, sizeof(request)) != 0)
            break;
        memset(&response, 0, sizeof(response));
        response.magic = T23_HELIX_IPC_MAGIC;
        response.version = T23_HELIX_IPC_VERSION;
        response.command = request.command;
        if (request.magic != T23_HELIX_IPC_MAGIC ||
            request.version != T23_HELIX_IPC_VERSION) {
            status = -EPROTO;
        } else {
            switch (request.command) {
            case T23_HELIX_COMMAND_INIT:
                status = worker.encoder ? -EALREADY
                                        : worker_init(&worker, &request);
                break;
            case T23_HELIX_COMMAND_ENCODE:
                status = worker_encode(&worker, &request, &response);
                break;
            case T23_HELIX_COMMAND_REQUEST_IDR:
                status = worker.encoder
                             ? IMP_Encoder_YuvRequestIDR(worker.encoder)
                             : -EINVAL;
                break;
            case T23_HELIX_COMMAND_EXIT:
                status = 0;
                break;
            default:
                status = -ENOSYS;
                break;
            }
        }
        response.status = status;
        if (write_all(worker.socket_fd, &response, sizeof(response)) != 0)
            break;
        if (request.command == T23_HELIX_COMMAND_EXIT) {
            result = status ? 1 : 0;
            break;
        }
    }

    worker_release(&worker);
    munmap(worker.shared, worker.shared_size);
    close(worker.socket_fd);
    closelog();
    return result;
}
