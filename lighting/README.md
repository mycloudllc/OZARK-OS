# Ozark lighting

One ESP32 on the head unit drives three addressable (RGBIC) LED outputs:
**interior front**, **interior rear** and **exterior**, each on its own GPIO.
The head unit talks to it over one serial link.

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

## D-Bus API (`org.ozark.Lighting1`, system bus)

`/org/ozark/Lighting1`, interface `org.ozark.Lighting1`:

| Method / signal | |
|---|---|
| `GetStatus() → a{sv}` | `connected`, `firmware`, `max_leds`, `pins` (as), `orders` (as), `port`, `sensitivity` |
| `GetConfig() → a{sv}` | `F`/`R`/`E` → {`pin`, `count`, `order`, `rgbw`, `khz`, `reverse`}, `G` → {`maxbri`} |
| `SetConfig(a{sv})` | Same shape, any subset. Result arrives as `ConfigApplied` |
| `Test(s zone)` | Red, green, blue sweep on `F`, `R` or `E` |
| `GetPorts() → as` | `auto`, `/dev/serial/by-id/…`, then device names |
| `SetPort(s)` | `auto` or a device path (stored) |
| `SetSensitivity(i)` | Music sensitivity, 25–400 % (stored) |
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
