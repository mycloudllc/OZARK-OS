// ozark-remoted: control the radio from a phone app, over Wi-Fi or Bluetooth LE.
// See ../PROTOCOL.md.
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusError>
#include <QDebug>

#include "ble.h"
#include "remoted.h"
#include "ws.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    qSetMessagePattern("%{message}");

    // Phones allowed in: the data partition, so pairing survives updates
    const QString store = qEnvironmentVariable("OZARK_REMOTE_CLIENTS", "/etc/ozark/remote/clients.json");
    Remote remote(store);

    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.registerObject("/org/ozark/Remote1", &remote, QDBusConnection::ExportScriptableContents)) {
        qCritical() << "Can't register /org/ozark/Remote1";
        return 1;
    }
    if (!bus.registerService("org.ozark.Remote1")) {
        qCritical() << "Can't own org.ozark.Remote1:" << bus.lastError().message();
        return 1;
    }

    WsTransport wifi(remote);
    wifi.start();
    BleTransport ble(remote);
    if (qEnvironmentVariable("OZARK_REMOTE_BLE", "1") != "0")
        ble.start();

    return app.exec();
}
