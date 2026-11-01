# TrailCommand Radio

A touchscreen car head unit built on a Raspberry Pi 4. It runs **Ozark OS**, a custom
Yocto (Scarthgap) Linux distribution. Ozark OS boots straight into a customised
[OpenDash (Dash)](https://github.com/openDsh/dash) with Android Auto, FM/AM radio,
Bluetooth and local music, vehicle controls for the TrailCommand hub, and RGB lighting.

![Android Auto](docs/screenshots/android-auto.png)

## Features

- **Android Auto**, wired or wireless, through OpenAuto and aasdk. A quick view in the
  control bar shows the next turn and what's playing.
- **Radio**: FM and AM via RTL-SDR, with six presets per band (tap to tune, hold to save).
- **Bluetooth player**: track details, album art and progress from your phone (AVRCP).
- **Local player**: browse folders on the SD card or a USB drive, with embedded cover art.
  Plays MP3, FLAC, Ogg/Opus and WAV.
- **TrailCommand**: live vehicle data and controls from the TrailCommand hub. Covers
  switches, tyres, service log and power flow.
- **Lighting**: interior front, interior rear and exterior RGBIC zones, driven by an ESP32,
  with patterns and music sync.
- **Phone**: pair, connect and forget phones, all from the touchscreen.
- **Network**: Wi-Fi hotspot (used by wireless Android Auto), Ethernet, LTE modem and internet sharing.
- **Equalizer**: 10-band, system-wide.
- **On-screen keyboard**: works for every text box, plugin settings included.
- **Boot**: a branded splash from power-on until Dash is drawn, then quiet.
- **Light and dark mode**, plus any accent colour from a colour wheel.

## Screenshots

### Media

| Radio | Radio (dark) |
|---|---|
| ![Radio](docs/screenshots/radio.png) | ![Radio, dark](docs/screenshots/radio-dark.png) |
| **Bluetooth player** | **Local player** |
| ![Bluetooth](docs/screenshots/bluetooth.png) | ![Local player](docs/screenshots/local-player.png) |
| **Local player (dark)** | **Android Auto** |
| ![Local player, dark](docs/screenshots/local-player-dark.png) | ![Android Auto](docs/screenshots/android-auto.png) |

### TrailCommand

| Overview | Overview (dark) |
|---|---|
| ![Overview](docs/screenshots/trailcommand-overview.png) | ![Overview, dark](docs/screenshots/trailcommand-overview-dark.png) |
| **Controls** | **Controls (dark)** |
| ![Controls](docs/screenshots/trailcommand-controls.png) | ![Controls, dark](docs/screenshots/trailcommand-controls-dark.png) |
| **Tyres** | **Service** |
| ![Tyres](docs/screenshots/trailcommand-tires.png) | ![Service](docs/screenshots/trailcommand-service.png) |
| **Power** | **Lighting** |
| ![Power](docs/screenshots/trailcommand-power.png) | ![Lighting](docs/screenshots/lighting.png) |

### Settings

| Settings | Phone |
|---|---|
| ![Settings](docs/screenshots/settings-main.png) | ![Phone](docs/screenshots/settings-phone.png) |
| **Network** | **On-screen keyboard** |
| ![Network](docs/screenshots/settings-network.png) | ![Keyboard](docs/screenshots/keyboard.png) |
| **Theme colour** | **About** |
| ![Theme colour](docs/screenshots/theme-color.png) | ![About](docs/screenshots/about.png) |

## Hardware

| Part | Notes |
|---|---|
| Raspberry Pi 4 | Use a solid 5 V / 3 A supply; undervoltage causes glitches. |
| Waveshare 8" DSI touchscreen | 1280×800, Goodix touch (default). The official 7" display works with `--rpi7`. |
| RTL-SDR USB dongle | FM/AM radio |
| ESP32 dev board | Lighting controller, on USB serial ([lighting/](lighting/)) |
| LTE modem (optional) | Any ModemManager-supported USB modem, such as Quectel or Sierra |
| USB microphone (optional) | For Android Auto voice. Without one, the mic is skipped. |
| TrailCommand hub | Vehicle data and controls, over the network |

## Repository layout

```
kas/                    kas build configs (Pi 4, dev add-on, 7" display, QEMU, sstate mirror)
meta-ozark/             The Yocto layer: distro, image, Dash/OpenAuto recipes and patches, services
  recipes-dash/         OpenDash, OpenAuto, aasdk and their libraries (patches in */files/)
  recipes-connectivity/ ozark-hotspot, ozark-network, BlueZ setup
  recipes-multimedia/   ozark-audio (PulseAudio), ozark-eq10 (equalizer)
  recipes-ozark/        ozark-lightd (lighting daemon), ozark-logs
  recipes-graphics/     ozark-display (Qt/EGLFS environment), Barlow Condensed font
opendash-trailcommand/  TrailCommand vehicle plugin for Dash
lighting/               Lighting plugins, ozark-lightd daemon, ESP32 firmware
branding/               Logo, badge and splash sources
scripts/                build.sh, flash.sh, run-qemu.sh, make-splash.sh
docs/                   Build guide and screenshots
```

## Building and flashing

You need an x86-64 Linux PC (or WSL2) with Docker or Podman and about 100 GB of disk.
Everything else runs in the kas container. The first build takes a few hours; later
builds take minutes.

```bash
git init && git add -A && git commit -m "initial"   # kas finds meta-ozark through git
./scripts/build.sh                 # Ozark OS with Dash, TrailCommand and lighting
./scripts/flash.sh /dev/sdX        # write the image to an SD card
```

See **[docs/BUILDING.md](docs/BUILDING.md)** for options (`--rpi7`, `--release`,
`--mirrors`), QEMU testing, debugging over SSH and serial, and iterating without reflashing.
For the lighting firmware, see [lighting/README.md](lighting/README.md).

## Changes to OpenDash

Ozark OS builds upstream [openDsh/dash](https://github.com/openDsh/dash) and
[openDsh/openauto](https://github.com/openDsh/openauto), with the patches below.
Everything is in `meta-ozark/recipes-dash/*/files/`, applied in order.

### Dash

| # | Change |
|---|---|
| 0001 | Cross-compile fixes for Yocto |
| 0002 | Power off and reboot through systemd, without sudo |
| 0003 | Full-screen splash that hands off from the boot splash |
| 0004 | Cursor hides automatically |
| 0005 | Head unit controls: reboot only |
| 0006 | Android Auto quick view: next turn and now playing in the control bar |
| 0007 | On-screen display for volume and Bluetooth connections |
| 0008 | Phone page: pair, connect and forget phones |
| 0009 | Equalizer tab |
| 0010 | Volume changes don't block the UI |
| 0011 | Settings → Lighting: controller port and strip setup |
| 0012 | Only the nav rail and control bar scale; status bar removed |
| 0013 | Side-bar-only scale, Yes/No restart prompt, Phone moved into Settings |
| 0014 | Settings → Network: hotspot, Ethernet, LTE and internet sharing |
| 0015 | Tab spacing, dark-mode OSD, theme colour wheel |
| 0016 | Restart only from Settings; Material-style scrollbars |
| 0017 | Colour pop-up margins |
| 0018 | Controls scale with DPI; no pointer during the splash |
| 0019 | Android Auto turn arrow (no black square) |
| 0020 | Touchscreen support for plugins: on-screen keyboard for any text box, dialogs sized to fit, input styles |
| 0021 | Radio: AM/FM switch, six presets per band, settings gear at the top right |
| 0022 | About page: Ozark Mountain Technologies branding, OS version, OpenDash credit |
| 0023 | New Bluetooth and Local players: album art, progress, folder browser, cover art from tags |

### OpenAuto

| # | Change |
|---|---|
| 0001 | Cross-compile fixes |
| 0002 | Wireless Android Auto: advertise the hotspot's address; the interface is configurable |
| 0003 | Defaults: 1080p, 300 DPI, reconnect to the last phone |
| 0004 | Crop the projection on the GPU instead of with `videocrop` (fixes green and gray bars) |
| 0005 | Right-hand drive by default |
| 0006 | Microphone: only open one that exists (fixes a freeze, then out-of-memory, when tapping the mic) |

## Ozark OS components

Beyond Dash, the layer adds:

- **ozark-image**: systemd, Qt 5 on EGLFS/KMS (no X11 or Wayland), PulseAudio and BlueZ.
- **Boot**: psplash with the Ozark splash and a quiet kernel command line. Display modules load
  early so the splash appears fast, and a udev fix keeps psplash on fb0.
- **Display**: `vc4-kms-dsi-waveshare-panel,8_0_inch` overlay by default. The `ili210x` touch
  driver is blacklisted, and `QT_FONT_DPI=180` pins text size because the panel reports a 7" size.
- **ozark-hotspot**: hostapd and systemd-networkd DHCP, with a per-unit SSID and passphrase.
  Wireless Android Auto uses it.
- **ozark-network**: the `ozark-net` CLI behind Settings → Network, `ozark-lte` (ModemManager),
  and NAT internet sharing.
- **ozark-audio / ozark-eq10**: PulseAudio setup and a 10-band equalizer.
- **ozark-lightd**: owns the ESP32 serial link, keeps each zone's look, analyses music for sync,
  and serves D-Bus to the lighting plugins ([lighting/](lighting/)).
- **ozark-logs**: keeps logs on an `OZARKLOGS` partition on the SD card, so you can read them on a PC.
- **TrailCommand plugin**: a Dash vehicle plugin for the TrailCommand hub
  ([opendash-trailcommand/](opendash-trailcommand/)).

## Credits and licenses

- [OpenDash (Dash)](https://github.com/openDsh/dash), [OpenAuto](https://github.com/openDsh/openauto)
  and [aasdk](https://github.com/openDsh/aasdk) are GPL-3.0. The patches in this repo modify them
  and are under the same license.
- [Barlow Condensed](https://github.com/jpt/barlow) is under the SIL Open Font License 1.1.
- Yocto Project / OpenEmbedded layers keep their own licenses.
- Ozark Mountain Technologies branding © Ozark Mountain Technologies.
