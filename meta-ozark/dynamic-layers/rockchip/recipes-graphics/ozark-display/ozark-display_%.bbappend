# Rockchip boards: point Qt at the display controller's DRM card, which
# a udev rule names, as on the Pi (dynamic-layers/raspberrypi). The
# eglfs-kms.json in files/rockchip/ (an override directory) replaces the
# generic one.
FILESEXTRAPATHS:prepend:rockchip := "${THISDIR}/files:"

SRC_URI:append:rockchip = " file://99-ozark-display-rockchip.rules"

do_install:append:rockchip() {
    install -d ${D}${nonarch_base_libdir}/udev/rules.d
    install -m 0644 ${WORKDIR}/99-ozark-display-rockchip.rules ${D}${nonarch_base_libdir}/udev/rules.d/
}

FILES:${PN} += "${nonarch_base_libdir}/udev/rules.d"
