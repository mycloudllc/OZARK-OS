SUMMARY = "Ozark 10-band equalizer (LADSPA plugin)"
DESCRIPTION = "Stereo graphic equalizer on the octave bands 31 Hz to 16 kHz, run by \
PulseAudio's module-ladspa-sink in front of the speakers (see ozark-audio). Dash's \
Media > Equalizer tab sets the bands live over PulseAudio's D-Bus interface."
# The plugin is MIT; ladspa.h (the LADSPA API header, LGPL) is only used to build it
LICENSE = "MIT & LGPL-2.1-or-later"
LICENSE:${PN} = "MIT"
LIC_FILES_CHKSUM = " \
    file://ozark_eq10.c;beginline=10;endline=10;md5=b400e912c9234ab93082e3f349a84aff \
    file://ladspa.h;beginline=1;endline=19;md5=9f0fcdb4ac9f51f84636ded580eaae66 \
"

SRC_URI = " \
    file://ozark_eq10.c \
    file://ladspa.h \
"

S = "${WORKDIR}"

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} -shared -fPIC -I${S} -o ozark_eq10.so ${S}/ozark_eq10.c -lm
}

do_install() {
    install -D -m 0755 ozark_eq10.so ${D}${libdir}/ladspa/ozark_eq10.so
}

FILES:${PN} = "${libdir}/ladspa"
