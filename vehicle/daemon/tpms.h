// Bluetooth LE tire pressure sensors (the common valve-cap and internal
// kind sold as "BLE TPMS"), read from their advertisements through BlueZ on
// the system D-Bus: no connection, nothing to pair.
//
// Format: manufacturer data, company id 0x0100, 16 bytes: the sensor's
// address (6), pressure in Pa (uint32 LE), temperature in 0.01 °C (int32 LE),
// battery % (1), alarm (1). Their names look like "TPMS1_10CAB4".
#pragma once

#include <QDBusMessage>
#include <QObject>
#include <QTimer>
#include <QVariantMap>

class BleTpms : public QObject {
    Q_OBJECT

   public:
    explicit BleTpms(QObject *parent = nullptr);

    void start();  // scan for LE advertisements (shared with other BlueZ clients)
    void stop();
    bool running() const { return running_; }
    QString message() const { return message_; }

    // Decode one advertisement's manufacturer data; false if it isn't a sensor
    static bool decode(quint16 company, const QByteArray &data, double &kpa, double &temp_c, int &battery,
                       bool &alarm);

   signals:
    // id: the sensor's Bluetooth address
    void reading(const QString &id, const QString &name, double kpa, double temp_c, int battery, bool alarm, int rssi);
    void message_changed(const QString &message);

   private slots:
    void interfaces_added(const QDBusMessage &message);
    void properties_changed(const QDBusMessage &message);

   private:
    void find_adapter();
    void start_discovery();
    void keep_alive();
    void device_properties(const QString &path, const QVariantMap &props);
    void set_message(const QString &text);

    bool running_ = false;
    QString adapter;  // "/org/bluez/hci0"
    QString message_;
    QTimer watchdog;
    QMap<QString, QString> names;  // device path -> name
    QMap<QString, int> rssi;       // device path -> last RSSI
};
