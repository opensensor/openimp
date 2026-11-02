#!/usr/bin/env python3
"""Issues #3/#5/#7 (development aid): reference structure of an H.264 Annex-B stream, one line per picture.

    tools/hskip_trace.py stream.h264 [--sizes log] [--qp]

--qp prints only the slice QP of every picture (PPS pic_init_qp_minus26 + slice_qp_delta), one per line
(the apitest case demask / tools/demask_check.py).

Runs `ffmpeg -bsf:v trace_headers` and prints per coded picture (first slice):
nal_ref_idc / nal type, slice_type, frame_num, poc lsb, ref list modification (idc:value ...), MMCO ops,
long_term_reference_flag.  Pictures are numbered from 0; the log of the apitest case hskip ("pic N bytes ...") can be given
with --sizes to add the coded size.  Used to compare the OEM encoder's HSkip / ChangeRef structure with OpenIMP's."""
import re
import subprocess
import sys

def trace(path):
    out = subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'quiet', '-i', path, '-c', 'copy', '-bsf:v', 'trace_headers',
                          '-f', 'null', '-'], capture_output=True, text=True)
    txt = out.stderr if 'trace_headers' in out.stderr else out.stdout + out.stderr
    return txt

def pictures(path):
    txt = subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'debug', '-i', path, '-c', 'copy', '-bsf:v', 'trace_headers',
                          '-f', 'null', '-'], capture_output=True, text=True).stderr
    pics = []
    cur = None
    pps_qp = 26
    for line in txt.splitlines():
        if '[trace_headers' not in line:
            continue
        line = re.sub(r'^\[trace_headers @ 0x[0-9a-f]+\] ', '', line)
        m = re.match(r'(\d+)\s+(\w+)\s+([01]+)? ?= (-?\d+)', line)
        if line.startswith('nal_unit_type:') or not m:
            if line.startswith('nal_unit_type:'):
                mm = re.match(r'nal_unit_type: (\d+)\((.*?)\), nal_ref_idc: (\d+)', line)
                if mm and int(mm.group(1)) in (1, 5):
                    cur = {'nal': int(mm.group(1)), 'ref': int(mm.group(3)), 'mod': [], 'mmco': [], 'new': True}
                    pics.append(cur)
                elif mm:
                    cur = None
            continue
        name, val = m.group(2), int(m.group(4))
        if name == 'pic_init_qp_minus26':
            pps_qp = 26 + val
            continue
        if cur is None or not cur.get('new'):
            continue
        if name == 'slice_qp_delta' and 'qp' not in cur:
            cur['qp'] = pps_qp + val
        if name == 'slice_type' and 'slice_type' not in cur:
            cur['slice_type'] = val
        elif name == 'frame_num' and 'frame_num' not in cur:
            cur['frame_num'] = val
        elif name == 'pic_order_cnt_lsb':
            cur['poc'] = val
        elif name == 'num_ref_idx_active_override_flag':
            cur['override'] = val
        elif name == 'ref_pic_list_modification_flag_l0':
            cur['modflag'] = val
        elif name == 'modification_of_pic_nums_idc':
            cur['mod'].append(val)
        elif name in ('abs_diff_pic_num_minus1', 'long_term_pic_num'):
            cur['mod'].append(('v', val))
        elif name == 'memory_management_control_operation':
            cur['mmco'].append(val)
        elif name in ('difference_of_pic_nums_minus1', 'long_term_frame_idx', 'max_long_term_frame_idx_plus1', 'long_term_pic_num'):
            cur['mmco'].append(('v', val))
        elif name == 'long_term_reference_flag':
            cur['ltr'] = val
        elif name == 'no_output_of_prior_pics_flag':
            cur['noout'] = val
        elif name == 'slice_qp_delta' and 'qpd' not in cur:
            cur['qpd'] = val
    # collapse multi-slice pictures: a new picture starts at a new frame_num/IDR
    res = []
    for p in pics:
        if res and p['nal'] == 1 and res[-1].get('frame_num') == p.get('frame_num') and p.get('poc') == res[-1].get('poc'):
            continue
        res.append(p)
    return res

def fmt(i, p, size=None):
    mod = ''
    if p.get('modflag'):
        mod = ' mod[' + ' '.join(str(x[1]) if isinstance(x, tuple) else 'idc%d' % x for x in p['mod']) + ']'
    mm = ''
    if p['mmco']:
        mm = ' mmco[' + ' '.join('%s' % (x[1] if isinstance(x, tuple) else 'op%d' % x) for x in p['mmco']) + ']'
    ltr = ' ltr' if p.get('ltr') else ''
    s = '' if size is None else ' %d' % size
    return '%d nal%d/%d st%d fn%d poc%s%s%s%s%s' % (i, p['nal'], p['ref'], p.get('slice_type', -1) % 5, p.get('frame_num', -1),
                                                p.get('poc', '-'), mod, mm, ltr, s)

if __name__ == '__main__':
    sizes = {}
    if '--sizes' in sys.argv:
        k = sys.argv.index('--sizes')
        for line in open(sys.argv[k + 1]):
            m = re.match(r'pic (\d+) (\d+)', line)
            if m:
                sizes[int(m.group(1))] = int(m.group(2))
    if '--qp' in sys.argv:
        for p in pictures(sys.argv[1]):
            print(p.get('qp', -1))
        sys.exit(0)
    for i, p in enumerate(pictures(sys.argv[1])):
        print(fmt(i, p, sizes.get(i)))
