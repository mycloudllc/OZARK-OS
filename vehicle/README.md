# ozark-vehicled

Everything about the car, on the system D-Bus as **`org.ozark.Vehicle1`** (object `/org/ozark/Vehicle1`):

```
OBD port ── ELM327 (USB serial or Bluetooth RFCOMM) ─┐
         └─ CAN transceiver (SocketCAN, can0) ───────┤
Headlight/dash-light wire ── optocoupler ── GPIO ────┼── ozark-vehicled ── D-Bus ── Dash: Vehicle page,
Bluetooth LE tire sensors (advertisements) ──────────┤                                Settings > Vehicle
TrailCommand hub: TireCommander tires (GET /api/tires)┘        └── POST /api/vehicle ── TrailCommand hub
```

Qt Core, DBus and Network only: serial, CAN, RFCOMM and GPIO go through the kernel directly (termios, `AF_CAN`,
`AF_BLUETOOTH`, the GPIO character device v2), BLE through BlueZ's D-Bus API. Settings live in
`/etc/ozark/vehicle.conf` (the data partition, so they survive updates).

## OBD-II

- **ELM327:** tries 38400, 115200, 9600, 230400, 500000 and 57600 baud, then `ATWS ATE0 ATL0 ATS1 ATH1 ATAT1 ATSP0`
  and `0100` (the car picks the protocol). Headers stay on so replies from several ECUs and multi-frame CAN messages
  can be told apart; ISO-TP is reassembled here. K-line and J1850 cars work too (3-byte headers).
- **CAN:** sets the interface's bit rate (`ip link`, 500 kbit/s by default, 0 leaves it alone), asks `0100` with
  11-bit ids (0x7DF) then 29-bit (0x18DB33F1), and does ISO-TP itself, flow control included.
- Finds the supported PIDs, then polls speed, rpm, throttle and load every cycle and the rest (coolant, intake,
  oil, outside, fuel level, module voltage, MAP, MAF, barometric pressure, fuel rate, run time, odometer, MIL)
  in turn. No answers for a while: "ignition off", and it asks `0100` every 4 s until the car wakes.
- **Trouble codes:** `01 01` (MIL, count), `03` stored, `07` pending, `0A` permanent, with names for the common
  generic codes. **Clear:** `04`; "conditions not correct" (engine running) is reported as such.
- VIN from `09 02`.

## Illumination

A GPIO line (`gpiochipN:line`; the settings list them by Rockchip name, e.g. `GPIO3_C1`), active high or low,
optional pull-up or pull-down, debounced. Dash switches to dark mode and/or night brightness while it's on.

## Tire sensors

- **BLE:** the common "BLE TPMS" valve-cap and internal sensors: manufacturer data, company id 0x0100, 16 bytes
  (address, pressure in Pa, temperature in 0.01 °C, battery %, alarm). Sensors heard nearby are listed; each is put
  on a wheel (`front_left`, `front_right`, `rear_left`, `rear_right`, `spare`, `trailer_left`, `trailer_right`).
  Needs Bluetooth LE on (`ControllerMode = dual`); phones stay on classic Bluetooth (BlueZ patch).
- **Hub:** the TrailCommand hub's own sensors, from `GET /api/tires` (its `source: "radio"` entries, ours sent back,
  are skipped). A BLE sensor on the same wheel wins.

## TrailCommand hub

`POST /api/vehicle` every 2 s when something changed, every 15 s otherwise: OBD state, VIN, values, trouble codes,
the headlights and the BLE tires. The hub address and PIN are the ones Dash's TrailCommand tab uses
(`TrailCommand/*` in Dash's settings), unless `hub_url` / `hub_pin` are set.

## D-Bus API

| Method | |
|---|---|
| `GetStatus() → a{sv}` | `obd` {state: off, connecting, no-car, connected, error; link, protocol, vin, message, ecus, supported}, `illumination` {line, ok, on, message}, `tpms` {ble, hub, message, seen}, `hub` {state: off, ok, no-hub, unreachable, pin; message, url, forwarding, last_sent} |
| `GetValues() → a{sv}` | SI units: `rpm`, `speed_kmh`, `coolant_c`, `intake_c`, `oil_c`, `ambient_c`, `load_pct`, `throttle_pct`, `fuel_pct`, `voltage_v`, `map_kpa`, `maf_gs`, `baro_kpa`, `fuel_rate_lph`, `run_time_s`, `odometer_km`, `mil`, `dtc_count` (what the car supports) |
| `ReadDtcs()`, `GetDtcs() → a{sv}` | Answer in `DtcsRead`: {ok, message, mil, count, stored/pending/permanent: [{code, description, ecu}], read_at} |
| `ClearDtcs()` | Answer in `DtcsCleared(b ok, s message)`, then a fresh read |
| `GetConfig() → a{sv}`, `SetConfig(a{sv})` | `obd_link` (off, elm327-usb, elm327-bt, can), `obd_serial`, `obd_bt_address`, `can_interface`, `can_bitrate`, `illum_line`, `illum_active_low`, `illum_bias` (none, pull-up, pull-down), `tpms_ble`, `tpms_hub`, `tpms_low_kpa`, `tpms_high_c`, `tpms_sensors` {position: address}, `hub_forward`, `hub_url`, `hub_pin` (write only; `hub_pin_set` reads back) |
| `GetSerialPorts()`, `GetCanInterfaces()`, `GetBluetoothDevices()`, `GetGpioLines()` | Choices for the settings page |
| `GetTires() → a{sv}` | Per position: {pressure_kpa, temp_c, battery, alarm, low, hot, age_s, source: ble or hub, sensor} |
| `GetTpmsSensors() → av`, `AssignTpmsSensor(s position, s address)` | BLE sensors heard lately, and putting one on a wheel |

Signals: `StatusChanged`, `ValuesChanged` (at most 4 a second), `DtcsRead`, `DtcsCleared`, `TiresChanged` (at most once
a second), `IlluminationChanged(b)`, `ConfigChanged`.

## Testing on a PC

```sh
cmake -S daemon -B build -DVEHICLED_TESTS=ON && make -C build
build/vehicled-tests                      # fake ELM327 on a pty: connect, values, VIN, codes, clear (25 checks)
python3 daemon/tests/fake_elm.py          # prints a pty; point obd_serial at it
OZARK_VEHICLE_CONF=./vehicle.conf build/ozark-vehicled   # with DBUS_SYSTEM_BUS_ADDRESS at a private bus
```

Not tested on hardware yet: a real ELM327, SocketCAN (needs a CAN interface; `vcan` works for wiring checks),
the GPIO input, and real BLE sensors.
