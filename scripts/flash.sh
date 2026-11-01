#!/usr/bin/env bash
# Write an Ozark OS image to an SD card with bmaptool.
#
#   ./scripts/flash.sh /dev/sdX [image.wic.bz2]
#
# Without an image argument it flashes the latest ozark-image build.
# Run it on the Linux host (not inside the kas container). Needs bmaptool
# (Debian/Ubuntu: sudo apt install bmap-tools) and sudo.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEPLOY="$ROOT/build/build/tmp/deploy/images/raspberrypi4-64"

dev="${1:-}"
img="${2:-$DEPLOY/ozark-image-raspberrypi4-64.rootfs.wic.bz2}"

if [ -z "$dev" ]; then
    echo "Usage: $0 /dev/sdX [image.wic.bz2]" >&2
    echo >&2
    echo "Removable disks on this machine:" >&2
    lsblk -d -o NAME,SIZE,MODEL,TRAN,RM | awk 'NR==1 || $NF==1' >&2
    exit 2
fi

command -v bmaptool >/dev/null 2>&1 || { echo "bmaptool not found (apt install bmap-tools)" >&2; exit 1; }
[ -b "$dev" ] || { echo "$dev is not a block device" >&2; exit 1; }
[ -f "$img" ] || { echo "Image not found: $img" >&2; exit 1; }

bmap="${img%.bz2}.bmap"
[ -f "$bmap" ] || { echo "Block map not found: $bmap" >&2; exit 1; }

# Refuse whole-disk targets that hold the running system.
root_src="$(findmnt -n -o SOURCE / || true)"
root_disk="$(lsblk -n -o PKNAME "$root_src" 2>/dev/null | head -n1 || true)"
if [ -n "$root_disk" ] && [ "/dev/$root_disk" = "$dev" ]; then
    echo "Refusing: $dev holds this machine's root filesystem." >&2
    exit 1
fi

if [ "$(lsblk -dn -o RM "$dev")" != "1" ]; then
    echo "WARNING: $dev is not marked as removable. Double-check it is your SD card." >&2
fi

echo "Image:  $img"
echo "Target: $dev"
lsblk -o NAME,SIZE,MODEL,FSTYPE,LABEL,MOUNTPOINT "$dev"
echo
echo "EVERYTHING on $dev will be erased."
read -r -p "Type the device path again to confirm: " confirm
[ "$confirm" = "$dev" ] || { echo "Aborted."; exit 1; }

# Unmount any mounted partitions on the card.
lsblk -ln -o PATH,MOUNTPOINT "$dev" | awk '$2 != "" {print $1}' | while read -r part; do
    echo "Unmounting $part"
    sudo umount "$part"
done

sudo bmaptool copy --bmap "$bmap" "$img" "$dev"
sync
echo "Done. Put the card in the Pi and power it on."
