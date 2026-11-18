#include "tpms.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDebug>
#include <QtEndian>

namespace {
const char *kBluez = "org.bluez";
const char *kAdapter = "org.bluez.Adapter1";
const char *kDevice = "org.bluez.Device1";
const char *kProperties = "org.freedesktop.DBus.Properties";

QVariantMap to_map(const QVariant &v)
{
    if (v.userType() == qMetaTypeId<QDBusArgument>()) {
        QVariantMap m;
        v.value<QDBusArgument>() >> m;
        return m;
    }
    return v.toMap();
}

// ManufacturerData: a{qv}, each value a byte array
QMap<quint16, QByteArray> manufacturer_data(const QVariant &v)
{
    QMap<quint16, QByteArray> out;
    if (v.userType() != qMetaTypeId<QDBusArgument>())
        return out;
    const QDBusArgument arg = v.value<QDBusArgument>();
    arg.beginMap();
    while (!arg.atEnd()) {
        quint16 company = 0;
        QDBusVariant value;
        arg.beginMapEntry();
        arg >> company >> value;
        arg.endMapEntry();
        out[company] = value.variant().toByteArray();
    }
    arg.endMap();
    return out;
}
}  // namespace

BleTpms::BleTpms(QObject *parent)
    : QObject(parent)
{
    watchdog.setInterval(15000);
    connect(&watchdog, &QTimer::timeout, this, &BleTpms::keep_alive);
}

bool BleTpms::decode(quint16 company, const QByteArray &data, double &kpa, double &temp_c, int &battery, bool &alarm)
{
    if (company != 0x0100 || data.size() != 16)
        return false;
    const auto *d = reinterpret_cast<const uchar *>(data.constData());
    const quint32 pa = qFromLittleEndian<quint32>(d + 6);
    const qint32 centi = qFromLittleEndian<qint32>(d + 10);
    kpa = pa / 1000.0;
    temp_c = centi / 100.0;
    battery = d[14];
    alarm = d[15] != 0;
    // Junk that happens to share the company id: implausible numbers
    return kpa >= 0 && kpa < 1500 && temp_c > -50 && temp_c < 150 && battery <= 100;
}

void BleTpms::start()
{
    if (running_)
        return;
    running_ = true;
    QDBusConnection bus = QDBusConnection::systemBus();
    bus.connect(kBluez, "/", "org.freedesktop.DBus.ObjectManager", "InterfacesAdded", this,
                SLOT(interfaces_added(QDBusMessage)));
    // Every device's property changes (empty path: any path)
    bus.connect(kBluez, QString(), kProperties, "PropertiesChanged", this, SLOT(properties_changed(QDBusMessage)));
    find_adapter();
    watchdog.start();
}

void BleTpms::stop()
{
    if (!running_)
        return;
    running_ = false;
    watchdog.stop();
    QDBusConnection bus = QDBusConnection::systemBus();
    bus.disconnect(kBluez, "/", "org.freedesktop.DBus.ObjectManager", "InterfacesAdded", this,
                   SLOT(interfaces_added(QDBusMessage)));
    bus.disconnect(kBluez, QString(), kProperties, "PropertiesChanged", this, SLOT(properties_changed(QDBusMessage)));
    if (!adapter.isEmpty())
        bus.asyncCall(QDBusMessage::createMethodCall(kBluez, adapter, kAdapter, "StopDiscovery"));
    set_message("Tire sensors off");
}

void BleTpms::set_message(const QString &text)
{
    if (text == message_)
        return;
    message_ = text;
    qInfo().noquote() << "[tpms]" << text;
    emit message_changed(text);
}

void BleTpms::find_adapter()
{
    auto call = QDBusConnection::systemBus().asyncCall(
        QDBusMessage::createMethodCall(kBluez, "/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects"));
    auto watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusMessage reply = w->reply();
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
            set_message("Bluetooth isn't running");
            return;
        }
        // a{oa{sa{sv}}}: every object, its interfaces and their properties
        const QDBusArgument arg = reply.arguments().first().value<QDBusArgument>();
        arg.beginMap();
        while (!arg.atEnd()) {
            QDBusObjectPath path;
            arg.beginMapEntry();
            arg >> path;
            arg.beginMap();
            while (!arg.atEnd()) {
                QString iface;
                QVariantMap props;
                arg.beginMapEntry();
                arg >> iface >> props;
                arg.endMapEntry();
                if (iface == kAdapter && adapter.isEmpty())
                    adapter = path.path();
                else if (iface == kDevice)
                    device_properties(path.path(), props);
            }
            arg.endMap();
            arg.endMapEntry();
        }
        arg.endMap();
        if (adapter.isEmpty()) {
            set_message("No Bluetooth adapter");
            return;
        }
        start_discovery();
    });
}

void BleTpms::start_discovery()
{
    if (!running_ || adapter.isEmpty())
        return;
    QDBusConnection bus = QDBusConnection::systemBus();
    // LE only, and every advertisement (not just the first from each sensor)
    QDBusMessage filter = QDBusMessage::createMethodCall(kBluez, adapter, kAdapter, "SetDiscoveryFilter");
    filter << QVariantMap {{"Transport", "le"}, {"DuplicateData", true}};
    bus.call(filter, QDBus::Block, 2000);
    const QDBusMessage reply =
        bus.call(QDBusMessage::createMethodCall(kBluez, adapter, kAdapter, "StartDiscovery"), QDBus::Block, 3000);
    if (reply.type() == QDBusMessage::ErrorMessage && !reply.errorName().endsWith("InProgress")) {
        set_message(QString("Can't scan for tire sensors: %1").arg(reply.errorMessage()));
        return;
    }
    set_message("Listening for tire sensors");
}

// Bluetooth restarted, the adapter was switched off and on, ...: scan again
void BleTpms::keep_alive()
{
    if (adapter.isEmpty()) {
        find_adapter();
        return;
    }
    QDBusMessage get = QDBusMessage::createMethodCall(kBluez, adapter, kProperties, "Get");
    get << kAdapter << "Discovering";
    const QDBusMessage reply = QDBusConnection::systemBus().call(get, QDBus::Block, 2000);
    if (reply.type() != QDBusMessage::ReplyMessage) {
        adapter.clear();
        find_adapter();
        return;
    }
    if (!reply.arguments().value(0).value<QDBusVariant>().variant().toBool())
        start_discovery();
}

void BleTpms::interfaces_added(const QDBusMessage &message)
{
    const QList<QVariant> args = message.arguments();
    if (args.size() < 2)
        return;
    const QString path = args[0].value<QDBusObjectPath>().path();
    const QDBusArgument arg = args[1].value<QDBusArgument>();
    arg.beginMap();
    while (!arg.atEnd()) {
        QString iface;
        QVariantMap props;
        arg.beginMapEntry();
        arg >> iface >> props;
        arg.endMapEntry();
        if (iface == kDevice)
            device_properties(path, props);
        else if (iface == kAdapter && adapter.isEmpty()) {
            adapter = path;
            start_discovery();
        }
    }
    arg.endMap();
}

void BleTpms::properties_changed(const QDBusMessage &message)
{
    const QList<QVariant> args = message.arguments();
    if (args.size() < 2 || args[0].toString() != kDevice)
        return;
    device_properties(message.path(), to_map(args[1]));
}

void BleTpms::device_properties(const QString &path, const QVariantMap &props)
{
    if (props.contains("Name"))
        names[path] = props.value("Name").toString();
    else if (props.contains("Alias") && !names.contains(path))
        names[path] = props.value("Alias").toString();
    if (props.contains("RSSI"))
        rssi[path] = props.value("RSSI").toInt();
    if (!props.contains("ManufacturerData"))
        return;

    const auto data = manufacturer_data(props.value("ManufacturerData"));
    for (auto it = data.begin(); it != data.end(); ++it) {
        double kpa, temp_c;
        int battery;
        bool alarm;
        if (!decode(it.key(), it.value(), kpa, temp_c, battery, alarm))
            continue;
        // /org/bluez/hci0/dev_80_EA_CA_10_8A_78 -> 80:EA:CA:10:8A:78
        const QString id = path.section('/', -1).mid(4).replace('_', ':');
        emit reading(id, names.value(path), kpa, temp_c, battery, alarm, rssi.value(path, 0));
    }
}
