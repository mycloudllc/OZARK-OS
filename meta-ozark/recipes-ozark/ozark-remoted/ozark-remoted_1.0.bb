SUMMARY = "Ozark remote control: the phone app over Wi-Fi and Bluetooth LE"
DESCRIPTION = "ozark-remoted lets the Ozark Remote phone app control the radio: \
media, radio, volume and theme (through Dash's org.ozark.Dash1), the lights \
(org.ozark.Lighting1), vehicle data (org.ozark.Vehicle1), the network and updates. \
Phones connect over the radio's Wi-Fi (WebSocket, port 54546) or Bluetooth LE (a GATT \
service) and pair once with a code shown on the radio's screen. \
Source: remote/daemon/ in this repository; protocol: remote/PROTOCOL.md."
# Ozark's own code: replace with the project's license when it has one
LICENSE = "CLOSED"

# Built straight from remote/daemon/ at the repo root
FILESEXTRAPATHS:prepend := "${THISDIR}/../../..:"
SRC_URI = "file://remote/daemon"
S = "${WORKDIR}/remote/daemon"

DEPENDS = "qtbase qtwebsockets"

inherit cmake_qt5 systemd

do_install:append() {
    install -d ${D}${systemd_system_unitdir} ${D}${datadir}/dbus-1/system.d
    install -m 0644 ${S}/ozark-remoted.service ${D}${systemd_system_unitdir}/
    install -m 0644 ${S}/org.ozark.Remote1.conf ${D}${datadir}/dbus-1/system.d/
}

SYSTEMD_SERVICE:${PN} = "ozark-remoted.service"
SYSTEMD_AUTO_ENABLE = "enable"

FILES:${PN} += "${datadir}/dbus-1/system.d"

# The hotspot (Wi-Fi), BlueZ (Bluetooth LE), ozark-net and ozark-update (system.*)
RDEPENDS:${PN} = "bluez5 ozark-network ozark-update"
