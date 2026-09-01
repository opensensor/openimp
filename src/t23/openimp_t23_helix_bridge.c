#include "openimp_t23_helix_bridge.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>

#include "openimp_t23_persist.h"

#define T23_HELIX_HELPER_DEFAULT "/opt/openimp-t23/openimp-t23-helixd"
#define T23_HELIX_PAGE_SIZE 4096u
#define T23_HELIX_MAX_RECOVERIES 8u
#define T23_HELIX_INIT_TIMEOUT_MS 5000
#define T23_HELIX_ENCODE_TIMEOUT_MS 3000
#define T23_HELIX_EXIT_TIMEOUT_MS 250

static pthread_mutex_t t23_worker_start_lock = PTHREAD_MUTEX_INITIALIZER;

_Static_assert(sizeof(T23EncoderYuvIn) == 0x3c,
               "T23 YUV encoder input ABI mismatch");

static void t23_log(int priority, const char *format, ...)
{
    char message[512];
    va_list ap;
    int length;
    size_t size;

    va_start(ap, format);
    length = vsnprintf(message, sizeof(message), format, ap);
    va_end(ap);
    if (length <= 0)
        return;
    size = (size_t)length;
    if (size >= sizeof(message))
        size = sizeof(message) - 1u;
    syslog(priority, "%s", message);
    (void)write(STDERR_FILENO, message, size);
    fputc('\n', stderr);
    openimp_t23_persist_write(message, size);
}

static void t23_trace_annexb(unsigned int frame_number,
                             const unsigned char *data, uint32_t length)
{
    char types[96];
    size_t used = 0u;
    uint32_t offset = 0u;
    unsigned int count = 0u;

    if (!data || !getenv("OPENIMP_T23_ENCODE_TRACE") ||
        frame_number >= 128u)
        return;
    while (offset + 3u < length && count < 12u) {
        uint32_t prefix = 0u;

        if (data[offset] == 0u && data[offset + 1u] == 0u) {
            if (data[offset + 2u] == 1u)
                prefix = 3u;
            else if (offset + 4u < length && data[offset + 2u] == 0u &&
                     data[offset + 3u] == 1u)
                prefix = 4u;
        }
        if (!prefix) {
            offset++;
            continue;
        }
        if (offset + prefix >= length)
            break;
        {
            int written = snprintf(types + used, sizeof(types) - used,
                                   "%s%u", count ? "," : "",
                                   data[offset + prefix] & 0x1fu);

            if (written < 0 || (size_t)written >= sizeof(types) - used)
                break;
            used += (size_t)written;
        }
        count++;
        offset += prefix + 1u;
    }
    if (!count)
        snprintf(types, sizeof(types), "none");
    t23_log(LOG_NOTICE,
            "openimp/T23: Helix AU frame=%u len=%u NALs=%s",
            frame_number, length, types);
}

static int t23_send_all(int fd, const void *buffer, size_t size)
{
    const unsigned char *bytes = buffer;
    size_t written = 0u;

    while (written < size) {
        ssize_t count = send(fd, bytes + written, size - written,
                             MSG_NOSIGNAL);

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

static int t23_receive_all(int fd, void *buffer, size_t size, int timeout_ms)
{
    unsigned char *bytes = buffer;
    size_t consumed = 0u;

    while (consumed < size) {
        struct pollfd descriptor;
        ssize_t count;
        int ready;

        descriptor.fd = fd;
        descriptor.events = POLLIN;
        descriptor.revents = 0;
        do {
            ready = poll(&descriptor, 1, timeout_ms);
        } while (ready < 0 && errno == EINTR);
        if (ready <= 0 ||
            (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)))
            return -1;
        count = recv(fd, bytes + consumed, size - consumed, 0);
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

static int t23_exchange(T23HelixBridge *bridge,
                        const T23HelixIpcRequest *request,
                        T23HelixIpcResponse *response, int timeout_ms)
{
    if (!bridge || bridge->socket_fd < 0 || !request || !response ||
        t23_send_all(bridge->socket_fd, request, sizeof(*request)) != 0 ||
        t23_receive_all(bridge->socket_fd, response, sizeof(*response),
                        timeout_ms) != 0 ||
        response->magic != T23_HELIX_IPC_MAGIC ||
        response->version != T23_HELIX_IPC_VERSION ||
        response->command != request->command)
        return -1;
    return response->status;
}

static void t23_make_request(const T23HelixBridge *bridge,
                             T23HelixIpcRequest *request, uint32_t command)
{
    memset(request, 0, sizeof(*request));
    request->magic = T23_HELIX_IPC_MAGIC;
    request->version = T23_HELIX_IPC_VERSION;
    request->command = command;
    request->width = bridge->width;
    request->height = bridge->height;
    request->input_size = bridge->input_size;
    request->input_capacity = bridge->input_capacity;
    request->output_capacity = bridge->output_capacity;
    request->encoder_input = bridge->input;
}

static void t23_stop_worker(T23HelixBridge *bridge)
{
    pid_t pid;
    unsigned int attempt;

    if (!bridge)
        return;
    pid = bridge->worker_pid;
    if (bridge->socket_fd >= 0) {
        T23HelixIpcRequest request;
        T23HelixIpcResponse response;

        t23_make_request(bridge, &request, T23_HELIX_COMMAND_EXIT);
        (void)t23_exchange(bridge, &request, &response,
                           T23_HELIX_EXIT_TIMEOUT_MS);
        close(bridge->socket_fd);
        bridge->socket_fd = -1;
    }
    if (pid > 0) {
        for (attempt = 0; attempt < 20u; attempt++) {
            pid_t result = waitpid(pid, NULL, WNOHANG);

            if (result == pid || (result < 0 && errno == ECHILD)) {
                pid = -1;
                break;
            }
            usleep(10000);
        }
        if (pid > 0) {
            kill(pid, SIGKILL);
            (void)waitpid(pid, NULL, WNOHANG);
        }
    }
    bridge->worker_pid = -1;
    if (bridge->shared_buffer && bridge->shared_size)
        munmap(bridge->shared_buffer, bridge->shared_size);
    bridge->shared_buffer = NULL;
    if (bridge->shared_fd >= 0)
        close(bridge->shared_fd);
    bridge->shared_fd = -1;
}

static int t23_start_worker(T23HelixBridge *bridge)
{
    static char *const helper_environment[] = {
        "PATH=/usr/bin:/bin",
        "LD_LIBRARY_PATH=/opt/openimp-t23",
        NULL,
    };
    T23HelixIpcRequest request;
    T23HelixIpcResponse response;
    const char *helper;
    char socket_text[16];
    char shared_fd_text[16];
    char shared_size_text[24];
    char shared_path[] = "/tmp/openimp-t23-helix-XXXXXX";
    char *helper_argv[5];
    int sockets[2] = {-1, -1};
    pid_t pid;
    int result = -1;

    if (!bridge || bridge->socket_fd >= 0 || bridge->shared_fd >= 0 ||
        bridge->shared_buffer)
        return -1;
    helper = getenv("OPENIMP_T23_HELIX_HELPER");
    if (!helper || !*helper)
        helper = T23_HELIX_HELPER_DEFAULT;
    if (access(helper, X_OK) != 0) {
        t23_log(LOG_ERR, "openimp/T23: Helix helper unavailable: %s",
                helper);
        return -1;
    }
    bridge->shared_fd = mkstemp(shared_path);
    if (bridge->shared_fd < 0 ||
        ftruncate(bridge->shared_fd, bridge->shared_size) != 0 ||
        socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0)
        goto out;
    bridge->shared_buffer =
        mmap(NULL, bridge->shared_size, PROT_READ | PROT_WRITE, MAP_SHARED,
             bridge->shared_fd, 0);
    if (bridge->shared_buffer == MAP_FAILED) {
        bridge->shared_buffer = NULL;
        goto out;
    }
    unlink(shared_path);
    snprintf(socket_text, sizeof(socket_text), "%d", sockets[1]);
    snprintf(shared_fd_text, sizeof(shared_fd_text), "%d",
             bridge->shared_fd);
    snprintf(shared_size_text, sizeof(shared_size_text), "%u",
             bridge->shared_size);
    helper_argv[0] = (char *)helper;
    helper_argv[1] = socket_text;
    helper_argv[2] = shared_fd_text;
    helper_argv[3] = shared_size_text;
    helper_argv[4] = NULL;

    pthread_mutex_lock(&t23_worker_start_lock);
    pid = fork();
    if (pid == 0) {
        close(sockets[0]);
        execve(helper, helper_argv, helper_environment);
        _exit(127);
    }
    pthread_mutex_unlock(&t23_worker_start_lock);
    if (pid < 0)
        goto out;
    bridge->worker_pid = pid;
    close(sockets[1]);
    sockets[1] = -1;
    bridge->socket_fd = sockets[0];
    sockets[0] = -1;

    t23_make_request(bridge, &request, T23_HELIX_COMMAND_INIT);
    if (t23_exchange(bridge, &request, &response,
                     T23_HELIX_INIT_TIMEOUT_MS) != 0) {
        t23_log(LOG_ERR, "openimp/T23: Helix helper initialization failed");
        goto out;
    }
    t23_log(LOG_NOTICE,
            "openimp/T23: isolated Helix helper pid=%ld ready %ux%u "
            "%u/%u fps gop=%u",
            (long)bridge->worker_pid, bridge->width, bridge->height,
            bridge->input.outFrmRate.frmRateNum,
            bridge->input.outFrmRate.frmRateDen, bridge->input.maxGop);
    result = 0;

out:
    unlink(shared_path);
    if (sockets[0] >= 0)
        close(sockets[0]);
    if (sockets[1] >= 0)
        close(sockets[1]);
    if (result != 0)
        t23_stop_worker(bridge);
    return result;
}

static void t23_fill_yuv_input(T23EncoderYuvIn *input,
                               const HWEncoderParams *params)
{
    uint32_t bitrate_kbps;

    memset(input, 0, sizeof(*input));
    input->type = PT_H264;
    input->outFrmRate.frmRateNum = params->fps_num ? params->fps_num : 25u;
    input->outFrmRate.frmRateDen = params->fps_den ? params->fps_den : 1u;
    input->maxGop = params->gop_length ? params->gop_length : 25u;
    bitrate_kbps = params->bitrate / 1000u;
    if (!bitrate_kbps)
        bitrate_kbps = 2000u;

    switch (params->rc_mode) {
    case HW_RC_MODE_FIXQP:
        input->mode.rcMode = IMP_ENC_RC_MODE_FIXQP;
        input->mode.attrH264FixQp.qp = params->qp ? params->qp : 26u;
        break;
    case HW_RC_MODE_VBR:
        input->mode.rcMode = IMP_ENC_RC_MODE_VBR;
        input->mode.attrH264Vbr.maxQp = params->max_qp;
        input->mode.attrH264Vbr.minQp = params->min_qp;
        input->mode.attrH264Vbr.staticTime = 1u;
        input->mode.attrH264Vbr.maxBitRate = bitrate_kbps;
        input->mode.attrH264Vbr.changePos = 80u;
        input->mode.attrH264Vbr.qualityLvl = 2u;
        input->mode.attrH264Vbr.frmQPStep = 3u;
        input->mode.attrH264Vbr.gopQPStep = 3u;
        break;
    case HW_RC_MODE_CBR:
    default:
        input->mode.rcMode = IMP_ENC_RC_MODE_CBR;
        input->mode.attrH264Cbr.maxQp = params->max_qp;
        input->mode.attrH264Cbr.minQp = params->min_qp;
        input->mode.attrH264Cbr.outBitRate = bitrate_kbps;
        input->mode.attrH264Cbr.frmQPStep = 3u;
        input->mode.attrH264Cbr.gopQPStep = 3u;
        break;
    }
}

int OpenIMP_T23_HelixInit(T23HelixBridge *bridge,
                          const HWEncoderParams *params)
{
    uint64_t input_size;
    uint64_t input_capacity;
    uint64_t output_capacity;
    uint64_t shared_size;

    if (!bridge || !params || !params->width || !params->height)
        return -1;
    if (bridge->worker_pid > 0)
        return 0;
    if (bridge->failed)
        return -1;

    memset(bridge, 0, sizeof(*bridge));
    bridge->socket_fd = -1;
    bridge->shared_fd = -1;
    bridge->worker_pid = -1;
    bridge->width = params->width;
    bridge->height = params->height;
    t23_fill_yuv_input(&bridge->input, params);

    input_size = ((uint64_t)params->width + 15u) & ~15u;
    input_size *= ((uint64_t)params->height + 15u) & ~15u;
    input_size = input_size * 3u / 2u;
    input_capacity = (input_size + T23_HELIX_PAGE_SIZE - 1u) &
                     ~((uint64_t)T23_HELIX_PAGE_SIZE - 1u);
    output_capacity = (uint64_t)params->width * params->height * 2u +
                      65536u;
    output_capacity =
        (output_capacity + T23_HELIX_PAGE_SIZE - 1u) &
        ~((uint64_t)T23_HELIX_PAGE_SIZE - 1u);
    shared_size = input_capacity + output_capacity;
    if (!input_size || input_size > UINT32_MAX ||
        input_capacity > UINT32_MAX || output_capacity > UINT32_MAX ||
        shared_size > UINT32_MAX)
        goto fail;
    bridge->input_size = (uint32_t)input_size;
    bridge->input_capacity = (uint32_t)input_capacity;
    bridge->output_capacity = (uint32_t)output_capacity;
    bridge->shared_size = (uint32_t)shared_size;
    if (t23_start_worker(bridge) != 0)
        goto fail;
    return 0;

fail:
    t23_stop_worker(bridge);
    bridge->failed = 1;
    return -1;
}

static int t23_encode_once(T23HelixBridge *bridge,
                           const IMPFrameInfo *frame,
                           T23HelixIpcResponse *response)
{
    T23HelixIpcRequest request;

    if (!bridge->shared_buffer || frame->size < bridge->input_size)
        return -1;
    memcpy(bridge->shared_buffer, (const void *)(uintptr_t)frame->virAddr,
           bridge->input_size);
    __sync_synchronize();
    t23_make_request(bridge, &request, T23_HELIX_COMMAND_ENCODE);
    request.pixel_format = frame->pixfmt;
    if (request.pixel_format == 0x3231564eu) /* V4L2_PIX_FMT_NV12 */
        request.pixel_format = PIX_FMT_NV12;
    else if (request.pixel_format == 0x3132564eu) /* V4L2_PIX_FMT_NV21 */
        request.pixel_format = PIX_FMT_NV21;
    request.timestamp = frame->timeStamp;
    return t23_exchange(bridge, &request, response,
                        T23_HELIX_ENCODE_TIMEOUT_MS);
}

static int t23_recover_worker(T23HelixBridge *bridge)
{
    if (!bridge || bridge->recoveries >= T23_HELIX_MAX_RECOVERIES)
        return -1;
    bridge->recoveries++;
    t23_log(LOG_WARNING,
            "openimp/T23: restarting Helix helper (%u/%u)",
            bridge->recoveries, T23_HELIX_MAX_RECOVERIES);
    t23_stop_worker(bridge);
    return t23_start_worker(bridge);
}

int OpenIMP_T23_HelixEncode(T23HelixBridge *bridge,
                            const IMPFrameInfo *frame,
                            HWStreamBuffer **stream)
{
    T23HelixIpcResponse response;
    HWStreamBuffer *result;
    unsigned char *encoded;
    void *copy;
    uint32_t frame_number;

    if (!bridge || bridge->worker_pid <= 0 || !frame || !stream ||
        !frame->virAddr || !bridge->input_size)
        return -1;
    *stream = NULL;
    frame_number = bridge->frames;
    if ((frame_number < 256u || frame_number % 100u == 0u) &&
        getenv("OPENIMP_T23_ENCODE_TRACE"))
        t23_log(LOG_NOTICE,
                "openimp/T23: enter helper encode frame=%u input=%d/%d "
                "virt=0x%08x size=%u",
                frame_number, frame->index, frame->pool_idx, frame->virAddr,
                frame->size);
    if (t23_encode_once(bridge, frame, &response) != 0) {
        t23_log(LOG_ERR,
                "openimp/T23: Helix helper failed or timed out at frame %u",
                frame_number);
        if (t23_recover_worker(bridge) != 0 ||
            t23_encode_once(bridge, frame, &response) != 0)
            return -1;
    }
    if (response.output_offset != bridge->input_capacity ||
        response.output_length == 0u ||
        response.output_offset > bridge->shared_size ||
        response.output_length > bridge->shared_size - response.output_offset) {
        t23_log(LOG_ERR,
                "openimp/T23: invalid helper output offset=%u len=%u",
                response.output_offset, response.output_length);
        return -1;
    }
    __sync_synchronize();
    encoded = (unsigned char *)bridge->shared_buffer +
              response.output_offset;
    if ((frame_number < 256u || frame_number % 100u == 0u) &&
        getenv("OPENIMP_T23_ENCODE_TRACE"))
        t23_log(LOG_NOTICE,
                "openimp/T23: leave helper encode frame=%u len=%u",
                frame_number, response.output_length);
    t23_trace_annexb(frame_number, encoded, response.output_length);

    copy = malloc(response.output_length);
    result = (HWStreamBuffer *)calloc(1, sizeof(*result));
    if (!copy || !result) {
        free(copy);
        free(result);
        return -1;
    }
    memcpy(copy, encoded, response.output_length);
    result->virt_addr = (uint32_t)(uintptr_t)copy;
    result->length = response.output_length;
    result->timestamp = (uint64_t)frame->timeStamp;
    bridge->frames++;
    *stream = result;
    return 0;
}

int OpenIMP_T23_HelixRequestIDR(T23HelixBridge *bridge)
{
    T23HelixIpcRequest request;
    T23HelixIpcResponse response;

    if (!bridge || bridge->worker_pid <= 0)
        return -1;
    t23_make_request(bridge, &request, T23_HELIX_COMMAND_REQUEST_IDR);
    return t23_exchange(bridge, &request, &response,
                        T23_HELIX_ENCODE_TIMEOUT_MS);
}

void OpenIMP_T23_HelixExit(T23HelixBridge *bridge)
{
    if (!bridge)
        return;
    t23_stop_worker(bridge);
    memset(bridge, 0, sizeof(*bridge));
}
