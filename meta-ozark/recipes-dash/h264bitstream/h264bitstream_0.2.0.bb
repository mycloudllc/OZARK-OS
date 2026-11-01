SUMMARY = "Read and write H.264 video bitstreams"
DESCRIPTION = "Used by OpenAuto to inspect the Android Auto video stream headers."
HOMEPAGE = "https://github.com/aizvorski/h264bitstream"
LICENSE = "LGPL-2.1-or-later"
LIC_FILES_CHKSUM = "file://LICENSE;md5=a6f89e2100d9b6cdffcea4f398e37343"

# The commit openDsh's install script builds (master, after the 0.2.0 tag)
SRC_URI = "git://github.com/aizvorski/h264bitstream.git;protocol=https;branch=master"
SRCREV = "70124d3051ba45e6b326264f0b25e6f48a7479e7"
PV = "0.2.0+git"

S = "${WORKDIR}/git"

# Autotools rather than its CMake build: gives a versioned soname and the
# include/h264bitstream/ header layout OpenAuto expects.
inherit autotools pkgconfig

# Its pkg-config macro copies the build's LDFLAGS (full of build paths) into Libs:
do_install:append() {
    sed -i 's|^Libs:.*|Libs: -L${libdir} -lh264bitstream|' ${D}${libdir}/pkgconfig/libh264bitstream.pc
}

PACKAGES =+ "${PN}-tools"
FILES:${PN}-tools = "${bindir}"
