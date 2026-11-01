#!/usr/bin/env bash
# Boot an Ozark OS qemuarm64 build (./scripts/build.sh --qemu) on this PC.
#
#   ./scripts/run-qemu.sh [options]
#
# Options:
#   --base       Boot ozark-image-base instead of ozark-image
#   --size WxH   Screen size (default 800x480, the official 7" Pi touchscreen)
#   --no-gl      Plain 2D virtio-gpu, for hosts where GTK OpenGL doesn't work
#   --persist    Keep changes to the rootfs (default: every boot starts clean)
#   -h|--help    Show this help
#
# A window shows the screen; this terminal is the serial console (log in as root).
# SSH: ssh -p 2222 root@localhost
# Quit: close the window, or Ctrl+A then X in this terminal.
#
# Needs QEMU on the host (Debian/Ubuntu/Pop!_OS: sudo apt install qemu-system-arm qemu-system-gui).

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEPLOY="$ROOT/build/build/tmp/deploy/images/qemuarm64"

usage() { sed -n '2,/^$/{s/^# \{0,1\}//;p}' "$0"; }

image="ozark-image"
size="800x480"
gl=1
snapshot=(-snapshot)

while [ $# -gt 0 ]; do
    case "$1" in
        --base)    image="ozark-image-base" ;;
        --size)    size="${2:?--size needs WxH}"; shift ;;
        --no-gl)   gl=0 ;;
        --persist) snapshot=() ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

[[ "$size" =~ ^([0-9]+)x([0-9]+)$ ]] || { echo "--size must look like 800x480" >&2; exit 2; }
xres="${BASH_REMATCH[1]}" yres="${BASH_REMATCH[2]}"

command -v qemu-system-aarch64 >/dev/null 2>&1 || {
    echo "qemu-system-aarch64 not found (sudo apt install qemu-system-arm qemu-system-gui)" >&2
    exit 1
}

kernel="$DEPLOY/Image"
rootfs="$DEPLOY/$image-qemuarm64.rootfs.ext4"
build_cmd="./scripts/build.sh --qemu"
[ "$image" = "ozark-image-base" ] && build_cmd="$build_cmd --base"
for f in "$kernel" "$rootfs"; do
    [ -f "$f" ] || { echo "Not found: $f (build it with $build_cmd)" >&2; exit 1; }
done

if [ "$gl" -eq 1 ]; then
    gpu="virtio-gpu-gl-pci"; display="gtk,gl=on"
else
    gpu="virtio-gpu-pci"; display="gtk"
fi

# Same quiet-boot flags the Pi gets from rpi-cmdline.bbappend
cmdline="root=/dev/vda rw console=ttyAMA0,115200 quiet loglevel=3 logo.nologo vt.global_cursor_default=0 consoleblank=0"

echo "==> Booting $image ($size, $([ "$gl" -eq 1 ] && echo "host OpenGL" || echo "2D only"))"
[ ${#snapshot[@]} -gt 0 ] && echo "==> Changes are thrown away on exit (use --persist to keep them)"

exec qemu-system-aarch64 \
    -machine virt -cpu max -smp 4 -m 2048 \
    -kernel "$kernel" -append "$cmdline" \
    -drive "file=$rootfs,if=none,format=raw,id=hd" -device virtio-blk-pci,drive=hd \
    "${snapshot[@]}" \
    -device "$gpu,xres=$xres,yres=$yres" -display "$display" \
    -device virtio-keyboard-pci -device virtio-tablet-pci \
    -netdev user,id=net0,hostfwd=tcp::2222-:22 -device virtio-net-pci,netdev=net0 \
    -serial mon:stdio
