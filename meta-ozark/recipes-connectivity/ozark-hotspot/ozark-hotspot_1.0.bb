SUMMARY = "Wi-Fi hotspot for wireless Android Auto"
DESCRIPTION = "Runs an access point (hostapd, systemd-networkd DHCP) that phones \
join for wireless Android Auto, with a name and key generated per unit on first \
boot, and gives Dash (OpenAuto) the same network to send to the phone. A USB \
Wi-Fi adapter, when plugged in, serves the other band on the same network."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = " \
    file://ozark-hotspot \
    file://ozark-hotspot-setup.service \
    file://ozark-hotspot.service \
    file://ozark-hotspot-radio@.service \
    file://90-ozark-hotspot-radio.rules \
"
S = "${WORKDIR}"

inherit systemd

do_install() {
    install -D -m 0755 ${S}/ozark-hotspot ${D}${sbindir}/ozark-hotspot
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${S}/ozark-hotspot-setup.service ${S}/ozark-hotspot.service \
        ${S}/ozark-hotspot-radio@.service ${D}${systemd_system_unitdir}/
    install -D -m 0644 ${S}/90-ozark-hotspot-radio.rules ${D}${nonarch_base_libdir}/udev/rules.d/90-ozark-hotspot-radio.rules
}

SYSTEMD_SERVICE:${PN} = "ozark-hotspot-setup.service ozark-hotspot.service"

FILES:${PN} += "${systemd_system_unitdir}/ozark-hotspot-radio@.service ${nonarch_base_libdir}/udev/rules.d"

# hostapd's own service stays off (its recipe default); this one runs it
# Drivers for USB adapters are in kernel-modules (ozark-image-base); their
# firmware is in ozark-wifi-firmware
RDEPENDS:${PN} = "hostapd iw ozark-wifi-firmware"
