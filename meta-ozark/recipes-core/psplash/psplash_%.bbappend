# Brand the boot splash with the Ozark Mountain Technologies logo.
# Source artwork: branding/ozark-splash.svg (regenerate with scripts/make-splash.sh).

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

# Replaces the Poky logo; "default" makes it the image /usr/bin/psplash shows.
SPLASH_IMAGES = "file://psplash-ozark-img.png;outsuffix=default"

SRC_URI += "file://psplash-colors.h"

# Logo only: no progress bar. (The startup message is already off by default;
# "fullscreen" stays on so the logo sits in the middle of the screen.)
PACKAGECONFIG:remove = "progress-bar"

do_configure:prepend() {
    cp ${WORKDIR}/psplash-colors.h ${S}/psplash-colors.h
}
