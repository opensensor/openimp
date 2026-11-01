#!/usr/bin/env python3
"""Issue #7 (development aid): the OEM T21 1.0.33 rate controller ignores the
IMP_Encoder_SetChnRcTrigLevel pair.  Runs i264e_ratecontrol_start/_is_reenc of
the vendor libimp under unicorn (see eprc_oracle.py) on the seven oracle
scenarios with the i264e parameters trigger level / new max QP (+272/+276 in
eprc_oracle numbering) at default and at four other pairs and compares every
picture type, QP, size and re-encode decision.  Expected: "differences: 0".

    tools/eprc_trig_oracle.py T21/lib/1.0.33/uclibc/5.4.0/libimp.so
"""
import sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import struct, eprc_oracle as O
lib=sys.argv[1]
def f2i(f): return struct.unpack('<I',struct.pack('<f',f))[0]
def run(sc, trig):
    (mode,w,h,gop,fps,minqp,maxqp,br,mbr,fstep,gstep,bias,static,cpos,qual,bgmul,initqp,idr)=sc[:18]
    extra=O.validated_vbv(lib,w,h)
    extra[204]=0
    if trig: extra[272]=f2i(trig[0]); extra[276]=trig[1]
    s=O.Sim(lib,mode=mode,w=w,h=h,gop=gop,fps=(fps,1),minqp=minqp,maxqp=maxqp,bitrate=br,maxbitrate=mbr,
      frm_step=fstep,gop_step=gstep,bias=bias,static_time=static,change_pos=cpos,quality=qual,bgmul=bgmul,
      rclevel=initqp&0xffffffff,extra=extra)
    L=s.L; out=[]
    for f in range(240):
        since=f%idr; L.w32(s.H,since)
        L.call('i264e_ratecontrol_start',s.H)
        t=L.rd(s.SL,1)[0]; qp=L.rd(s.R+L.lay['qp'],1)[0]
        size=O.picture_bytes(f,qp,since==0,w*h,1)
        regs=O.statistics(f)
        while True:
            for i,v in enumerate(regs): L.w32(s.TB+8*i+4,v)
            L.w32(s.H+3888,size); L.w32(s.H+736,1)
            r=L.call('i264e_ratecontrol_is_reenc',s.H)
            out.append((t,qp,size,r))
            if r!=1: break
            qp=L.rd(s.R+L.lay['qp'],1)[0]
            size=O.picture_bytes(f,qp,since==0,w*h,1)
    return out
tot=0
for n,sc in enumerate(O.SCENARIOS):
    base=run(sc,None)
    for trig in ((2.0,20),(10.0,51),(2.0,sc[6]),(5.5,30)):
        o=run(sc,trig)
        print(n,trig,len(base),'identical' if o==base else 'DIFF')
        tot+= o!=base
print('differences:',tot)
