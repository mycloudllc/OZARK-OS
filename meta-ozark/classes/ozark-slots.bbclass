# Two system slots (A/B) on the Raspberry Pi SD card, for software updates.
#
# Each slot is a root partition (A: mmcblk0p2, B: mmcblk0p3; see
# wic/ozark-sdimage.wks.in) plus a folder on the boot partition, a/ or b/, with
# that system's kernel, device trees, overlays, cmdline.txt and config.txt.
# The firmware finds the folder through os_prefix= in /boot/config.txt. An
# update writes the other slot and boots it once through tryboot.txt; it only
# goes into config.txt once it has come up (ozark-update commit).
#
# Shared by the image (slot A's files on the SD card) and ozark-swu (the files
# an update installs into a slot).

# Slot size, in MiB. Both slots, and the image must fit: two of these, plus
# boot, logs and data, on an 8 GB card.
OZARK_SLOT_SIZE ?= "2560"

# IMAGE_BOOT_FILES from meta-raspberrypi, with everything that belongs to one
# system (kernel, dtbs, overlays) moved into the slot's folder. The firmware
# (bootfiles/*: start4.elf, fixup4.dat, ...) stays at the top: shared.
# Adds the slot's own config.txt and cmdline.txt, and overlays/README, without
# which the firmware ignores the slot's overlays (rpi-cmdline.bbappend).
def ozark_slot_boot_files(d, slot):
    import os
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
