# Ozark OS: start system slot A or B (classes/ozark-slots.bbclass in
# meta-ozark). Compiled to boot.scr, which U-Boot runs from the boot partition.
#
# The boot partition has each slot's kernel, device tree and command line in
# a/ and b/, and:
#   slot.txt     slot=a or b: the system to start (ozark-update commits it)
#   tryboot.txt  try=a or b: start that slot once, to try an update
#   tried.txt    tryboot.txt is ignored while this exists. The install
#                writes it, so an update only starts when ozark-update
#                reboot removes it; and it's written here before that one
#                start. Any later start that ozark-update hasn't committed by
#                then (it removes both files) goes back to slot.txt's system.
# The Raspberry Pi's tryboot works the same way: a trial that never commits
# is undone by the next reset (watchdog, panic=10, power).

# Small files go here, clear of this script (at scriptaddr) and the kernel
setenv ozark_addr ${ramdisk_addr_r}
setenv ozark_part ${devnum}:${distro_bootpart}

setenv slot a
if load ${devtype} ${ozark_part} ${ozark_addr} slot.txt; then
    env import -t ${ozark_addr} ${filesize} slot
fi
setenv committed ${slot}

setenv try
if load ${devtype} ${ozark_part} ${ozark_addr} tryboot.txt; then
    if test -e ${devtype} ${ozark_part} tried.txt; then
        echo "Ozark: an update is installed but not started (tried.txt): slot ${slot}"
    else
        env import -t ${ozark_addr} ${filesize} try
        if test "${try}" = a || test "${try}" = b; then
            env export -t ${ozark_addr} try
            fatwrite ${devtype} ${ozark_part} ${ozark_addr} tried.txt ${filesize}
            setenv slot ${try}
            echo "Ozark: trying slot ${slot} once"
        fi
    fi
fi

# The chosen slot, or the committed one if that slot can't be loaded
for s in ${slot} ${committed}; do
    if load ${devtype} ${ozark_part} ${kernel_addr_r} ${s}/Image; then
        echo "Ozark: starting slot ${s}"
        load ${devtype} ${ozark_part} ${fdt_addr_r} ${s}/ozark.dtb
        load ${devtype} ${ozark_part} ${ozark_addr} ${s}/cmdline.txt
        env import -t ${ozark_addr} ${filesize} bootargs
        booti ${kernel_addr_r} - ${fdt_addr_r}
    fi
done
echo "Ozark: no system slot could be started"
