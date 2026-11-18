# Two system slots (A/B) on the SD card, for software updates.
#
# Each slot is a root partition (A: <disk>p2, B: <disk>p3, OZARK_SLOT_DISK;
# see wic/ozark-sdimage*.wks.in) plus a folder on the boot partition, a/ or
# b/, with that system's kernel, device tree(s) and command line. An update
# writes the other slot and starts it once; it only becomes the system that
# starts once it has come up (ozark-update commit). How a board picks the
# slot, OZARK_BOOTLOADER:
#
#   rpi    The Pi firmware finds the folder through os_prefix= in
#          /boot/config.txt; tryboot.txt starts the other one once. The
#          folder also has the kernel's overlays and the slot's config.txt.
#   uboot  boot.scr (ozark-boot) reads slot.txt; tryboot.txt starts the
#          other slot once (tried.txt marks that start as used). The folder
#          has Image, ozark.dtb (OZARK_BOOT_DTB) and cmdline.txt.
#
# Shared by the image (slot A's files on the SD card) and ozark-swu (the files
# an update installs into a slot).

# Slot size, in MiB. Both slots, and the image must fit: two of these, plus
# boot, logs and data, on an 8 GB card.
OZARK_SLOT_SIZE ?= "2560"

# The files that belong to one system, as (file in DEPLOY_DIR_IMAGE, path on
# the boot partition) pairs, in the slot's folder.
#
# rpi: IMAGE_BOOT_FILES from meta-raspberrypi, with everything that belongs to
# one system (kernel, dtbs, overlays) moved into the folder. The firmware
# (bootfiles/*: start4.elf, fixup4.dat, ...) stays at the top: shared.
# Adds the slot's own config.txt and cmdline.txt, and overlays/README, without
# which the firmware ignores the slot's overlays (rpi-cmdline.bbappend).
def ozark_slot_boot_files(d, slot):
    import os
    if d.getVar('OZARK_BOOTLOADER') == 'uboot':
        return [
            (d.getVar('KERNEL_IMAGETYPE'), '%s/Image' % slot),
            (d.getVar('OZARK_BOOT_DTB'), '%s/ozark.dtb' % slot),
            ('ozark-boot/cmdline.txt', '%s/cmdline.txt' % slot),
        ]

    bootfiles = d.getVar('BOOTFILES_DIR_NAME')
    files = []
    for entry in (d.getVar('IMAGE_BOOT_FILES') or '').split():
        src, _, dest = entry.partition(';')
        if src.startswith(bootfiles + '/'):
            continue
        files.append((src, '%s/%s' % (slot, dest or os.path.basename(src))))
    files.append(('%s/config.txt' % bootfiles, '%s/config.txt' % slot))
    files.append(('%s/cmdline.txt' % bootfiles, '%s/cmdline.txt' % slot))
    files.append(('ozark-slot/overlays-README', '%s/overlays/README' % slot))
    return files

# IMAGE_BOOT_FILES for a new SD card: what's shared at the top of the boot
# partition, and slot A's folder
def ozark_image_boot_files(d):
    if d.getVar('OZARK_BOOTLOADER') == 'uboot':
        top = ['ozark-boot/boot.scr;boot.scr', 'ozark-boot/slot.txt;slot.txt']
    else:
        top = ['%s/*' % d.getVar('BOOTFILES_DIR_NAME')]
    return ' '.join(top + ['%s;%s' % f for f in ozark_slot_boot_files(d, 'a')])
