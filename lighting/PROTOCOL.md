# Ozark lighting controller: serial protocol v3.1

The head unit (ozark-lightd) talks to the ESP32 lighting controller over one serial link:
**460800 baud, 8N1, no flow control**. Messages are ASCII lines ending in `\n` (a `\r`
before it is ignored). Numbers are decimal. The controller answers commands in order.

Version 3 is v2 (the RGBIC plugin's firmware) with a third output, **exterior**. 3.1 (firmware
3.1.0) adds the background colour (`COLOR3`) and patterns 11–19; firmware 3.0 answers those
with `ERR`.

## Zones

| Zone | Output | Default data pin |
|---|---|---|
| `F` | Interior front | GPIO 16 |
| `R` | Interior rear | GPIO 17 |
| `E` | Exterior | GPIO 18 |
| `I` | Interior: front and rear (look commands only) | |
| `A` | All three (look commands only) | |
| `G` | Global settings (`CFG` only) | |

## From the controller, unprompted

| Line | Meaning |
|---|---|
| `READY fw=3.1.0 zones=F,R,E` | Booted (also after `FACTORY`). Its look and config are loaded from flash. |

## Commands

Each answers with one line unless noted. `ERR <reason>` on any bad input. A line longer than
255 characters is dropped whole (`ERR line too long`).

| Command | Answer | |
|---|---|---|
| `PING` | `PONG` | Handshake; the head unit sends it until it gets `PONG` or `READY`. |
| `INFO` | `INFO fw=3.1.0 max_leds=600 zones=F,R,E pins=2,4,5,13,… orders=RGB,RBG,GRB,GBR,BRG,BGR` | `max_leds` is per zone. `pins` are the GPIOs a zone may use. |

### Look (per zone, live; `SAVE` makes it survive a reboot)

| Command | Answer | |
|---|---|---|
| `COLOR <z> <r> <g> <b>` | `OK` | Primary colour, 0–255 each |
| `COLOR2 <z> <r> <g> <b>` | `OK` | Secondary colour (Gradient, Comet, Beat Pulse…) |
| `COLOR3 <z> <r> <g> <b>` | `OK` | Background: fills the LEDs a pattern leaves dark (Comet, VU Meter, Spectrum, Breathe's low point, and the patterns from 11 on except Wipe and Fade). Black by default. |
| `PAT <z> <id>` | `OK` | Pattern, see below |
| `SPEED <z> <0-255>` | `OK` | Animation speed |
| `BRI <z> <0-255>` | `OK` | Zone brightness (scaled by `MAXBRI`) |
| `SAVE` | `OK SAVED` | Store all zones' looks in flash |
| `AUD <b0> … <b7> <beat>` | *(none)* | Music frame: 8 log-spaced band levels 0–255 (low to high), `beat` 0/1. Sent ~43 times a second while a music pattern is on; no answer, so it never backs up. Music patterns fade out if frames stop for 1 s. |

Patterns (ids are fixed; the UI lists them in this order):

| id | Pattern | id | Pattern |
|---|---|---|---|
| 0 | Off | 10 | Beat Pulse *(music)* |
| 1 | Solid | 11 | Fire: background (cold) → colour 2 (embers) → colour (hottest) |
| 2 | Rainbow | 12 | Sparkle: glints of colour 2 over the background |
| 3 | Chase | 13 | Scanner: a dot in colour sweeping back and forth with a trail |
| 4 | Breathe: background ↔ colour | 14 | Wipe: colour fills the strip, then colour 2 |
| 5 | Gradient | 15 | Wave: bands of colour rolling over the background |
| 6 | Twinkle | 16 | Fade: colour ↔ colour 2, the whole strip |
| 7 | Comet | 17 | Strobe: flashes of colour over the background |
| 8 | VU Meter *(music)* | 18 | Theater: every third LED in colour, stepping along |
| 9 | Spectrum *(music)* | 19 | Rainbow Runner: a short rainbow running over the background |

### Hardware config (staged, then applied)

| Command | Answer | |
|---|---|---|
| `CFGGET` | `CFG F pin=16 count=60 order=GRB rgbw=0 khz=800 reverse=0`, the same for `R` and `E`, `CFG G maxbri=200`, then `END` | Current (applied) config |
| `CFG <z> PIN <gpio>` | `OK` | Data pin, one of `INFO`'s `pins` |
| `CFG <z> COUNT <n>` | `OK` | LEDs on the strip, 1…`max_leds` |
| `CFG <z> ORDER <RGB…>` | `OK` | Colour order, one of `INFO`'s `orders` |
| `CFG <z> RGBW <0/1>` | `OK` | Strip has a white channel |
| `CFG <z> KHZ <800/400>` | `OK` | Data speed (400 for old WS2811) |
| `CFG <z> REVERSE <0/1>` | `OK` | Run patterns from the far end |
| `CFG G MAXBRI <1-255>` | `OK` | Brightness cap for all zones (power budget) |
| `CFGAPPLY` | `OK APPLIED` or `ERR <reason>` | Check the staged config (pins valid and distinct), apply it live and store it in flash. All or nothing: if any `CFG` change since the last `CFGAPPLY`/`CFGREVERT` was refused, nothing is applied (`ERR not applied, a change was refused (…)`). Staged changes are dropped either way. |
| `CFGREVERT` | `OK` | Drop staged changes (the head unit sends it before staging a new edit) |

### Other

| Command | Answer | |
|---|---|---|
| `TEST <z>` | `OK` | Light the zone red, green, then blue from start to end (1 s each), then back to its look. Shows the colour order and direction. |
| `FACTORY` | `OK` then reboot (`READY`) | Erase looks and config |
