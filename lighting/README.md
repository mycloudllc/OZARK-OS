# Ozark lighting

One ESP32 on the head unit drives three addressable (RGBIC) LED outputs:
**interior front**, **interior rear** and **exterior**, each on its own GPIO.
The head unit talks to it over one serial link. The ESP32 runs either the Ozark
firmware here, or [WLED](#wled).

```
Dash: Interior Lighting tab ─┐                           ┌─ GPIO 16 → interior front (F)
Dash: Exterior Lighting tab ─┼─ D-Bus ── ozark-lightd ── serial ── ESP32 ─┼─ GPIO 17 → interior rear  (R)
scripts, CAN, ignition…     ─┘ (system bus)   (one port)                  └─ GPIO 18 → exterior       (E)
```

| Folder | What |
|---|---|
| `firmware/` | ESP32 firmware (PlatformIO, Arduino). Protocol: [PROTOCOL.md](PROTOCOL.md) |
| `daemon/` | `ozark-lightd`: owns the serial link, keeps each zone's look, runs the music analysis, serves D-Bus |
| `common/` | `LightingWidget`: a lighting tab's look controls (pattern, colours, speed, brightness), a D-Bus client of ozark-lightd |
| `interior_lighting/` | Dash plugin: tab **Interior Lighting** (zones F and R, with Front / Rear / Both) |
| `exterior_lighting/` | Dash plugin: tab **Exterior Lighting** (zone E) |

Ozark OS builds and installs the daemon (`meta-ozark/recipes-ozark/ozark-lightd`) and both
plugins (the opendash recipe copies `common/` into each plugin folder, since Dash builds a
plugin from the `.cpp` files in its folder). The firmware is flashed separately.

## Firmware

```bash
cd lighting/firmware
pio run -t upload        # build and flash over USB (ESP32 dev module)
pio device monitor       # 460800 baud; type PING, INFO, CFGGET…
```

Targets a classic ESP32 (WROOM). The data pins a zone may use are GPIO 4, 5, 13, 14,
16–19, 21–23, 25–27, 32, 33 (on a WROVER module, avoid 16 and 17: they're its PSRAM).
Pins, LED counts, colour order, RGBW, data speed and direction are set in Dash's
**Settings > Lighting** and stored on the board. The daemon finds the board by itself
(USB serial: CP210x, CH340/CH9102, FTDI, ESP32 native USB); Settings > Lighting can pin
a port instead.

## WLED

Instead of the Ozark firmware, the controller can be a stock
[WLED](https://kno.wled.ge) board (0.14 or later). Choose **Controller: WLED** in
Dash's **Settings > Lighting**, then the connection:

- **Network** (Wi-Fi or Ethernet): everything works, music included. Join the WLED to
  the head unit's hotspot (its name and password are in Settings > Network), or plug an
  Ethernet WLED board into the head unit. **find** looks for WLED boards (mDNS); or type
  the address. ESP32s only do 2.4 GHz Wi-Fi: with the hotspot on 5 GHz for Android Auto,
  plug in a USB Wi-Fi adapter to add the 2.4 GHz band (see `ozark-hotspot`).
- **USB**: the same cable as the Ozark firmware, WLED's JSON API over serial.
  Music patterns only react if the WLED board has its own microphone.
- **USB (Adalight)**: the same cable, but ozark-lightd draws every LED itself, with the Ozark
  firmware's patterns (`daemon/PixelRenderer.cpp`), and streams them as Adalight frames. Every
  pattern looks as on the Ozark controller, music included, with no microphone. JSON on the
  same line still reads WLED's LED count and segments (including reversed ones). The frame
  rate follows the baud rate: 200 LEDs run at about 16 frames/s at 115200 baud and 40
  (the maximum) at 921600. Settings > Lighting shows it.

For both USB connections, **USB speed** in Settings > Lighting must match WLED's serial baud
rate (WLED: Config > Sync Interfaces > Serial; 115200 unless changed).

Each zone (interior front, interior rear, exterior) is one WLED **segment**, chosen in
Settings > Lighting (by default 0, 1, 2). Set up the LEDs, outputs and segments in WLED's
own web page; Max brightness sets WLED's brightness.

Over the network and USB (JSON), the tabs' patterns map to WLED effects:

| Pattern | WLED effect | Pattern | WLED effect |
|---|---|---|---|
| Off | segment off | Twinkle | Twinkle |
| Solid | Solid | Comet | Lighthouse |
| Rainbow | Rainbow | VU Meter | Gravimeter |
| Chase | Chase | Spectrum | Freqmatrix |
| Breathe | Breathe | Beat Pulse | Puddlepeak |
| Gradient | Gradient | | |

Music: ozark-lightd's analysis of what's playing goes to WLED as **audio sync** packets
(UDP port 11988), which WLED's sound-reactive effects follow. That needs the
**AudioReactive** usermod, included in the official ESP32 builds; ozark-lightd switches
its sync mode to *receive* by itself. Settings > Lighting says so if the usermod is missing.

## D-Bus API (`org.ozark.Lighting1`, system bus)

`/org/ozark/Lighting1`, interface `org.ozark.Lighting1`:

| Method / signal | |
|---|---|
| `GetStatus() → a{sv}` | `connected`, `firmware`, `max_leds`, `pins` (as), `orders` (as), `port`, `sensitivity`, `type` (`ozark`/`wled`); for WLED also `link` (`network`/`serial`/`adalight`), `host`, `baud`, `fps` (Adalight), `name`, `segments` (as, `"0: name"`), `map` ({`F`,`R`,`E` → segment id, -1 = none}), `audio` (`""`/`on`/`missing`) |
| `GetConfig() → a{sv}` | `F`/`R`/`E` → {`pin`, `count`, `order`, `rgbw`, `khz`, `reverse`}, `G` → {`maxbri`} |
| `SetConfig(a{sv})` | Same shape, any subset. Result arrives as `ConfigApplied` |
| `Test(s zone)` | Red, green, blue sweep on `F`, `R` or `E` |
| `GetPorts() → as` | `auto`, `/dev/serial/by-id/…`, then device names |
| `SetPort(s)` | `auto` or a device path (stored) |
| `SetSensitivity(i)` | Music sensitivity, 25–400 % (stored) |
| `SetController(a{sv})` | Any subset of `type` (`ozark`/`wled`), `link` (`network`/`serial`/`adalight`), `host` (`address[:port]`), `baud` (WLED's serial baud rate), `F`/`R`/`E` (WLED segment id, -1 = none). Stored, applied at once |
| `FindWled()` | Look for WLED boards on the network (mDNS); the answer is `WledFound` about 2 s later |
| `WledFound(as)` | `"address name"` for each WLED found |
| `StatusChanged(a{sv})`, `ConfigChanged(a{sv})`, `ConfigApplied(b ok, s message)` | |

`/org/ozark/Lighting1/F`, `/R`, `/E`, interface `org.ozark.Lighting1.Zone`:

| Method / signal | |
|---|---|
| `GetLook() → a{sv}` | `color`, `color2` (`#rrggbb`), `pattern` (0–10, see PROTOCOL.md), `speed`, `brightness` (0–255) |
| `SetLook(a{sv})` | Any subset; applied immediately, saved on the board 2 s after the last change |
| `LookChanged(a{sv})` | Whoever changed it: every tab stays in sync |

From a shell on the unit, e.g. turn the exterior strip red:

```bash
busctl call org.ozark.Lighting1 /org/ozark/Lighting1/E org.ozark.Lighting1.Zone SetLook 'a{sv}' 2 color s '#ff0000' pattern i 1
```
