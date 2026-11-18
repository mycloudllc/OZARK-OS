#!/bin/sh
# Runs on the radio, inside SWUpdate, while this update installs into the
# system slot that isn't running (classes/ozark-slots.bbclass):
#   preinst   after the system image is written: empty the slot's boot folder
#   postinst  after its boot files are in: fit the filesystem to the slot,
#             point cmdline.txt at it, and write tryboot.txt to start it once
# It ships in the update, so each release decides how it's installed.
# /etc/ozark/slots.conf (ozark-update) says how the board starts a slot;
# without it (0.2.0 on the Pi), the Pi.

set -e

LOADER=rpi DISK=/dev/mmcblk0
[ -f /etc/ozark/slots.conf ] && . /etc/ozark/slots.conf

case " $(cat /proc/cmdline) " in
    *" root=${DISK}p2 "*) slot=b dev=${DISK}p3 ;;
    *" root=${DISK}p3 "*) slot=a dev=${DISK}p2 ;;
    *) echo "Not running from a system slot: can't install" >&2; exit 1 ;;
esac
boot=/boot
case "$LOADER" in
    uboot) marker=$boot/slot.txt ;;
    *) marker=$boot/config.txt ;;
esac
# /boot is mounted when used (automount): this mounts it
[ -f "$marker" ] || { echo "No boot partition at $boot" >&2; exit 1; }

case "$1" in
    preinst)
        rm -f "$boot/tryboot.txt" "$boot/tried.txt"
        rm -rf "${boot:?}/$slot"
        mkdir -p "$boot/$slot"
        ;;
    postinst)
        # The image is smaller than the slot: grow it (e2fsck exits 1 when it fixed something)
        e2fsck -fy "$dev" > /dev/null 2>&1 || [ $? -le 1 ]
        resize2fs "$dev" > /dev/null 2>&1
        sed -i "s|root=[^ ]*|root=$dev|" "$boot/$slot/cmdline.txt"
        if [ "$LOADER" = uboot ]; then
            # Not started until "ozark-update reboot" removes tried.txt
            echo "armed=no" > "$boot/tried.txt"
            printf 'try=%s\n' "$slot" > "$boot/tryboot.txt.new"
        else
            { grep -v '^os_prefix=' "$boot/$slot/config.txt"; printf '[all]\nos_prefix=%s/\n' "$slot"; } > "$boot/tryboot.txt.new"
        fi
        sync
        mv "$boot/tryboot.txt.new" "$boot/tryboot.txt"
        sync
        ;;
esac
