SUMMARY = "Ozark OS sound system"
DESCRIPTION = "System-wide PulseAudio with Bluetooth phone audio: music (A2DP sink) and \
hands-free calls through oFono (HFP). ALSA's default device goes through PulseAudio, \
so Dash's volume control (amixer Master) sets the real output volume. A 10-band \
equalizer (ozark-eq10) sits in front of the output."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = " \
    file://ozark-pulseaudio.service \
    file://ozark.pa \
    file://client.conf \
    file://ozark-audio.conf \
"

S = "${WORKDIR}"

inherit systemd allarch

do_install() {
    install -d ${D}${systemd_system_unitdir} ${D}${sysconfdir}/pulse/client.conf.d ${D}${datadir}/dbus-1/system.d
    install -m 0644 ${S}/ozark-pulseaudio.service ${D}${systemd_system_unitdir}/
    install -m 0644 ${S}/ozark.pa ${D}${sysconfdir}/pulse/ozark.pa
    install -m 0644 ${S}/client.conf ${D}${sysconfdir}/pulse/client.conf.d/ozark.conf
    install -m 0644 ${S}/ozark-audio.conf ${D}${datadir}/dbus-1/system.d/
}

SYSTEMD_SERVICE:${PN} = "ozark-pulseaudio.service"
SYSTEMD_AUTO_ENABLE = "enable"

FILES:${PN} += "${datadir}/dbus-1/system.d"
CONFFILES:${PN} = "${sysconfdir}/pulse/ozark.pa ${sysconfdir}/pulse/client.conf.d/ozark.conf"

RDEPENDS:${PN} = " \
    pulseaudio-server \
    pulseaudio-misc \
    pulseaudio-module-device-restore \
    pulseaudio-module-stream-restore \
    pulseaudio-module-card-restore \
    pulseaudio-module-udev-detect \
    pulseaudio-module-alsa-card \
    pulseaudio-module-bluetooth-policy \
    pulseaudio-module-bluetooth-discover \
    pulseaudio-module-bluez5-discover \
    pulseaudio-module-bluez5-device \
    pulseaudio-module-native-protocol-unix \
    pulseaudio-module-switch-on-connect \
    pulseaudio-module-default-device-restore \
    pulseaudio-module-always-sink \
    pulseaudio-module-null-sink \
    pulseaudio-module-suspend-on-idle \
    pulseaudio-module-loopback \
    pulseaudio-module-ladspa-sink \
    pulseaudio-module-dbus-protocol \
    ozark-eq10 \
    alsa-plugins-pulseaudio-conf \
    alsa-utils-amixer \
    bluez5 \
    ofono \
"
