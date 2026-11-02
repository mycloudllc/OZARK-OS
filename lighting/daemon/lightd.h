// ozark-lightd: owns the link to the lighting controller (the Ozark firmware
// over serial, or WLED over the network or serial) and serves it on the
// system D-Bus as org.ozark.Lighting1 (see ../README.md for the API).
#pragma once

#include <QColor>
#include <QObject>
#include <QSettings>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

#include "AudioAnalyzer.h"
#include "Controller.h"
#include "WledFinder.h"

class Lighting;

// One output's look, at /org/ozark/Lighting1/<F|R|E>
class Zone : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.ozark.Lighting1.Zone")

   public:
    Zone(Lighting &lighting, char name);

    char name() const { return name_; }
    int pattern() const { return pattern_; }
    void push() const;  // send the whole look to the controller

   public slots:
    // {color: "#rrggbb", color2: "#rrggbb", pattern: 0-10, speed: 0-255, brightness: 0-255}
    QVariantMap GetLook() const;
    // Any subset of GetLook()'s keys
    void SetLook(const QVariantMap &changes);

   signals:
    void LookChanged(const QVariantMap &look);

   private:
    void store() const;

    Lighting &lighting;
    char name_;
    QColor color, color2;
    int pattern_, speed, brightness;
};

// The controller, at /org/ozark/Lighting1
class Lighting : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.ozark.Lighting1")

   public:
    explicit Lighting(QObject *parent = nullptr);
    bool start();  // register on the system bus

    Controller *controller = nullptr;
    QSettings settings;
    void look_changed();  // a zone's look changed: maybe music, save soon

   public slots:
    // {connected, firmware, max_leds, pins: [s], orders: [s], port, sensitivity,
    //  type: "ozark"|"wled", and for WLED: link: "network"|"serial"|"adalight", host,
    //  baud, name, segments: ["0: name", ...], map: {F, R, E: segment id or -1},
    //  audio: ""|"on"|"missing", fps (Adalight frames a second)}
    QVariantMap GetStatus() const;
    // {F|R|E: {pin, count, order, rgbw, khz, reverse}, G: {maxbri}} as the controller reports it
    QVariantMap GetConfig() const;
    // Same shape, any subset; result in ConfigApplied
    void SetConfig(const QVariantMap &config);
    // Red, green, blue sweep on one zone
    void Test(const QString &zone);
    // Serial ports to choose from: "auto", /dev/serial/by-id names, then device names
    QStringList GetPorts() const;
    // "auto" or a device path
    void SetPort(const QString &port);
    // Music sensitivity in percent, 25-400
    void SetSensitivity(int percent);
    // Which controller: any subset of {type: "ozark"|"wled",
    // link: "network"|"serial"|"adalight", host, baud (WLED's serial baud rate),
    // F, R, E: WLED segment id or -1}
    void SetController(const QVariantMap &changes);
    // Look for WLED controllers on the network; the answer is WledFound
    void FindWled();

   signals:
    void StatusChanged(const QVariantMap &status);
    void ConfigChanged(const QVariantMap &config);
    void ConfigApplied(bool ok, const QString &message);
    // "address name" for each WLED found
    void WledFound(const QStringList &found);

   private:
    void make_controller();
    QMap<char, int> segments() const;
    bool wled() const;
    void refresh_audio();
    void status_changed();

    AudioAnalyzer audio;
    WledFinder finder;
    QList<Zone *> zones;
    QTimer save_timer;
    QVariantMap info;
    QVariantMap config;
    bool applying = false;
    bool started = false;
};
