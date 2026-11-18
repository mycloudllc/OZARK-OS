#pragma once

#include <QElapsedTimer>
#include <QSerialPort>
#include <QStringList>
#include <QTimer>

#include "Controller.h"

// Talks to the Ozark lighting controller firmware over serial
// (../PROTOCOL.md, v3). Handles auto-detect, reconnect, handshake and
// response parsing.
class LedController : public Controller {
    Q_OBJECT
public:
    explicit LedController(QObject* parent = nullptr);

    // USB serial adapters an ESP32 board may be on: USB-to-serial chips
    // (ttyUSB) first, then native USB (ttyACM)
    static QStringList autoDetectPorts();

    // Auto-detect with several boards plugged in (say, a WLED board next to
    // the Ozark controller): the ports to try, leaving out those that opened
    // and didn't answer. Forgotten whenever a board is plugged in or removed.
    class PortScan {
    public:
        QStringList candidates();
        void reject(const QString& port) { if (!m_rejected.contains(port)) m_rejected.append(port); }

    private:
        QStringList m_present;
        QStringList m_rejected;
    };

    void setPortName(const QString& name) override;
    void start() override;
    void stop() override;
    bool isConnected() const override { return m_ready; }

    // ---- look ----
    void setColor(char zone, const QColor& c) override;
    void setColor2(char zone, const QColor& c) override;
    void setBackground(char zone, const QColor& c) override;
    void setPattern(char zone, int id) override;
    void setSpeed(char zone, int value) override;
    void setBrightness(char zone, int value) override;
    void saveLook() override;
    void sendAudio(const AudioFrame& frame) override;

    // ---- hardware config (staged, then applied) ----
    void requestInfo() override;
    void requestConfig() override;
    void stageConfig(char zone, const QString& key, const QString& value) override;
    void applyConfig() override;
    void revertConfig() override;
    void test(char zone) override;
    void factoryReset();

signals:
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

    QSerialPort m_port;
    QTimer m_timer;
    QString m_portName;
    PortScan m_scan;
    QString m_openPort;        // the port open now
    QElapsedTimer m_openedAt;  // auto-detect: how long it's had to answer
    QByteArray m_buf;
    bool m_started = false;
    bool m_ready = false;
    bool m_collectingCfg = false;
    QVariantMap m_cfgAcc;
};
