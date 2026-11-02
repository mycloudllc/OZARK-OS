#pragma once

#include <QColor>
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <array>

// One analysis frame of what's playing (AudioAnalyzer, ~43 a second)
struct AudioFrame {
    std::array<quint8, 8> bands{};  // log-spaced 40 Hz - 10 kHz, low to high, 0-255
    std::array<quint8, 16> fft{};   // the same range in 16 bands (WLED's GEQ channels)
    bool beat = false;
    float volume = 0;               // 0-255, this frame
    float smooth_volume = 0;        // 0-255, smoothed
    float peak_hz = 0;              // loudest frequency
    float peak_magnitude = 0;       // its level, 0-255
};

// A lighting controller ozark-lightd drives: the Ozark firmware
// (LedController, ../PROTOCOL.md) or WLED (WledController).
//
// Zones: 'F' interior front, 'R' interior rear, 'E' exterior, 'G' global (config).
class Controller : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    // Serial port; empty = auto-detect (CP210x, CH340, FTDI, ESP32 native USB)
    virtual void setPortName(const QString& name) = 0;
    virtual void start() = 0;  // begin connect / auto-reconnect loop
    virtual void stop() = 0;
    virtual bool isConnected() const = 0;

    // ---- look ----
    virtual void setColor(char zone, const QColor& c) = 0;
    virtual void setColor2(char zone, const QColor& c) = 0;
    virtual void setPattern(char zone, int id) = 0;
    virtual void setSpeed(char zone, int value) = 0;       // 0-255
    virtual void setBrightness(char zone, int value) = 0;  // 0-255
    virtual void saveLook() = 0;                           // persist looks on the controller
    virtual void sendAudio(const AudioFrame& frame) = 0;

    // ---- hardware config (staged, then applied) ----
    virtual void requestInfo() = 0;
    virtual void requestConfig() = 0;
    // keys: PIN COUNT ORDER RGBW KHZ REVERSE (zones F/R/E), MAXBRI (zone 'G')
    virtual void stageConfig(char zone, const QString& key, const QString& value) = 0;
    virtual void applyConfig() = 0;  // validates, applies, saves on the controller
    virtual void test(char zone) = 0;  // 3 s red / green / blue test

signals:
    void connectedChanged(bool connected);
    void boardReady();  // connected, also after the controller restarts: send it the looks
    // {fw, max_leds, pins: [int], orders: [string]}, plus controller-specific keys
    void infoReceived(const QVariantMap& info);
    // {F: {pin,count,order,rgbw,khz,reverse}, R: {...}, E: {...}, G: {maxbri}}
    void configReceived(const QVariantMap& config);
    void commandResult(bool ok, const QString& message);
};
