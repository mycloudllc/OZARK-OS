# Building Ozark OS

Ozark OS is the in-vehicle OS for the Raspberry Pi 4 behind TrailCommand Radio,
built with Yocto/OpenEmbedded. It boots to the Ozark Mountain Technologies
splash and then [openDsh/dash](https://github.com/openDsh/dash) with the
TrailCommand and lighting plugins.

![hello-ozark at 800×480](../branding/hello-ozark-preview.png)

## What's here

```
ozark-os/
├── kas/
│   ├── ozark-rpi4.yml      Base config: Scarthgap, layers, machine, distro, image
│   ├── dev.yml             Dev add-on: serial console, SSH, empty root password (default ON)
│   ├── display-rpi7.yml    Official 7" DSI touchscreen
│   ├── mirrors.yml         Optional Yocto public sstate mirror (faster first build)
│   └── qemu.yml            Build for QEMU (qemuarm64) to test on the build PC
├── meta-ozark/             Our layer — board-agnostic
│   ├── conf/distro/ozark.conf               systemd, no X11/Wayland, Qt on EGLFS
│   ├── recipes-core/images/                 ozark-image-base, ozark-image
│   ├── recipes-core/psplash/                Ozark boot splash + colours
│   ├── recipes-graphics/fonts/              Barlow Condensed (brand font, OFL)
│   ├── recipes-graphics/ozark-display/      Qt/EGLFS environment shared by UI apps
│   ├── recipes-qt/hello-ozark/              The hello-world app + systemd unit
│   ├── recipes-dash/                        OpenDash, OpenAuto, aasdk and their libraries
│   ├── recipes-kf5/                         BluezQt + extra-cmake-modules (for Dash's Bluetooth)
│   ├── recipes-multimedia/                  ozark-audio (PulseAudio setup), ozark-eq10 (equalizer)
│   ├── recipes-connectivity/bluez5/         Bluetooth name, class, start without adapter
│   ├── recipes-ozark/ozark-lightd/          Lighting controller service (source in lighting/daemon)
│   ├── recipes-ozark/ozark-update/          Software updates (Settings > Update), ozark-swu: the .swu
│   ├── recipes-ozark/ozark-data/            Data partition: settings kept across updates
│   ├── recipes-support/swupdate/            SWUpdate build: signed images, no U-Boot
│   ├── classes/ozark-slots.bbclass          A/B system slots on the SD card
│   └── dynamic-layers/raspberrypi/          Pi-only bits (quiet cmdline, DRM card name, splash override)
├── branding/               Source SVGs for the splash and badge
└── scripts/
    ├── build.sh            Builds inside the kas container
    ├── flash.sh            Writes the image to an SD card (bmaptool, with safety checks)
    ├── make-keys.sh        Makes the key that signs updates (keys/, run by build.sh)
    ├── make-release.sh     Update release: the .swu and the feed radios check
    ├── run-qemu.sh         Boots a --qemu build in a window on this PC
    └── make-splash.sh      Re-renders the PNGs from branding/*.svg
```

## Build host

Any x86-64 Linux PC (or WSL2 on Windows) with:

- **Docker or Podman**, git, bash, curl. Nothing Yocto-specific — it all runs in the kas container.
- **~100 GB free disk**, ideally on an SSD. More RAM and cores = faster.
- On WSL2: keep this repo inside your Linux home (e.g. `~/ozark-os`), **not** under `/mnt/c`. Builds from a Windows drive are painfully slow.

The first build takes a few hours (it compiles a toolchain, kernel, Qt…).
Later builds reuse `cache/sstate` and take minutes.

## Quick start

```bash
cd github-trailcommand-radio
git init && git add -A && git commit -m "Ozark OS: initial layer"   # kas finds meta-ozark through git

./scripts/build.sh --base          # Milestones 1–2: splash + login prompt
./scripts/build.sh                 # Milestone 4: splash + OpenDash with TrailCommand
./scripts/build.sh --rpi7          # ...for the official 7" touchscreen (default: Waveshare 8" DSI)
./scripts/build.sh --mirrors       # use the Yocto sstate mirror to speed things up

./scripts/flash.sh /dev/sdX        # write the latest ozark-image to an SD card
```

`build.sh` downloads `kas-container` 5.5 on first run if it isn't installed.
Caches default to `cache/downloads` and `cache/sstate`; point them at a big
disk with `OZARK_DL_DIR` / `OZARK_SSTATE_DIR`.

Other commands:

| Command | What it does |
|---|---|
| `./scripts/build.sh shell` | BitBake shell in the container (`bitbake`, `devtool`, `bitbake -e`…) |
| `./scripts/build.sh sdk` | Cross-SDK installer for building apps on your PC |
| `./scripts/build.sh lock` | Pin every layer to its current commit (`kas/ozark-rpi4.lock.yml`) — do this once a build works |
| `./scripts/build.sh --release` | Image **without** the dev fragment (no SSH/serial login/empty password) |

## Milestones

### 1. Base image boots
- [ ] `./scripts/build.sh --base` completes
- [ ] Flash, boot, log in as `root` (no password) over serial (GPIO 14/15, 115200 8N1) or `ssh root@<pi-ip>`

### 2. Ozark boot splash
- [ ] No rainbow square at power-on (`disable_splash=1`)
- [ ] No kernel text or Tux logo on screen (`quiet logo.nologo vt.global_cursor_default=0`)
- [ ] Ozark logo appears centred on deep green, then a login prompt

Expect 1–3 s of black before the logo — the Pi bootloader can't show a custom
image, and psplash starts once the kernel is up.

### 3. Hello-world Qt app at boot
- [ ] `./scripts/build.sh` (default `ozark-image`) completes
- [ ] Splash hands over to the hello-ozark screen with no login prompt
- [ ] Barlow Condensed fonts render (not a fallback font)
- [ ] "Tap to test touch" counts taps on the touchscreen
- [ ] `systemctl restart hello-ozark` brings it back; killing it restarts it in 2 s

### 4. OpenDash head unit
`ozark-image` starts [OpenDash](https://github.com/openDsh/dash) (`opendash.service`) with
two plugins built straight from this repo, both on by default
(`meta-ozark/recipes-dash/opendash/files/dash.conf`):
**TrailCommand** (`opendash-trailcommand/`, Vehicle page) and **Interior Lighting** +
**Exterior Lighting** (launcher page). The lighting tabs talk over D-Bus to `ozark-lightd`, which
drives one ESP32 with three LED outputs (interior front/rear, exterior): see [lighting/](../lighting/README.md),
including the firmware. It can drive a WLED controller instead (Settings > Lighting).

Ozark additions to Dash (patches `0001`–`0011` in `recipes-dash/opendash/files/`):

| What | Where | Patch |
|---|---|---|
| Seamless splash: Ozark logo from power-on to Dash, no black gap | `opendash.service`, `DASH_SPLASH_*` | 0003 |
| Mouse pointer shows when used, hides after 3 s, never for touch | `DASH_CURSOR_AUTOHIDE` | 0004 |
| No exit/minimize/shut down; the power button only restarts (with confirmation) | | 0005 |
| Control bar shows Android Auto's next turn and now playing | Settings > Layout > control bar | 0006 |
| Volume OSD; Volume Up/Down/Mute keys work (USB knobs, wheel adapters); Bluetooth connect/disconnect toast | | 0007 |
| **Phone** page: pair (code confirmation), connect, forget; trusted phones reconnect at start | Phone in the left rail | 0008 |
| **Media > Equalizer**: 10 bands, 31 Hz–16 kHz, ±12 dB, presets + Custom | | 0009 |
| Volume changes no longer block the UI | | 0010 |
| **Settings > Lighting**: lighting controller status, port override, strip setup | | 0011 |

The audio side (`recipes-multimedia/ozark-audio`, `ozark-eq10`): one system-wide PulseAudio
with Bluetooth phone audio (A2DP music, HFP calls via oFono), and the equalizer as a LADSPA
filter in front of the output (sink `ozark_eq`) that Dash drives over PulseAudio's D-Bus.
BlueZ (`recipes-connectivity/bluez5`): the unit is **TrailCommand Radio** (`files/main.conf`),
car-audio device class, and `bluetoothd` starts even before an adapter appears.

Checked in QEMU: splash handover, plugins load, Android Auto bar (simulated data), OSD and
volume keys, pairing end to end with a virtual phone (`btvirt`), equalizer presets reaching
the filter. **Still to check on a Pi:**
- [ ] Android Auto over USB (needs the phone and hardware H.264 decode)
- [ ] TrailCommand connects to the hub
- [ ] Phone music and calls through the speakers (calls need a microphone)
- [ ] Equalizer by ear; lighting controller (flash `lighting/firmware`, all three outputs); touch hides the pointer
- [ ] Boot to Dash within 15–20 s (`systemd-analyze blame` is on dev images)

| Recipe | Source | Ozark changes |
|---|---|---|
| `opendash` | openDsh/dash `develop` | Patches above; installed under `/usr/lib/opendash`, command `opendash` (`/usr/bin/dash` is a shell) |
| `openauto` | openDsh/openauto `develop` | GStreamer video path; sysroot find modules; `autoapp` in its own package |
| `aasdk` | openDsh/aasdk `develop` | OpenSSL 3 fix (from Dash's `patches/`) |
| `qt-gstreamer` | GStreamer/qt-gstreamer `master` | Dash's two patches; QtQuick2 video sink forced on |
| `rtaudio` 5.2.0, `h264bitstream`, `bluez-qt` + `extra-cmake-modules` 5.115 | upstream | none |

aasdk, OpenAuto and Dash are built as C++17 and link Protobuf through `protobuf.pc`:
Scarthgap's Protobuf 4.25 depends on Abseil, which CMake's stock `FindProtobuf` doesn't link.
Qt is built with xkbcommon: without it, EGLFS drops every key press.

### Next (not started)
Pi hardware checks above → volume knob hardware (GPIO encoder overlay if not USB) →
CAN/OBD → read-only rootfs and ignition-sense shutdown.

## Testing in QEMU (no Pi needed)

```bash
sudo apt install qemu-system-arm qemu-system-gui   # once, on the host
./scripts/build.sh --qemu          # ozark-image for qemuarm64 (add --base for the console image)
./scripts/run-qemu.sh              # window = screen, terminal = serial console
ssh -p 2222 root@localhost         # while it's running
```

The emulator checks systemd, the splash, services, fonts and the Qt app
(mouse clicks stand in for touch). It does not check anything Pi-specific:
firmware and `config.txt`, `cmdline.txt`, the vc4 DRM rule, Wi-Fi/Bluetooth.
If the window stays black, try `./scripts/run-qemu.sh --no-gl`.

## Debugging on the Pi

### Wireless Android Auto

The Pi's Wi-Fi runs a hotspot (`ozark-hotspot`) that phones join for wireless
Android Auto: pair the phone over Bluetooth and Dash sends it the network
details. Each unit makes its own name (`TrailCommand-XXXX`) and key on first
boot, in `/etc/ozark/hotspot.conf`. Change `COUNTRY` there to match where the
unit is used (it decides the legal channels), and `CHANNEL` to a 2.4 GHz one
(1-11) if the 5 GHz default (36) won't come up. Then reboot.

**Dual band:** the built-in radio serves 5 GHz. Plug in a USB Wi-Fi adapter
that supports AP mode (MediaTek MT7610U, MT7612U and MT7921AU, and Ralink
chips, have firmware in the image) and it serves the same network on 2.4 GHz
for devices that can't do 5 GHz, such as WLED controllers. Both radios share
the `ozarkap` bridge, so they share one address and DHCP. `IFACE2` picks the
adapter (`auto`, a name, or `none` to turn it off) and `CHANNEL2` its channel
(empty: 6, or 36 if the main radio is moved to 2.4 GHz). Settings → Network
shows which bands are up.

```bash
systemctl status ozark-hotspot      # access point (hostapd), built-in radio
systemctl status 'ozark-hotspot-radio@*'   # the USB adapter's access point
networkctl status ozarkap           # hotspot bridge: 10.42.0.1, DHCP for the phone
journalctl -u opendash -b | grep -i bluetoothserver   # the phone handshake
```

### Boot logs on the SD card

Every boot's full system log is saved as plain text on the SD card's
**OZARKLOGS** partition, so a Pi that won't come up can still be diagnosed:
put the card in any PC and open `boot-NNNN.log` (`latest.txt` names the
newest; the last 20 boots are kept). On the Pi the same files are in `/logs`.
Writes are flushed every 5 s, so a power cut loses at most the last few
seconds. The Pi has no clock battery, so trust the boot number over the
timestamps until it has had network time.

Not captured: a hang before the root filesystem mounts, or a kernel panic.
Use the serial console (dev images) for those.

```bash
journalctl -u hello-ozark -b           # app output and Qt errors
systemctl status psplash-start          # splash service
ls -l /dev/dri/                         # expect card0, card1, ozark-display -> cardN
cat /boot/config.txt /boot/a/cmdline.txt  # firmware settings (b/ when slot B runs)
```

For verbose Qt display logs, uncomment `QT_LOGGING_RULES` and
`QT_QPA_EGLFS_DEBUG` in `/etc/default/ozark-display` and restart the service.

## Software updates

Settings → Update installs a new version of the whole system, from a USB
drive or online, with [SWUpdate](https://swupdate.org). The radio keeps
working while it installs, and after a restart it runs the new version with
every setting, paired phone and music file kept. If the new version doesn't
start properly, the radio goes back to the old one by itself.

**How it works.** The SD card has two system slots, A and B
(`meta-ozark/classes/ozark-slots.bbclass`):

| Partition | What's on it |
|---|---|
| 1 `boot` | Pi firmware; each slot's kernel, device trees, overlays, `cmdline.txt` and `config.txt` in `a/` and `b/` |
| 2 `root` | System slot A (2.5 GB) |
| 3 `rootb` | System slot B (2.5 GB), empty until the first update |
| 4 `OZARKLOGS` | Boot logs (see below) |
| 5 `ozarkdata` | Settings, pairings and Dash's files: kept across updates. Grows to fill the card on first boot. |

The whole layout fits an 8 GB card. `/boot/config.txt` picks the running
slot with `os_prefix=a/`. An update (`.swu`) goes into the other slot:
the root filesystem, then that system's boot files. The radio then restarts
with the firmware's *tryboot*, which starts the new slot (`tryboot.txt`)
for that one boot only. Once Dash has run for 30 seconds, `ozark-update`
writes the new slot into `config.txt` and the update is done. If it doesn't
get there (Dash keeps crashing, the kernel panics, the system hangs and the
watchdog resets it, or the power goes), the next start is the old system.

`ozark-data` keeps `/etc/ozark`, `/etc/systemd/network`, `/root` (Dash and
OpenAuto settings), `/var/lib/bluetooth`, `/var/lib/ozark-lightd` and the
clock on the data partition (overlayfs). Files the system ships in those
folders still come from the new version unless they were changed on the radio.

**Signing.** Each update is signed when it's built, and radios only install
updates signed with the key their image was built with. `build.sh` makes the
key pair on the first build (`scripts/make-keys.sh`, in `keys/`, which git
ignores). **Back up `keys/swupdate-private.pem`.** With a different key,
radios already out there won't take your updates, and you'd have to reflash them.

**Making a release.**

1. Raise `DISTRO_VERSION` in `meta-ozark/conf/distro/ozark.conf`.
2. Run `./scripts/build.sh`. Next to the SD card image it builds
   `ozark-swu-raspberrypi4-64.rootfs.swu`.
3. Run `./scripts/make-release.sh --notes "What's new"`. It writes
   `release/<version>-<build>/` with the `.swu`, the SD card image, and
   `ozark-update.json` (the update feed: version, build, file name, SHA-256,
   size and notes).
4. Upload the `.swu` and `ozark-update.json` side by side, for example as
   assets of a GitHub release. Radios then use this update address:
   `https://github.com/OWNER/REPO/releases/latest/download/ozark-update.json`

On the radio, enter that address in Settings → Update. With **Check
automatically** on, it checks a few minutes after starting and every 6 hours
after that. The radio only offers updates, and only installs one when you
tap **download and install**. To bake an address into new images, set
`OZARK_UPDATE_FEED` in `local.conf`.

**From USB.** Copy the `.swu` to a USB drive (top folder), plug it in, then
tap **look on USB** and **install from USB**.

**Over SSH**, the same tool the page uses:

```bash
ozark-update status                  # version, slot, update state, what the feed offers
ozark-update install /path/file.swu  # install into the other slot
ozark-update reboot                  # restart into it (tryboot)
journalctl -u ozark-update-boot -b   # the first start: committed, or why it went back
cat /boot/config.txt /boot/tryboot.txt
```

The first card with this layout has to be flashed: an older card (one root
partition) can't take updates.

## Iterating on the app without reflashing

```bash
./scripts/build.sh shell
devtool modify hello-ozark              # source checked out into the build workspace
# edit ..., then:
devtool build hello-ozark
devtool deploy-target hello-ozark root@<pi-ip>
```

Or build and run it on your desktop first — it's plain Qt 5 + CMake:

```bash
cmake -S meta-ozark/recipes-qt/hello-ozark/files/src -B /tmp/hello && cmake --build /tmp/hello
/tmp/hello/hello-ozark
```

## Build checks

- BitBake parsed all 2,607 recipes against the current Scarthgap branches of poky, meta-openembedded, meta-raspberrypi and meta-qt5 with **0 errors**
- `ozark-image` resolves a full dependency graph (318 recipes) including all of meta-ozark
- psplash picks up the Ozark logo and colours with the progress bar off; `cmdline.txt` gets the quiet-boot flags; qtbase gets `eglfs kms gbm gles2 libinput fontconfig`
- hello-ozark compiles cleanly with `-Wall -Wextra` against Qt 5.15 and renders correctly at 800×480 and 1920×1080
- All kas files validate against the kas 5.5 schema; shell scripts pass shellcheck

## Troubleshooting

- **DRM card name**: the udev rule matches the `vc4-drm` driver to create `/dev/dri/ozark-display`. If it's missing, `udevadm info -a /dev/dri/card1 | grep DRIVERS` shows the right name.
- **Splash → app handoff**: expect a brief blink when Qt takes the screen. If the splash lingers over the app, check `psplash-write` works (`journalctl -u hello-ozark`).
- **Touch rotation** on some panels: set `QT_QPA_EGLFS_ROTATION` in `/etc/default/ozark-display`.

## Branding

The splash and app badge come from the *Technologies badge* in the
Ozark Mountain Brand Adventures logo canvas. Edit `branding/*.svg` and run
`./scripts/make-splash.sh` to regenerate the PNGs. The splash background
(`#142A1D`) must match `PSPLASH_BACKGROUND_COLOR` in
`meta-ozark/recipes-core/psplash/files/psplash-colors.h`.

Barlow Condensed is licensed under the SIL Open Font License 1.1
(`meta-ozark/recipes-graphics/fonts/ttf-barlow-condensed/OFL.txt`).
