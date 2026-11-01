SUMMARY = "Network settings: Ethernet, LTE and internet sharing"
DESCRIPTION = "ozark-net, the backend of Dash's Settings > Network (hotspot, \
Ethernet, LTE, sharing the internet over the hotspot), and ozark-lte, which \
keeps an LTE modem connected through ModemManager."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = " \
    file://ozark-net \
    file://ozark-lte \
    file://ozark-lte.service \
    file://77-ozark-mm-ignore.rules \
    file://ozark-hotspot-dns.conf \
"
S = "${WORKDIR}"

inherit systemd

do_install() {
    install -d ${D}${sbindir}
    install -m 0755 ${S}/ozark-net ${S}/ozark-lte ${D}${sbindir}/
    install -D -m 0644 ${S}/ozark-lte.service ${D}${systemd_system_unitdir}/ozark-lte.service
    install -D -m 0644 ${S}/77-ozark-mm-ignore.rules ${D}${nonarch_base_libdir}/udev/rules.d/77-ozark-mm-ignore.rules
    install -D -m 0644 ${S}/ozark-hotspot-dns.conf ${D}${sysconfdir}/systemd/resolved.conf.d/ozark-hotspot.conf
}

SYSTEMD_SERVICE:${PN} = "ozark-lte.service"

FILES:${PN} += "${nonarch_base_libdir}/udev/rules.d"

RDEPENDS:${PN} = "ozark-hotspot modemmanager iw"
