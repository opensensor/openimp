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
/* The OEM encoder core behind every IMP_Encoder_Set/Get* channel call. */
extern int i264e_set_param(void *encoder, int id, void *param);
extern int i264e_get_param(void *encoder, int id, void *param);
/* IMP_Encoder_Yuv* and IMP_Encoder_Vbm* are declared by imp/imp_encoder.h */
/* OEM hardware JPEG decoder (imp_decoder.h), channel 0 only */
extern int DecoderInit(void);
extern int DecoderExit(void);
typedef struct {
    int len;
    uint8_t *data;
    int64_t timestamp;
} T23DecoderStream;
extern int IMP_Decoder_CreateChn(int chn, const void *attr);
extern int IMP_Decoder_DestroyChn(int chn);
extern int IMP_Decoder_StartRecvPic(int chn);
extern int IMP_Decoder_StopRecvPic(int chn);
extern int IMP_Decoder_SendStreamTimeout(int chn, T23DecoderStream *stream,
                                         uint32_t timeout_ms);
extern int IMP_Decoder_PollingFrame(int chn, uint32_t timeout_ms);
extern int IMP_Decoder_GetFrame(int chn, IMPFrameInfo **frame);
extern int IMP_Decoder_ReleaseFrame(int chn, IMPFrameInfo *frame);

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
    T23HelixFrameCtl frame_ctl;
    int frame_ctl_set;
    int decoder;                /* 1 initialised, 2 receiving */
} T23HelixWorker;

/* OEM IMP_Encoder_YuvInit handle (76 bytes): +0 payload kind (1 = H.264),
 * +60 i264e encoder, +64 i264e parameters, +68 the i264e picture that
 * IMP_Encoder_YuvEncode hands to i264e_encode, +72 IDR request flag.  The
 * picture is the same 432-byte structure the OEM channel thread fills;
 * the fields below are the ones it sets for GDR and the initial QP. */
#define T23_YUV_KIND        0
#define T23_YUV_I264E       60
#define T23_YUV_PICTURE     68
#define T23_PIC_GDR_ENABLE  392
#define T23_PIC_GDR_REQUEST 396
#define T23_PIC_GDR_CYCLE   400
#define T23_PIC_GDR_FRAMES  404
#define T23_PIC_FORCE_QP    428

static void *worker_i264e(const T23HelixWorker *worker)
{
    const unsigned char *handle = worker ? worker->encoder : NULL;
    void *i264e = NULL;
    int32_t kind;

    if (!handle)
        return NULL;
    memcpy(&kind, handle + T23_YUV_KIND, sizeof(kind));
    if (kind != 1)
        return NULL;
    memcpy(&i264e, handle + T23_YUV_I264E, sizeof(i264e));
    return i264e;
}

static void worker_apply_frame_ctl(T23HelixWorker *worker)
{
    unsigned char *picture = NULL;
    int32_t value;
    int8_t qp;

    if (!worker->frame_ctl_set || !worker_i264e(worker))
        return;
    memcpy(&picture, (unsigned char *)worker->encoder + T23_YUV_PICTURE,
           sizeof(picture));
    if (!picture)
        return;
    value = worker->frame_ctl.gdr_enable ? 1 : 0;
    memcpy(picture + T23_PIC_GDR_ENABLE, &value, sizeof(value));
    if (value) {
        memcpy(picture + T23_PIC_GDR_CYCLE, &worker->frame_ctl.gdr_cycle,
               sizeof(int32_t));
        memcpy(picture + T23_PIC_GDR_FRAMES, &worker->frame_ctl.gdr_frames,
               sizeof(int32_t));
    }
    value = worker->frame_ctl.gdr_request ? 1 : 0;
    memcpy(picture + T23_PIC_GDR_REQUEST, &value, sizeof(value));
    worker->frame_ctl.gdr_request = 0;
    qp = (int8_t)(worker->frame_ctl.init_qp > 0 ? worker->frame_ctl.init_qp
                                                 : 0);
    memcpy(picture + T23_PIC_FORCE_QP, &qp, sizeof(qp));
}

static int worker_param(T23HelixWorker *worker,
                        const T23HelixIpcRequest *request,
                        T23HelixIpcResponse *response, int set)
{
    /* i264e may write more than a request carries: give it room */
    unsigned char param[256];
    void *i264e = worker_i264e(worker);

    if (!i264e || request->param_size > T23_HELIX_PARAM_MAX)
        return -EINVAL;
    memset(param, 0, sizeof(param));
    memcpy(param, request->param, request->param_size);
    if ((set ? i264e_set_param(i264e, (int)request->param_id, param)
             : i264e_get_param(i264e, (int)request->param_id, param)) != 0)
        return -EIO;
    if (!set) {
        memcpy(response->param, param, T23_HELIX_PARAM_MAX);
        response->param_size = request->param_size;
    }
    return 0;
}

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
    if (worker->decoder) {
        /* frames still held are returned with the channel */
        if (worker->decoder > 1)
            IMP_Decoder_StopRecvPic(0);
        IMP_Decoder_DestroyChn(0);
        DecoderExit();
        worker->decoder = 0;
    }
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

static int worker_init(T23HelixWorker *worker,
                       const T23HelixIpcRequest *request)
{
    intptr_t physical;
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

    worker->input_buffer =
        IMP_Encoder_VbmAlloc(request->input_capacity, T23_HELIX_PAGE_SIZE);
    worker->output_buffer =
        IMP_Encoder_VbmAlloc(request->output_capacity, T23_HELIX_PAGE_SIZE);
    if (!worker->input_buffer || !worker->output_buffer)
        return -ENOMEM;
    physical = IMP_Encoder_VbmV2P((intptr_t)(uintptr_t)worker->input_buffer);
    if (physical <= 0 || (uintptr_t)physical > UINT32_MAX)
        return -EFAULT;

    memset(worker->input_buffer, 0, request->input_capacity);
    memset(worker->output_buffer, 0, request->output_capacity);
    if (IMP_FlushCache(worker->input_buffer, request->input_capacity,
                       T23_CACHE_WBACK) != 0 ||
        IMP_FlushCache(worker->output_buffer, request->output_capacity,
                       T23_CACHE_WBACK) != 0)
        return -EIO;

    worker->input_physical = (uint32_t)(uintptr_t)physical;
    worker->input_size = request->input_size;
    worker->input_capacity = request->input_capacity;
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

    memcpy(worker->input_buffer, worker->shared, worker->input_size);
    if (IMP_FlushCache(worker->input_buffer, worker->input_size,
                       T23_CACHE_WBACK) != 0)
        return -EIO;

    memset(&frame, 0, sizeof(frame));
    frame.index = -1;
    frame.pool_idx = -1;
    frame.width = worker->width;
    frame.height = worker->height;
    frame.pixfmt = request->pixel_format;
    frame.size = worker->input_size;
    frame.phyAddr = worker->input_physical;
    frame.virAddr = (uint32_t)(uintptr_t)worker->input_buffer;
    frame.direct_phyAddr = worker->input_physical;
    frame.timeStamp = request->timestamp;

    worker_apply_frame_ctl(worker);
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

static int worker_dec_init(T23HelixWorker *worker,
                           const T23HelixIpcRequest *request)
{
    if (worker->encoder || worker->decoder || request->param_size != 28u ||
        request->input_capacity > worker->shared_size)
        return -EINVAL;
    if (DecoderInit() != 0)
        return -EIO;
    if (IMP_Decoder_CreateChn(0, request->param) != 0) {
        DecoderExit();
        return -EIO;
    }
    worker->decoder = 1;
    if (IMP_Decoder_StartRecvPic(0) != 0)
        return -EIO;
    worker->decoder = 2;
    worker->input_capacity = request->input_capacity;
    worker->output_capacity = request->output_capacity;
    syslog(LOG_NOTICE, "openimp/T23 helper: JPEG decoder ready");
    return 0;
}

static int worker_dec_decode(T23HelixWorker *worker,
                             const T23HelixIpcRequest *request,
                             T23HelixIpcResponse *response)
{
    T23DecoderStream stream;
    IMPFrameInfo *frame = NULL;
    uint32_t timeout = request->param_id ? request->param_id : 1000u;

    if (worker->decoder < 2 || !request->input_size ||
        request->input_size > worker->input_capacity)
        return -EINVAL;
    stream.len = (int)request->input_size;
    stream.data = worker->shared;
    stream.timestamp = request->timestamp;
    if (IMP_Decoder_SendStreamTimeout(0, &stream, timeout) != 0)
        return -EAGAIN;
    if (IMP_Decoder_PollingFrame(0, timeout) != 0 ||
        IMP_Decoder_GetFrame(0, &frame) != 0 || !frame)
        return -EIO;
    memcpy(response->param, frame, sizeof(*frame));
    response->param_size = sizeof(*frame);
    response->output_offset = (uint32_t)(uintptr_t)frame;
    response->output_length = frame->size;
    return 0;
}

static int worker_dec_copy(T23HelixWorker *worker,
                           const T23HelixIpcRequest *request,
                           T23HelixIpcResponse *response)
{
    const IMPFrameInfo *frame =
        (const IMPFrameInfo *)(uintptr_t)request->param_id;
    uint32_t size;

    if (worker->decoder < 2 || !frame || !frame->virAddr)
        return -EINVAL;
    size = frame->size;
    if (!size || size > worker->output_capacity ||
        (uint64_t)worker->input_capacity + size > worker->shared_size)
        return -EOVERFLOW;
    /* the hardware wrote it behind this process's cache */
    IMP_FlushCache((void *)(uintptr_t)frame->virAddr, size, 2);
    memcpy(worker->shared + worker->input_capacity,
           (const void *)(uintptr_t)frame->virAddr, size);
    response->output_offset = worker->input_capacity;
    response->output_length = size;
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
            case T23_HELIX_COMMAND_SET_PARAM:
                status = worker_param(&worker, &request, &response, 1);
                break;
            case T23_HELIX_COMMAND_GET_PARAM:
                status = worker_param(&worker, &request, &response, 0);
                break;
            case T23_HELIX_COMMAND_DEC_INIT:
                status = worker_dec_init(&worker, &request);
                break;
            case T23_HELIX_COMMAND_DEC_DECODE:
                status = worker_dec_decode(&worker, &request, &response);
                break;
            case T23_HELIX_COMMAND_DEC_RELEASE:
                status = worker.decoder < 2 || !request.param_id
                             ? -EINVAL
                             : IMP_Decoder_ReleaseFrame(
                                   0, (IMPFrameInfo *)(uintptr_t)
                                          request.param_id);
                break;
            case T23_HELIX_COMMAND_DEC_COPY:
                status = worker_dec_copy(&worker, &request, &response);
                break;
            case T23_HELIX_COMMAND_SET_FRAME_CTL:
                worker.frame_ctl = request.frame_ctl;
                worker.frame_ctl_set = 1;
                status = 0;
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
