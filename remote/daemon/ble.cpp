#include "ble.h"

#include <QDBusAbstractAdaptor>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QDebug>

#include "bridge.h"
#include "remoted.h"

const QString BleTransport::kService = "7a6f0001-5a3b-4c1e-9f2d-0c8e4b1a6d20";
const QString BleTransport::kRx = "7a6f0002-5a3b-4c1e-9f2d-0c8e4b1a6d20";
const QString BleTransport::kTx = "7a6f0003-5a3b-4c1e-9f2d-0c8e4b1a6d20";

namespace {
const QString kRoot = "/org/ozark/remote";
const QString kServicePath = kRoot + "/service0";
const QString kRxPath = kServicePath + "/rx";
const QString kTxPath = kServicePath + "/tx";
const QString kAdvPath = kRoot + "/advertisement0";
const QString kGattService = "org.bluez.GattService1";
const QString kGattChar = "org.bluez.GattCharacteristic1";
const QString kAdvIface = "org.bluez.LEAdvertisement1";
constexpr int kMaxBuffer = 64 * 1024;

using InterfaceMap = QMap<QString, QVariantMap>;

// a{oa{sa{sv}}}, as org.freedesktop.DBus.ObjectManager.GetManagedObjects answers
struct ManagedObjects {
    QList<QPair<QDBusObjectPath, InterfaceMap>> objects;
};
}  // namespace

Q_DECLARE_METATYPE(InterfaceMap)
Q_DECLARE_METATYPE(ManagedObjects)

QDBusArgument &operator<<(QDBusArgument &arg, const ManagedObjects &m)
{
    arg.beginMap(qMetaTypeId<QDBusObjectPath>(), qMetaTypeId<InterfaceMap>());
    for (const auto &o : m.objects) {
        arg.beginMapEntry();
        arg << o.first << o.second;
        arg.endMapEntry();
    }
    arg.endMap();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, ManagedObjects &)
{
    return arg;  // only ever sent
}

namespace {
QVariantMap service_props()
{
    return {{"UUID", BleTransport::kService}, {"Primary", true}};
}
QVariantMap rx_props()
{
    return {{"UUID", BleTransport::kRx},
            {"Service", QVariant::fromValue(QDBusObjectPath(kServicePath))},
            {"Flags", QStringList{"write", "write-without-response"}}};
}
QVariantMap tx_props(bool notifying)
{
    return {{"UUID", BleTransport::kTx},
            {"Service", QVariant::fromValue(QDBusObjectPath(kServicePath))},
            {"Flags", QStringList{"notify", "read"}},
            {"Notifying", notifying}};
}

// ---- The D-Bus objects BlueZ talks to

class ObjectManagerAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.DBus.ObjectManager")

   public:
    explicit ObjectManagerAdaptor(QObject *parent) : QDBusAbstractAdaptor(parent) {}
    bool tx_notifying = false;

   public slots:
    ManagedObjects GetManagedObjects()
    {
        ManagedObjects m;
        m.objects << qMakePair(QDBusObjectPath(kServicePath), InterfaceMap{{kGattService, service_props()}});
        m.objects << qMakePair(QDBusObjectPath(kRxPath), InterfaceMap{{kGattChar, rx_props()}});
        m.objects << qMakePair(QDBusObjectPath(kTxPath), InterfaceMap{{kGattChar, tx_props(this->tx_notifying)}});
        return m;
    }
};

class ServiceAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.bluez.GattService1")
    Q_PROPERTY(QString UUID READ uuid)
    Q_PROPERTY(bool Primary READ primary)

   public:
    explicit ServiceAdaptor(QObject *parent) : QDBusAbstractAdaptor(parent) {}
    QString uuid() const { return BleTransport::kService; }
    bool primary() const { return true; }
};

class RxAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.bluez.GattCharacteristic1")
    Q_PROPERTY(QString UUID READ uuid)
    Q_PROPERTY(QDBusObjectPath Service READ service)
    Q_PROPERTY(QStringList Flags READ flags)

   public:
    RxAdaptor(QObject *parent, BleTransport *transport) : QDBusAbstractAdaptor(parent), transport(transport) {}
    QString uuid() const { return BleTransport::kRx; }
    QDBusObjectPath service() const { return QDBusObjectPath(kServicePath); }
    QStringList flags() const { return {"write", "write-without-response"}; }

   public slots:
    void WriteValue(const QByteArray &value, const QVariantMap &options) { this->transport->write(value, options); }
    QByteArray ReadValue(const QVariantMap &) { return QByteArray(); }

   private:
    BleTransport *transport;
};

class TxAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.bluez.GattCharacteristic1")
    Q_PROPERTY(QString UUID READ uuid)
    Q_PROPERTY(QDBusObjectPath Service READ service)
    Q_PROPERTY(QStringList Flags READ flags)
    Q_PROPERTY(bool Notifying READ notifying)

   public:
    TxAdaptor(QObject *parent, BleTransport *transport, ObjectManagerAdaptor *manager)
        : QDBusAbstractAdaptor(parent), transport(transport), manager(manager) {}
    QString uuid() const { return BleTransport::kTx; }
    QDBusObjectPath service() const { return QDBusObjectPath(kServicePath); }
    QStringList flags() const { return {"notify", "read"}; }
    bool notifying() const { return this->manager->tx_notifying; }

   public slots:
    QByteArray ReadValue(const QVariantMap &) { return QByteArray(); }
    void StartNotify()
    {
        this->manager->tx_notifying = true;
        this->transport->notifying(true);
    }
    void StopNotify()
    {
        this->manager->tx_notifying = false;
        this->transport->notifying(false);
    }

   private:
    BleTransport *transport;
    ObjectManagerAdaptor *manager;
};

class AdvertisementAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.bluez.LEAdvertisement1")
    Q_PROPERTY(QString Type READ type)
    Q_PROPERTY(QStringList ServiceUUIDs READ uuids)
    Q_PROPERTY(QString LocalName READ name)
    Q_PROPERTY(bool Discoverable READ discoverable)

   public:
    explicit AdvertisementAdaptor(QObject *parent) : QDBusAbstractAdaptor(parent) {}
    QString type() const { return "peripheral"; }
    QStringList uuids() const { return {BleTransport::kService}; }
    // Short: a 128-bit service UUID leaves little room in the 31-byte advert
    QString name() const { return "Ozark"; }
    bool discoverable() const { return true; }

   public slots:
    void Release() { qInfo() << "[ble] BlueZ released the advertisement"; }
};
}  // namespace

// ---- One phone

class BleSession : public Session {
   public:
    BleSession(BleTransport *transport, const QString &address, QObject *parent)
        : Session("ble", address, parent), transport(transport) {}
    void send(const QByteArray &message) override { this->transport->send(message); }
    void close() override { emit closed(); }
    void deliver(const QByteArray &message) { emit received(message); }

   private:
    BleTransport *transport;
};

// ---- Transport

BleTransport::BleTransport(Remote &remote, QObject *parent)
    : QObject(parent)
    , remote(remote)
{
    qDBusRegisterMetaType<InterfaceMap>();
    qDBusRegisterMetaType<ManagedObjects>();

    QDBusConnection bus = QDBusConnection::systemBus();
    auto root = new QObject(this);
    auto manager = new ObjectManagerAdaptor(root);
    bus.registerObject(kRoot, root);
    auto service = new QObject(this);
    new ServiceAdaptor(service);
    bus.registerObject(kServicePath, service);
    auto rx = new QObject(this);
    new RxAdaptor(rx, this);
    bus.registerObject(kRxPath, rx);
    auto tx = new QObject(this);
    new TxAdaptor(tx, this, manager);
    bus.registerObject(kTxPath, tx);
    auto adv = new QObject(this);
    new AdvertisementAdaptor(adv);
    bus.registerObject(kAdvPath, adv);

    // Register again whenever bluetoothd (re)starts or an adapter appears
    auto watcher = new QDBusServiceWatcher("org.bluez", bus, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this, [this](const QString &, const QString &, const QString &owner) {
        this->registered = false;
        this->end_session();
        if (!owner.isEmpty())
            QTimer::singleShot(1000, this, &BleTransport::register_all);
        else
            this->remote.set_transport_state("ble", "waiting for Bluetooth");
    });
    this->retry.setInterval(10000);
    connect(&this->retry, &QTimer::timeout, this, &BleTransport::register_all);
}

void BleTransport::start()
{
    this->register_all();
}

void BleTransport::register_all()
{
    if (this->registered)
        return;
    QDBusConnection bus = QDBusConnection::systemBus();

    // The first adapter that can host GATT services and advertise
    QDBusMessage call = QDBusMessage::createMethodCall("org.bluez", "/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
    const QDBusMessage reply = bus.call(call, QDBus::Block, 5000);
    this->adapter.clear();
    if (reply.type() == QDBusMessage::ReplyMessage) {
        const QVariantMap objects = Bridge::unwrap(reply.arguments().value(0)).toMap();
        for (auto it = objects.begin(); it != objects.end(); ++it) {
            const QVariantMap ifaces = it.value().toMap();
            if (ifaces.contains("org.bluez.GattManager1") && ifaces.contains("org.bluez.LEAdvertisingManager1")) {
                this->adapter = it.key();
                break;
            }
        }
    }
    if (this->adapter.isEmpty()) {
        this->remote.set_transport_state("ble", "no Bluetooth LE adapter");
        this->retry.start();
        return;
    }

    QDBusMessage app = QDBusMessage::createMethodCall("org.bluez", this->adapter, "org.bluez.GattManager1", "RegisterApplication");
    app << QVariant::fromValue(QDBusObjectPath(kRoot)) << QVariantMap();
    auto w1 = new QDBusPendingCallWatcher(bus.asyncCall(app, 10000), this);
    connect(w1, &QDBusPendingCallWatcher::finished, this, [this, bus](QDBusPendingCallWatcher *w) mutable {
        w->deleteLater();
        if (w->isError()) {
            this->remote.set_transport_state("ble", "GATT registration failed: " + w->error().message());
            this->retry.start();
            return;
        }
        QDBusMessage adv = QDBusMessage::createMethodCall("org.bluez", this->adapter, "org.bluez.LEAdvertisingManager1", "RegisterAdvertisement");
        adv << QVariant::fromValue(QDBusObjectPath(kAdvPath)) << QVariantMap();
        auto w2 = new QDBusPendingCallWatcher(bus.asyncCall(adv, 10000), this);
        connect(w2, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
            w->deleteLater();
            this->registered = true;
            this->retry.stop();
            if (w->isError())
                // Still usable by a phone that already knows the radio; say why it can't be found
                this->remote.set_transport_state("ble", "ready, not advertising: " + w->error().message());
            else
                this->remote.set_transport_state("ble", "advertising on " + this->adapter.section('/', -1));
        });
    });
}

void BleTransport::write(const QByteArray &value, const QVariantMap &options)
{
    const QString device = options.value("device").value<QDBusObjectPath>().path();
    const int mtu = options.value("mtu").toInt();
    if (!this->device.isEmpty() && !device.isEmpty() && device != this->device) {
        qInfo().noquote() << "[ble] Ignored" << device << ": another phone is connected";
        return;
    }
    if (!this->session) {
        this->device = device;
        this->buffer.clear();
        // /org/bluez/hci0/dev_AA_BB_.. -> AA:BB:..
        const QString address = device.section('/', -1).mid(4).replace('_', ':');
        this->session = new BleSession(this, address.isEmpty() ? QString("unknown") : address, &this->remote);
        if (!device.isEmpty())
            QDBusConnection::systemBus().connect("org.bluez", device, "org.freedesktop.DBus.Properties", "PropertiesChanged",
                                                 this, SLOT(device_changed(QString, QVariantMap, QStringList)));
        this->remote.add(this->session);
    }
    if (mtu > 0)
        this->mtu = mtu;

    this->buffer += value;
    if (this->buffer.size() > kMaxBuffer) {
        qInfo() << "[ble] Message too long, dropped";
        this->buffer.clear();
        return;
    }
    int end;
    while ((end = this->buffer.indexOf('\n')) >= 0) {
        const QByteArray message = this->buffer.left(end).trimmed();
        this->buffer.remove(0, end + 1);
        if (!message.isEmpty() && this->session)
            this->session->deliver(message);
    }
}

void BleTransport::notifying(bool on)
{
    qInfo() << "[ble] Notifications" << (on ? "on" : "off");
}

void BleTransport::send(const QByteArray &message)
{
    QByteArray data = message;
    data += '\n';
    const int piece = std::max(20, std::min(this->mtu - 3, 512));
    QDBusConnection bus = QDBusConnection::systemBus();
    for (int i = 0; i < data.size(); i += piece) {
        QDBusMessage signal = QDBusMessage::createSignal(kTxPath, "org.freedesktop.DBus.Properties", "PropertiesChanged");
        signal << kGattChar << QVariantMap{{"Value", data.mid(i, piece)}} << QStringList();
        bus.send(signal);
    }
}

void BleTransport::device_changed(const QString &interface, const QVariantMap &changed, const QStringList &)
{
    if (interface == "org.bluez.Device1" && changed.contains("Connected") && !changed.value("Connected").toBool())
        this->end_session();
}

void BleTransport::end_session()
{
    if (!this->device.isEmpty())
        QDBusConnection::systemBus().disconnect("org.bluez", this->device, "org.freedesktop.DBus.Properties", "PropertiesChanged",
                                                this, SLOT(device_changed(QString, QVariantMap, QStringList)));
    this->device.clear();
    this->buffer.clear();
    this->mtu = 23;
    if (this->session)
        this->session->close();
    this->session = nullptr;
}

void BleTransport::unregister_all()
{
    this->registered = false;
}

#include "ble.moc"
