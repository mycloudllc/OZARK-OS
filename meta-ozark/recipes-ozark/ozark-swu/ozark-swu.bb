SUMMARY = "Ozark OS software update (.swu)"
DESCRIPTION = "ozark-image as a signed SWUpdate file, for Settings > Update \
(ozark-update): the root filesystem, written raw into the system slot that \
isn't running, and that system's boot files (kernel, device trees, and on \
the Pi its overlays and config.txt; cmdline.txt) for the slot's folder on \
the boot partition. \
Deployed as ozark-swu-${MACHINE}.rootfs.swu; scripts/make-release.sh makes the \
update feed for it."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

# Boards with the A/B layout (OZARK_AB_MACHINES in the distro conf)
COMPATIBLE_MACHINE = "^ozarkab$"

SRC_URI = "file://sw-description file://ozark-install.sh"

inherit swupdate ozark-slots

IMAGE_DEPENDS = "ozark-image"
OZARK_SWU_ROOTFS = "ozark-image-${MACHINE}.rootfs.ext4.zst"
SWUPDATE_IMAGES = "${OZARK_SWU_ROOTFS} ${WORKDIR}/ozark-boot.tar.gz"

# Signed with the build's key (scripts/make-keys.sh); units check it
# against the public key in their image
SWUPDATE_SIGNING = "RSA"
SWUPDATE_PRIVATE_KEY ?= "${OZARK_KEYS_DIR}/swupdate-private.pem"

# The image's build stamp (ozark-image-base.bb), for sw-description
OZARK_BUILD = ""
# Slot partitions in sw-description: ${OZARK_SLOT_DISK}p2 and p3
do_swuimage[vardeps] += "OZARK_SLOT_DISK"

python do_swuimage:prepend() {
    import os, tarfile

    deploy = d.getVar('DEPLOY_DIR_IMAGE')
    with open(os.path.join(deploy, 'ozark-image-%s.rootfs.build' % d.getVar('MACHINE'))) as f:
        for line in f:
            if line.startswith('BUILD='):
                d.setVar('OZARK_BUILD', line.strip().split('=', 1)[1])

    # The slot's boot folder, as the SD card image has it in a/
    with tarfile.open(os.path.join(d.getVar('WORKDIR'), 'ozark-boot.tar.gz'), 'w:gz') as tar:
        for src, dest in ozark_slot_boot_files(d, '.'):
            tar.add(os.path.realpath(os.path.join(deploy, src)), arcname=dest)
}
do_swuimage[file-checksums] += "${SWUPDATE_PRIVATE_KEY}:True"
