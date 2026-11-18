SUMMARY = "Wi-Fi and Bluetooth firmware for the BCM43456 (Orange Pi 3B v2.1)"
DESCRIPTION = "brcmfmac43456-sdio firmware, its regulatory data and board \
settings, and the BCM4345C5 Bluetooth patch. linux-firmware doesn't have this \
chip. The Wi-Fi firmware is the Raspberry Pi 400 / CM4 build (same chip), \
7.84.17.1 from 2020; the rest is Armbian's, pinned."
HOMEPAGE = "https://github.com/RPi-Distro/firmware-nonfree"
# Broadcom/Cypress redistributable firmware, no license text in the source
LICENSE = "CLOSED"

ARMBIAN_FW = "https://raw.githubusercontent.com/armbian/firmware/${ARMBIAN_FW_REV}"
ARMBIAN_FW_REV = "bf5f7a802b8e1998e6bf95daed5ea965441af83d"

# Wi-Fi firmware: Armbian's 7.45.96.0 (2017) stalled the wireless Android
# Auto stream for several seconds about 10 minutes into a session (AA then
# drops on its ping timeout). The Pi 400's 7.84.17.1 runs hotspots on Pi OS.
# Armbian's is still installed, as brcmfmac43456-sdio.bin.armbian-2017, to go
# back to by hand: cp it over brcmfmac43456-sdio.bin and reboot.
RPI_FW = "https://raw.githubusercontent.com/RPi-Distro/firmware-nonfree/${RPI_FW_REV}/debian/config/brcm80211/brcm"
RPI_FW_REV = "c91cd2804cf7463aab913e7247c176049f16bbd6"

SRC_URI = " \
    ${RPI_FW}/brcmfmac43456-sdio.bin;downloadfilename=rpi-${RPI_FW_REV}-brcmfmac43456-sdio.bin;name=wifi \
    ${ARMBIAN_FW}/brcm/brcmfmac43456-sdio.bin;downloadfilename=armbian-${ARMBIAN_FW_REV}-brcmfmac43456-sdio.bin;name=wifiold \
    ${ARMBIAN_FW}/brcm/brcmfmac43456-sdio.clm_blob;downloadfilename=armbian-${ARMBIAN_FW_REV}-brcmfmac43456-sdio.clm_blob;name=clm \
    ${ARMBIAN_FW}/brcm/brcmfmac43456-sdio.txt;downloadfilename=armbian-${ARMBIAN_FW_REV}-brcmfmac43456-sdio.txt;name=nvram \
    ${ARMBIAN_FW}/brcm/BCM4345C5.hcd;downloadfilename=armbian-${ARMBIAN_FW_REV}-BCM4345C5.hcd;name=bt \
"
SRC_URI[wifi.sha256sum] = "ddf83f2100885b166be52d21c8966db164fdd4e1d816aca2acc67ee9cc28d726"
SRC_URI[wifiold.sha256sum] = "3167956a7b2cffc4cfcaf6a282b95728c529eebef18a5d9e6d9ff32de32cc67c"
SRC_URI[clm.sha256sum] = "2dbd7d22fc9af0eb560ceab45b19646d211bc7b34a1dd00c6bfac5dd6ba25e8a"
SRC_URI[nvram.sha256sum] = "66c71eb53b47c49d42386b66735134578640b0946f3f46e44f384fef5aacfd9e"
SRC_URI[bt.sha256sum] = "f67164f0eda8d4ca96305e177a61542bf8b470f2f1c456b66fe8c660650f1c7a"

S = "${WORKDIR}"

inherit allarch

do_install() {
    d=${D}${nonarch_base_libdir}/firmware/brcm
    install -d $d
    install -m 0644 ${WORKDIR}/rpi-${RPI_FW_REV}-brcmfmac43456-sdio.bin $d/brcmfmac43456-sdio.bin
    install -m 0644 ${WORKDIR}/armbian-${ARMBIAN_FW_REV}-brcmfmac43456-sdio.bin $d/brcmfmac43456-sdio.bin.armbian-2017
    # Board settings (nvram) stay Armbian's: they describe this board's
    # AP6256 module, not the Pi 400's. The clm_blob is identical in both.
    for f in brcmfmac43456-sdio.clm_blob brcmfmac43456-sdio.txt BCM4345C5.hcd; do
        install -m 0644 ${WORKDIR}/armbian-${ARMBIAN_FW_REV}-$f $d/$f
    done
}

FILES:${PN} = "${nonarch_base_libdir}/firmware/brcm"
