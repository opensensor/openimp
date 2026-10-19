#!/bin/bash
# ana.sh FILE W H [rot] : decode (H.264: frame 100 / JPEG), compare with the pattern W x H, write png
# Host side of the OPENIMP_TEST_PATTERN=1 device test; build: gcc -O2 -o tbcmp tbcmp.c -lm
F=$1; W=$2; H=$3; ROT=${4:-0}; T=$(dirname $0); D=$(dirname $F)/dec; mkdir -p $D; B=$D/$(basename $F | tr . _)
$T/tbcmp gen $W $H $D/ref_${W}x${H}.yuv
case $F in *.jpg) PF=yuvj420p; SEL=;; *) PF=yuv420p; SEL="-vf select=eq(n\,${FRAME:-100}),format=yuv420p";; esac
[ "$PF" = yuvj420p ] && SEL="-vf format=yuvj420p"
ffmpeg -v error -y -i $F $SEL -frames:v 1 -f rawvideo $B.yuv
ffprobe -v error -select_streams v:0 -show_entries stream=codec_name,width,height,pix_fmt,color_range,color_space,color_primaries,color_transfer,profile -of compact=p=0 $F
[ "$ROT" = 0 ] && DW=$W DH=$H || { DW=$H; DH=$W; }
ffmpeg -v error -y -f rawvideo -pix_fmt yuv420p -s ${DW}x${DH} -i $B.yuv $B.png
$T/tbcmp cmp $D/ref_${W}x${H}.yuv $B.yuv $W $H $ROT
