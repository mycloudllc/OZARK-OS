SUMMARY = "Ozark OS base image"
DESCRIPTION = "Milestones 1-2: boots with the Ozark Mountain Technologies splash \
to a console login. No graphics stack beyond the splash."
LICENSE = "MIT"

inherit core-image

# "splash" pulls in psplash, which meta-ozark brands with the Ozark logo.
IMAGE_FEATURES += "splash"

IMAGE_INSTALL:append = " \
    kernel-modules \
    usbutils \
"

# Plain-text boot logs on the SD card's OZARKLOGS partition (boards with the
# A/B layout: the partition comes from wic/ozark-sdimage*.wks.in)
IMAGE_INSTALL:append:ozarkab = " ozark-logs"

# Room for logs and experiments on the dev image
IMAGE_ROOTFS_EXTRA_SPACE = "524288"

# A/B system slots (classes/ozark-slots.bbclass): this system is slot A on a
# new SD card, its kernel and device tree(s) in the boot partition's a/
# folder. It must fit a slot.
inherit ozark-slots
IMAGE_ROOTFS_MAXSIZE:ozarkab = "${@int(d.getVar('OZARK_SLOT_SIZE')) * 1024}"
python () {
    if 'ozarkab' in (d.getVar('MACHINEOVERRIDES') or '').split(':'):
        d.setVar('IMAGE_BOOT_FILES', ozark_image_boot_files(d))
}

# U-Boot boards: the boot partition's files and the raw bootloader come from
# these recipes' deploy dirs
do_image_wic[depends] += "${@'ozark-boot:do_deploy virtual/bootloader:do_deploy virtual/kernel:do_deploy' if d.getVar('OZARK_BOOTLOADER') == 'uboot' else ''}"

# Build stamp, so two builds of one version can be told apart (Settings >
# Update, and whether an online update is newer). Deployed next to the image
# for ozark-swu.
ROOTFS_POSTPROCESS_COMMAND += "ozark_build_stamp; "
IMAGE_POSTPROCESS_COMMAND += "ozark_deploy_build_stamp; "
ozark_build_stamp() {
    echo "BUILD=${DATETIME}" > ${IMAGE_ROOTFS}${sysconfdir}/ozark-build
}
ozark_build_stamp[vardepsexclude] += "DATETIME"
ozark_deploy_build_stamp() {
    install -m 0644 ${IMAGE_ROOTFS}${sysconfdir}/ozark-build ${IMGDEPLOYDIR}/${IMAGE_LINK_NAME}.build
}
