# Orange Pi 3B: mainline U-Boot has the board (orangepi-3b-rk3566_defconfig)
# since 2024.10; Scarthgap's recipe is 2024.01. Same approach as
# meta-rockchip's radxa-zero-3 (a newer tree on Poky's recipe).
FILESEXTRAPATHS:prepend:orangepi-3b := "${THISDIR}/files:"

SRC_URI:orangepi-3b = " \
    git://github.com/u-boot/u-boot.git;protocol=https;nobranch=1 \
    file://0001-rockchip-rk3566-orangepi-3b-enable-npu-regulator.patch \
    file://ozark-boot.cfg \
"
# v2026.01, as Armbian builds this board
SRCREV:orangepi-3b = "127a42c7257a6ffbbd1575ed1cbaa8f5408a44b3"
PV:orangepi-3b = "2026.01"

# 0001 is required by Rockchip's kernel (linux-ozark-rockchip): U-Boot leaves
# vdd_npu off, the kernel doesn't know that regulator feeds the NPU's power
# domain, and it stops before any output when it touches the NPU. Mainline
# doesn't, so it starts without this.

# binman (u-boot-rockchip.bin, idbloader.img, u-boot.itb), mkeficapsule
DEPENDS:append:orangepi-3b = " gnutls-native python3-pyelftools-native"
