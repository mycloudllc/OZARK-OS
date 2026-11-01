SUMMARY = "Wi-Fi hotspot for wireless Android Auto"
DESCRIPTION = "Runs an access point (hostapd, systemd-networkd DHCP) that phones \
join for wireless Android Auto, with a name and key generated per unit on first \
boot, and gives Dash (OpenAuto) the same network to send to the phone."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = " \
    file://ozark-hotspot \
    file://ozark-hotspot-setup.service \
    file://ozark-hotspot.service \
"
S = "${WORKDIR}"

inherit systemd

do_install() {
    install -D -m 0755 ${S}/ozark-hotspot ${D}${sbindir}/ozark-hotspot
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${S}/ozark-hotspot-setup.service ${S}/ozark-hotspot.service ${D}${systemd_system_unitdir}/
}

SYSTEMD_SERVICE:${PN} = "ozark-hotspot-setup.service ozark-hotspot.service"

# hostapd's own service stays off (its recipe default); this one runs it
RDEPENDS:${PN} = "hostapd iw"
