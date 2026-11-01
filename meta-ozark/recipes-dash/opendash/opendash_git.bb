SUMMARY = "OpenDash head unit UI with Android Auto and the Ozark plugins"
DESCRIPTION = "openDsh/dash: Qt head unit with Android Auto (OpenAuto), media, camera \
and vehicle pages. Built with the TrailCommand vehicle plugin from \
opendash-trailcommand/ and the Interior/Exterior Lighting launcher plugins from \
lighting/ in this repository, and started fullscreen at boot."
HOMEPAGE = "https://github.com/openDsh/dash"
LICENSE = "GPL-3.0-only"
LIC_FILES_CHKSUM = "file://LICENSE;md5=1ebbd3e34237af26da5dc08a4e440464"

# The TrailCommand plugin is used straight from opendash-trailcommand/ at the
# repo root, so there is one copy of it. Dash builds every folder under
# plugins/<type>/ as a plugin, so it is unpacked into plugins/vehicle/.
FILESEXTRAPATHS:prepend := "${THISDIR}/../../../opendash-trailcommand/plugins/vehicle:"
# Likewise the lighting plugins from lighting/ at the repo root (assembled in do_configure)
FILESEXTRAPATHS:prepend := "${THISDIR}/../../..:"
# Dash's splash reuses the boot splash logo, so the handover from psplash is seamless
FILESEXTRAPATHS:prepend := "${THISDIR}/../../recipes-core/psplash/files:"

SRC_URI = " \
    git://github.com/openDsh/dash.git;protocol=https;branch=develop \
    file://0001-Cross-compile-fixes-for-Yocto.patch \
    file://0002-Power-off-and-reboot-through-systemd-without-sudo.patch \
    file://0003-Optional-full-screen-splash-and-boot-splash-handoff.patch \
    file://0004-Optional-cursor-auto-hide.patch \
    file://0005-Head-unit-controls-reboot-only.patch \
    file://0006-Android-Auto-quick-view-next-turn-and-now-playing.patch \
    file://0007-On-screen-display-for-volume-and-Bluetooth-connectio.patch \
    file://0008-Phone-page-pair-connect-and-forget-phones.patch \
    file://0009-Equalizer-tab-on-the-Media-page.patch \
    file://0010-Set-the-volume-without-blocking-the-UI.patch \
    file://0011-Settings-Lighting-controller-port-override-and-strip.patch \
    file://0012-Layout-scale-only-the-nav-rail-and-control-bar-drop-.patch \
    file://0013-Side-bar-only-scale-Yes-No-restart-Phone-moves-to-Se.patch \
    file://0014-Settings-Network-hotspot-Ethernet-LTE-internet-shari.patch \
    file://0015-Tab-spacing-dark-mode-OSD-theme-colour-wheel.patch \
    file://0016-Restart-from-Settings-only-Material-style-scrollbars.patch \
    file://0017-Theme-colour-pop-up-margins-around-the-wheel-and-sli.patch \
    file://0018-Controls-scale-with-the-screen-DPI-no-pointer-during.patch \
    file://0019-AA-quick-view-draw-the-turn-arrow-not-a-black-square.patch \
    file://0020-Touchscreen-support-for-plugins-keyboard-dialog-fit-.patch \
    file://0021-Radio-AM-FM-switch-presets-settings-at-the-top-right.patch \
    file://0022-About-Ozark-branding-built-with-OpenDash.patch \
    file://0023-Bluetooth-and-Local-players-now-playing-view-library.patch \
    file://trailcommand;subdir=git/plugins/vehicle \
    file://lighting/common \
    file://lighting/interior_lighting \
    file://lighting/exterior_lighting \
    file://opendash.service \
    file://dash.conf \
    file://psplash-ozark-img.png \
"
SRCREV = "2d56f99cff5990e420297ca02e7cf45fe2aebb81"
PV = "0.0+git"

S = "${WORKDIR}/git"

# Unpack lighting/ fresh: files removed from it must not linger from an earlier build
do_unpack[cleandirs] += "${WORKDIR}/lighting"

DEPENDS = " \
    openauto aasdk boost libusb1 openssl protobuf protobuf-native \
    rtaudio taglib bluez-qt \
    qtbase qtmultimedia qtconnectivity qtdeclarative qtserialbus qtserialport \
    qtwebsockets qtsvg \
    qt-gstreamer gstreamer1.0 gstreamer1.0-plugins-base gstreamer1.0-plugins-bad \
"

inherit cmake_qt5 pkgconfig systemd

# No rpath: do_install copies the build tree's binary, which would otherwise
# keep an rpath into the build directory
EXTRA_OECMAKE = "-DGST_BUILD=TRUE -DCMAKE_SKIP_RPATH=ON"

# The app launcher plugin embeds other programs' X11 windows; Ozark OS has no X11
do_configure:prepend() {
    rm -rf ${S}/plugins/launcher/app

    # Interior and Exterior Lighting: two launcher plugins sharing lighting/common
    # (a D-Bus client of ozark-lightd). Dash builds each plugins/launcher/<name>/
    # from the .cpp files inside it, so each gets its own copy.
    for plugin in interior_lighting exterior_lighting; do
        rm -rf ${S}/plugins/launcher/$plugin
        cp -r ${WORKDIR}/lighting/$plugin ${S}/plugins/launcher/
        cp ${WORKDIR}/lighting/common/* ${S}/plugins/launcher/$plugin/
    done
}

# Dash writes its binary to bin/ and plugins to lib/plugins/<type>/ in the
# source tree, and finds plugins at <binary dir>/../lib/plugins. Keep that
# layout under /usr/lib/opendash. The command is "opendash", because /usr/bin/dash
# is the Debian Almquist shell.
do_install() {
    install -d ${D}${libdir}/opendash/bin ${D}${libdir}/opendash/lib ${D}${bindir}
    install -m 0755 ${S}/bin/dash ${D}${libdir}/opendash/bin/dash
    cp -R --no-dereference --preserve=mode,links ${S}/lib/plugins ${D}${libdir}/opendash/lib/

    # Ozark's own plugins must be there: a missing SRC_URI entry still builds, just without them
    for plugin in vehicle/libtrailcommand.so launcher/libinterior_lighting.so launcher/libexterior_lighting.so; do
        [ -f ${D}${libdir}/opendash/lib/plugins/$plugin ] || bbfatal "Dash plugin missing from the build: $plugin"
    done
    ln -sf ../lib/opendash/bin/dash ${D}${bindir}/opendash

    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/opendash.service ${D}${systemd_system_unitdir}/

    install -d ${D}${datadir}/opendash
    install -m 0644 ${WORKDIR}/psplash-ozark-img.png ${D}${datadir}/opendash/splash.png

    # System-wide defaults (TrailCommand plugin); user changes go to ~/.config
    install -d ${D}${sysconfdir}/xdg/openDsh
    install -m 0644 ${WORKDIR}/dash.conf ${D}${sysconfdir}/xdg/openDsh/dash.conf
}

SYSTEMD_SERVICE:${PN} = "opendash.service"
SYSTEMD_AUTO_ENABLE = "enable"

FILES:${PN} += "${libdir}/opendash ${datadir}/opendash"

# Media > Local plays files through QtMultimedia's GStreamer backend: playbin,
# decoders for MP3, FLAC, Ogg Vorbis/Opus and WAV (with their tags), and the
# PulseAudio output. (AAC/M4A would need gstreamer1.0-libav, i.e. FFmpeg.)
LOCAL_PLAYER_GST = " \
    gstreamer1.0-plugins-base-playback \
    gstreamer1.0-plugins-base-typefindfunctions \
    gstreamer1.0-plugins-base-audioconvert \
    gstreamer1.0-plugins-base-audioresample \
    gstreamer1.0-plugins-base-volume \
    gstreamer1.0-plugins-base-ogg \
    gstreamer1.0-plugins-base-vorbis \
    gstreamer1.0-plugins-base-opus \
    gstreamer1.0-plugins-good-pulseaudio \
    gstreamer1.0-plugins-good-autodetect \
    gstreamer1.0-plugins-good-mpg123 \
    gstreamer1.0-plugins-good-flac \
    gstreamer1.0-plugins-good-wavparse \
    gstreamer1.0-plugins-good-audioparsers \
    gstreamer1.0-plugins-good-id3demux \
"

RDEPENDS:${PN} += " \
    ozark-display \
    qtbase-plugins \
    qtdeclarative-qmlplugins \
    qtmultimedia-plugins \
    qtmultimedia-qmlplugins \
    qtsvg-plugins \
    ttf-barlow-condensed \
    ${LOCAL_PLAYER_GST} \
    qtserialbus-plugins \
    qt-gstreamer \
    gstreamer1.0-plugins-base-app \
    gstreamer1.0-plugins-base-videoconvertscale \
    gstreamer1.0-plugins-good-videocrop \
    gstreamer1.0-plugins-good-debug \
    gstreamer1.0-plugins-good-video4linux2 \
    gstreamer1.0-plugins-bad-videoparsersbad \
    bluez5 \
    ozark-lightd \
"
# ozark-lightd: the lighting tabs' controller service
