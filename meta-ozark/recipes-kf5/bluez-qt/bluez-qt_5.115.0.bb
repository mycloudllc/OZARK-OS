SUMMARY = "Qt wrapper for the BlueZ 5 D-Bus API (KDE Frameworks 5)"
DESCRIPTION = "Dash uses BluezQt (KF5::BluezQt) for its Bluetooth page and media controls."
HOMEPAGE = "https://invent.kde.org/frameworks/bluez-qt"
LICENSE = "LGPL-2.1-only | LGPL-3.0-only"
LIC_FILES_CHKSUM = " \
    file://LICENSES/LGPL-2.1-only.txt;md5=fabba2a3bfeb22a6483d44e9ae824d3f \
    file://LICENSES/LGPL-3.0-only.txt;md5=c51d3eef3be114124d11349ca0d7e117 \
"

SRC_URI = "git://invent.kde.org/frameworks/bluez-qt.git;protocol=https;nobranch=1"
# v5.115.0
SRCREV = "26515daa59c0b835bf30d5ae0de19b20a4a78018"

S = "${WORKDIR}/git"

DEPENDS = "extra-cmake-modules qtbase qtdeclarative"

inherit cmake_qt5

EXTRA_OECMAKE = " \
    -DBUILD_TESTING=OFF \
    -DBUILD_QCH=OFF \
    -DKDE_INSTALL_QMLDIR=${OE_QMAKE_PATH_QML} \
    -DKDE_INSTALL_QTPLUGINDIR=${OE_QMAKE_PATH_PLUGINS} \
    -DECM_MKSPECS_INSTALL_DIR=${OE_QMAKE_PATH_ARCHDATA}/mkspecs/modules \
    -DUDEV_RULES_INSTALL_DIR=${nonarch_base_libdir}/udev/rules.d \
"

RDEPENDS:${PN} = "bluez5"

FILES:${PN} += " \
    ${OE_QMAKE_PATH_QML} \
    ${nonarch_base_libdir}/udev/rules.d \
    ${datadir}/qlogging-categories5 \
"
FILES:${PN}-dev += "${OE_QMAKE_PATH_ARCHDATA}/mkspecs"
