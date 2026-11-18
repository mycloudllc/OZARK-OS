# Ozark Remote protocol (version 1)

How a phone app controls the radio. The radio side is `ozark-remoted` ([daemon/](daemon/));
the app is [app/](app/).

## Transports

Both carry the same JSON messages.

### Wi-Fi: WebSocket

`ws://<radio>:54546/`. One JSON message per text frame. The phone is normally on the radio's
own hotspot, where the radio is `10.42.0.1`. The radio only accepts connections from local
addresses (private and link-local ranges), never from the LTE side. Traffic is protected by the
hotspot's WPA2 encryption, not by TLS.

### Bluetooth LE: GATT

The radio advertises as **Ozark** with the service UUID below.

| | UUID | |
|---|---|---|
| Service | `7a6f0001-5a3b-4c1e-9f2d-0c8e4b1a6d20` | |
| RX | `7a6f0002-5a3b-4c1e-9f2d-0c8e4b1a6d20` | The phone **writes** requests (with or without response) |
| TX | `7a6f0003-5a3b-4c1e-9f2d-0c8e4b1a6d20` | The radio **notifies** replies and events (subscribe first) |

Framing: every message is compact JSON followed by `\n` (JSON never contains a raw newline).
A message is split into pieces of at most MTU − 3 bytes; the receiver appends pieces until it
sees `\n`. Ask for a large MTU (Android: `requestMtu(512)`; iOS negotiates by itself); 23 is
the minimum. **One phone at a time:** while one phone is connected over BLE, writes from
another are ignored. BLE traffic is not link-encrypted in version 1: only the pairing token
protects access.

## Messages

Request (phone → radio):

```json
{"id": 7, "method": "dash.volume", "params": {"value": 30}}
```

`id` is any JSON value; the reply carries it back. `params` is an object (may be omitted).

Reply:

```json
{"id": 7, "result": {"ok": true}}
{"id": 7, "error": {"code": "bad_params", "message": "value: 0 to 100"}}
```

Event (radio → phone, unprompted, only after `hello` or pairing):

```json
{"event": "dash.state", "data": { ... }}
```

Error codes: `unauthorized`, `bad_message`, `too_big` (over 64 KiB), `unknown_method`,
`bad_params`, `unavailable` (that part of the radio isn't running or installed), `failed`,
`busy`, `no_pairing`, `wrong_code`. `message` is readable text the app can show.

## Pairing and signing in

Before signing in, only `info`, `ping`, `pair.request`, `pair.confirm` and `hello` work;
everything else answers `unauthorized`.

1. `pair.request {"name": "Pixel 8", "platform": "android"}` → `{"expires_in": 120, "digits": 6}`.
   The radio shows a 6-digit code on its screen for 120 s. (`busy`: another phone is pairing,
   or a pairing started less than 5 s ago.)
2. The user types the code: `pair.confirm {"code": "635173"}` → `{"token": "...", "client_id": "...",
   "name": <radio>, "protocol": 1, "methods": [...]}`. Five wrong codes end the pairing.
3. Keep `token` safe (Android Keystore / iOS Keychain). From then on, each connection starts
   with `hello {"token": "..."}` → `{"client_id", "name", "protocol", "methods"}`.
   `unauthorized` means the phone was removed on the radio (Settings > Bluetooth > Phone app)
   or with `clients.revoke`: pair again.

A paired session gets every event. Removing a phone closes its open connections at once.

## Methods

### General

| Method | Params | Result |
|---|---|---|
| `info` | | `name`, `protocol`, `product` |
| `ping` | | `pong` |
| `state.get` | | `{dash, lighting, vehicle}`: everything below, at once |
| `clients.list` | | `clients` [{`id`, `name`, `platform`, `created`, `last_seen`, `connected`}], `you` |
| `clients.revoke` | `id` | `ok` |

### Dash: volume, brightness, theme, pages

| Method | Params |
|---|---|
| `dash.volume` | `value` 0–100 |
| `dash.volume_step` | `delta` (e.g. 2 or −2) |
| `dash.brightness` | `value` 0–255 |
| `dash.theme` | `mode` `light`/`dark`, and/or `color` `#rrggbb` |
| `dash.page` | `name`, one of `dash.pages` |

State (`state.get`'s `dash`, and the `dash.state` event):

```json
{"volume": 30, "brightness": 255, "mode": "dark", "color": "#1a73e8",
 "page": "Media", "pages": ["Android Auto", "Media", "Vehicle", "Camera", "Launcher", "Settings"],
 "media": {"active": "radio", "radio": {...}, "bluetooth": {...}, "local": {...}}}
```

### Media

Sources: `radio`, `bluetooth` (the phone's music over Bluetooth, or Android Auto's), `local`
(music files on the radio). `media.active` is the source that played last; play/pause/next go
there unless `source` is given.

| Method | Params |
|---|---|
| `media.play`, `media.pause`, `media.toggle`, `media.next`, `media.previous` | optional `source` |
| `media.source` | `source`: makes it active and shows its tab on the radio |

A source that can't play answers `bad_params` with the reason ("No radio tuner is loaded",
"Nothing is playing over Bluetooth"). Source state:

| Key | |
|---|---|
| `available` | it can play now |
| `playing` | |
| `title`, `artist`, `album` | |
| `position_ms`, `duration_ms` | position when the state was sent; count on while `playing` |
| `device` | Bluetooth: the phone's name, or `Android Auto` |

The radio also has `band` (`fm`/`am`), `value` (what the dial shows ×10 for FM: 1015 = 101.5 MHz;
AM: 101 = 1010 kHz), `freq_hz`, `label` ("101.5"), `unit` ("MHz"/"kHz"), `min`, `max` (of `value`),
and `presets` [{`index` 0–5, `value` (0 = empty), `label`}].

### Radio

| Method | Params |
|---|---|
| `radio.play`, `radio.stop` | |
| `radio.tune` | `freq_hz`, or `value` as above (clamped to the band) |
| `radio.step` | `delta` steps (FM 0.1 MHz, AM 10 kHz) |
| `radio.band` | `band` `fm`/`am` (each band keeps its station and presets) |
| `radio.preset` | `index` 0–5: tune to it |
| `radio.save_preset` | `index` 0–5: save the current station there |

### Lighting

| Method | Params | Result |
|---|---|---|
| `lighting.get` | | `status` (controller, see lighting/README.md), `zones` {`F`, `R`, `E` → look} |
| `lighting.set` | `zone` `F` (interior front), `R` (interior rear), `E` (exterior); `look` | `ok` |

A look: `color`, `color2`, `background` (`#rrggbb`), `pattern` (0–19, lighting/PROTOCOL.md: 0 off,
1 solid, ...), `speed` 0–255, `brightness` 0–255. Send any subset. Events: `lighting.look`
`{zone, look}`, `lighting.status`.

### Vehicle

| Method | Result |
|---|---|
| `vehicle.get` | `status`, `values`, `tires`, `dtcs` (vehicle/README.md: ozark-vehicled's GetStatus, GetValues, GetTires, GetDtcs) |
| `vehicle.dtcs.get` | the last trouble-code read |
| `vehicle.dtcs.read` | `started`; the codes arrive as the `vehicle.dtcs` event |
| `vehicle.dtcs.clear` | `started`; then `vehicle.dtcs_cleared` {`ok`, `message`} and a fresh `vehicle.dtcs` |

Values are SI (`speed_kmh`, `coolant_c`, ...); convert in the app. Events: `vehicle.status`,
`vehicle.values` (at most twice a second), `vehicle.tires`, `vehicle.dtcs`, `vehicle.dtcs_cleared`.

### System

| Method | Params | Result |
|---|---|---|
| `system.info` | | `hostname`, `version`, `build`, `slot`, `uptime_s`, `time` |
| `system.network` | | `ozark-net status` as a map (hotspot name, internet, LTE, ...) |
| `system.hotspot.set` | `name`, `password`, optional `channel` (36), `country` (US) | `ok` |
| `system.update.status` | | `ozark-update status` as a map (`version`, `state`, `available.*`, ...) |
| `system.update.check` | | the status after asking the update feed (may take a minute) |
| `system.reboot` | `confirm: true` | `ok`, then the radio restarts |

Changing the hotspot's name or password disconnects every phone on Wi-Fi, this one included.
Installing updates isn't offered remotely: it's started on the radio, where its warning shows.

## D-Bus (on the radio)

`org.ozark.Remote1` at `/org/ozark/Remote1`: `GetStatus()`, `ListClients()`, `RevokeClient(s id)`,
`CancelPairing()`; signals `PairingStarted(s code, s name, i seconds)`, `PairingEnded(s name, b paired)`,
`ClientsChanged()`, `StatusChanged()`. Dash shows the code and lists the phones from these.

`org.ozark.Dash1` at `/org/ozark/Dash1` (Dash): `GetState() → a{sv}`, `Control(s method, a{sv} params) → a{sv}`
(the `dash.*`, `media.*` and `radio.*` methods above), signal `StateChanged(a{sv})`.
