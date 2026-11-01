SUMMARY = "RtAudio realtime audio I/O library"
DESCRIPTION = "C++ audio I/O used by OpenAuto for Android Auto sound. Version 5.x: \
OpenAuto uses the RtAudioError exceptions that 6.0 removed."
HOMEPAGE = "https://www.music.mcgill.ca/~gary/rtaudio/"
# MIT terms plus a non-binding request to send modifications upstream
LICENSE = "RtAudio"
NO_GENERIC_LICENSE[RtAudio] = "LICENSE"
LIC_FILES_CHKSUM = "file://LICENSE;md5=34bd14de91674a563fe79386431546bb"

SRC_URI = "git://github.com/thestk/rtaudio.git;protocol=https;nobranch=1"
SRCREV = "46b01b5b134f33d8ddc3dab76829d4b1350e0522"

S = "${WORKDIR}/git"

inherit cmake pkgconfig

PACKAGECONFIG ??= "alsa ${@bb.utils.filter('DISTRO_FEATURES', 'pulseaudio', d)}"
PACKAGECONFIG[alsa] = "-DRTAUDIO_API_ALSA=ON,-DRTAUDIO_API_ALSA=OFF,alsa-lib"
PACKAGECONFIG[pulseaudio] = "-DRTAUDIO_API_PULSE=ON,-DRTAUDIO_API_PULSE=OFF,pulseaudio"

EXTRA_OECMAKE = "-DRTAUDIO_API_JACK=OFF -DRTAUDIO_BUILD_TESTING=OFF -DBUILD_TESTING=OFF"

FILES:${PN}-dev += "${datadir}/rtaudio"
