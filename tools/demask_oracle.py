#!/usr/bin/env python3
"""Issues #3/#5 (development aid): reference vectors for src/demask from the OEM T10/T20 libimp 3.12.0.

IMP_Encoder_SetChnDemask of the vendor library is not a stored value: do_channel_process_h264e runs
encoder_channel_h264e_demask_strategy for every H.264 picture and steps the ISP sharpness
(IMP_ISP_Tuning_SetSharpness_internal) by 0x14 per picture, driven by the QP the encoder gave the previous
picture (i264e_encode writes the picture QP into word 0 of the picture record, the channel keeps it at +0x618).

This script runs the real vendor code under the unicorn CPU emulator on a channel record it controls:
IMP_Encoder_SetChnDemask (the attribute setter), encoder_channel_h264e_demask_strategy (the call before the
picture, made while IMP_Encoder_SetChnFrmUsedMode is off) and the post-picture block of
do_channel_process_h264e (strategy, sharpness step, the ISP call; 0x41dd4..0x41e84 of the library), and
prints per scenario what the OEM does.  tests/demask/demask_test.c checks src/demask against the output.
Nothing of the vendor library is copied into OpenIMP; it is read from the path you pass.

    pip install unicorn pyelftools
    tools/demask_oracle.py T20/lib/3.12.0/uclibc/4.7.2/libimp.so > tests/demask/demask_vectors.txt

Lines: 'S <name> frm_used' starts a scenario (channel record zeroed as CreateChn does);
'A en auto cnt thr : ret restore' = IMP_Encoder_SetChnDemask (restore 1: the setter called SetSharpness_internal(0x80, 0));
'P qp : pre post sharp mode value' = one picture coded at QP qp: the pre-picture strategy result (-1: not called,
frm_used on), the post-picture strategy result, the channel's sharpness word and the SetSharpness_internal
arguments (mode -1: enable off, no call).
"""
import os
import random
import struct
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import eprc_oracle as O
from unicorn import UC_HOOK_CODE
from unicorn.mips_const import *

O.LAYOUT_T21['code'] = (0x40000, 0x42600)
CHN = 0
REC = 0xf0000 - 0x2db8 + CHN * 0x810      # channel record of the library's channel table (.bss)
BLOCK = (0x41dd4, 0x41e84)                # post-picture demask block of do_channel_process_h264e
GP = 0xb0000 + 12036 + 0x412fc            # gp of do_channel_process_h264e (prologue)


class Vendor:
    def __init__(self, path):
        self.L = L = O.Lib(path)
        self.calls = []
        # global GOT entries of the library's own functions are filled by the dynamic linker; do it here
        for ea, name in (0xec740, 'IMP_Log_Get_Option'), (0xec858, 'imp_log_fun'), (0xecdb0, 'IMP_ISP_Tuning_SetSharpness_internal'):
            if L.r32(ea) == 0:
                L.w32(ea, L.syms[name])
        L.uc.hook_add(UC_HOOK_CODE, self._ret, begin=L.syms['IMP_ISP_Tuning_SetSharpness_internal'],
                      end=L.syms['IMP_ISP_Tuning_SetSharpness_internal'] + 4)
        L.uc.hook_add(UC_HOOK_CODE, self._ret0, begin=L.syms['imp_log_fun'], end=L.syms['imp_log_fun'] + 4)
        L.uc.hook_add(UC_HOOK_CODE, self._post, begin=BLOCK[0] + 12, end=BLOCK[0] + 16)   # bnez v0 after the strategy call
        self.post = -1

    def _post(self, uc, addr, size, _):
        self.post = 1 if uc.reg_read(UC_MIPS_REG_V0) & 0xffffffff else 0

    def _ret(self, uc, addr, size, _):
        self.calls.append((uc.reg_read(UC_MIPS_REG_A1), uc.reg_read(UC_MIPS_REG_A0) & 0xff))
        uc.reg_write(UC_MIPS_REG_V0, 0)
        uc.reg_write(UC_MIPS_REG_PC, uc.reg_read(UC_MIPS_REG_RA))

    def _ret0(self, uc, addr, size, _):
        uc.reg_write(UC_MIPS_REG_V0, 0)
        uc.reg_write(UC_MIPS_REG_PC, uc.reg_read(UC_MIPS_REG_RA))

    def reset(self, frm_used):
        self.L.wr(REC, bytes(0x810))                 # IMP_Encoder_CreateChn: memset of the record
        self.L.w8(REC + 0x70, frm_used)

    def set_attr(self, en, auto, cnt, thr):
        A = O.HEAP + 0x100
        self.L.w32(A, (en & 0xff) | ((auto & 0xff) << 8))
        self.L.w32(A + 4, cnt)
        self.L.w32(A + 8, thr)
        self.calls = []
        r = self.L.call('IMP_Encoder_SetChnDemask', CHN, A)
        restore = 1 if self.calls == [(0, 0x80)] else 0
        assert len(self.calls) <= 1
        return r & 0xffffffff, restore

    def strategy(self):
        return self.L.call('encoder_channel_h264e_demask_strategy', REC) & 0xffffffff

    def picture(self, qp):
        L = self.L
        pre = -1
        if L.rd(REC + 0x70, 1)[0] == 0:
            pre = self.strategy()
        post, mode, value = -1, -1, -1
        if L.rd(REC + 0x7c, 1)[0] != 0:
            uc = L.uc
            sp = O.STACK - 0x400
            uc.reg_write(UC_MIPS_REG_SP, sp)
            L.w32(sp + 104, GP)
            uc.reg_write(UC_MIPS_REG_GP, GP)
            uc.reg_write(UC_MIPS_REG_S2, REC)
            uc.reg_write(UC_MIPS_REG_T9, L.r32(GP - 32680))      # loaded in the delay slot before the block (bal strategy)
            self.calls = []
            self.post = -1
            uc.emu_start(BLOCK[0], BLOCK[1], count=100000)
            assert uc.reg_read(UC_MIPS_REG_PC) == BLOCK[1], hex(uc.reg_read(UC_MIPS_REG_PC))
            assert len(self.calls) == 1, self.calls
            mode, value = self.calls[0]
            post = self.post
        sharp = L.r32(REC + 0x7c8)
        L.w32(REC + 0x618, qp)                       # param_1[0x186] = param_1[0xd4] (the QP i264e_encode reported)
        return pre, post, sharp, mode, value



def walk(seed, n, lo, hi, step):
    r = random.Random(seed)
    q = r.randint(lo, hi)
    out = []
    for _ in range(n):
        q = max(lo, min(hi, q + r.randint(-step, step)))
        out.append(q)
    return out


def scenarios():
    S = []
    # constant QP above / below / at the threshold, at threshold - 1
    for cnt in (0, 1, 3, 5):
        for thr in (30,):
            for qp in (thr + 5, thr, thr - 1, thr - 5):
                S.append(('const_c%d_t%d_q%d' % (cnt, thr, qp), 0, [('A', 1, 0, cnt, thr)] + [('P', qp)] * 12))
    # a pulse above the threshold, then below: the hold counter
    for cnt in (0, 1, 2, 3, 6):
        S.append(('pulse_c%d' % cnt, 0, [('A', 1, 0, cnt, 30)] + [('P', 20)] * 3 + [('P', 36)] * 2 + [('P', 20)] * 12))
        S.append(('pulse_edge_c%d' % cnt, 0, [('A', 1, 0, cnt, 30)] + [('P', 20)] * 3 + [('P', 36)] * 2 + [('P', 29)] * 4 + [('P', 30)] * 3 + [('P', 28)] * 8))
    # ramps across the threshold in both directions
    S.append(('ramp_up', 0, [('A', 1, 0, 3, 30)] + [('P', q) for q in range(20, 41)] + [('P', 40)] * 8))
    S.append(('ramp_down', 0, [('A', 1, 0, 3, 30)] + [('P', q) for q in range(40, 19, -1)] + [('P', 20)] * 8))
    # auto mode (the vendor logs an error and returns the hold state), enable off (pre-call only)
    S.append(('auto', 0, [('A', 1, 1, 3, 30)] + [('P', 40)] * 5 + [('P', 20)] * 5))
    S.append(('off', 0, [('A', 0, 0, 3, 30)] + [('P', 40)] * 5))
    S.append(('off_after_on', 0, [('A', 1, 0, 3, 30)] + [('P', 40)] * 5 + [('A', 0, 0, 3, 30)] + [('P', 40)] * 3 + [('A', 1, 0, 3, 30)] + [('P', 20)] * 9))
    S.append(('on_twice', 0, [('A', 1, 0, 3, 30), ('A', 1, 0, 2, 25)] + [('P', 27)] * 6 + [('A', 1, 0, 2, 40)] + [('P', 27)] * 6))
    S.append(('thr_change_hold', 0, [('A', 1, 0, 4, 30)] + [('P', 40)] * 10 + [('A', 1, 0, 4, 45)] + [('P', 40)] * 12))
    # IMP_Encoder_SetChnFrmUsedMode on: no pre-picture call, the hold counter runs once per picture
    S.append(('frm_used_pulse', 1, [('A', 1, 0, 3, 30)] + [('P', 20)] * 3 + [('P', 36)] * 2 + [('P', 20)] * 12))
    S.append(('frm_used_ramp', 1, [('A', 1, 0, 2, 30)] + [('P', q) for q in range(40, 19, -1)]))
    # negative and large counts, threshold 0 (the ENHANCE pictures of BlackEnhance report QP 0), QP 51
    S.append(('cnt_neg', 0, [('A', 1, 0, -1, 30)] + [('P', 40)] * 3 + [('P', 20)] * 6))
    S.append(('thr0', 0, [('A', 1, 0, 2, 0)] + [('P', 0)] * 4 + [('P', 1)] * 4 + [('P', 0)] * 6))
    S.append(('qp51', 0, [('A', 1, 0, 2, 50)] + [('P', 51)] * 8 + [('P', 49)] * 10))
    # random walks
    for seed in range(12):
        qs = walk(seed, 60, 15, 45, 4)
        S.append(('walk%d' % seed, seed % 3 == 2, [('A', 1, 0, 1 + seed % 4, 25 + seed % 7)] + [('P', q) for q in qs]))
    return S


def main():
    V = Vendor(sys.argv[1])
    for name, frm, ops in scenarios():
        V.reset(frm)
        print('S %s %d' % (name, frm))
        for op in ops:
            if op[0] == 'A':
                r, restore = V.set_attr(*op[1:])
                print('A %d %d %d %d : %d %d' % (op[1], op[2], op[3], op[4], 0 if r == 0 else -1, restore))
            else:
                pre, post, sharp, mode, value = V.picture(op[1])
                print('P %d : %d %d %d %d %d' % (op[1], pre, post, sharp, mode, value))


if __name__ == '__main__':
    main()
