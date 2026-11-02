# Small packages for the USB Wi-Fi adapters ozark-hotspot can use as a second
# radio. Their firmware is otherwise only in linux-firmware-mediatek, which
# carries every MediaTek blob (tens of MB). Listed first, so they claim the
# files before linux-firmware-mediatek does.
PACKAGES =+ "${PN}-mt76x0u ${PN}-mt76x2u ${PN}-mt7921u"

# MT7610U (MT76x0U): e.g. TP-Link Archer T2U, Netgear A6100
LICENSE:${PN}-mt76x0u = "Firmware-ralink_a_mediatek_company_firmware"
FILES:${PN}-mt76x0u = "${nonarch_base_libdir}/firmware/mediatek/mt7610u.bin"
RDEPENDS:${PN}-mt76x0u += "${PN}-mt76x-license"

# MT7612U (MT76x2U): e.g. Alfa AWUS036ACM, Netgear A6210
LICENSE:${PN}-mt76x2u = "Firmware-ralink_a_mediatek_company_firmware"
FILES:${PN}-mt76x2u = " \
    ${nonarch_base_libdir}/firmware/mediatek/mt7662u.bin \
    ${nonarch_base_libdir}/firmware/mediatek/mt7662u_rom_patch.bin \
"
RDEPENDS:${PN}-mt76x2u += "${PN}-mt76x-license"

# MT7921AU: e.g. Alfa AWUS036AXML, Netgear A8000
LICENSE:${PN}-mt7921u = "Firmware-mediatek"
FILES:${PN}-mt7921u = " \
    ${nonarch_base_libdir}/firmware/mediatek/WIFI_MT7961_patch_mcu_1_2_hdr.bin \
    ${nonarch_base_libdir}/firmware/mediatek/WIFI_RAM_CODE_MT7961_1.bin \
"
RDEPENDS:${PN}-mt7921u += "${PN}-mediatek-license"
