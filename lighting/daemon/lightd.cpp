#include "lightd.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDir>
#include <QSerialPortInfo>

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
    LedController &c = lighting.controller;
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
    LedController &c = lighting.controller;
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
    , controller(this)
    , settings(kSettings, QSettings::IniFormat)
    , audio(this)
{
    for (char name : {'F', 'R', 'E'})
        zones.append(new Zone(*this, name));

    // Looks are stored on the board too, but not on every change: flash wears
    save_timer.setSingleShot(true);
    save_timer.setInterval(kSaveDelayMs);
    connect(&save_timer, &QTimer::timeout, this, [this] {
        controller.saveLook();
        settings.sync();
    });

    connect(&controller, &LedController::connectedChanged, this, [this](bool) {
        refresh_audio();
        status_changed();
    });
    // READY/PONG, also after the board restarts: give it our looks, read its setup
    connect(&controller, &LedController::boardReady, this, [this] {
        for (Zone *zone : zones)
            zone->push();
        controller.requestInfo();
        controller.requestConfig();
        refresh_audio();
    });
    connect(&controller, &LedController::infoReceived, this, [this](const QVariantMap &received) {
        info = received;
        status_changed();
    });
    connect(&controller, &LedController::configReceived, this, [this](const QVariantMap &received) {
        config = received;
        emit ConfigChanged(config);
    });
    connect(&controller, &LedController::commandResult, this, [this](bool ok, const QString &message) {
        if (!applying || (ok && !message.startsWith("OK APPLIED")))
            return;
        applying = false;
        emit ConfigApplied(ok, message);
        controller.requestConfig();
    });
    connect(&audio, &AudioAnalyzer::frame, &controller, &LedController::sendAudio);

    audio.setSensitivity(settings.value("sensitivity", 100).toInt() / 100.0f);
    const QString port = settings.value("port", "auto").toString();
    controller.setPortName(port == "auto" ? QString() : port);
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

    controller.start();
    return true;
}

QVariantMap Lighting::GetStatus() const
{
    QStringList pins;
    for (const QVariant &pin : info.value("pins").toList())
        pins.append(QString::number(pin.toInt()));
    return {{"connected", controller.isConnected()},
            {"firmware", info.value("fw").toString()},
            {"max_leds", info.value("max_leds", 600).toInt()},
            {"pins", pins},
            {"orders", info.value("orders").toStringList()},
            {"port", settings.value("port", "auto").toString()},
            {"sensitivity", settings.value("sensitivity", 100).toInt()}};
}

QVariantMap Lighting::GetConfig() const
{
    return config;
}

void Lighting::SetConfig(const QVariantMap &changes)
{
    if (!controller.isConnected()) {
        emit ConfigApplied(false, "Controller not connected");
        return;
    }

    static const QStringList kKeys = {"pin", "count", "order", "rgbw", "khz", "reverse"};
    for (const QString &zone : QStringList{"F", "R", "E"}) {
        const QVariantMap strip = to_map(changes.value(zone));
        for (const QString &key : kKeys) {
            if (strip.contains(key))
                controller.stageConfig(zone[0].toLatin1(), key.toUpper(), strip.value(key).toString());
        }
    }
    const QVariantMap global = to_map(changes.value("G"));
    if (global.contains("maxbri"))
        controller.stageConfig('G', "MAXBRI", global.value("maxbri").toString());

    applying = true;
    controller.applyConfig();
}

void Lighting::Test(const QString &zone)
{
    if (zone.size() == 1)
        controller.test(zone[0].toLatin1());
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
    controller.setPortName(port == "auto" ? QString() : port);
    status_changed();
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
    if (music && controller.isConnected())
        audio.start();
    else
        audio.stop();
}

void Lighting::status_changed()
{
    emit StatusChanged(GetStatus());
}
