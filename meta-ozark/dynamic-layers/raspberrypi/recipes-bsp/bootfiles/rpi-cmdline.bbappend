# Quiet boot so the screen goes black -> Ozark splash -> UI, with no
# kernel text, Tux logos or blinking cursor in between.
# Kernel messages still go to the serial console on dev images.
CMDLINE:append = " quiet loglevel=3 logo.nologo vt.global_cursor_default=0 consoleblank=0"

# Reboot 10 s after a kernel panic, so an update that can't boot falls back
# to the system that could (a failed tryboot boots the committed slot)
CMDLINE:append = " panic=10"

# The firmware only loads overlays from the slot's folder (os_prefix=a/ ->
# a/overlays/) if a/overlays/README exists; without it, it looks in a
# top-level overlays/ that isn't there and boots with no display.
# ozark_slot_boot_files (classes/ozark-slots.bbclass) puts this in each slot.
do_deploy:append() {
    install -d "${DEPLOYDIR}/ozark-slot"
    echo "Overlays for this system slot (os_prefix needs this file to use them)" \
        > "${DEPLOYDIR}/ozark-slot/overlays-README"
}
