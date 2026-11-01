SUMMARY = "Boot logs on the SD card"
DESCRIPTION = "Writes each boot's system journal as plain text to the OZARKLOGS \
vfat partition (see ozark-sdimage.wks), mounted at /logs, so logs from a Pi that \
won't come up can be read by putting its SD card in any PC."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://ozark-logs file://ozark-logs.service"
S = "${WORKDIR}"

inherit systemd

do_install() {
    install -D -m 0755 ${S}/ozark-logs ${D}${sbindir}/ozark-logs
    install -D -m 0644 ${S}/ozark-logs.service ${D}${systemd_system_unitdir}/ozark-logs.service
    install -d ${D}/logs
}

# fstab belongs to base-files, so add the line at rootfs time.
# nofail + short timeout: a card without the partition still boots normally.
# flush: vfat writes data out sooner. Pass 2: fsck.vfat repairs it after a power cut.
pkg_postinst:${PN}() {
    grep -q OZARKLOGS $D${sysconfdir}/fstab || \
        echo "LABEL=OZARKLOGS /logs vfat noatime,flush,umask=0022,nofail,x-systemd.device-timeout=10s 0 2" >> $D${sysconfdir}/fstab
}

SYSTEMD_SERVICE:${PN} = "ozark-logs.service"

FILES:${PN} += "/logs"

# fsck.vfat for the fstab pass
RDEPENDS:${PN} = "dosfstools"
