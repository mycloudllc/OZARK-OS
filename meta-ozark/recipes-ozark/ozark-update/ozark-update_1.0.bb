SUMMARY = "Software updates for Ozark OS (Settings > Update)"
DESCRIPTION = "ozark-update: installs signed updates (.swu, from ozark-swu) into \
the system slot that isn't running, with SWUpdate, then boots it once \
(Raspberry Pi tryboot) and keeps it only if it comes up. Updates come from a \
USB drive or an update feed on the web (a small JSON file)."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = " \
    file://ozark-update \
    file://ozark-update-helper.c \
    file://ozark-update-boot.service \
    file://ozark-update-check.service \
    file://ozark-update-check.timer \
    file://update.conf \
    file://10-ozark-watchdog.conf \
"
S = "${WORKDIR}"

DEPENDS = "swupdate json-c"

inherit systemd

# The update feed a new unit checks: a JSON file (scripts/make-release.sh
# writes it), e.g. https://github.com/OWNER/REPO/releases/latest/download/ozark-update.json
# Settings > Update can change it. Empty: none until set there.
OZARK_UPDATE_FEED ?= ""

# Public half of the update signing key (scripts/make-keys.sh)
OZARK_UPDATE_KEY ?= "${OZARK_KEYS_DIR}/swupdate-public.pem"
do_install[file-checksums] += "${OZARK_UPDATE_KEY}:True"

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} -o ozark-update-helper ozark-update-helper.c -lswupdate -ljson-c
}

do_install() {
    if [ ! -f "${OZARK_UPDATE_KEY}" ]; then
        bbfatal "No update signing key at ${OZARK_UPDATE_KEY}: run scripts/make-keys.sh"
    fi
    install -D -m 0755 ${S}/ozark-update ${D}${sbindir}/ozark-update
    install -D -m 0755 ${B}/ozark-update-helper ${D}${libexecdir}/ozark-update-helper
    install -D -m 0644 ${OZARK_UPDATE_KEY} ${D}${sysconfdir}/swupdate/public.pem

    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${S}/ozark-update-boot.service ${S}/ozark-update-check.service \
        ${S}/ozark-update-check.timer ${D}${systemd_system_unitdir}/
    install -D -m 0644 ${S}/10-ozark-watchdog.conf ${D}${systemd_unitdir}/system.conf.d/10-ozark-watchdog.conf

    install -d ${D}${sysconfdir}/ozark
    sed -e 's|@FEED@|${OZARK_UPDATE_FEED}|' ${S}/update.conf > ${D}${sysconfdir}/ozark/update.conf

    # Which updates fit (sw-description's hardware-compatibility)
    echo "${MACHINE} 1.0" > ${D}${sysconfdir}/hwrevision
}

# /boot: the boot partition, mounted only while it's used (installing,
# committing an update), so a power cut can't catch it mid-write.
# fstab belongs to base-files, so add the line at rootfs time.
pkg_postinst:${PN}() {
    grep -q '^LABEL=boot ' $D${sysconfdir}/fstab || \
        echo "LABEL=boot /boot vfat noauto,x-systemd.automount,x-systemd.idle-timeout=30,noatime,flush,umask=0077,nofail 0 0" >> $D${sysconfdir}/fstab
}

SYSTEMD_SERVICE:${PN} = "ozark-update-boot.service ozark-update-check.timer"

FILES:${PN} += "${systemd_unitdir}/system.conf.d ${systemd_system_unitdir} ${libexecdir}"
CONFFILES:${PN} = "${sysconfdir}/ozark/update.conf"

RDEPENDS:${PN} = " \
    swupdate \
    ozark-data \
    curl \
    ca-certificates \
    e2fsprogs-e2fsck \
    e2fsprogs-resize2fs \
"
