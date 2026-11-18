SUMMARY = "Linux kernel for Ozark OS on the Orange Pi 3B (Rockchip 6.1)"
DESCRIPTION = "Rockchip's 6.1 kernel as Armbian maintains it (armbian/linux-rockchip \
rk-6.1-rkr7.2, with upstream stable merges), configured like Armbian's \
'vendor' kernel for RK35xx boards, plus the Waveshare DSI panel and polled \
touch drivers and a device tree for the panel on the board's DSI connector. \
Used instead of mainline (linux-ozark-mainline) because mainline's DSI1 on \
the RK3566 gives the panel only a black picture, while this kernel drives \
it on the same hardware."
SECTION = "kernel"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://COPYING;md5=6bc538ed5bd9a7fc9398086aedcd7e46"

COMPATIBLE_MACHINE = "^$"
COMPATIBLE_MACHINE:orangepi-3b = "orangepi-3b"

# armbian/linux-rockchip rk-6.1-rkr7.2: the kernel verified with the panel
# (Armbian's 6.1.172-vendor-rk35xx)
RK_COMMIT = "858550966271b1f59897a106bc3f92842ea0e750"
LINUX_VERSION = "6.1.172"
PV = "${LINUX_VERSION}+rk"
KERNEL_VERSION_SANITY_SKIP = "1"

SRC_URI = " \
    https://github.com/armbian/linux-rockchip/archive/${RK_COMMIT}.tar.gz;downloadfilename=linux-rockchip-${RK_COMMIT}.tar.gz \
    file://0001-Ozark-Orange-Pi-3B-v2.1-with-a-Waveshare-8-DSI-panel.patch \
    file://0002-Ozark-decode-video-through-V4L2-hantro-vpu.patch \
    file://defconfig \
    file://ozark-rockchip-vendor.cfg \
"
SRC_URI[sha256sum] = "49aa8ca9c6f17ce1d71acdfe5878bfd18a3f2617bdb3d6efb3841b8e635bc5cc"

# defconfig: Armbian's config/kernel/linux-rk35xx-vendor.config (armbian/build
# de7e73b30e0e), a savedefconfig; ozark-rockchip-vendor.cfg is applied on top.
# rk3566.inc sets KBUILD_DEFCONFIG ?= "defconfig" (the tree's own arm64
# defconfig), which would make kernel-yocto drop this one: clear it.
KBUILD_DEFCONFIG = ""
KCONFIG_MODE = "alldefconfig"

# rk3566.inc's kernel feature (meta-rockchip's kmeta: leave out other SoCs'
# drivers) is for linux-yocto style trees; Armbian's config already does it
KERNEL_FEATURES:remove = "bsp/rockchip/remove-non-rockchip-arch-arm64.scc"

inherit kernel
inherit kernel-yocto
require recipes-kernel/linux/linux-yocto.inc

# The archive's tree (kernel.bbclass sets S to STAGING_KERNEL_DIR, so this
# comes after it; do_symlink_kernsrc moves the tree there)
S = "${WORKDIR}/linux-rockchip-${RK_COMMIT}"

ERROR_QA:remove = "buildpaths"

# One Bluetooth adapter: the board's own (UART). USB dongles are ignored,
# as on the Pi (linux-raspberrypi_%.bbappend).
KERNEL_MODULE_PROBECONF += "btusb"
module_conf_btusb = "blacklist btusb"

# Wi-Fi is the RTL8822CE card in the M.2 slot: the onboard BCM43456's Wi-Fi
# (brcmfmac, SDIO) stays off. Its Bluetooth is on the UART and unaffected.
# To use it again on one board: create an empty /etc/modprobe.d/brcmfmac.conf
# (overrides /usr/lib/modprobe.d/brcmfmac.conf) and reboot.
KERNEL_MODULE_PROBECONF += "brcmfmac rtw88_pci"
module_conf_brcmfmac = "blacklist brcmfmac"
# PCIe power saving (ASPM) is a common cause of rtw88 cards stalling or
# dropping off the bus on ARM boards; the hotspot carries AA video
module_conf_rtw88_pci = "options rtw88_pci disable_aspm=1"
