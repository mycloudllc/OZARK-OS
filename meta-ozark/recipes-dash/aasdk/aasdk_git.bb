SUMMARY = "Android Auto SDK (openDsh fork)"
DESCRIPTION = "Android Auto protocol library: USB and TCP transport, SSL, channels and \
the generated protobuf messages (aasdk_proto)."
HOMEPAGE = "https://github.com/openDsh/aasdk"
LICENSE = "GPL-3.0-or-later"
LIC_FILES_CHKSUM = "file://include/aasdk/Version.hpp;beginline=1;endline=17;md5=8baeefcb6804a26f51624002bd73e681"

SRC_URI = " \
    git://github.com/openDsh/aasdk.git;protocol=https;branch=develop \
    file://0001-SSLWrapper-build-against-OpenSSL-3.patch \
    file://0002-Build-with-C-17-and-link-Protobuf-s-Abseil-dependenc.patch \
"
SRCREV = "1bc0fe69d5f5f505c978a0c6e32c860e820fa8f6"
PV = "3.1.0+git"

S = "${WORKDIR}/git"

DEPENDS = "boost libusb1 openssl protobuf protobuf-native"

inherit cmake pkgconfig
