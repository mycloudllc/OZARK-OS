#include "lightd.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDir>
#include <QSerialPortInfo>

#include "LedController.h"
#include "WledController.h"

namespace {
const char *kService = "org.ozark.Lighting1";
const char *kPath = "/org/ozark/Lighting1";
const char *kSettings = "/var/lib/ozark-lightd/lighting.conf";

constexpr int kPatterns = 11;
constexpr int kFirstMusicPattern = 8;  // VU Meter, Spectrum, Beat Pulse
constexpr int kSaveDelayMs = 2000;     // debounce flash writes on the board

// Nested maps arrive from D-Bus as QDBusArgument, not QVariantMap
QVariantMap to_map(const QVariant &value)
{
    if (value.userType() == qMetaTypeId<QDBusArgument>())
        return qdbus_cast<QVariantMap>(value.value<QDBusArgument>());
    return value.toMap();
}
}  // namespace

// ---------------------------------------------------------------- zone

Zone::Zone(Lighting &lighting, char name)
    : QObject(&lighting)
    , lighting(lighting)
    , name_(name)
{
    QSettings &s = lighting.settings;
    s.beginGroup(QString("look_%1").arg(name));
    color = QColor(s.value("color", "#ff0050").toString());
    color2 = QColor(s.value("color2", "#0078ff").toString());
    pattern_ = qBound(0, s.value("pattern", 1).toInt(), kPatterns - 1);
    speed = qBound(0, s.value("speed", 128).toInt(), 255);
    brightness = qBound(0, s.value("brightness", 255).toInt(), 255);
    s.endGroup();
}

QVariantMap Zone::GetLook() const
{
    return {{"color", color.name()},
            {"color2", color2.name()},
            {"pattern", pattern_},
            {"speed", speed},
            {"brightness", brightness}};
}

void Zone::SetLook(const QVariantMap &changes)
{
    Controller &c = *lighting.controller;
    if (changes.contains("color")) {
        color = QColor(changes.value("color").toString());
        c.setColor(name_, color);
    }
    if (changes.contains("color2")) {
        color2 = QColor(changes.value("color2").toString());
        c.setColor2(name_, color2);
    }
    if (changes.contains("pattern")) {
        pattern_ = qBound(0, changes.value("pattern").toInt(), kPatterns - 1);
        c.setPattern(name_, pattern_);
    }
    if (changes.contains("speed")) {
        speed = qBound(0, changes.value("speed").toInt(), 255);
        c.setSpeed(name_, speed);
    }
    if (changes.contains("brightness")) {
        brightness = qBound(0, changes.value("brightness").toInt(), 255);
        c.setBrightness(name_, brightness);
    }
    store();
    emit LookChanged(GetLook());
    lighting.look_changed();
}

void Zone::push() const
{
    Controller &c = *lighting.controller;
    c.setColor(name_, color);
    c.setColor2(name_, color2);
    c.setPattern(name_, pattern_);
    c.setSpeed(name_, speed);
    c.setBrightness(name_, brightness);
}

void Zone::store() const
{
    QSettings &s = lighting.settings;
    s.beginGroup(QString("look_%1").arg(name_));
    s.setValue("color", color.name());
    s.setValue("color2", color2.name());
    s.setValue("pattern", pattern_);
    s.setValue("speed", speed);
    s.setValue("brightness", brightness);
    s.endGroup();
}

// ---------------------------------------------------------------- lighting

Lighting::Lighting(QObject *parent)
    : QObject(parent)
    , settings(kSettings, QSettings::IniFormat)
    , audio(this)
    , finder(this)
{
    for (char name : {'F', 'R', 'E'})
        zones.append(new Zone(*this, name));

    // Looks are stored on the board too, but not on every change: flash wears
    save_timer.setSingleShot(true);
    save_timer.setInterval(kSaveDelayMs);
    connect(&save_timer, &QTimer::timeout, this, [this] {
        controller->saveLook();
        settings.sync();
    });

    connect(&audio, &AudioAnalyzer::frame, this, [this](const AudioFrame &frame) { controller->sendAudio(frame); });
    connect(&finder, &WledFinder::finished, this, &Lighting::WledFound);

    audio.setSensitivity(settings.value("sensitivity", 100).toInt() / 100.0f);
    make_controller();
}

bool Lighting::wled() const
{
    return settings.value("controller", "ozark").toString() == "wled";
}

QMap<char, int> Lighting::segments() const
{
    QMap<char, int> map;
    int index = 0;
    for (char zone : {'F', 'R', 'E'})
        map[zone] = settings.value(QString("wled/%1").arg(zone), index++).toInt();
    return map;
}

// The controller the settings name; replaces the current one
void Lighting::make_controller()
{
    if (controller) {
        controller->stop();
        controller->disconnect(this);
        controller->deleteLater();
    }
    info.clear();
    config.clear();
    applying = false;

    const QString port = settings.value("port", "auto").toString();
    if (wled()) {
        const QString name = settings.value("wled/link", "network").toString();
        const auto link = name == "serial"     ? WledController::Serial
                          : name == "adalight" ? WledController::Adalight
                                               : WledController::Network;
        auto w = new WledController(link, settings.value("wled/host").toString(),
                                    settings.value("wled/baud", 115200).toInt(), this);
        w->setSegments(segments());
        w->setMaxBrightness(settings.value("wled/maxbri", 200).toInt());
        controller = w;
    } else {
        controller = new LedController(this);
    }
    controller->setPortName(port == "auto" ? QString() : port);

    connect(controller, &Controller::connectedChanged, this, [this](bool) {
        refresh_audio();
        status_changed();
    });
    // Connected, also after the controller restarts: give it our looks, read its setup
    connect(controller, &Controller::boardReady, this, [this] {
        for (Zone *zone : zones)
            zone->push();
        controller->requestInfo();
        controller->requestConfig();
        refresh_audio();
    });
    connect(controller, &Controller::infoReceived, this, [this](const QVariantMap &received) {
        info = received;
        status_changed();
    });
    connect(controller, &Controller::configReceived, this, [this](const QVariantMap &received) {
        config = received;
        emit ConfigChanged(config);
    });
    connect(controller, &Controller::commandResult, this, [this](bool ok, const QString &message) {
        if (!applying || (ok && !message.startsWith("OK APPLIED")))
            return;
        applying = false;
        emit ConfigApplied(ok, message);
        controller->requestConfig();
    });

    status_changed();
    if (started)
        controller->start();
}

bool Lighting::start()
{
    QDBusConnection bus = QDBusConnection::systemBus();
    const auto options = QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals;
    if (!bus.registerObject(kPath, this, options))
        return false;
    for (Zone *zone : zones) {
        if (!bus.registerObject(QString("%1/%2").arg(kPath).arg(zone->name()), zone, options))
            return false;
    }
    if (!bus.registerService(kService))
        return false;

    started = true;
    controller->start();
    return true;
}

QVariantMap Lighting::GetStatus() const
{
    QStringList pins;
    for (const QVariant &pin : info.value("pins").toList())
        pins.append(QString::number(pin.toInt()));
    QVariantMap status{{"connected", controller->isConnected()},
                       {"firmware", info.value("fw").toString()},
                       {"max_leds", info.value("max_leds", 600).toInt()},
                       {"pins", pins},
                       {"orders", info.value("orders").toStringList()},
                       {"port", settings.value("port", "auto").toString()},
                       {"sensitivity", settings.value("sensitivity", 100).toInt()},
                       {"type", wled() ? "wled" : "ozark"}};
    if (wled()) {
        QVariantMap map;
        const QMap<char, int> segs = segments();
        for (auto it = segs.begin(); it != segs.end(); ++it)
            map[QString(QChar(it.key()))] = it.value();
        status["link"] = settings.value("wled/link", "network").toString();
        status["host"] = settings.value("wled/host").toString();
        status["name"] = info.value("name").toString();
        status["segments"] = info.value("segments").toStringList();
        status["map"] = map;
        status["audio"] = info.value("audio").toString();
        status["baud"] = settings.value("wled/baud", 115200).toInt();
        status["fps"] = info.value("fps", 0).toInt();
    }
    return status;
}

QVariantMap Lighting::GetConfig() const
{
    return config;
}

void Lighting::SetConfig(const QVariantMap &changes)
{
    // WLED: only the brightness cap, kept here (it's WLED's brightness)
    const QVariantMap global = to_map(changes.value("G"));
    if (wled() && global.contains("maxbri")) {
        settings.setValue("wled/maxbri", qBound(1, global.value("maxbri").toInt(), 255));
        settings.sync();
    }
    if (!controller->isConnected()) {
        emit ConfigApplied(false, "Controller not connected");
        return;
    }

    static const QStringList kKeys = {"pin", "count", "order", "rgbw", "khz", "reverse"};
    for (const QString &zone : QStringList{"F", "R", "E"}) {
        const QVariantMap strip = to_map(changes.value(zone));
        for (const QString &key : kKeys) {
            if (strip.contains(key))
                controller->stageConfig(zone[0].toLatin1(), key.toUpper(), strip.value(key).toString());
        }
    }
    if (global.contains("maxbri"))
        controller->stageConfig('G', "MAXBRI", global.value("maxbri").toString());

    applying = true;
    controller->applyConfig();
}

void Lighting::Test(const QString &zone)
{
    if (zone.size() == 1)
        controller->test(zone[0].toLatin1());
}

QStringList Lighting::GetPorts() const
{
    // Stable names first: /dev/serial/by-id survives reboots and replugging
    QStringList ports{"auto"};
    for (const QString &name : QDir("/dev/serial/by-id").entryList(QDir::System | QDir::Files | QDir::NoDotAndDotDot))
        ports.append("/dev/serial/by-id/" + name);
    for (const QSerialPortInfo &port : QSerialPortInfo::availablePorts())
        ports.append(port.systemLocation());
    return ports;
}

void Lighting::SetPort(const QString &port)
{
    settings.setValue("port", port);
    settings.sync();
    controller->setPortName(port == "auto" ? QString() : port);
    status_changed();
}

void Lighting::SetController(const QVariantMap &changes)
{
    bool remake = false;
    auto set = [&](const QString &key, const QString &setting, const QStringList &allowed = {}) {
        if (!changes.contains(key))
            return;
        const QString value = changes.value(key).toString().trimmed();
        if (!allowed.isEmpty() && !allowed.contains(value))
            return;
        if (settings.value(setting).toString() != value) {
            settings.setValue(setting, value);
            remake = true;
        }
    };
    set("type", "controller", {"ozark", "wled"});
    set("link", "wled/link", {"network", "serial", "adalight"});
    set("host", "wled/host");
    set("baud", "wled/baud", {"115200", "230400", "460800", "500000", "576000", "921600", "1000000", "1500000"});

    bool remap = false;
    for (const QString &zone : QStringList{"F", "R", "E"}) {
        if (changes.contains(zone)) {
            settings.setValue("wled/" + zone, qBound(-1, changes.value(zone).toInt(), 31));
            remap = true;
        }
    }
    settings.sync();

    if (remake)
        make_controller();
    else if (remap && wled())
        static_cast<WledController *>(controller)->setSegments(segments());
    status_changed();
}

void Lighting::FindWled()
{
    finder.find();
}

void Lighting::SetSensitivity(int percent)
{
    percent = qBound(25, percent, 400);
    settings.setValue("sensitivity", percent);
    audio.setSensitivity(percent / 100.0f);
    status_changed();
}

void Lighting::look_changed()
{
    refresh_audio();
    save_timer.start();
}

// Capture what's playing only while a zone uses a music pattern
void Lighting::refresh_audio()
{
    bool music = false;
    for (Zone *zone : zones)
        music |= zone->pattern() >= kFirstMusicPattern;
    if (music && controller->isConnected())
        audio.start();
    else
        audio.stop();
}

void Lighting::status_changed()
{
    emit StatusChanged(GetStatus());
}
