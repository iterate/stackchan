#!/bin/sh
# Regenerate the animated previews in preview/ from the deterministic
# keyframe scripts. Requires cc and ffmpeg.
set -eu
cd "$(dirname "$0")/.."

make frames
mkdir -p preview build/frames

for slug in wc-scope-beam wc-flipdot-cascade wc-chladni-sand \
            wc-halftone-press wc-wayang-lamp wc-ferro-pool \
            wc-teletext-sextant; do
    for script in idle speech; do
        dir="build/frames/$slug-$script"
        mkdir -p "$dir"
        ./build/render_frames "$slug" "$script" 120 30 "$dir"
        ffmpeg -loglevel error -y -framerate 30 -i "$dir/frame_%04d.ppm" \
            -vf "fps=15,scale=240:180:flags=neighbor,split[s0][s1];[s0]palettegen=max_colors=128[p];[s1][p]paletteuse=dither=bayer" \
            -loop 0 "preview/$slug-$script.gif"
    done
    echo "previewed $slug"
done
