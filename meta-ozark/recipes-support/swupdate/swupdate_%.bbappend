# Ozark's SWUpdate build (files/defconfig): signed images, raw/archive/shell
# handlers, no U-Boot, no web server.
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

# ozark-update runs swupdate when an update is installed; no daemon at boot
SYSTEMD_AUTO_ENABLE:${PN} = "disable"
