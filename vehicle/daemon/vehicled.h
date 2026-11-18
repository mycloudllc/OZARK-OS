// ozark-vehicled: everything about the car, on the system D-Bus as
// org.ozark.Vehicle1 (see ../README.md): OBD-II live data and trouble codes,
// the illumination (headlights) input, tire pressures, and forwarding all of
// it to a TrailCommand hub.
#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QSettings>
#include <QTimer>
#include <QVariantMap>

#include "gpio.h"
#include "hub.h"
#include "obd.h"
#include "tpms.h"

class Vehicle : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.ozark.Vehicle1")

   public:
    explicit Vehicle(QObject *parent = nullptr);
    bool start();  // register on the system bus and start what's configured

   public slots:
    // {obd: {state: off|connecting|no-car|connected|error, link, protocol, vin, message, ecus, supported},
    //  illumination: {line, on, ok, message}, tpms: {ble, hub, message, seen},
    //  hub: {state: off|ok|no-hub|unreachable|pin, message, url, forwarding, last_sent}}
    QVariantMap GetStatus() const;
    // Latest live values, SI units: rpm, speed_kmh, coolant_c, intake_c, oil_c, ambient_c,
    // load_pct, throttle_pct, fuel_pct, voltage_v, map_kpa, maf_gs, baro_kpa, fuel_rate_lph,
    // run_time_s, odometer_km, mil, dtc_count (only what the car supports)
    QVariantMap GetValues() const;
    // The last ReadDtcs result
    QVariantMap GetDtcs() const;
    // Read trouble codes; answer in DtcsRead:
    // {ok, message, mil, count, stored: [{code, description, ecu}], pending: [...],
    //  permanent: [...], read_at}
    void ReadDtcs();
    // Clear stored and pending codes (turns the check-engine light off); answer in DtcsCleared
    void ClearDtcs();
    // Everything in the settings file; SetConfig takes any subset (see README)
    QVariantMap GetConfig() const;
    void SetConfig(const QVariantMap &changes);
    // Choices for the settings page
    QStringList GetSerialPorts() const;     // /dev/serial/by-id/... then /dev/tty{USB,ACM}N
    QStringList GetCanInterfaces() const;   // can0, vcan0, ...
    QStringList GetBluetoothDevices() const;  // paired: "AA:BB:CC:DD:EE:FF name"
    QVariantList GetGpioLines() const;      // [{id, label, used, consumer}]
    // Per position (front_left, front_right, rear_left, rear_right, spare, trailer_left,
    // trailer_right): {pressure_kpa, temp_c, battery, alarm, low, hot, age_s, source: ble|hub, sensor}
    QVariantMap GetTires() const;
    // BLE sensors heard lately: [{id, name, pressure_kpa, temp_c, battery, alarm, rssi, age_s, position}]
    QVariantList GetTpmsSensors() const;
    // Put a sensor on a wheel ("" to take that wheel's sensor off)
    void AssignTpmsSensor(const QString &position, const QString &id);

   signals:
    void StatusChanged(const QVariantMap &status);
    void ValuesChanged(const QVariantMap &values);
    void DtcsRead(const QVariantMap &result);
    void DtcsCleared(bool ok, const QString &message);
    void TiresChanged(const QVariantMap &tires);
    void IlluminationChanged(bool on);
    void ConfigChanged(const QVariantMap &config);

   private:
    void apply_obd();
    void apply_illumination();
    void apply_tpms();
    void apply_hub();
    void status_changed();
    void tires_changed();
    QJsonObject hub_snapshot() const;
    HubLink::Hub hub_address() const;
    QVariant setting(const QString &key) const;

    QSettings settings;
    Obd obd;
    GpioInput illumination;
    QString illumination_message;
    BleTpms tpms;
    HubLink hub;

    struct Sensor {
        QString name;
        double kpa = 0, temp_c = 0;
        int battery = 0, rssi = 0;
        bool alarm = false;
        QDateTime seen;
    };
    QMap<QString, Sensor> sensors;  // by Bluetooth address
    QTimer tires_timer;             // TiresChanged at most once a second
    bool tires_dirty = false;
    QTimer status_timer;            // StatusChanged batched
};
