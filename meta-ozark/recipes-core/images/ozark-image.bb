require ozark-image-base.bb

SUMMARY = "Ozark OS image"
DESCRIPTION = "Splash, then OpenDash (Android Auto head unit, with the TrailCommand \
plugin) fullscreen on EGLFS. The hello-ozark recipe is still there for display \
bring-up: add it to IMAGE_INSTALL and stop opendash.service."

IMAGE_INSTALL:append = " \
    opendash \
    ozark-audio \
    ozark-lightd \
"

# Wi-Fi hotspot for wireless Android Auto (the Pi's Wi-Fi; QEMU has none),
# and Settings > Network: Ethernet, LTE, sharing the internet over the hotspot
IMAGE_INSTALL:append:rpi = " ozark-hotspot ozark-network"

# Software updates (Settings > Update) into the other system slot, and the
# data partition that keeps settings across them. The update (ozark-swu)
# carries this image's root filesystem as ext4.
IMAGE_INSTALL:append:rpi = " ozark-update ozark-data"
IMAGE_FSTYPES:append:rpi = " ext4.zst"

# The UI owns the display; no login prompt on the screen.
# (Serial console and SSH still work on dev images.)
ROOTFS_POSTPROCESS_COMMAND += "ozark_mask_tty1_getty; ozark_mask_psplash_systemd; "

ozark_mask_tty1_getty() {
    install -d ${IMAGE_ROOTFS}${sysconfdir}/systemd/system
    ln -sf /dev/null ${IMAGE_ROOTFS}${sysconfdir}/systemd/system/getty@tty1.service
}

# psplash-systemd quits the splash as soon as boot finishes, which is before
# Dash can draw: the screen would go black in between. The UI service stops
# psplash itself once it is on screen (see opendash.service).
ozark_mask_psplash_systemd() {
    ln -sf /dev/null ${IMAGE_ROOTFS}${sysconfdir}/systemd/system/psplash-systemd.service
}
