SUMMARY = "Ozark OS base image"
DESCRIPTION = "Milestones 1-2: boots with the Ozark Mountain Technologies splash \
to a console login. No graphics stack beyond the splash."
LICENSE = "MIT"

inherit core-image

# "splash" pulls in psplash, which meta-ozark brands with the Ozark logo.
IMAGE_FEATURES += "splash"

IMAGE_INSTALL:append = " \
    kernel-modules \
"

# Plain-text boot logs on the SD card's OZARKLOGS partition (Pi only: the
# partition comes from ozark-sdimage.wks)
IMAGE_INSTALL:append:rpi = " ozark-logs"

# Room for logs and experiments on the dev image
IMAGE_ROOTFS_EXTRA_SPACE = "524288"
