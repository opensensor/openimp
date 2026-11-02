#!/usr/bin/env python3
"""Issues #3/#5 (device evidence helper): macroblock QPs of an H.264 stream, from the decoder's exported
video-encoding parameters (libavcodec through ctypes; no ffmpeg headers needed).

    tools/fisheye_mbqp.py RUN.h264 [--table]

Per decoded picture: slice type letter (I when the picture decoded as a key frame), the QPs of the macroblocks
x 0..3, y 0..1 (the Fisheye region: top-left 64x32 pixels) as min/max, and min/mean/max QP of all other macroblocks.
Verdict line: pictures with region QP <= 15 and the rest above it ('fisheye pictures'), by index.
"""
import ctypes as C
import ctypes.util
import sys

AV_FRAME_DATA_VIDEO_ENC_PARAMS = 19
PAD = 64


def lib(names):
    for n in names:
        try:
            return C.CDLL(n)
        except OSError:
            pass
    raise SystemExit('missing ' + names[0])


def mbqps(path):
    avc = lib(['libavcodec.so.62', 'libavcodec.so.61', 'libavcodec.so.60', 'libavcodec.so'])
    avu = lib(['libavutil.so.60', 'libavutil.so.59', 'libavutil.so.58', 'libavutil.so'])
    vp = C.c_void_p
    avc.avcodec_find_decoder_by_name.restype = vp
    avc.avcodec_find_decoder_by_name.argtypes = [C.c_char_p]
    avc.avcodec_alloc_context3.restype = vp
    avc.avcodec_alloc_context3.argtypes = [vp]
    avc.avcodec_open2.argtypes = [vp, vp, vp]
    avc.av_parser_init.restype = vp
    avc.av_parser_init.argtypes = [C.c_int]
    avc.av_parser_parse2.argtypes = [vp, vp, C.POINTER(C.POINTER(C.c_uint8)), C.POINTER(C.c_int),
                                     C.c_char_p, C.c_int, C.c_int64, C.c_int64, C.c_int64]
    avc.av_parser_parse2.restype = C.c_int
    avc.av_packet_alloc.restype = vp
    avc.avcodec_send_packet.argtypes = [vp, vp]
    avc.avcodec_receive_frame.argtypes = [vp, vp]
    avu.av_frame_alloc.restype = vp
    avu.av_frame_get_side_data.restype = vp
    avu.av_frame_get_side_data.argtypes = [vp, C.c_int]
    avu.av_opt_set.argtypes = [vp, C.c_char_p, C.c_char_p, C.c_int]
    avu.av_log_set_level(-8)
    codec = avc.avcodec_find_decoder_by_name(b'h264')
    ctx = avc.avcodec_alloc_context3(codec)
    avu.av_opt_set(ctx, b'export_side_data', b'venc_params', 1)
    avu.av_opt_set(ctx, b'threads', b'1', 1)
    if avc.avcodec_open2(ctx, codec, None) < 0:
        raise SystemExit('open failed')
    parser = avc.av_parser_init(27)          # AV_CODEC_ID_H264
    data = open(path, 'rb').read()
    pkt = avc.av_packet_alloc()
    frame = avu.av_frame_alloc()
    out = []

    def drain():
        while avc.avcodec_receive_frame(ctx, frame) == 0:
            sd = avu.av_frame_get_side_data(frame, AV_FRAME_DATA_VIDEO_ENC_PARAMS)
            if not sd:
                out.append(None)
                continue
            dptr, size = C.c_void_p.from_address(sd + 8).value, C.c_size_t.from_address(sd + 16).value
            raw = C.string_at(dptr, size)
            nb = int.from_bytes(raw[0:4], 'little')
            off = int.from_bytes(raw[8:16], 'little')
            bsz = int.from_bytes(raw[16:24], 'little')
            base = int.from_bytes(raw[28:32], 'little', signed=True)
            blocks = {}
            for i in range(nb):
                b = raw[off + i * bsz: off + i * bsz + 20]
                x, y = (int.from_bytes(b[0:4], 'little', signed=True) // 16,
                        int.from_bytes(b[4:8], 'little', signed=True) // 16)
                d = int.from_bytes(b[16:20], 'little', signed=True)
                blocks[(x, y)] = base + d
            out.append((base, blocks))

    pos = 0
    pdata = C.POINTER(C.c_uint8)()
    psize = C.c_int(0)
    while pos < len(data):
        chunk = data[pos:pos + 65536]
        buf = chunk + bytes(PAD)
        n = avc.av_parser_parse2(parser, ctx, C.byref(pdata), C.byref(psize), buf, len(chunk), 0, 0, 0)
        pos += n
        if psize.value:
            C.c_void_p.from_address(pkt + 24).value = C.cast(pdata, vp).value     # AVPacket.data
            C.c_int.from_address(pkt + 32).value = psize.value                     # AVPacket.size
            avc.avcodec_send_packet(ctx, pkt)
            drain()
    avc.avcodec_send_packet(ctx, None)
    drain()
    return out


def main():
    path = sys.argv[1]
    rows = []
    for i, f in enumerate(mbqps(path)):
        if f is None:
            rows.append((i, None))
            continue
        base, blocks = f
        reg = [q for (x, y), q in blocks.items() if x <= 3 and y <= 1]
        rest = [q for (x, y), q in blocks.items() if not (x <= 3 and y <= 1)]
        rows.append((i, (base, min(reg), max(reg), min(rest), sum(rest) / len(rest), max(rest))))
    print('# pic slice_qp region_min region_max rest_min rest_mean rest_max')
    for i, r in rows:
        print(i, *(('%.1f' % v if isinstance(v, float) else v) for v in r) if r else ['-'])
    fe = [i for i, r in rows if r and r[2] <= 15 and r[3] > 15]
    print('# pictures %d, with the top-left region at QP <= 15 and the rest above: %s' % (len(rows), ' '.join(map(str, fe))))


if __name__ == '__main__':
    main()
