SUMMARY = "Display settings shared by Ozark OS Qt apps"
DESCRIPTION = "Environment for running Qt fullscreen on KMS/DRM (EGLFS) and the \
EGLFS KMS config. BSP-specific tweaks (e.g. which DRM card drives the screen) \
come from dynamic-layers/<bsp>/ bbappends."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = " \
    file://ozark-display.env \
    file://eglfs-kms.json \
"

S = "${WORKDIR}"

# The EGLFS KMS config differs per machine: files/qemuall/ for QEMU (its display
# plane only takes XRGB8888) and the Pi's in dynamic-layers/raspberrypi/.
PACKAGE_ARCH = "${MACHINE_ARCH}"

do_install() {
    install -d ${D}${sysconfdir}/default ${D}${sysconfdir}/ozark
    install -m 0644 ${S}/ozark-display.env ${D}${sysconfdir}/default/ozark-display
    install -m 0644 ${S}/eglfs-kms.json ${D}${sysconfdir}/ozark/eglfs-kms.json
}

CONFFILES:${PN} = " \
    ${sysconfdir}/default/ozark-display \
    ${sysconfdir}/ozark/eglfs-kms.json \
"
