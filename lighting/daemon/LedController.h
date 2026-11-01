#pragma once

#include <QColor>
#include <QObject>
#include <QSerialPort>
#include <QString>
#include <QTimer>
#include <QVariantMap>
#include <array>

// Talks to the lighting controller firmware over serial (../PROTOCOL.md, v3).
// Handles auto-detect, reconnect, handshake and response parsing.
//
// Zones: 'F' interior front, 'R' interior rear, 'E' exterior, 'G' global (CFG).
class LedController : public QObject {
    Q_OBJECT
public:
    explicit LedController(QObject* parent = nullptr);

    // Empty name = auto-detect (CP210x, CH340, FTDI, ESP32 native USB)
    void setPortName(const QString& name);
    void start();  // begin connect / auto-reconnect loop
    void stop();
    bool isConnected() const { return m_ready; }

    // ---- look ----
    void setColor(char zone, const QColor& c);
    void setColor2(char zone, const QColor& c);
    void setPattern(char zone, int id);
    void setSpeed(char zone, int value);       // 0-255
    void setBrightness(char zone, int value);  // 0-255
    void saveLook();                           // persist look on the board
    void sendAudio(std::array<quint8, 8> bands, bool beat);

    // ---- hardware config (staged, then applied) ----
    void requestInfo();
    void requestConfig();
    // keys: PIN COUNT ORDER RGBW KHZ REVERSE (zone F/R/A), MAXBRI (zone 'G')
    void stageConfig(char zone, const QString& key, const QString& value);
    void applyConfig();   // validates, applies, saves on the board
    void revertConfig();
    void test(char zone); // 3 s red / green / blue test
    void factoryReset();

signals:
    void connectedChanged(bool connected);
    void boardReady();  // every READY/PONG, also after a board reboot
    // {fw, max_leds, pins: [int], orders: [string]}
    void infoReceived(const QVariantMap& info);
    // {F: {pin,count,order,rgbw,khz,reverse}, R: {...}, G: {maxbri}}
    void configReceived(const QVariantMap& config);
    void commandResult(bool ok, const QString& message);
    void lineReceived(const QString& line);

private:
    void tick();
    void tryOpen();
    void closePort();
    void setReady(bool ready);
    void onReadyRead();
    void onError(QSerialPort::SerialPortError error);
    void handleLine(const QString& line);
    void send(const QString& cmd);
    QStringList autoDetectPorts() const;

    QSerialPort m_port;
    QTimer m_timer;
    QString m_portName;
    QByteArray m_buf;
    bool m_started = false;
    bool m_ready = false;
    bool m_collectingCfg = false;
    QVariantMap m_cfgAcc;
};
