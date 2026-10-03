"""ctypes binding of build/librcsim.so (the vendor-identical controllers)."""
import ctypes
import os
import struct

_HERE = os.path.dirname(os.path.abspath(__file__))
_lib = ctypes.CDLL(os.path.join(_HERE, 'build', 'librcsim.so'))

SOC = {'T23': 0, 'T21': 0, 'T20': 1, 'T10': 2}
MODE = {'CBR': 1, 'VBR': 2, 'SMART': 3}


class Params(ctypes.Structure):
    _fields_ = [(n, ctypes.c_int32) for n in (
        'mode', 'width', 'height', 'gop', 'fps', 'kbps', 'min_qp', 'max_qp',
        'frm_step', 'gop_step', 'i_bias', 'change_pos', 'quality', 'idr_gops')]


class Stats(ctypes.Structure):
    _fields_ = [(n, ctypes.c_uint32) for n in ('cmpx', 'mv_sum', 'moving_mbs', 'intra_mbs')]


_lib.rcs_new.restype = ctypes.c_void_p
_lib.rcs_new.argtypes = [ctypes.POINTER(Params)]
_lib.rcs_init.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(Params)]
_lib.rcs_free.argtypes = [ctypes.c_void_p]
_lib.rcs_start.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.POINTER(ctypes.c_int)]
_lib.rcs_end.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.POINTER(Stats), ctypes.c_int,
                         ctypes.POINTER(ctypes.c_int)]
_lib.rcs_set_opt.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
_lib.rcs_note_bits.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_uint32]
_lib.rcs_block.restype = ctypes.c_void_p
_lib.rcs_block.argtypes = [ctypes.c_void_p, ctypes.c_int]


class Controller:
    def __init__(self, soc, mode, width=1920, height=1080, gop=50, fps=15, kbps=1500,
                 min_qp=15, max_qp=45, frm_step=3, gop_step=15, i_bias=0, change_pos=80,
                 quality=4, idr_gops=1):
        self.soc = soc
        self.mode = mode
        self.p = Params(MODE[mode], width, height, gop, fps, kbps, min_qp, max_qp, frm_step,
                        gop_step, i_bias, change_pos, quality, idr_gops)
        self.h = _lib.rcs_new(ctypes.byref(self.p))
        if _lib.rcs_init(self.h, SOC[soc], ctypes.byref(self.p)) != 0:
            raise RuntimeError('init failed')

    def __del__(self):
        if getattr(self, 'h', None):
            _lib.rcs_free(self.h)
            self.h = None

    def start(self, idr, since_idr):
        t = ctypes.c_int()
        qp = _lib.rcs_start(self.h, int(idr), int(since_idr), ctypes.byref(t))
        return qp, t.value

    def end(self, bits, cmpx, mv_sum, moving, intra, may_repeat=1):
        s = Stats(int(cmpx) & 0xffffffff, int(mv_sum), int(moving), int(intra))
        q = ctypes.c_int()
        rep = _lib.rcs_end(self.h, int(bits) & 0xffffffff, ctypes.byref(s), int(may_repeat), ctypes.byref(q))
        return (q.value if rep else None)

    def set_opt(self, key, value):
        _lib.rcs_set_opt(self.h, key, value)

    def note_bits(self, idr, bits):
        _lib.rcs_note_bits(self.h, int(idr), int(bits) & 0xffffffff)

    # raw state access (OEM layout): which 0 = E, 1 = P / eprc state, 2 = S
    def _addr(self, which, off):
        return _lib.rcs_block(self.h, which) + off

    def rd(self, which, off, fmt='<i'):
        n = struct.calcsize(fmt)
        return struct.unpack(fmt, ctypes.string_at(self._addr(which, off), n))[0]

    def wr(self, which, off, v, fmt='<i'):
        b = struct.pack(fmt, v)
        ctypes.memmove(self._addr(which, off), b, len(b))
