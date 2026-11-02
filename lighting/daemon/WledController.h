#pragma once

#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QNetworkAccessManager>
#include <QSerialPort>
#include <QStringList>
#include <QTimer>
#include <QUdpSocket>
#include <functional>

#include "Controller.h"
#include "PixelRenderer.h"

// Drives a WLED controller (https://kno.wled.ge) through its JSON API, over
// the network (HTTP to http://<host>/json) or USB serial (JSON lines, at
// WLED's serial baud rate: 115200 unless changed). Each zone is one WLED
// segment, set in Settings > Lighting.
//
// Adalight (USB): we draw every LED ourselves (PixelRenderer: the Ozark
// firmware's patterns, music included) and stream them to WLED as Adalight
// frames. JSON on the same line still reads WLED's LED count and segment
// ranges. The frame rate follows the baud rate and the LED count.
//
// Patterns map to WLED effects by name (the effect list WLED reports; fixed
// ids over serial, which can't fetch it). Music patterns map to WLED's
// sound-reactive effects. Over the network they're fed with our audio
// analysis as WLED audio sync packets (UDP 11988, the AudioReactive usermod
// in "receive" mode, which we switch on); over serial they only react if the
// WLED board has its own microphone.
//
// LED pins, counts and segments are set up in WLED's own web page; the strip
// setup of the Ozark firmware doesn't apply. MAXBRI sets WLED's brightness.
class WledController : public Controller {
    Q_OBJECT
public:
    enum Link { Network, Serial, Adalight };

    WledController(Link link, const QString& host, int baud = 115200, QObject* parent = nullptr);

    // Which WLED segment each zone is: 'F', 'R', 'E' -> segment id, -1 = none
    void setSegments(const QMap<char, int>& segments);

    void setPortName(const QString& name) override;
    void start() override;
    void stop() override;
    bool isConnected() const override { return m_connected; }

    void setColor(char zone, const QColor& c) override;
    void setColor2(char zone, const QColor& c) override;
    void setPattern(char zone, int id) override;
    void setSpeed(char zone, int value) override;
    void setBrightness(char zone, int value) override;
    void saveLook() override {}  // WLED keeps its state; the looks are sent again on connect
    void sendAudio(const AudioFrame& frame) override;

    void requestInfo() override;
    void requestConfig() override;
    void stageConfig(char zone, const QString& key, const QString& value) override;
    void applyConfig() override;
    void test(char zone) override;

    void setMaxBrightness(int value) { m_maxBrightness = qBound(1, value, 255); }

private:
    using Look = PixelRenderer::Look;

    // A WLED segment's LEDs: [start, stop), reversed or not
    struct Range {
        int start = 0, stop = 0;
        bool reverse = false;
    };

    void tick();
    void setConnected(bool connected);
    void changed(char zone);
    void flush();
    QJsonObject segment(char zone, const Look& look) const;
    int effect(int pattern) const;
    void post(const QJsonObject& state);
    void handleState(const QJsonObject& root);  // {state, info} or /json
    void checkAudioSync();
    void renderFrame();  // Adalight
    void updateFrameRate();

    // network
    void get(const QString& path, std::function<void(const QJsonDocument&)> done);
    // serial
    void tryOpen();
    void onReadyRead();

    Link m_link;
    QString m_host;
    QHostAddress m_address;  // m_host resolved, for audio sync
    QString m_portName;
    int m_baud;
    QMap<char, int> m_segments{{'F', 0}, {'R', 1}, {'E', 2}};
    QMap<char, Look> m_looks;
    QMap<char, int> m_testStep;  // zone -> 1..3 while testing
    QList<char> m_dirty;
    int m_maxBrightness = 200;
    int m_stagedMaxBrightness = -1;

    QTimer m_timer;       // connect / keep-alive
    QTimer m_flushTimer;  // coalesces look changes (colour wheel drags)
    QTimer m_testTimer;
    bool m_started = false;
    bool m_connected = false;
    int m_misses = 0;
    bool m_busy = false;  // a state POST is in flight
    bool m_audioChecked = false;
    QString m_audioStatus;  // "", "on", "missing" (no AudioReactive usermod)
    quint16 m_audioPort = 11988;
    quint8 m_audioFrame = 0;

    QVariantMap m_info;
    QStringList m_effects;
    QStringList m_segmentNames;

    // Adalight
    QMap<int, Range> m_ranges;  // segment id -> its LEDs
    int m_ledCount = 0;
    QMap<char, PixelRenderer> m_renderers;
    Music m_music;
    QElapsedTimer m_clock;
    QTimer m_frameTimer;
    QByteArray m_frame;
    std::vector<quint8> m_zonePixels;

    QNetworkAccessManager m_net;
    QUdpSocket m_udp;
    QSerialPort m_port;
    QByteArray m_buf;
};
