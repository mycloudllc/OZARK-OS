SUMMARY = "Qt bindings for GStreamer"
DESCRIPTION = "QtGStreamer libraries, the qt5videosink GStreamer element and the \
QtGStreamer QML import. OpenAuto draws Android Auto video with it and Dash's \
camera page uses it."
HOMEPAGE = "https://gstreamer.freedesktop.org/modules/qt-gstreamer.html"
LICENSE = "LGPL-2.1-or-later"
LIC_FILES_CHKSUM = "file://COPYING;md5=2d5025d4aa3495befef8f17206a5b0a1"

# Upstream is unmaintained; master has fixes after 1.2.0. Patches come from openDsh/dash.
SRC_URI = " \
    git://github.com/GStreamer/qt-gstreamer.git;protocol=https;branch=master \
    file://qt-gstreamer-1.18.patch \
    file://qt-gstreamer_atomic-load.patch \
"
SRCREV = "6e4fb2f3fcfb453c5522c66457ac5ed8c3b1b05c"
PV = "1.2.0+git"

S = "${WORKDIR}/git"

DEPENDS = " \
    qtbase qtdeclarative boost glib-2.0 \
    gstreamer1.0 gstreamer1.0-plugins-base \
    virtual/egl virtual/libgles2 \
"

inherit cmake_qt5 pkgconfig

# Codegen stays off (the default): its generated sources ship in the tree, and
# it would have to run on the build host.
# USE_OPENGLES: its own probe for a GLES Qt compiles without -fPIC, which Qt 5.15
# rejects, so it always fails and qtquick2videosink (what OpenAuto draws Android
# Auto video into) is left out. meta-qt5 builds Qt for GLES2 here.
EXTRA_OECMAKE = " \
    -DQT_VERSION=5 \
    -DUSE_OPENGLES=ON \
    -DCMAKE_CXX_STANDARD=11 \
    -DQTGSTREAMER_EXAMPLES=OFF \
    -DQTGSTREAMER_TESTS=OFF \
    -DUSE_GST_PLUGIN_DIR=OFF \
    -DUSE_QT_PLUGIN_DIR=OFF \
"

# With USE_QT_PLUGIN_DIR off the QML import lands in lib/qt5/qml; meta-qt5 uses lib/qml
do_install:append() {
    if [ -d ${D}${libdir}/qt5/qml ]; then
        install -d ${D}${OE_QMAKE_PATH_QML}
        mv ${D}${libdir}/qt5/qml/* ${D}${OE_QMAKE_PATH_QML}/
        rm -rf ${D}${libdir}/qt5
    fi
}

FILES:${PN} += " \
    ${libdir}/gstreamer-1.0/*.so \
    ${OE_QMAKE_PATH_QML} \
"
FILES:${PN}-dev += "${libdir}/cmake"
