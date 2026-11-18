# Orange Pi 3B: DDR init v1.21 with BL31 v1.44, the pair Armbian builds this
# board with and the one tested here. meta-rockchip's v1.23 wasn't shown to be
# at fault (see u-boot_%.bbappend's 0001), just not tested with the fix.
SRCREV:orangepi-3b = "a8008bfe3c9296f6e44d5aa221e865e450ed921d"
DDRBIN_VERS:rk3566:orangepi-3b = "v1.21"
# Same terms as meta-rockchip's revision: its LICENSE only fixes a typo
LIC_FILES_CHKSUM:orangepi-3b = "file://LICENSE;md5=15faa4a01e7eb0f5d33f9f2bcc7bff62"
