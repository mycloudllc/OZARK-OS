# Ozark OS on the Orange Pi 3B

The same system as on the Raspberry Pi 4 (Dash, TrailCommand, lighting,
hotspot, software updates) for the **Orange Pi 3B v2.1** (Rockchip RK3566,
Mali-G52, BCM43456 Wi-Fi/Bluetooth), with the **Waveshare 8" DSI LCD (C)**.

This folder is a development copy of `../ozark-os/ozark-os`. The Pi build is
unchanged; the Orange Pi pieces are meant to be merged back once they work.

## Build and flash

    ./scripts/build.sh --opi3b
    ./scripts/flash.sh /dev/sdX      # the .wic.bz2 from build/build/tmp/deploy/images/orangepi-3b/

Downloads and shared state come from the Pi tree's `cache/` next to this one,
so only what differs on this board is built (about 75 minutes the first
time). `build.sh` also copies the Pi build's hash equivalence database once,
without which nothing in that cache matches, and uses the host's Docker
engine even when the current docker context is Docker Desktop: through
Docker Desktop's file sharing, Yocto builds fail at random. The SD card layout is the Pi's
(boot, root, rootb, OZARKLOGS, ozarkdata, 8 GB card or larger), with U-Boot
before the first partition where the RK3566 boot ROM looks for it.

The boot ROM tries the board's SPI flash before the SD card. If the SPI flash
holds a bootloader (Orange Pi's own images can write one), that one starts
instead: erase it, or it won't run this card's U-Boot.

Serial console: the 3-pin debug header, **1500000** baud (U-Boot and Linux).

## What's different from the Pi

| | Raspberry Pi 4 | Orange Pi 3B |
|---|---|---|
| Kernel | linux-raspberrypi 6.6 | Rockchip 6.1 (`linux-ozark-rockchip`) |
| Bootloader | Pi firmware, `config.txt` | U-Boot 2026.01, `boot.scr` (`ozark-boot`) |
| SD card in Linux | `/dev/mmcblk0` | `/dev/mmcblk1` |
| GPU | VC4/V3D | Mali-G52 (panfrost) |
| Video decode | `v4l2h264dec` | `v4l2slh264dec` (Hantro VPU on the V4L2 driver, not MPP; openauto 0008, kernel 0002) |
| Wi-Fi/BT | BCM43455 | BCM43456 (`ozark-bcm43456-firmware`) |
| DSI panel | `dtoverlay=vc4-kms-dsi-waveshare-panel,8_0_inch` | its own device tree, `rk3566-orangepi-3b-v2.1-ozark-waveshare8.dtb` |

All of it is in `meta-ozark/conf/machine/orangepi-3b.conf` and
`meta-ozark/dynamic-layers/rockchip/`.

### Display

The 3B's 15-pin DSI connector is wired like a Raspberry Pi's: DSI1, two
lanes, with the panel's I2C on `i2c1`. The kernel gets the Raspberry Pi
kernel's Waveshare panel driver (patch 0001) and polled Goodix touch (0002,
the connector has no touch interrupt line), and a device tree with the panel
on DSI1 (0003). HDMI stays on as well. For an HDMI screen only, set in
`local.conf` (or the machine conf):

    OZARK_BOOT_DTB = "rk3566-orangepi-3b-v2.1.dtb"

### Software updates

Same updates, feed and Settings > Update page as on the Pi; the update file
is built for this board (`hwrevision`: `orangepi-3b 1.0`), and a Pi update
won't install here, nor the other way round. Instead of the Pi's tryboot,
`boot.scr` reads three files on the boot partition:

- `slot.txt`: `slot=a` or `b`, the system that starts
- `tryboot.txt`: `try=b` (say), written by the install
- `tried.txt`: while it exists, `tryboot.txt` is ignored. The install writes
  it; "restart" in Settings > Update removes it, and `boot.scr` writes it
  again as it starts the update. So the update starts once, on that restart
  only. If it doesn't come up and commit (Dash running for 30 s), the next
  start (watchdog, `panic=10`, power) is the old system.

`/etc/ozark/slots.conf` tells `ozark-update` and the update's install script
which kind of board they're on (`LOADER=uboot`, `DISK=/dev/mmcblk1`).

## Not tested on hardware yet

All of it, so far. In particular:

- DSI1 and the panel. The DSI wiring is from Xunlong's own kernel, not from a
  schematic. If the screen stays dark, try HDMI (above) and look for
  `ws-ts-dsi` and `dw-mipi-dsi` in `dmesg`.
- Touch orientation (`touchscreen-inverted-x`, `touchscreen-swapped-x-y`,
  copied from the Pi overlay's 8_0_inch settings).
- Wi-Fi access point mode and 5 GHz on the BCM43456 with Armbian's firmware.
- An update's restart into slot B, and falling back.
