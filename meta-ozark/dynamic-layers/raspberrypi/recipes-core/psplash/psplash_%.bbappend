# meta-raspberrypi swaps in its own logo with SPLASH_IMAGES:rpi, which beats
# the plain assignment in meta-ozark/recipes-core/psplash. Put the Ozark logo
# back for Pi machines (meta-ozark's higher layer priority makes this win).
# The PNG and psplash-colors.h come from the main bbappend's files/ dir.
SPLASH_IMAGES:rpi = "file://psplash-ozark-img.png;outsuffix=default"
