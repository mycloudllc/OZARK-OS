#!/usr/bin/env bash
# Ozark OS build wrapper. Runs every Yocto step inside the kas container,
# so the host only needs Docker (or Podman), git and bash.
#
#   ./scripts/build.sh [command] [options]
#
# Commands:
#   build      Build the image (default)
#   shell      Open a BitBake shell inside the container (bitbake, devtool, ...)
#   sdk        Build the cross-SDK installer for app development
#   lock       Pin every layer to its current commit (writes kas/ozark-rpi4.lock.yml)
#
# Options:
#   --base     Build ozark-image-base (splash + login prompt, no Qt) instead of ozark-image
#   --release  Leave out the dev fragment (no serial console, SSH or empty root password)
#   --rpi7     Official 7" DSI touchscreen instead of the Waveshare 8" panel
#   --mirrors  Use the Yocto Project public sstate mirror to speed up the first build
#   --qemu     Build for QEMU (qemuarm64) instead of the Pi; boot it with scripts/run-qemu.sh
#   -h|--help  Show this help
#
# Environment:
#   OZARK_DL_DIR      Download cache        (default: <repo>/cache/downloads)
#   OZARK_SSTATE_DIR  Shared-state cache    (default: <repo>/cache/sstate)
#   KAS_CONTAINER_ENGINE  docker or podman  (default: auto-detect)

set -euo pipefail

KAS_VERSION="5.5"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

usage() { sed -n '2,/^$/{s/^# \{0,1\}//;p}' "$0"; }

cmd="build"
target=""
dev=1
fragments=()
machine="raspberrypi4-64"

while [ $# -gt 0 ]; do
    case "$1" in
        build|shell|sdk|lock) cmd="$1" ;;
        --base)    target="ozark-image-base" ;;
        --release) dev=0 ;;
        --rpi7)    fragments+=("kas/display-rpi7.yml") ;;
        --mirrors) fragments+=("kas/mirrors.yml") ;;
        --qemu)    fragments+=("kas/qemu.yml"); machine="qemuarm64" ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

[ "$dev" -eq 1 ] && fragments=("kas/dev.yml" "${fragments[@]}")

config="kas/ozark-rpi4.yml"
for f in "${fragments[@]}"; do
    config="${config}:${f}"
done

# --- Host checks -------------------------------------------------------------

if ! command -v docker >/dev/null 2>&1 && ! command -v podman >/dev/null 2>&1; then
    echo "Neither docker nor podman is installed. Install one of them first." >&2
    exit 1
fi

# kas resolves the local meta-ozark layer through this repository's git root.
if ! git -C "$ROOT" rev-parse --show-toplevel >/dev/null 2>&1; then
    echo "==> $ROOT is not a git repository yet; running 'git init' so kas can find meta-ozark."
    git -C "$ROOT" init -q
fi

case "$ROOT" in
    /mnt/[a-z]/*)
        echo "WARNING: building from a Windows drive under WSL is extremely slow." >&2
        echo "         Move the repo into your WSL home directory (ext4) instead." >&2
        ;;
esac

avail_gb=$(df -Pk "$ROOT" | awk 'NR==2 {print int($4/1024/1024)}')
if [ "$avail_gb" -lt 100 ]; then
    echo "WARNING: only ${avail_gb} GB free here; a first build needs roughly 100 GB." >&2
fi

# Updates (.swu) are signed: make the key pair on the first build
"$ROOT/scripts/make-keys.sh"

# --- kas-container -------------------------------------------------------------

KAS_CONTAINER="$(command -v kas-container || true)"
if [ -z "$KAS_CONTAINER" ]; then
    KAS_CONTAINER="$ROOT/.tools/kas-container"
    if [ ! -x "$KAS_CONTAINER" ]; then
        echo "==> Downloading kas-container $KAS_VERSION"
        mkdir -p "$ROOT/.tools"
        curl -fsSL -o "$KAS_CONTAINER" \
            "https://raw.githubusercontent.com/siemens/kas/${KAS_VERSION}/kas-container"
        chmod +x "$KAS_CONTAINER"
    fi
fi

# --- Caches (kept outside the build dir so 'rm -rf build' is cheap) -----------

export DL_DIR="${OZARK_DL_DIR:-$ROOT/cache/downloads}"
export SSTATE_DIR="${OZARK_SSTATE_DIR:-$ROOT/cache/sstate}"
export KAS_WORK_DIR="$ROOT/build"
mkdir -p "$DL_DIR" "$SSTATE_DIR" "$KAS_WORK_DIR"

echo "==> Config:  $config"
[ -n "$target" ] && echo "==> Target:  $target"
[ "$dev" -eq 1 ] && echo "==> Dev image: serial console + SSH + empty root password (use --release to drop)"

target_args=()
[ -n "$target" ] && target_args=(--target "$target")

case "$cmd" in
    build) "$KAS_CONTAINER" build "${target_args[@]}" "$config" ;;
    sdk)   "$KAS_CONTAINER" build "${target_args[@]}" --task populate_sdk "$config" ;;
    shell) "$KAS_CONTAINER" shell "$config" ;;
    lock)  "$KAS_CONTAINER" lock "kas/ozark-rpi4.yml" ;;
esac

if [ "$cmd" = "build" ]; then
    img_dir="$KAS_WORK_DIR/build/tmp/deploy/images/$machine"
    echo
    echo "==> Done. Images are in: $img_dir"
    if [ "$machine" = "qemuarm64" ]; then
        echo "    Boot with: ./scripts/run-qemu.sh$([ "$target" = "ozark-image-base" ] && echo " --base")"
    else
        echo "    Flash with: ./scripts/flash.sh /dev/sdX"
    fi
fi
