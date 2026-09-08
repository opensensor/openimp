/* SPDX-License-Identifier: GPL-2.0 */
/* T41 zero-copy proof: V4L2 capture DMA-BUF -> shared OpenIMP AVPU core. */

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <openimp/openimp_avc.h>

#define BUFFER_COUNT 3U
#define FRAME_COUNT 100U
#define FREEZE_WARMUP_FRAMES 50U

struct capture_buffer {
    void *address;
    size_t length;
    int dma_fd;
    uint32_t physical_address;
};

static int checked_ioctl(int fd, unsigned long request, void *argument,
                         const char *name)
{
    int ret;

    do {
        ret = ioctl(fd, request, argument);
    } while (ret < 0 && errno == EINTR);
    if (ret < 0)
        fprintf(stderr, "%s: %s\n", name, strerror(errno));
    return ret;
}

static int write_all(int fd, const uint8_t *data, uint32_t length)
{
    uint32_t written = 0;

    while (written < length) {
        ssize_t count = write(fd, data + written, length - written);

        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            return -1;
        written += (uint32_t)count;
    }
    return 0;
}

static uint32_t source_hash(const uint8_t *data, size_t length)
{
    uint32_t hash = 2166136261u;
    size_t i;

    for (i = 0; i < length; ++i)
        hash = (hash ^ data[i]) * 16777619u;
    return hash;
}

int main(int argc, char **argv)
{
    const char *video_path = argc > 1 ? argv[1] : "/dev/video0";
    const char *output_path = argc > 2 ? argv[2] : "/tmp/v4l2-openimp.h264";
    struct capture_buffer buffers[BUFFER_COUNT];
    OpenIMPAVCEncoder *encoder = NULL;
    struct v4l2_requestbuffers request;
    struct v4l2_exportbuffer export;
    struct v4l2_format format;
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    unsigned int requested_frames = FRAME_COUNT;
    int freeze_qp = -1;
    uint32_t frozen_hash = 0;
    struct v4l2_buffer frozen_buffer;
    unsigned int frame_index;
    unsigned int index;
    int video_fd = -1;
    int output_fd = -1;
    int streaming = 0;
    int status = 1;

    if (argc > 3) {
        char *end = NULL;
        unsigned long parsed = strtoul(argv[3], &end, 0);

        if (!end || *end || !parsed || parsed > 10000u) {
            fprintf(stderr, "invalid frame count: %s\n", argv[3]);
            return 2;
        }
        requested_frames = (unsigned int)parsed;
    }
    /* Optional frozen-source diagnostic: warm up capture, stop after DQBUF
     * but retain its allocation, then encode identical NV12 bytes at FIXQP.
     * This separates encoder temporal drift from live ISP/scene changes.
     * Usage: v4l2_avc_test [device [output [frames [freeze_qp]]]] */
    if (argc > 4) {
        char *end = NULL;
        long parsed = strtol(argv[4], &end, 10);

        if (argc > 5 || !end || end == argv[4] || *end ||
            parsed < 1 || parsed > 51) {
            fprintf(stderr, "freeze QP must be 1..51\n");
            return 2;
        }
        freeze_qp = (int)parsed;
    }
    memset(&frozen_buffer, 0, sizeof(frozen_buffer));
    memset(buffers, 0, sizeof(buffers));
    for (index = 0; index < BUFFER_COUNT; ++index)
        buffers[index].dma_fd = -1;

    video_fd = open(video_path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (video_fd < 0) {
        fprintf(stderr, "open %s: %s\n", video_path, strerror(errno));
        goto out;
    }
    output_fd = open(output_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
                     0644);
    if (output_fd < 0) {
        fprintf(stderr, "open %s: %s\n", output_path, strerror(errno));
        goto out;
    }

    memset(&format, 0, sizeof(format));
    format.type = type;
    format.fmt.pix.width = 2560;
    format.fmt.pix.height = 1440;
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_NV12;
    if (checked_ioctl(video_fd, VIDIOC_S_FMT, &format, "VIDIOC_S_FMT"))
        goto out;

    memset(&request, 0, sizeof(request));
    request.count = BUFFER_COUNT;
    request.type = type;
    request.memory = V4L2_MEMORY_MMAP;
    if (checked_ioctl(video_fd, VIDIOC_REQBUFS, &request,
                      "VIDIOC_REQBUFS") || request.count != BUFFER_COUNT)
        goto out;

    for (index = 0; index < request.count; ++index) {
        struct v4l2_buffer buffer;
        int ret;

        memset(&buffer, 0, sizeof(buffer));
        buffer.type = type;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = index;
        if (checked_ioctl(video_fd, VIDIOC_QUERYBUF, &buffer,
                          "VIDIOC_QUERYBUF"))
            goto out;
        memset(&export, 0, sizeof(export));
        export.type = type;
        export.index = index;
        export.flags = O_CLOEXEC;
        if (checked_ioctl(video_fd, VIDIOC_EXPBUF, &export, "VIDIOC_EXPBUF"))
            goto out;
        buffers[index].dma_fd = export.fd;
        buffers[index].length = buffer.length;
        buffers[index].address = mmap(NULL, buffer.length,
            PROT_READ, MAP_SHARED, export.fd, 0);
        if (buffers[index].address == MAP_FAILED) {
            buffers[index].address = NULL;
            fprintf(stderr, "mmap DMA-BUF %u: %s\n", index,
                    strerror(errno));
            goto out;
        }
        ret = OpenIMP_AVC_ImportDMABuf(export.fd, (uint32_t)buffer.length,
                                       &buffers[index].physical_address);
        if (ret) {
            fprintf(stderr, "OpenIMP_AVC_ImportDMABuf[%u]: %d\n", index,
                    ret);
            goto out;
        }
        printf("buffer=%u dmafd=%d phys=%08x bytes=%u\n", index,
               export.fd, buffers[index].physical_address, buffer.length);
        if (checked_ioctl(video_fd, VIDIOC_QBUF, &buffer, "VIDIOC_QBUF"))
            goto out;
    }

    {
        OpenIMPAVCConfig config = {
            .width = format.fmt.pix.width,
            .height = format.fmt.pix.height,
            .fps_num = 25,
            .fps_den = 1,
            .bitrate = 8000000,
            .gop_length = 25,
            .stream_buffer_count = 1,
            .profile = OPENIMP_AVC_PROFILE_HIGH,
            .rate_control = OPENIMP_AVC_RATE_CBR,
            .initial_qp = 26,
            .min_qp = 15,
            .max_qp = 45,
            .entropy_coding = 1,
        };
        int ret;

        if (freeze_qp >= 0) {
            config.rate_control = OPENIMP_AVC_RATE_FIXQP;
            config.initial_qp = (uint8_t)freeze_qp;
            config.min_qp = (uint8_t)freeze_qp;
            config.max_qp = (uint8_t)freeze_qp;
        }
        ret = OpenIMP_AVC_Create(&encoder, &config);

        if (ret) {
            fprintf(stderr, "OpenIMP_AVC_Create: %d\n", ret);
            goto out;
        }
    }

    if (checked_ioctl(video_fd, VIDIOC_STREAMON, &type, "VIDIOC_STREAMON"))
        goto out;
    streaming = 1;
    if (freeze_qp >= 0) {
        /* Let automatic tuning advance before freezing. This is a bounded
         * diagnostic warmup, not a claim that AE/AWB have converged. */
        for (index = 0; index < FREEZE_WARMUP_FRAMES; ++index) {
            struct pollfd pollfd = { video_fd, POLLIN, 0 };
            struct v4l2_buffer buffer = {0};

            buffer.type = type;
            buffer.memory = V4L2_MEMORY_MMAP;
            if (poll(&pollfd, 1, 3000) <= 0 ||
                checked_ioctl(video_fd, VIDIOC_DQBUF, &buffer,
                              "VIDIOC_DQBUF(warmup)") ||
                checked_ioctl(video_fd, VIDIOC_QBUF, &buffer,
                              "VIDIOC_QBUF(warmup)"))
                goto out;
        }
    }
    for (frame_index = 0; frame_index < requested_frames; ++frame_index) {
        struct pollfd pollfd = { video_fd, POLLIN, 0 };
        struct v4l2_buffer buffer;
        OpenIMPAVCFrame frame;
        OpenIMPAVCPacket packet;
        int packet_dequeued = 0;
        int ret;

        if (freeze_qp >= 0 && frame_index) {
            buffer = frozen_buffer;
        } else {
            if (poll(&pollfd, 1, 3000) <= 0) {
                fprintf(stderr, "capture poll timeout at frame %u\n", frame_index);
                goto out;
            }
            memset(&buffer, 0, sizeof(buffer));
            buffer.type = type;
            buffer.memory = V4L2_MEMORY_MMAP;
            if (checked_ioctl(video_fd, VIDIOC_DQBUF, &buffer, "VIDIOC_DQBUF"))
                goto out;
            if (buffer.index >= request.count ||
                buffer.bytesused > buffers[buffer.index].length ||
                !buffer.bytesused) {
                fprintf(stderr, "invalid captured buffer extent\n");
                goto out;
            }
            if (freeze_qp >= 0) {
                frozen_buffer = buffer;
                if (checked_ioctl(video_fd, VIDIOC_STREAMOFF, &type,
                                  "VIDIOC_STREAMOFF(freeze)"))
                    goto out;
                streaming = 0;
                frozen_hash = source_hash(buffers[buffer.index].address,
                                          buffer.bytesused);
                printf("frozen source: qp=%d sequence=%u bytes=%u "
                       "fnv1a=%08x warmup=%u\n", freeze_qp, buffer.sequence,
                       buffer.bytesused, frozen_hash, FREEZE_WARMUP_FRAMES);
            }
        }
        memset(&frame, 0, sizeof(frame));
        frame.width = format.fmt.pix.width;
        frame.height = format.fmt.pix.height;
        frame.pixel_format = OPENIMP_AVC_PIXFMT_NV12;
        frame.size = buffer.bytesused;
        frame.physical_address =
            buffers[buffer.index].physical_address;
        frame.virtual_address =
            (uintptr_t)buffers[buffer.index].address;
        frame.timestamp = (uint64_t)buffer.timestamp.tv_sec * 1000000u +
                          (uint64_t)buffer.timestamp.tv_usec;
        if (freeze_qp >= 0)
            frame.timestamp += (uint64_t)frame_index * 40000u;
        frame.cookie = (void *)(uintptr_t)buffer.index;
        ret = OpenIMP_AVC_Submit(encoder, &frame);
        if (!ret) {
            ret = OpenIMP_AVC_Dequeue(encoder, &packet, 2000);
            packet_dequeued = !ret;
        }
        if (!ret && write_all(output_fd, packet.data, packet.length))
            ret = -EIO;
        if (!ret) {
            printf("frame=%u capture_seq=%u buffer=%u bytes=%u key=%d "
                   "timestamp=%llu\n", frame_index, buffer.sequence,
                   buffer.index, packet.length, packet.keyframe,
                   (unsigned long long)packet.timestamp);
        }
        if (packet_dequeued) {
            int release_ret = OpenIMP_AVC_Release(encoder, &packet);

            if (!ret)
                ret = release_ret;
        }
        if (ret) {
            fprintf(stderr, "encode frame %u: %d\n", frame_index, ret);
            goto out;
        }
        if (freeze_qp < 0 &&
            checked_ioctl(video_fd, VIDIOC_QBUF, &buffer, "VIDIOC_QBUF"))
            goto out;
    }
    if (freeze_qp >= 0 &&
        source_hash(buffers[frozen_buffer.index].address,
                    frozen_buffer.bytesused) != frozen_hash) {
        fprintf(stderr, "frozen source changed during encoding\n");
        goto out;
    }
    status = 0;

out:
    if (streaming)
        checked_ioctl(video_fd, VIDIOC_STREAMOFF, &type, "VIDIOC_STREAMOFF");
    if (encoder) {
        int ret = OpenIMP_AVC_Destroy(encoder);

        if (ret) {
            fprintf(stderr, "OpenIMP_AVC_Destroy: %d\n", ret);
            status = 1;
        }
    }
    for (index = 0; index < BUFFER_COUNT; ++index) {
        if (buffers[index].address)
            munmap(buffers[index].address, buffers[index].length);
        if (buffers[index].dma_fd >= 0)
            close(buffers[index].dma_fd);
    }
    if (video_fd >= 0) {
        memset(&request, 0, sizeof(request));
        request.type = type;
        request.memory = V4L2_MEMORY_MMAP;
        checked_ioctl(video_fd, VIDIOC_REQBUFS, &request,
                      "VIDIOC_REQBUFS(0)");
        close(video_fd);
    }
    if (output_fd >= 0)
        close(output_fd);
    if (!status)
        printf("wrote %u zero-copy frames to %s\n", requested_frames,
               output_path);
    return status;
}
