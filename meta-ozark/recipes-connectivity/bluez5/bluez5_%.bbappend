FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI += " \
    file://ozark.conf \
    file://main.conf \
"

do_install:append() {
    install -d ${D}${systemd_system_unitdir}/bluetooth.service.d
    install -m 0644 ${WORKDIR}/ozark.conf ${D}${systemd_system_unitdir}/bluetooth.service.d/
    install -d ${D}${sysconfdir}/bluetooth
    install -m 0644 ${WORKDIR}/main.conf ${D}${sysconfdir}/bluetooth/main.conf
}

FILES:${PN} += "${systemd_system_unitdir}/bluetooth.service.d"
