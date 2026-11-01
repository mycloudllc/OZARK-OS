# Raspberry Pi 4: the GPU shows up as two DRM cards (v3d = render only,
# vc4 = display), and their numbering isn't stable between boots/kernels.
# A udev rule gives the display card a fixed name and Qt is pointed at it.
#
# Everything here is limited to Pi machines (:rpi). meta-raspberrypi being in
# the layer list is not enough: a qemuarm64 build (kas/qemu.yml) also has it.

FILESEXTRAPATHS:prepend:rpi := "${THISDIR}/files:"

SRC_URI:append:rpi = " file://99-ozark-display.rules"

do_install:append:rpi() {
    install -d ${D}${nonarch_base_libdir}/udev/rules.d
    install -m 0644 ${WORKDIR}/99-ozark-display.rules ${D}${nonarch_base_libdir}/udev/rules.d/
}

FILES:${PN} += "${nonarch_base_libdir}/udev/rules.d"

# The Pi-specific eglfs-kms.json in files/ (which names the device) takes
# precedence over the generic one through FILESEXTRAPATHS.
