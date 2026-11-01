SUMMARY = "Extra CMake Modules (KDE Frameworks 5)"
DESCRIPTION = "CMake modules that KDE Frameworks need to build. Here only for BluezQt."
HOMEPAGE = "https://invent.kde.org/frameworks/extra-cmake-modules"
LICENSE = "BSD-3-Clause"
LIC_FILES_CHKSUM = "file://LICENSES/BSD-3-Clause.txt;md5=954f4d71a37096249f837652a7f586c0"

SRC_URI = "git://invent.kde.org/frameworks/extra-cmake-modules.git;protocol=https;nobranch=1"
# v5.115.0
SRCREV = "09025604cf16a63c8bd0d8d871c7edc44ca47aab"

S = "${WORKDIR}/git"

inherit cmake allarch

EXTRA_OECMAKE = " \
    -DBUILD_TESTING=OFF \
    -DBUILD_HTML_DOCS=OFF \
    -DBUILD_MAN_DOCS=OFF \
    -DBUILD_QTHELP_DOCS=OFF \
"

# Only CMake files: everything is for building other packages
FILES:${PN}-dev += "${datadir}/ECM"
ALLOW_EMPTY:${PN} = "1"
