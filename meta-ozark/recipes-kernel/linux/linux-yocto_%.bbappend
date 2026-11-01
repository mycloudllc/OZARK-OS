FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

# QEMU only: virtual Bluetooth controllers for testing (see kas/qemu.yml).
# The Bluetooth subsystem itself comes from KERNEL_FEATURES there.
SRC_URI:append:qemuall = " file://bt-vhci.cfg"
