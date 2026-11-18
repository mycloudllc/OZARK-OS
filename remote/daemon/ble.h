#pragma once

#include <QDBusObjectPath>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantMap>

class Remote;
class BleSession;

// Phones over Bluetooth LE, which iPhones and Android phones both offer to
// apps. A GATT service hosted by BlueZ (org.bluez.GattManager1) with two
// characteristics, and an advertisement so the app finds the radio:
//   service  7a6f0001-5a3b-4c1e-9f2d-0c8e4b1a6d20
//   rx       7a6f0002-...  the phone writes requests (write, write without response)
//   tx       7a6f0003-...  the radio notifies replies and events
// Each JSON message ends with a newline and is cut into pieces that fit the
// link (MTU - 3 bytes); the other side joins them up to the newline.
// One phone at a time: notifications reach every phone listening.
class BleTransport : public QObject {
    Q_OBJECT

   public:
    static const QString kService, kRx, kTx;

    BleTransport(Remote &remote, QObject *parent = nullptr);
    void start();

    // From the D-Bus objects
    void write(const QByteArray &value, const QVariantMap &options);
    void notifying(bool on);
    void send(const QByteArray &message);

   private:
    Remote &remote;
    QString adapter;
    QPointer<BleSession> session;
    QString device;  // the phone's D-Bus path
    QByteArray buffer;
    int mtu = 23;
    bool registered = false;
    QTimer retry;

    void register_all();
    void unregister_all();
    void end_session();

   private slots:
    void device_changed(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);
};
