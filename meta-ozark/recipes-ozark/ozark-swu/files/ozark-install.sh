#!/bin/sh
# Runs on the radio, inside SWUpdate, while this update installs into the
# system slot that isn't running (classes/ozark-slots.bbclass):
#   preinst   after the system image is written: empty the slot's boot folder
#   postinst  after its boot files are in: fit the filesystem to the slot,
#             point cmdline.txt at it, and write tryboot.txt to boot it once
# It ships in the update, so each release decides how it's installed.

set -e

case " $(cat /proc/cmdline) " in
    *" root=/dev/mmcblk0p2 "*) slot=b dev=/dev/mmcblk0p3 ;;
    *" root=/dev/mmcblk0p3 "*) slot=a dev=/dev/mmcblk0p2 ;;
    *) echo "Not running from a system slot: can't install" >&2; exit 1 ;;
esac
boot=/boot
# /boot is mounted when used (automount): this mounts it
[ -f "$boot/config.txt" ] || { echo "No boot partition at $boot" >&2; exit 1; }

case "$1" in
    preinst)
        rm -f "$boot/tryboot.txt"
        rm -rf "${boot:?}/$slot"
        mkdir -p "$boot/$slot"
        ;;
    postinst)
        # The image is smaller than the slot: grow it (e2fsck exits 1 when it fixed something)
        e2fsck -fy "$dev" > /dev/null 2>&1 || [ $? -le 1 ]
        resize2fs "$dev" > /dev/null 2>&1
        sed -i "s|root=[^ ]*|root=$dev|" "$boot/$slot/cmdline.txt"
        { grep -v '^os_prefix=' "$boot/$slot/config.txt"; printf '[all]\nos_prefix=%s/\n' "$slot"; } > "$boot/tryboot.txt.new"
        sync
        mv "$boot/tryboot.txt.new" "$boot/tryboot.txt"
        sync
        ;;
esac
