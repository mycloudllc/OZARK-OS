SUMMARY = "Ozark OS hello-world Qt app"
DESCRIPTION = "Fullscreen Qt Widgets app that proves the display, GPU, fonts \
and touch input work. Started at boot by systemd after the splash."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = " \
    file://src/ \
    file://hello-ozark.service \
"

S = "${WORKDIR}/src"

DEPENDS = "qtbase"

inherit cmake_qt5 systemd

SYSTEMD_SERVICE:${PN} = "hello-ozark.service"
SYSTEMD_AUTO_ENABLE = "enable"

do_install:append() {
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/hello-ozark.service ${D}${systemd_system_unitdir}/
}

RDEPENDS:${PN} += " \
    qtbase-plugins \
    ozark-display \
    ttf-barlow-condensed \
"
