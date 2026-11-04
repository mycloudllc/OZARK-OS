SUMMARY = "Ozark OS base image"
DESCRIPTION = "Milestones 1-2: boots with the Ozark Mountain Technologies splash \
to a console login. No graphics stack beyond the splash."
LICENSE = "MIT"

inherit core-image

# "splash" pulls in psplash, which meta-ozark brands with the Ozark logo.
IMAGE_FEATURES += "splash"

IMAGE_INSTALL:append = " \
    kernel-modules \
"

# Plain-text boot logs on the SD card's OZARKLOGS partition (Pi only: the
# partition comes from ozark-sdimage.wks.in)
IMAGE_INSTALL:append:rpi = " ozark-logs"

# Room for logs and experiments on the dev image
IMAGE_ROOTFS_EXTRA_SPACE = "524288"

# A/B system slots on the Pi (classes/ozark-slots.bbclass): this system is
# slot A on a new SD card, its kernel, dtbs and overlays in the boot
# partition's a/ folder. It must fit a slot.
inherit ozark-slots
IMAGE_ROOTFS_MAXSIZE:rpi = "${@int(d.getVar('OZARK_SLOT_SIZE')) * 1024}"
python () {
    if 'rpi' in (d.getVar('MACHINEOVERRIDES') or '').split(':'):
        bootfiles = d.getVar('BOOTFILES_DIR_NAME')
        files = ['%s/*' % bootfiles] + ['%s;%s' % f for f in ozark_slot_boot_files(d, 'a')]
        d.setVar('IMAGE_BOOT_FILES', ' '.join(files))
}

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
