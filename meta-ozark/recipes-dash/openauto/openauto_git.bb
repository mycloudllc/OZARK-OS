SUMMARY = "OpenAuto Android Auto head unit library (openDsh fork)"
DESCRIPTION = "Android Auto projection, audio and input services that Dash embeds. \
Built with the GStreamer video path (QtGStreamer); autoapp, the standalone \
OpenAuto app, goes in its own package."
HOMEPAGE = "https://github.com/openDsh/openauto"
LICENSE = "GPL-3.0-or-later"
LIC_FILES_CHKSUM = "file://include/openauto/App.hpp;beginline=1;endline=17;md5=3ce8c062a9aa9ec1b1533425588dad32"

SRC_URI = " \
    git://github.com/openDsh/openauto.git;protocol=https;branch=develop \
    file://0001-Cross-compile-fixes-for-Yocto.patch \
    file://0002-Wireless-AA-advertise-the-access-point-s-address-con.patch \
    file://0003-Ozark-defaults-1080p-300-DPI-reconnect-the-last-phon.patch \
    file://0004-Crop-the-projection-on-the-GPU-not-with-videocrop.patch \
    file://0005-Ozark-default-right-hand-drive.patch \
    file://0006-Microphone-open-only-a-microphone-that-exists.patch \
    file://0007-Wireless-AA-phones-no-longer-get-stuck-on-Connecting.patch \
"
SRCREV = "e7caeb4d49186af867c1d4693c9f3bf9e9ef99e0"
PV = "2.1.0+git"

S = "${WORKDIR}/git"

DEPENDS = " \
    aasdk boost libusb1 openssl protobuf protobuf-native \
    rtaudio h264bitstream \
    qtbase qtmultimedia qtconnectivity qtdeclarative \
    qt-gstreamer gstreamer1.0 gstreamer1.0-plugins-base \
"

inherit cmake_qt5 pkgconfig

# RPI_BUILD is left off: it reads /proc/cpuinfo of the build machine, and only
# selects the old OMX video path. The GStreamer path picks the Pi's V4L2
# hardware decoder at runtime.
EXTRA_OECMAKE = "-DGST_BUILD=TRUE"

PACKAGES =+ "${PN}-autoapp"
FILES:${PN}-autoapp = "${bindir}/autoapp"

# libbtservice_proto has no soname version; it's a runtime library, not a dev symlink
FILES_SOLIBSDEV = "${libdir}/libopenauto.so"
FILES:${PN} += "${libdir}/libbtservice_proto.so"
