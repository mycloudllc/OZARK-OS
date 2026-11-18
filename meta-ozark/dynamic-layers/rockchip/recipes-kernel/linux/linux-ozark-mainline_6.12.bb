SUMMARY = "Linux kernel for Ozark OS on Rockchip boards (mainline 6.12 LTS)"
DESCRIPTION = "Stable Linux 6.12 with the arm64 defconfig, plus what Ozark OS \
needs on the Orange Pi 3B: the Waveshare DSI panel driver from the Raspberry Pi \
kernel, polled Goodix touch, and a device tree for the panel on the board's \
Pi-style DSI connector. Mainline has the board (rk3566-orangepi-3b-v2.1) since \
6.10; Scarthgap's linux-yocto is 6.6."
SECTION = "kernel"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://COPYING;md5=6bc538ed5bd9a7fc9398086aedcd7e46"

COMPATIBLE_MACHINE = "^$"
COMPATIBLE_MACHINE:orangepi-3b = "orangepi-3b"

LINUX_VERSION = "6.12.115"
PV = "${LINUX_VERSION}"
KERNEL_VERSION_SANITY_SKIP = "1"

SRC_URI = " \
    https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-${PV}.tar.xz \
    file://0001-drm-panel-add-the-Waveshare-DSI-touchscreen-panel-dr.patch \
    file://0002-Input-goodix-poll-when-there-is-no-interrupt-line.patch \
    file://0003-arm64-dts-rockchip-Orange-Pi-3B-v2.1-with-a-Waveshar.patch \
    file://0004-drm-dsi-command-line-overrides-for-the-DSI-mode-and-.patch \
    file://0005-drm-panel-waveshare-dsi-set-the-sync-polarities.patch \
    file://rockchip-kmeta;type=kmeta;name=rockchip-kmeta;destsuffix=rockchip-kmeta \
    file://ozark-rockchip.cfg \
"
SRC_URI[sha256sum] = "0ec90fe4b20422b1b664b77e6d2dde22b40bd41e7eb2cad8ac8739a4749cb1bb"

# rockchip-kmeta (bsp/rockchip/remove-non-rockchip-arch-arm64.scc, from
# rk3566.inc's KERNEL_FEATURES: leaves out other SoCs' drivers) is
# meta-rockchip's, next to its own mainline kernel recipe
FILESEXTRAPATHS:append := ":${@[l for l in d.getVar('BBLAYERS').split() if l.rstrip('/').endswith('/meta-rockchip')][0]}/recipes-kernel/linux/linux-yocto-dev"

KBUILD_DEFCONFIG = "defconfig"
KCONFIG_MODE = "alldefconfig"

inherit kernel
inherit kernel-yocto
require recipes-kernel/linux/linux-yocto.inc

# The release tarball's tree (kernel.bbclass sets S to STAGING_KERNEL_DIR,
# so this comes after it; do_symlink_kernsrc moves the tree there)
S = "${WORKDIR}/linux-${PV}"

ERROR_QA:remove = "buildpaths"

# One Bluetooth adapter: the board's own (UART). USB dongles are ignored,
# as on the Pi (linux-raspberrypi_%.bbappend).
KERNEL_MODULE_PROBECONF += "btusb"
module_conf_btusb = "blacklist btusb"
