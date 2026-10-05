#!/bin/sh
set -eu
helper=$1
command -v ffmpeg >/dev/null || { echo "ffmpeg not installed, skipping"; exit 77; }
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT HUP INT TERM
# What an omanta without action options does: sets no codec at all.
unset OMANTA_OPTION_CODEC
probe() { ffprobe -v error -select_streams "$1" -show_entries "stream=$2" -of csv=p=0 "$3"; }

ffmpeg -v error -f lavfi -i testsrc=size=64x64:rate=10 -f lavfi -i sine=sample_rate=96000 \
    -t 1 -c:v mpeg4 -c:a aac "$scratch/clip one.mp4"
sh "$helper" "$scratch/clip one.mp4" >/dev/null
out="$scratch/clip one.resolve.mov"
[ "$(probe a "codec_name" "$out")" = pcm_s16le ]
[ "$(probe a "sample_rate" "$out")" = 48000 ]
[ "$(probe v "codec_name" "$out")" = mpeg4 ]
[ ! -e "$scratch/.clip one.resolve.part.mov" ]

# Never overwrites an earlier result.
echo keep > "$out"
sh "$helper" "$scratch/clip one.mp4" >/dev/null
[ "$(cat "$out")" = keep ]

# The codec chosen in Preferences, as omanta passes it.
rm "$out"
OMANTA_OPTION_CODEC=alac sh "$helper" "$scratch/clip one.mp4" >/dev/null
[ "$(probe a "codec_name" "$out")" = alac ]
rm "$out"
OMANTA_OPTION_CODEC=bogus sh "$helper" "$scratch/clip one.mp4" >/dev/null
[ "$(probe a "codec_name" "$out")" = pcm_s16le ]

# Already-PCM sources are left alone.
ffmpeg -v error -f lavfi -i testsrc=size=64x64:rate=10 -f lavfi -i sine \
    -t 1 -c:v mpeg4 -c:a pcm_s16le "$scratch/pcm.mov"
sh "$helper" "$scratch/pcm.mov" >/dev/null
[ ! -e "$scratch/pcm.resolve.mov" ]
