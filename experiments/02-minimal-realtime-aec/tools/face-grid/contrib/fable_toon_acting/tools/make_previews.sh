#!/bin/sh
# Render animated GIF previews (requires ffmpeg).
#   idle      — breathing, blinks, saccades, activity beats
#   speech    — pseudo-phrase viseme cycling
#   emotions  — all 11 stage expressions, 1.5 s each with attack/release
#   gestures  — nod/shake/tilt/lean-in/bounce cues
set -eu
cd "$(dirname "$0")/.."

DUMP=build/fta_dump
if [ ! -x "$DUMP" ]; then
    echo "build fta_dump first (make sheets does)" >&2
    exit 1
fi
if ! command -v ffmpeg >/dev/null 2>&1; then
    echo "ffmpeg not found; skipping GIF previews" >&2
    exit 0
fi
mkdir -p preview

render_gif() {
    slug="$1"
    script="$2"
    frames="$3"
    dir="build/frames-$slug-$script"
    rm -rf "$dir"
    mkdir -p "$dir"
    "$DUMP" frames "$slug" "$script" "$dir" "$frames" 30
    ffmpeg -loglevel error -y -framerate 30 -i "$dir/frame_%04d.ppm" \
        -vf "fps=15,scale=240:180:flags=neighbor,split[s0][s1];[s0]palettegen=max_colors=128[p];[s1][p]paletteuse=dither=bayer" \
        -loop 0 "preview/$slug-$script.gif"
    echo "preview/$slug-$script.gif"
}

render_gif toon-bean idle 240
render_gif toon-bean speech 120
render_gif toon-bean emotions 495
render_gif toon-bean gestures 300
render_gif toon-ink emotions 495
render_gif toon-ember emotions 495
