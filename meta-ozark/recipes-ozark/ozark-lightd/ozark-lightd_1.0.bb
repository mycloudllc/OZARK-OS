SUMMARY = "Ozark lighting controller service"
DESCRIPTION = "ozark-lightd owns the serial link to the ESP32 lighting controller \
(interior front, interior rear and exterior LED outputs) and serves it on the system \
D-Bus as org.ozark.Lighting1. Dash's Interior and Exterior Lighting tabs use it. \
Source: lighting/daemon/ in this repository; firmware in lighting/firmware/."
# Ozark's own code: replace with the project's license when it has one
LICENSE = "CLOSED"

# Built straight from lighting/daemon/ at the repo root
FILESEXTRAPATHS:prepend := "${THISDIR}/../../..:"
SRC_URI = "file://lighting/daemon"
S = "${WORKDIR}/lighting/daemon"

DEPENDS = "qtbase qtserialport"

inherit cmake_qt5 systemd

do_install:append() {
    install -d ${D}${systemd_system_unitdir} ${D}${datadir}/dbus-1/system.d
    install -m 0644 ${S}/ozark-lightd.service ${D}${systemd_system_unitdir}/
    install -m 0644 ${S}/org.ozark.Lighting1.conf ${D}${datadir}/dbus-1/system.d/
}

SYSTEMD_SERVICE:${PN} = "ozark-lightd.service"
SYSTEMD_AUTO_ENABLE = "enable"

FILES:${PN} += "${datadir}/dbus-1/system.d"

# parec: what's playing, for the music patterns
RDEPENDS:${PN} = "pulseaudio-misc"
