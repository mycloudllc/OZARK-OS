SUMMARY = "Ozark vehicle service: OBD-II, illumination input, tire sensors"
DESCRIPTION = "ozark-vehicled owns the OBD-II adapter (ELM327 on USB or Bluetooth, or \
the CAN bus through SocketCAN), the illumination (headlights) GPIO input and Bluetooth LE \
tire pressure sensors, serves them on the system D-Bus as org.ozark.Vehicle1 for Dash's \
Vehicle page and Settings > Vehicle, and forwards them to a TrailCommand hub. \
Source: vehicle/daemon/ in this repository."
# Ozark's own code: replace with the project's license when it has one
LICENSE = "CLOSED"

# Built straight from vehicle/daemon/ at the repo root
FILESEXTRAPATHS:prepend := "${THISDIR}/../../..:"
SRC_URI = "file://vehicle/daemon"
S = "${WORKDIR}/vehicle/daemon"

# Serial, CAN, RFCOMM and GPIO through the kernel; BLE through BlueZ on D-Bus
DEPENDS = "qtbase"

inherit cmake_qt5 systemd

do_install:append() {
    install -d ${D}${systemd_system_unitdir} ${D}${datadir}/dbus-1/system.d
    install -m 0644 ${S}/ozark-vehicled.service ${D}${systemd_system_unitdir}/
    install -m 0644 ${S}/org.ozark.Vehicle1.conf ${D}${datadir}/dbus-1/system.d/
}

SYSTEMD_SERVICE:${PN} = "ozark-vehicled.service"
SYSTEMD_AUTO_ENABLE = "enable"

FILES:${PN} += "${datadir}/dbus-1/system.d"

# ip: sets a CAN interface's bit rate (busybox's ip can't)
RDEPENDS:${PN} = "iproute2-ip"
