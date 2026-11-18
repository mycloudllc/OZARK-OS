#include "vehicled.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QUrl>

#include "cantransport.h"
#include "elm327.h"

namespace {
const char *kService = "org.ozark.Vehicle1";
const char *kPath = "/org/ozark/Vehicle1";
// On the data partition, so it survives software updates
const char *kSettings = "/etc/ozark/vehicle.conf";
// Dash's settings: where its TrailCommand tab finds the hub
const char *kDashSettings = "/root/.config/openDsh/dash.conf";

const QStringList kPositions = {"front_left", "front_right", "rear_left", "rear_right",
                                "spare",      "trailer_left", "trailer_right"};

// Defaults for every setting (and the list of known keys)
const QVariantMap kDefaults = {
    {"obd_link", "off"},         // off | elm327-usb | elm327-bt | can
    {"obd_serial", ""},          // /dev/serial/by-id/... for elm327-usb
    {"obd_bt_address", ""},      // AA:BB:CC:DD:EE:FF for elm327-bt
    {"can_interface", "can0"},
    {"can_bitrate", 500000},     // 0: leave the interface as it is
    {"illum_line", ""},          // "gpiochip3:13", empty for none
    {"illum_active_low", false},
    {"illum_bias", "none"},      // none | pull-up | pull-down
    {"tpms_ble", false},
    {"tpms_hub", true},
    {"tpms_low_kpa", 200.0},     // about 29 psi
    {"tpms_high_c", 75.0},
    {"hub_forward", true},
    {"hub_url", ""},             // empty: the hub Dash's TrailCommand tab uses
    {"hub_pin", ""},
};

QVariantMap dbus_map(const QVariant &v)
{
    if (v.userType() == qMetaTypeId<QDBusArgument>()) {
        QVariantMap m;
        v.value<QDBusArgument>() >> m;
        return m;
    }
    return v.toMap();
}
}  // namespace

Vehicle::Vehicle(QObject *parent)
    : QObject(parent)
    // The environment may point elsewhere (tests on a PC)
    , settings(qEnvironmentVariable("OZARK_VEHICLE_CONF", kSettings), QSettings::IniFormat)
    , hub([this] { return this->hub_snapshot(); }, [this] { return this->hub_address(); }, this)
{
    connect(&obd, &Obd::status_changed, this, [this] {
        status_changed();
        hub.changed();
    });
    connect(&obd, &Obd::values_changed, this, [this](const QVariantMap &values) {
        emit ValuesChanged(values);
        hub.changed();
    });
    connect(&obd, &Obd::dtcs_read, this, [this](const QVariantMap &result) {
        emit DtcsRead(result);
        hub.changed();
    });
    connect(&obd, &Obd::dtcs_cleared, this, &Vehicle::DtcsCleared);

    connect(&illumination, &GpioInput::changed, this, [this](bool on) {
        qInfo() << "[illumination]" << (on ? "on" : "off");
        emit IlluminationChanged(on);
        status_changed();
        hub.changed();
    });

    connect(&tpms, &BleTpms::message_changed, this, [this] { status_changed(); });
    connect(&tpms, &BleTpms::reading, this,
            [this](const QString &id, const QString &name, double kpa, double temp_c, int battery, bool alarm, int rssi) {
                Sensor &s = sensors[id];
                if (s.seen.isValid() && qAbs(s.kpa - kpa) < 0.05 && qAbs(s.temp_c - temp_c) < 0.05 &&
                    s.alarm == alarm && s.seen.secsTo(QDateTime::currentDateTimeUtc()) < 30)
                    return;  // the same advertisement again
                s.name = name;
                s.kpa = kpa;
                s.temp_c = temp_c;
                s.battery = battery;
                s.alarm = alarm;
                s.rssi = rssi;
                s.seen = QDateTime::currentDateTimeUtc();
                tires_changed();
            });

    connect(&hub, &HubLink::status_changed, this, [this] { status_changed(); });
    connect(&hub, &HubLink::tires_changed, this, [this] { tires_changed(); });

    tires_timer.setInterval(1000);
    connect(&tires_timer, &QTimer::timeout, this, [this] {
        if (!tires_dirty)
            return;
        tires_dirty = false;
        emit TiresChanged(GetTires());
        hub.changed();
    });
    tires_timer.start();

    status_timer.setSingleShot(true);
    status_timer.setInterval(100);
    connect(&status_timer, &QTimer::timeout, this, [this] { emit StatusChanged(GetStatus()); });
}

bool Vehicle::start()
{
    QDBusConnection bus = QDBusConnection::systemBus();
    const auto options = QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals;
    if (!bus.registerObject(kPath, this, options) || !bus.registerService(kService))
        return false;
    apply_obd();
    apply_illumination();
    apply_tpms();
    apply_hub();
    return true;
}

QVariant Vehicle::setting(const QString &key) const
{
    return settings.value(key, kDefaults.value(key));
}

void Vehicle::status_changed()
{
    status_timer.start();
}

void Vehicle::tires_changed()
{
    tires_dirty = true;
}

/* ---------- settings ---------- */

QVariantMap Vehicle::GetConfig() const
{
    QVariantMap config;
    for (auto it = kDefaults.begin(); it != kDefaults.end(); ++it) {
        QVariant v = setting(it.key());
        // QSettings gives strings back from the file: put the types back
        if (it.value().type() == QVariant::Bool)
            v = v.toBool();
        else if (it.value().type() == QVariant::Int)
            v = v.toInt();
        else if (it.value().type() == QVariant::Double)
            v = v.toDouble();
        config[it.key()] = v;
    }
    config.remove("hub_pin");
    config["hub_pin_set"] = !setting("hub_pin").toString().isEmpty();
    QVariantMap assigned;
    for (const QString &pos : kPositions) {
        const QString id = settings.value("tpms_sensors/" + pos).toString();
        if (!id.isEmpty())
            assigned[pos] = id;
    }
    config["tpms_sensors"] = assigned;
    return config;
}

void Vehicle::SetConfig(const QVariantMap &changes)
{
    bool obd_changed = false, illum_changed = false, tpms_changed = false, hub_changed = false;
    for (auto it = changes.begin(); it != changes.end(); ++it) {
        const QString &key = it.key();
        if (key == "tpms_sensors") {
            const QVariantMap m = dbus_map(it.value());
            for (const QString &pos : kPositions)
                if (m.contains(pos))
                    settings.setValue("tpms_sensors/" + pos, m.value(pos).toString());
            tires_changed();
            continue;
        }
        if (!kDefaults.contains(key)) {
            qWarning().noquote() << "[config] Unknown setting" << key;
            continue;
        }
        if (settings.value(key, kDefaults.value(key)) == it.value())
            continue;
        settings.setValue(key, it.value());
        if (key.startsWith("obd_") || key.startsWith("can_"))
            obd_changed = true;
        else if (key.startsWith("illum_"))
            illum_changed = true;
        else if (key.startsWith("tpms_"))
            tpms_changed = true;
        else if (key.startsWith("hub_"))
            hub_changed = true;
    }
    settings.sync();
    if (obd_changed)
        apply_obd();
    if (illum_changed)
        apply_illumination();
    if (tpms_changed)
        apply_tpms();
    if (hub_changed)
        apply_hub();
    emit ConfigChanged(GetConfig());
    status_changed();
}

void Vehicle::apply_obd()
{
    const QString link = setting("obd_link").toString();
    if (link == "elm327-usb") {
        const QString port = setting("obd_serial").toString();
        if (port.isEmpty()) {
            obd.set_transport(nullptr, "ELM327 (USB): choose its port");
            return;
        }
        obd.set_transport(Elm327::serial(port), QString("ELM327 on %1").arg(QFileInfo(port).fileName()));
    } else if (link == "elm327-bt") {
        const QString address = setting("obd_bt_address").toString();
        if (address.isEmpty()) {
            obd.set_transport(nullptr, "ELM327 (Bluetooth): choose the adapter");
            return;
        }
        obd.set_transport(Elm327::bluetooth(address), QString("ELM327 over Bluetooth (%1)").arg(address));
    } else if (link == "can") {
        const QString iface = setting("can_interface").toString();
        obd.set_transport(new CanTransport(iface, setting("can_bitrate").toInt()), QString("CAN bus on %1").arg(iface));
    } else {
        obd.set_transport(nullptr, QString());
    }
}

void Vehicle::apply_illumination()
{
    const QString line = setting("illum_line").toString();
    const bool was = illumination.value() && illumination.is_open();
    illumination.close();
    illumination_message.clear();
    if (!line.isEmpty()) {
        const QString chip = line.section(':', 0, 0);
        const int offset = line.section(':', 1, 1).toInt();
        const QString bias = setting("illum_bias").toString();
        QString error;
        if (illumination.open(chip, offset, setting("illum_active_low").toBool(),
                              bias == "pull-up"     ? GpioInput::Bias::PullUp
                              : bias == "pull-down" ? GpioInput::Bias::PullDown
                                                    : GpioInput::Bias::None,
                              &error)) {
            illumination_message = QString("Reading %1 line %2").arg(chip).arg(offset);
        } else {
            illumination_message = error;
            qWarning().noquote() << "[illumination]" << error;
        }
    }
    const bool now = illumination.value() && illumination.is_open();
    if (now != was)
        emit IlluminationChanged(now);
    status_changed();
    hub.changed();
}

void Vehicle::apply_tpms()
{
    if (setting("tpms_ble").toBool())
        tpms.start();
    else
        tpms.stop();
    hub.set_fetch_tires(setting("tpms_hub").toBool());
    tires_changed();
    status_changed();
}

void Vehicle::apply_hub()
{
    hub.set_forwarding(setting("hub_forward").toBool());
    hub.changed();
}

HubLink::Hub Vehicle::hub_address() const
{
    HubLink::Hub h;
    QString url = setting("hub_url").toString().trimmed();
    h.pin = setting("hub_pin").toString();
    if (url.isEmpty()) {
        // The hub Dash's TrailCommand tab connects to
        QSettings dash(qEnvironmentVariable("OZARK_DASH_CONF", kDashSettings), QSettings::IniFormat);
        const QString host = dash.value("TrailCommand/host").toString().trimmed();
        if (host.isEmpty())
            return h;
        const bool https = dash.value("TrailCommand/https", false).toBool();
        url = QString("%1://%2:%3").arg(https ? "https" : "http", host).arg(dash.value("TrailCommand/port", 8080).toInt());
        h.trust = dash.value("TrailCommand/trust", false).toBool();
        if (h.pin.isEmpty() && dash.value("TrailCommand/remember_pin", false).toBool())
            h.pin = dash.value("TrailCommand/pin").toString();
    } else if (!url.contains("://")) {
        url = "http://" + url;
    }
    while (url.endsWith('/'))
        url.chop(1);
    h.base = url;
    return h;
}

/* ---------- status and data ---------- */

QVariantMap Vehicle::GetStatus() const
{
    int seen = 0;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    for (const Sensor &s : sensors)
        if (s.seen.secsTo(now) < 600)
            seen++;
    return {
        {"obd", obd.status()},
        {"illumination",
         QVariantMap {{"line", setting("illum_line").toString()},
                      {"ok", illumination.is_open()},
                      {"on", illumination.is_open() && illumination.value()},
                      {"message", illumination_message}}},
        {"tpms",
         QVariantMap {{"ble", tpms.running()},
                      {"hub", setting("tpms_hub").toBool()},
                      {"message", tpms.running() ? tpms.message() : QString("BLE tire sensors off")},
                      {"seen", seen}}},
        {"hub", hub.status()},
    };
}

QVariantMap Vehicle::GetValues() const
{
    return obd.values();
}

QVariantMap Vehicle::GetDtcs() const
{
    return obd.dtcs();
}

void Vehicle::ReadDtcs()
{
    obd.read_dtcs();
}

void Vehicle::ClearDtcs()
{
    obd.clear_dtcs();
}

QStringList Vehicle::GetSerialPorts() const
{
    QStringList out;
    const QDir by_id("/dev/serial/by-id");
    for (const QFileInfo &f : by_id.entryInfoList(QDir::System | QDir::Files | QDir::NoDotAndDotDot))
        out << f.absoluteFilePath();
    for (const QString &name : QDir("/dev").entryList({"ttyUSB*", "ttyACM*"}, QDir::System | QDir::Files))
        out << "/dev/" + name;
    return out;
}

QStringList Vehicle::GetCanInterfaces() const
{
    QStringList out;
    for (const QString &name : QDir("/sys/class/net").entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QFile type("/sys/class/net/" + name + "/type");
        // ARPHRD_CAN
        if (type.open(QIODevice::ReadOnly) && type.readAll().trimmed() == "280")
            out << name;
    }
    return out;
}

QStringList Vehicle::GetBluetoothDevices() const
{
    QStringList out;
    const QDBusMessage reply = QDBusConnection::systemBus().call(
        QDBusMessage::createMethodCall("org.bluez", "/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects"),
        QDBus::Block, 3000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return out;
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
            if (iface == "org.bluez.Device1" && props.value("Paired").toBool())
                out << QString("%1 %2").arg(props.value("Address").toString(), props.value("Alias").toString());
        }
        arg.endMap();
        arg.endMapEntry();
    }
    arg.endMap();
    return out;
}

QVariantList Vehicle::GetGpioLines() const
{
    return GpioInput::lines();
}

QVariantMap Vehicle::GetTires() const
{
    const double low_kpa = setting("tpms_low_kpa").toDouble();
    const double high_c = setting("tpms_high_c").toDouble();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QVariantMap tires;

    // The hub's own sensors first; a BLE sensor on the same wheel wins
    if (setting("tpms_hub").toBool()) {
        for (const QJsonValue &v : hub.tires()) {
            const QJsonObject t = v.toObject();
            tires[t.value("pos").toString()] = QVariantMap {
                {"pressure_kpa", t.value("pressure_kpa").toDouble()},
                {"temp_c", t.value("temp_c").toDouble()},
                {"low", t.value("low").toBool()},
                {"hot", t.value("hot").toBool()},
                {"alarm", false},
                {"age_s", 0},
                {"source", "hub"},
            };
        }
    }
    for (const QString &pos : kPositions) {
        const QString id = settings.value("tpms_sensors/" + pos).toString();
        if (id.isEmpty() || !sensors.contains(id))
            continue;
        const Sensor &s = sensors[id];
        const qint64 age = s.seen.secsTo(now);
        // Parked sensors go quiet; a day-old reading isn't worth showing
        if (age > 86400)
            continue;
        tires[pos] = QVariantMap {
            {"pressure_kpa", qRound(s.kpa * 10) / 10.0},
            {"temp_c", qRound(s.temp_c * 10) / 10.0},
            {"battery", s.battery},
            {"alarm", s.alarm},
            {"low", s.alarm || s.kpa < low_kpa},
            {"hot", s.temp_c > high_c},
            {"age_s", age},
            {"source", "ble"},
            {"sensor", id},
        };
    }
    return tires;
}

QVariantList Vehicle::GetTpmsSensors() const
{
    QMap<QString, QString> position_of;
    for (const QString &pos : kPositions) {
        const QString id = settings.value("tpms_sensors/" + pos).toString();
        if (!id.isEmpty())
            position_of[id] = pos;
    }
    QVariantList out;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    for (auto it = sensors.begin(); it != sensors.end(); ++it) {
        const qint64 age = it->seen.secsTo(now);
        if (age > 3600 && !position_of.contains(it.key()))
            continue;
        out << QVariantMap {
            {"id", it.key()},
            {"name", it->name},
            {"pressure_kpa", qRound(it->kpa * 10) / 10.0},
            {"temp_c", qRound(it->temp_c * 10) / 10.0},
            {"battery", it->battery},
            {"alarm", it->alarm},
            {"rssi", it->rssi},
            {"age_s", age},
            {"position", position_of.value(it.key())},
        };
    }
    return out;
}

void Vehicle::AssignTpmsSensor(const QString &position, const QString &id)
{
    if (!kPositions.contains(position))
        return;
    // One wheel per sensor
    if (!id.isEmpty())
        for (const QString &pos : kPositions)
            if (pos != position && settings.value("tpms_sensors/" + pos).toString() == id)
                settings.setValue("tpms_sensors/" + pos, QString());
    settings.setValue("tpms_sensors/" + position, id);
    settings.sync();
    qInfo().noquote() << "[tpms]" << (id.isEmpty() ? QString("No sensor on %1").arg(position)
                                                   : QString("%1 is on %2").arg(id, position));
    tires_changed();
    emit ConfigChanged(GetConfig());
}

/* ---------- to the hub ---------- */

QJsonObject Vehicle::hub_snapshot() const
{
    const QVariantMap status = obd.status();
    QJsonObject out {
        {"source", "ozark-radio"},
        {"obd",
         QJsonObject {{"state", status.value("state").toString()},
                      {"protocol", status.value("protocol").toString()},
                      {"vin", status.value("vin").toString()}}},
        {"values", QJsonObject::fromVariantMap(obd.values())},
    };
    const QVariantMap dtcs = obd.dtcs();
    if (dtcs.value("ok").toBool()) {
        QJsonObject d;
        d["mil"] = dtcs.value("mil").toBool();
        d["count"] = dtcs.value("count").toInt();
        d["read_at"] = dtcs.value("read_at").toString();
        for (const char *key : {"stored", "pending", "permanent"}) {
            QJsonArray list;
            for (const QVariant &c : dtcs.value(key).toList()) {
                const QVariantMap m = c.toMap();
                list.append(QJsonObject {{"code", m.value("code").toString()},
                                         {"description", m.value("description").toString()}});
            }
            d[key] = list;
        }
        out["dtcs"] = d;
    }
    if (illumination.is_open())
        out["illumination"] = illumination.value();
    // Only our own (BLE) tires: the hub has its own sensors already
    QJsonArray tires;
    const QVariantMap all = GetTires();
    for (auto it = all.begin(); it != all.end(); ++it) {
        const QVariantMap t = it.value().toMap();
        if (t.value("source").toString() != "ble")
            continue;
        tires.append(QJsonObject {
            {"pos", it.key()},
            {"pressure_kpa", t.value("pressure_kpa").toDouble()},
            {"temp_c", t.value("temp_c").toDouble()},
            {"battery", t.value("battery").toInt()},
            {"alarm", t.value("alarm").toBool()},
            {"age_s", t.value("age_s").toLongLong()},
        });
    }
    out["tires"] = tires;
    return out;
}
