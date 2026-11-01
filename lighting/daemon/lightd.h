// ozark-lightd: owns the lighting controller's serial link and serves it on
// the system D-Bus as org.ozark.Lighting1 (see ../README.md for the API).
#pragma once

#include <QColor>
#include <QObject>
#include <QSettings>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

#include "AudioAnalyzer.h"
#include "LedController.h"

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

    LedController controller;
    QSettings settings;
    void look_changed();  // a zone's look changed: maybe music, save soon

   public slots:
    // {connected, firmware, max_leds, pins: [s], orders: [s], port, sensitivity}
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

   signals:
    void StatusChanged(const QVariantMap &status);
    void ConfigChanged(const QVariantMap &config);
    void ConfigApplied(bool ok, const QString &message);

   private:
    void refresh_audio();
    void status_changed();

    AudioAnalyzer audio;
    QList<Zone *> zones;
    QTimer save_timer;
    QVariantMap info;
    QVariantMap config;
    bool applying = false;
};
