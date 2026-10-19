#!/bin/bash
# anamotion.sh FILE.h264 W H [step] : host side of OPENIMP_TEST_PATTERN=2 (moving square + counter)
# decodes all frames, runs tbmotion (per-frame PSNR/SSIM, counter drops/dups/reorder, ghosts), prints bit rate.
# build: gcc -O2 -o tbmotion tbmotion.c -lm
F=$1; W=$2; H=$3; STEP=${4:-8}; T=$(dirname $0); D=$(dirname $F)/dec; mkdir -p $D; B=$D/$(basename $F | tr . _)
ffmpeg -v error -y -i $F -vsync passthrough -f rawvideo -pix_fmt yuv420p $B.all.yuv
ffprobe -v error -select_streams v:0 -show_entries frame=pict_type -of csv=p=0 $F > $B.types
ffprobe -v error -select_streams v:0 -show_entries packet=size,pts_time,flags -of csv=p=0 $F > $B.pkts
$T/tbmotion $B.all.yuv $W $H $STEP $B.types
# no timestamps in a raw H.264 stream: duration = packets / FPS (env FPS, default 25)
awk -F, -v fps=${FPS:-25} '{n++; b+=$2; if($3 ~ /K/){ni++; bi+=$2} else {bp+=$2; np++}} END{d=n/fps;
  printf "BITRATE packets %d (I %d, P %d), %.0f kbit/s avg over %.1f s (at %d fps), I avg %.0f B, P avg %.0f B\n", n, ni, np, b*8/1000/d, d, fps, ni?bi/ni:0, np?bp/np:0}' $B.pkts
