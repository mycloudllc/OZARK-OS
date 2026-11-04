SUMMARY = "Data partition: settings that survive software updates"
DESCRIPTION = "Mounts the SD card's ozarkdata partition (see ozark-sdimage.wks.in) \
at /data, grows it to fill the card on first boot, and keeps the directories \
with settings and pairings there (overlayfs), so an update that replaces the \
system (ozark-update) keeps them."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://ozark-data file://ozark-data.service file://ozark-data-grow.service"
S = "${WORKDIR}"

inherit systemd

do_install() {
    install -D -m 0755 ${S}/ozark-data ${D}${sbindir}/ozark-data
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${S}/ozark-data.service ${S}/ozark-data-grow.service ${D}${systemd_system_unitdir}/
    install -d ${D}/data
}

# fstab belongs to base-files, so add the line at rootfs time. nofail: a card
# without the partition still boots (settings then stay in the system).
pkg_postinst:${PN}() {
    grep -q '^LABEL=ozarkdata ' $D${sysconfdir}/fstab || \
        echo "LABEL=ozarkdata /data ext4 noatime,nofail,x-systemd.device-timeout=10s,x-systemd.growfs 0 2" >> $D${sysconfdir}/fstab
}

SYSTEMD_SERVICE:${PN} = "ozark-data.service ozark-data-grow.service"

FILES:${PN} += "/data"

RDEPENDS:${PN} = "util-linux-sfdisk util-linux-partx e2fsprogs-e2fsck"
