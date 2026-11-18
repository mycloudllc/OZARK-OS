#include "bridge.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QDateTime>
#include <QFile>
#include <QProcess>
#include <QSysInfo>
#include <QTimer>

namespace {
const QString kDash = "org.ozark.Dash1";
const QString kDashPath = "/org/ozark/Dash1";
const QString kLight = "org.ozark.Lighting1";
const QString kLightPath = "/org/ozark/Lighting1";
const QString kZone = "org.ozark.Lighting1.Zone";
const QString kVehicle = "org.ozark.Vehicle1";
const QString kVehiclePath = "/org/ozark/Vehicle1";
const QStringList kZones = {"F", "R", "E"};

// Values arrive several times a second while driving: at most this often to phones
constexpr qint64 kValuesEveryMs = 500;

QVariantMap first_map(const QVariantList &args)
{
    return Bridge::unwrap(args.value(0)).toMap();
}

QString text(const QVariantMap &params, const QString &key)
{
    return params.value(key).toString();
}

// Only what ozark-net would accept anyway; refuse anything that could reach a shell
bool plain(const QString &value)
{
    for (const QChar c : value)
        if (c.unicode() < 0x20 || c == '\'' || c == '\\' || c.unicode() == 0x7f)
            return false;
    return true;
}
}  // namespace

const QStringList &Bridge::methods()
{
    static const QStringList list = {
        "state.get",
        // Dash: forwarded as they are, Dash knows what they mean (see PROTOCOL.md)
        "dash.volume", "dash.volume_step", "dash.brightness", "dash.theme", "dash.page",
        "media.play", "media.pause", "media.toggle", "media.next", "media.previous", "media.source",
        "radio.play", "radio.stop", "radio.tune", "radio.step", "radio.band", "radio.preset", "radio.save_preset",
        "lighting.get", "lighting.set",
        "vehicle.get", "vehicle.dtcs.get", "vehicle.dtcs.read", "vehicle.dtcs.clear",
        "system.info", "system.network", "system.hotspot.set", "system.update.status", "system.update.check",
        "system.reboot",
    };
    return list;
}

Bridge::Bridge(QObject *parent)
    : QObject(parent)
    , watcher(new QDBusServiceWatcher(this))
{
    QDBusConnection bus = QDBusConnection::systemBus();
    bus.connect(kDash, kDashPath, kDash, "StateChanged", this, SLOT(on_dash_state(QDBusMessage)));
    bus.connect(kLight, kLightPath, kLight, "StatusChanged", this, SLOT(on_light_status(QDBusMessage)));
    for (const QString &zone : kZones)
        bus.connect(kLight, kLightPath + "/" + zone, kZone, "LookChanged", this, SLOT(on_light_look(QDBusMessage)));
    bus.connect(kVehicle, kVehiclePath, kVehicle, "StatusChanged", this, SLOT(on_vehicle_status(QDBusMessage)));
    bus.connect(kVehicle, kVehiclePath, kVehicle, "ValuesChanged", this, SLOT(on_vehicle_values(QDBusMessage)));
    bus.connect(kVehicle, kVehiclePath, kVehicle, "TiresChanged", this, SLOT(on_vehicle_tires(QDBusMessage)));
    bus.connect(kVehicle, kVehiclePath, kVehicle, "DtcsRead", this, SLOT(on_vehicle_dtcs(QDBusMessage)));
    bus.connect(kVehicle, kVehiclePath, kVehicle, "DtcsCleared", this, SLOT(on_vehicle_cleared(bool, QString)));

    // Pick everything up again whenever one of them (re)starts
    this->watcher->setConnection(bus);
    this->watcher->setWatchMode(QDBusServiceWatcher::WatchForRegistration);
    for (const QString &s : {kDash, kLight, kVehicle})
        this->watcher->addWatchedService(s);
    connect(this->watcher, &QDBusServiceWatcher::serviceRegistered, this, &Bridge::refresh);
    for (const QString &s : {kDash, kLight, kVehicle})
        this->refresh(s);
}

QVariant Bridge::unwrap(const QVariant &value)
{
    if (value.userType() == qMetaTypeId<QDBusVariant>())
        return unwrap(value.value<QDBusVariant>().variant());
    if (value.userType() == qMetaTypeId<QDBusObjectPath>())
        return value.value<QDBusObjectPath>().path();
    if (value.userType() == qMetaTypeId<QDBusArgument>()) {
        const QDBusArgument arg = value.value<QDBusArgument>();
        switch (arg.currentType()) {
            case QDBusArgument::MapType: {
                QVariantMap map;
                arg.beginMap();
                while (!arg.atEnd()) {
                    arg.beginMapEntry();
                    // Keys are plain (s, o, q...), values often variants: read each as it is
                    const QVariant key = arg.asVariant();
                    const QVariant val = arg.asVariant();
                    arg.endMapEntry();
                    map.insert(unwrap(key).toString(), unwrap(val));
                }
                arg.endMap();
                return map;
            }
            case QDBusArgument::ArrayType: {
                if (arg.currentSignature() == "ay") {
                    QByteArray bytes;
                    arg >> bytes;
                    return bytes;
                }
                QVariantList list;
                arg.beginArray();
                while (!arg.atEnd())
                    list << unwrap(arg.asVariant());
                arg.endArray();
                return list;
            }
            case QDBusArgument::StructureType: {
                QVariantList list;
                arg.beginStructure();
                while (!arg.atEnd())
                    list << unwrap(arg.asVariant());
                arg.endStructure();
                return list;
            }
            default:
                return arg.asVariant();
        }
    }
    if (value.type() == QVariant::Map) {
        QVariantMap map = value.toMap();
        for (auto it = map.begin(); it != map.end(); ++it)
            it.value() = unwrap(it.value());
        return map;
    }
    if (value.type() == QVariant::List) {
        QVariantList list = value.toList();
        for (QVariant &v : list)
            v = unwrap(v);
        return list;
    }
    return value;
}

QVariantMap Bridge::snapshot() const
{
    QVariantMap lighting{{"status", this->light_status}, {"zones", this->looks}};
    QVariantMap vehicle{{"status", this->vehicle_status}, {"values", this->vehicle_values},
                        {"tires", this->vehicle_tires}, {"dtcs", this->vehicle_dtcs}};
    return {{"dash", this->dash}, {"lighting", lighting}, {"vehicle", vehicle}};
}

void Bridge::refresh(const QString &service)
{
    auto ignore = [](const QString &, const QString &) {};
    if (service == kDash) {
        this->dbus(kDash, kDashPath, kDash, "GetState", {}, [this](const QVariantList &r) {
            this->dash = first_map(r);
            emit event("dash.state", this->dash);
        }, ignore);
    }
    else if (service == kLight) {
        this->dbus(kLight, kLightPath, kLight, "GetStatus", {}, [this](const QVariantList &r) {
            this->light_status = first_map(r);
            emit event("lighting.status", this->light_status);
        }, ignore);
        for (const QString &zone : kZones) {
            this->dbus(kLight, kLightPath + "/" + zone, kZone, "GetLook", {}, [this, zone](const QVariantList &r) {
                this->looks[zone] = first_map(r);
                emit event("lighting.look", QVariantMap{{"zone", zone}, {"look", this->looks[zone]}});
            }, ignore);
        }
    }
    else if (service == kVehicle) {
        this->dbus(kVehicle, kVehiclePath, kVehicle, "GetStatus", {}, [this](const QVariantList &r) {
            this->vehicle_status = first_map(r);
            emit event("vehicle.status", this->vehicle_status);
        }, ignore);
        this->dbus(kVehicle, kVehiclePath, kVehicle, "GetValues", {}, [this](const QVariantList &r) {
            this->vehicle_values = first_map(r);
            emit event("vehicle.values", this->vehicle_values);
        }, ignore);
        this->dbus(kVehicle, kVehiclePath, kVehicle, "GetTires", {}, [this](const QVariantList &r) {
            this->vehicle_tires = first_map(r);
            emit event("vehicle.tires", this->vehicle_tires);
        }, ignore);
        this->dbus(kVehicle, kVehiclePath, kVehicle, "GetDtcs", {}, [this](const QVariantList &r) {
            this->vehicle_dtcs = first_map(r);
        }, ignore);
    }
}

void Bridge::call(const QString &method, const QVariantMap &params, Done done, Fail fail)
{
    if (method == "state.get") {
        done(this->snapshot());
        return;
    }

    // ---- Dash: media, radio, volume, brightness, theme
    if (method.startsWith("dash.") || method.startsWith("media.") || method.startsWith("radio.")) {
        this->dbus(kDash, kDashPath, kDash, "Control", {method, params},
                   [done](const QVariantList &r) { done(first_map(r)); }, fail);
        return;
    }

    // ---- Lighting
    if (method == "lighting.get") {
        done(QVariantMap{{"status", this->light_status}, {"zones", this->looks}});
        return;
    }
    if (method == "lighting.set") {
        const QString zone = text(params, "zone").toUpper();
        if (!kZones.contains(zone)) {
            fail("bad_params", "zone must be F, R or E");
            return;
        }
        const QVariantMap look = params.value("look").toMap();
        static const QStringList keys = {"color", "color2", "background", "pattern", "speed", "brightness"};
        QVariantMap clean;
        for (auto it = look.begin(); it != look.end(); ++it) {
            if (!keys.contains(it.key()))
                continue;
            // JSON numbers arrive as doubles; Lighting1 wants ints for these
            if (it.key() == "pattern" || it.key() == "speed" || it.key() == "brightness")
                clean[it.key()] = it.value().toInt();
            else
                clean[it.key()] = it.value().toString();
        }
        if (clean.isEmpty()) {
            fail("bad_params", "look needs one of color, color2, background, pattern, speed, brightness");
            return;
        }
        this->dbus(kLight, kLightPath + "/" + zone, kZone, "SetLook", {clean},
                   [done](const QVariantList &) { done(QVariantMap{{"ok", true}}); }, fail);
        return;
    }

    // ---- Vehicle
    if (method == "vehicle.get") {
        done(this->snapshot().value("vehicle"));
        return;
    }
    if (method == "vehicle.dtcs.get") {
        done(this->vehicle_dtcs);
        return;
    }
    if (method == "vehicle.dtcs.read" || method == "vehicle.dtcs.clear") {
        // The answer comes as the vehicle.dtcs / vehicle.dtcs_cleared event
        this->dbus(kVehicle, kVehiclePath, kVehicle, method == "vehicle.dtcs.read" ? "ReadDtcs" : "ClearDtcs", {},
                   [done](const QVariantList &) { done(QVariantMap{{"started", true}}); }, fail);
        return;
    }

    // ---- System
    if (method == "system.info") {
        QFile uptime("/proc/uptime");
        uptime.open(QFile::ReadOnly);
        const qint64 up = qint64(QString::fromLatin1(uptime.readAll()).section(' ', 0, 0).toDouble());
        this->run("ozark-update", {"status"}, [done, up](const QVariant &r) {
            const QVariantMap u = r.toMap();
            done(QVariantMap{{"hostname", QSysInfo::machineHostName()},
                             {"version", u.value("version")},
                             {"build", u.value("build")},
                             {"slot", u.value("slot")},
                             {"uptime_s", up},
                             {"time", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}});
        }, [done, up](const QString &, const QString &) {
            done(QVariantMap{{"hostname", QSysInfo::machineHostName()}, {"uptime_s", up}});
        }, true);
        return;
    }
    if (method == "system.network") {
        this->run("ozark-net", {"status"}, done, fail, true);
        return;
    }
    if (method == "system.hotspot.set") {
        const QStringList args = {"hotspot", text(params, "name"), text(params, "password"),
                                  params.contains("channel") ? QString::number(params.value("channel").toInt()) : QString("36"),
                                  params.contains("country") ? text(params, "country") : QString("US")};
        for (const QString &a : args) {
            if (!plain(a)) {
                fail("bad_params", "Names and passwords can't contain quotes, backslashes or control characters");
                return;
            }
        }
        this->run("ozark-net", args, [done](const QVariant &) { done(QVariantMap{{"ok", true}}); }, fail, false);
        return;
    }
    if (method == "system.update.status") {
        this->run("ozark-update", {"status"}, done, fail, true);
        return;
    }
    if (method == "system.update.check") {
        this->run("ozark-update", {"check"}, [this, done](const QVariant &) {
            this->run("ozark-update", {"status"}, done, [done](const QString &, const QString &) { done(QVariantMap()); }, true);
        }, fail, false);
        return;
    }
    if (method == "system.reboot") {
        if (!params.value("confirm").toBool()) {
            fail("bad_params", "Send {\"confirm\": true} to restart the radio");
            return;
        }
        done(QVariantMap{{"ok", true}});
        // After the answer has gone out
        QTimer::singleShot(1500, [] { QProcess::startDetached("systemctl", {"reboot"}); });
        return;
    }

    fail("unknown_method", QString("No method %1").arg(method));
}

void Bridge::dbus(const QString &service, const QString &path, const QString &interface, const QString &method,
                  const QVariantList &args, std::function<void(const QVariantList &)> done, Fail fail)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(service, path, interface, method);
    msg.setArguments(args);
    auto watch = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(msg, 10000), this);
    connect(watch, &QDBusPendingCallWatcher::finished, this, [done, fail, service](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusMessage reply = w->reply();
        if (reply.type() == QDBusMessage::ErrorMessage) {
            const QString name = reply.errorName();
            if (name == "org.freedesktop.DBus.Error.ServiceUnknown" || name == "org.freedesktop.DBus.Error.NoReply")
                fail("unavailable", QString("%1 isn't running").arg(service));
            else if (name.endsWith(".InvalidArgs") || name.endsWith(".BadParams"))
                fail("bad_params", reply.errorMessage());
            else
                fail("failed", reply.errorMessage());
            return;
        }
        done(reply.arguments());
    });
}

// Runs one of the radio's command-line tools; key=value output becomes a map
void Bridge::run(const QString &program, const QStringList &args, Done done, Fail fail, bool key_values)
{
    auto process = new QProcess(this);
    process->setProgram(program);
    process->setArguments(args);
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [process, done, fail, key_values](int code, QProcess::ExitStatus status) {
        process->deleteLater();
        const QString out = QString::fromUtf8(process->readAllStandardOutput());
        if (status != QProcess::NormalExit || code != 0) {
            QString err = QString::fromUtf8(process->readAllStandardError()).trimmed().section('\n', -1);
            fail("failed", err.isEmpty() ? QString("%1 failed").arg(process->program()) : err);
            return;
        }
        if (!key_values) {
            done(out);
            return;
        }
        QVariantMap map;
        for (const QString &line : out.split('\n', Qt::SkipEmptyParts)) {
            const int eq = line.indexOf('=');
            if (eq > 0)
                map.insert(line.left(eq), line.mid(eq + 1));
        }
        done(map);
    });
    connect(process, &QProcess::errorOccurred, this, [process, fail](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        process->deleteLater();
        fail("unavailable", QString("%1 isn't installed").arg(process->program()));
    });
    // Updates checks can take a while; anything else should be quick
    QTimer::singleShot(args.value(0) == "check" ? 120000 : 20000, process, [process] { process->kill(); });
    process->start();
}

// ---------------------------------------------------------------- signals

void Bridge::on_dash_state(const QDBusMessage &message)
{
    this->dash = first_map(message.arguments());
    emit event("dash.state", this->dash);
}

void Bridge::on_light_status(const QDBusMessage &message)
{
    this->light_status = first_map(message.arguments());
    emit event("lighting.status", this->light_status);
}

void Bridge::on_light_look(const QDBusMessage &message)
{
    const QString zone = message.path().section('/', -1);
    QVariantMap look = this->looks.value(zone).toMap();
    const QVariantMap changed = first_map(message.arguments());
    for (auto it = changed.begin(); it != changed.end(); ++it)
        look[it.key()] = it.value();
    this->looks[zone] = look;
    emit event("lighting.look", QVariantMap{{"zone", zone}, {"look", look}});
}

void Bridge::on_vehicle_status(const QDBusMessage &message)
{
    this->vehicle_status = first_map(message.arguments());
    emit event("vehicle.status", this->vehicle_status);
}

void Bridge::on_vehicle_values(const QDBusMessage &message)
{
    const QVariantMap changed = first_map(message.arguments());
    for (auto it = changed.begin(); it != changed.end(); ++it)
        this->vehicle_values[it.key()] = it.value();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - this->values_sent < kValuesEveryMs)
        return;
    this->values_sent = now;
    emit event("vehicle.values", this->vehicle_values);
}

void Bridge::on_vehicle_tires(const QDBusMessage &message)
{
    this->vehicle_tires = first_map(message.arguments());
    emit event("vehicle.tires", this->vehicle_tires);
}

void Bridge::on_vehicle_dtcs(const QDBusMessage &message)
{
    this->vehicle_dtcs = first_map(message.arguments());
    emit event("vehicle.dtcs", this->vehicle_dtcs);
}

void Bridge::on_vehicle_cleared(bool ok, const QString &message)
{
    emit event("vehicle.dtcs_cleared", QVariantMap{{"ok", ok}, {"message", message}});
}
