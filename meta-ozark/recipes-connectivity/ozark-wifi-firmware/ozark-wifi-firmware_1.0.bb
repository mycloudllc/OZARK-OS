SUMMARY = "Firmware for USB Wi-Fi adapters that can be the hotspot's second radio"
DESCRIPTION = "Pulls in the firmware for USB Wi-Fi adapters known to work as an \
access point with the kernel's drivers: MediaTek MT7610U, MT7612U and MT7921AU \
(mt76), and Ralink RT5370/RT5572/RT3070 (rt2800usb). The drivers themselves \
are in kernel-modules."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

inherit allarch

ALLOW_EMPTY:${PN} = "1"

RDEPENDS:${PN} = " \
    linux-firmware-mt76x0u \
    linux-firmware-mt76x2u \
    linux-firmware-mt7921u \
    linux-firmware-ralink \
"
