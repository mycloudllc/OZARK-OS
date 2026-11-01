#!/usr/bin/env bash
# Regenerate the boot splash and app badge PNGs from branding/*.svg.
# Needs rsvg-convert (apt install librsvg2-bin) and the Barlow Condensed
# fonts installed on the host (they are vendored in meta-ozark, see below).
#
#   ./scripts/make-splash.sh

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
B="$ROOT/branding"
L="$ROOT/meta-ozark"
FONTS="$L/recipes-graphics/fonts/ttf-barlow-condensed"

command -v rsvg-convert >/dev/null 2>&1 || { echo "rsvg-convert not found (apt install librsvg2-bin)" >&2; exit 1; }

# (grep without -q reads all input, so pipefail doesn't trip on SIGPIPE)
if ! fc-list : family | grep -i "Barlow Condensed" >/dev/null; then
    echo "Installing Barlow Condensed into ~/.local/share/fonts for rendering"
    mkdir -p "$HOME/.local/share/fonts"
    cp "$FONTS"/*.ttf "$HOME/.local/share/fonts/"
    fc-cache -f >/dev/null
fi

rsvg-convert -o "$L/recipes-core/psplash/files/psplash-ozark-img.png" "$B/ozark-splash.svg"
rsvg-convert -w 256 -h 256 -o "$L/recipes-qt/hello-ozark/files/src/ozark-badge.png" "$B/ozark-badge.svg"
cp "$L/recipes-core/psplash/files/psplash-ozark-img.png" "$B/ozark-splash.png"

echo "Updated splash and badge PNGs."
