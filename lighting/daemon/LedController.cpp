#include "LedController.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace {
constexpr int kBaud = 460800;  // must match SERIAL_BAUD in the firmware

QVariantMap parseKv(const QStringList& toks, int from) {
    QVariantMap m;
    for (int i = from; i < toks.size(); ++i) {
        const int eq = toks[i].indexOf('=');
        if (eq < 0) continue;
        const QString k = toks[i].left(eq);
        const QString v = toks[i].mid(eq + 1);
        bool ok = false;
        const int n = v.toInt(&ok);
        m[k] = ok ? QVariant(n) : QVariant(v);
    }
    return m;
}
}  // namespace

LedController::LedController(QObject* parent) : QObject(parent) {
    connect(&m_port, &QSerialPort::readyRead, this, &LedController::onReadyRead);
    connect(&m_port, &QSerialPort::errorOccurred, this, &LedController::onError);
    m_timer.setInterval(500);
    connect(&m_timer, &QTimer::timeout, this, &LedController::tick);
}

void LedController::setPortName(const QString& name) {
    m_portName = name;
    if (m_port.isOpen()) closePort();  // tick() reopens with the new name
}

void LedController::start() {
    m_started = true;
    m_timer.start();
    tick();
}

void LedController::stop() {
    m_started = false;
    m_timer.stop();
    closePort();
}

// USB vendor id of a tty, read from sysfs: walk up from the tty's device to
// the USB device that has idVendor (one level for ttyACM, two for ttyUSB).
static int usbVendorId(const QString& tty) {
    QString dir = QFileInfo("/sys/class/tty/" + tty + "/device").canonicalFilePath();
    for (int up = 0; up < 4 && !dir.isEmpty(); ++up) {
        QFile id(dir + "/idVendor");
        if (id.open(QIODevice::ReadOnly)) return id.readAll().trimmed().toInt(nullptr, 16);
        dir = QFileInfo(dir).path();
    }
    return -1;
}

// Called every 500 ms while no controller is connected, so it has to be cheap:
// QSerialPortInfo::availablePorts() enumerates every tty through udev, which
// kept Dash's UI thread busy. USB serial adapters are always ttyUSB*/ttyACM*.
QStringList LedController::autoDetectPorts() const {
    QStringList ports;
    const QStringList ttys = QDir("/dev").entryList({"ttyUSB*", "ttyACM*"}, QDir::System);
    for (const QString& tty : ttys) {
        const QString path = "/dev/" + tty;
        switch (usbVendorId(tty)) {
            case 0x10C4:  // Silicon Labs CP210x
            case 0x1A86:  // WCH CH340 / CH9102
            case 0x0403:  // FTDI
            case 0x303A:  // Espressif native USB
                ports.append(path);
        }
    }
    return ports;
}

void LedController::tick() {
    if (!m_started) return;
    if (!m_port.isOpen()) {
        tryOpen();
    } else if (!m_ready) {
        m_port.write("PING\n");  // handshake until PONG / READY arrives
    }
}

void LedController::tryOpen() {
    // Auto-detect tries each candidate until one opens
    const QStringList names = m_portName.isEmpty() ? autoDetectPorts() : QStringList{m_portName};
    bool opened = false;
    for (const QString& name : names) {
        m_port.setPortName(name);
        if (m_port.open(QIODevice::ReadWrite)) {
            opened = true;
            break;
        }
    }
    if (!opened) return;

    m_port.setBaudRate(kBaud);
    m_port.setDataBits(QSerialPort::Data8);
    m_port.setParity(QSerialPort::NoParity);
    m_port.setStopBits(QSerialPort::OneStop);
    m_port.setFlowControl(QSerialPort::NoFlowControl);
    // Many ESP32 boards reset on DTR/RTS toggles. Release both; the board may
    // still reboot once on open, which the PING handshake covers.
    m_port.setDataTerminalReady(false);
    m_port.setRequestToSend(false);
    m_buf.clear();
}

void LedController::closePort() {
    if (m_port.isOpen()) m_port.close();
    m_buf.clear();
    setReady(false);
}

void LedController::setReady(bool ready) {
    if (m_ready == ready) return;
    m_ready = ready;
    emit connectedChanged(ready);
}

void LedController::onError(QSerialPort::SerialPortError error) {
    if (error == QSerialPort::ResourceError || error == QSerialPort::DeviceNotFoundError ||
        error == QSerialPort::PermissionError) {
        closePort();
    }
}

void LedController::onReadyRead() {
    m_buf += m_port.readAll();
    if (m_buf.size() > 8192) m_buf.clear();  // runaway line, resync

    int idx;
    while ((idx = m_buf.indexOf('\n')) >= 0) {
        const QString line = QString::fromUtf8(m_buf.left(idx)).trimmed();
        m_buf.remove(0, idx + 1);
        if (!line.isEmpty()) handleLine(line);
    }
}

void LedController::handleLine(const QString& line) {
    emit lineReceived(line);

    // READY is printed at every boot (including after a pin-change reboot)
    if (line == "PONG" || line.startsWith("READY")) {
        setReady(true);
        emit boardReady();
        requestInfo();
        requestConfig();
        return;
    }

    if (line.startsWith("INFO ")) {
        const QStringList toks = line.split(' ', Qt::SkipEmptyParts);
        QVariantMap raw = parseKv(toks, 1);
        QVariantMap info;
        info["fw"] = raw.value("fw");
        info["max_leds"] = raw.value("max_leds");
        QVariantList pins;
        for (const QString& p : raw.value("pins").toString().split(',', Qt::SkipEmptyParts))
            pins << p.toInt();
        info["pins"] = pins;
        info["orders"] = raw.value("orders").toString().split(',', Qt::SkipEmptyParts);
        emit infoReceived(info);
        return;
    }

    if (line.startsWith("CFG ")) {
        const QStringList toks = line.split(' ', Qt::SkipEmptyParts);
        if (toks.size() >= 3) {
            m_cfgAcc[toks[1]] = parseKv(toks, 2);
            m_collectingCfg = true;
        }
        return;
    }

    if (line == "END") {
        if (m_collectingCfg) {
            emit configReceived(m_cfgAcc);
            m_cfgAcc.clear();
            m_collectingCfg = false;
        }
        return;
    }

    if (line.startsWith("OK")) {
        emit commandResult(true, line);
    } else if (line.startsWith("ERR")) {
        emit commandResult(false, line);
    }
}

void LedController::send(const QString& cmd) {
    if (!m_port.isOpen() || !m_ready) return;
    m_port.write(cmd.toUtf8() + '\n');
}

// ---- look ----

void LedController::setColor(char zone, const QColor& c) {
    send(QString("COLOR %1 %2 %3 %4").arg(QChar(zone)).arg(c.red()).arg(c.green()).arg(c.blue()));
}

void LedController::setColor2(char zone, const QColor& c) {
    send(QString("COLOR2 %1 %2 %3 %4").arg(QChar(zone)).arg(c.red()).arg(c.green()).arg(c.blue()));
}

void LedController::setPattern(char zone, int id) {
    send(QString("PAT %1 %2").arg(QChar(zone)).arg(id));
}

void LedController::setSpeed(char zone, int value) {
    send(QString("SPEED %1 %2").arg(QChar(zone)).arg(qBound(0, value, 255)));
}

void LedController::setBrightness(char zone, int value) {
    send(QString("BRI %1 %2").arg(QChar(zone)).arg(qBound(0, value, 255)));
}

void LedController::saveLook() { send("SAVE"); }

void LedController::sendAudio(std::array<quint8, 8> bands, bool beat) {
    // Drop frames rather than queue them if the link is backed up
    if (!m_ready || m_port.bytesToWrite() > 256) return;
    QString s = "AUD";
    for (quint8 b : bands) s += ' ' + QString::number(b);
    s += beat ? " 1" : " 0";
    send(s);
}

// ---- hardware config ----

void LedController::requestInfo() { send("INFO"); }

void LedController::requestConfig() {
    m_cfgAcc.clear();
    m_collectingCfg = false;
    send("CFGGET");
}

void LedController::stageConfig(char zone, const QString& key, const QString& value) {
    send(QString("CFG %1 %2 %3").arg(QChar(zone)).arg(key.toUpper()).arg(value));
}

void LedController::applyConfig() { send("CFGAPPLY"); }

void LedController::revertConfig() { send("CFGREVERT"); }

void LedController::test(char zone) { send(QString("TEST %1").arg(QChar(zone))); }

void LedController::factoryReset() { send("FACTORY"); }
