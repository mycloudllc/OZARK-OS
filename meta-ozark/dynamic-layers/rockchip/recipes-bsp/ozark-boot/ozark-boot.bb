SUMMARY = "Ozark OS boot script and slot files for U-Boot boards"
DESCRIPTION = "boot.scr, which starts system slot A or B and does one-time \
trial starts of updates (like the Raspberry Pi's tryboot), with the files it \
reads: slot.txt and each slot's cmdline.txt. Deployed to ozark-boot/ for the \
SD card image's boot partition (classes/ozark-slots.bbclass) and ozark-swu."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

COMPATIBLE_MACHINE = "^$"
COMPATIBLE_MACHINE:orangepi-3b = "orangepi-3b"

SRC_URI = "file://boot.cmd"
S = "${WORKDIR}"

DEPENDS = "u-boot-tools-native"

inherit deploy nopackages

# Extra kernel arguments for the panel. None with Rockchip's kernel
# (linux-ozark-rockchip): its panel driver uses the Raspberry Pi's DSI
# settings, which work there. With mainline (linux-ozark-mainline), patches
# 0004/0005 take e.g. "panel_waveshare_dsi.mode_flags=0x803
# panel_waveshare_dsi.sync=2 rockchipdrm.dsi_lane_mbps=1000", though no
# combination gave a picture. Updates write cmdline.txt afresh, so changes
# for the hardware go here.
OZARK_PANEL_ARGS ?= ""

# Slot A's root; ozark-install.sh points an updated slot's cmdline.txt at its own
OZARK_CMDLINE ?= "root=${OZARK_SLOT_DISK}p2 rootfstype=ext4 rootwait rw \
console=${RK_CONSOLE_DEVICE},${RK_CONSOLE_BAUD}n8 ${OZARK_PANEL_ARGS} \
quiet loglevel=3 logo.nologo vt.global_cursor_default=0 consoleblank=0 panic=10"

do_compile() {
    mkimage -A arm64 -O linux -T script -C none -n "Ozark OS" -d ${S}/boot.cmd ${B}/boot.scr
    echo "bootargs=${OZARK_CMDLINE}" > ${B}/cmdline.txt
    echo "slot=a" > ${B}/slot.txt
}

do_deploy() {
    install -d ${DEPLOYDIR}/ozark-boot
    install -m 0644 ${B}/boot.scr ${B}/cmdline.txt ${B}/slot.txt ${DEPLOYDIR}/ozark-boot/
}
addtask deploy after do_compile before do_build

PACKAGE_ARCH = "${MACHINE_ARCH}"
