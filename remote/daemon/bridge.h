#pragma once

#include <functional>

#include <QDBusMessage>
#include <QObject>
#include <QVariantMap>

class QDBusServiceWatcher;

// The radio's other services, as the remote protocol's methods and events:
//   Dash (org.ozark.Dash1)            media, radio, volume, brightness, theme
//   ozark-lightd (org.ozark.Lighting1) the light strips
//   ozark-vehicled (org.ozark.Vehicle1) OBD-II values, trouble codes, tires
//   ozark-net, ozark-update, systemctl  network, updates, reboot
// Keeps the latest state of each so a phone gets everything at once.
class Bridge : public QObject {
    Q_OBJECT

   public:
    using Done = std::function<void(const QVariant &result)>;
    using Fail = std::function<void(const QString &code, const QString &message)>;

    explicit Bridge(QObject *parent = nullptr);

    // Runs a protocol method (one of methods()); answers through done or fail, once
    void call(const QString &method, const QVariantMap &params, Done done, Fail fail);
    static const QStringList &methods();

    QVariantMap snapshot() const;

    // D-Bus replies hold nested maps and lists as QDBusArgument: make them plain
    static QVariant unwrap(const QVariant &value);

   signals:
    void event(const QString &name, const QVariant &data);

   private slots:
    void on_dash_state(const QDBusMessage &message);
    void on_light_status(const QDBusMessage &message);
    void on_light_look(const QDBusMessage &message);
    void on_vehicle_status(const QDBusMessage &message);
    void on_vehicle_values(const QDBusMessage &message);
    void on_vehicle_tires(const QDBusMessage &message);
    void on_vehicle_dtcs(const QDBusMessage &message);
    void on_vehicle_cleared(bool ok, const QString &message);

   private:
    QVariantMap dash, light_status, vehicle_status, vehicle_values, vehicle_tires, vehicle_dtcs;
    QVariantMap looks;  // zone -> look
    qint64 values_sent = 0;
    QDBusServiceWatcher *watcher;

    void refresh(const QString &service);
    void dbus(const QString &service, const QString &path, const QString &interface, const QString &method,
              const QVariantList &args, std::function<void(const QVariantList &)> done, Fail fail);
    void run(const QString &program, const QStringList &args, Done done, Fail fail, bool key_values);
};
