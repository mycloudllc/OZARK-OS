#include "WledController.h"

#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QUrl>
#include <cmath>
#include <cstring>
#include <functional>

#include "LedController.h"

namespace {
constexpr int kPollMs = 3000;        // keep-alive / state poll
constexpr int kMaxMisses = 3;        // polls without an answer: disconnected
constexpr int kFlushMs = 40;         // at most ~25 state updates a second
constexpr int kTimeoutMs = 2500;
constexpr int kMinFrameMs = 25;      // Adalight: at most 40 frames a second

// Our patterns (../PROTOCOL.md ids) as WLED effects: the name in WLED's
// effect list, and its id in WLED 0.14/0.15 when there's no list (serial)
struct Effect {
    const char* name;
    int id;
};
const Effect kEffects[] = {
    {"Solid", 0},         // 0 Off: the segment is switched off instead
    {"Solid", 0},         // 1 Solid
    {"Rainbow", 9},       // 2 Rainbow
    {"Chase", 28},        // 3 Chase
    {"Breathe", 2},       // 4 Breathe
    {"Gradient", 46},     // 5 Gradient
    {"Twinkle", 17},      // 6 Twinkle
    {"Lighthouse", 41},   // 7 Comet (WLED's comet effect)
    {"Gravimeter", 132},  // 8 VU Meter (sound reactive)
    {"Freqmatrix", 138},  // 9 Spectrum (sound reactive)
    {"Puddlepeak", 144},  // 10 Beat Pulse (sound reactive)
};
constexpr int kPatterns = sizeof(kEffects) / sizeof(kEffects[0]);

QJsonArray rgb(const QColor& c) { return {c.red(), c.green(), c.blue()}; }

// WLED AudioReactive usermod's audio sync packet, version 2 (44 bytes)
QByteArray audioPacket(const AudioFrame& f, quint8 counter) {
    QByteArray p(44, '\0');
    char* d = p.data();
    auto putFloat = [d](int offset, float value) { std::memcpy(d + offset, &value, 4); };  // little-endian
    std::memcpy(d, "00002", 6);          // header
    putFloat(8, f.volume);               // sampleRaw
    putFloat(12, f.smooth_volume);       // sampleSmth
    d[16] = f.beat ? 1 : 0;              // samplePeak
    d[17] = char(counter);               // frameCounter
    for (int i = 0; i < 16; ++i)         // fftResult
        d[18 + i] = char(f.fft[i]);
    // 34: zeroCrossingCount (unused)
    putFloat(36, f.peak_magnitude * 16);  // FFT_Magnitude, roughly WLED's own scale
    putFloat(40, f.peak_hz);              // FFT_MajorPeak
    return p;
}
}  // namespace

WledController::WledController(Link link, const QString& host, int baud, QObject* parent)
    : Controller(parent), m_link(link), m_host(host.trimmed()), m_baud(baud > 0 ? baud : 115200) {
    for (char zone : {'F', 'R', 'E'}) {
        m_looks[zone] = Look();
        m_renderers[zone] = PixelRenderer();
    }
    m_clock.start();
    connect(&m_frameTimer, &QTimer::timeout, this, &WledController::renderFrame);

    m_timer.setInterval(kPollMs);
    connect(&m_timer, &QTimer::timeout, this, &WledController::tick);
    m_flushTimer.setSingleShot(true);
    m_flushTimer.setInterval(kFlushMs);
    connect(&m_flushTimer, &QTimer::timeout, this, &WledController::flush);
    m_testTimer.setInterval(1000);
    connect(&m_testTimer, &QTimer::timeout, this, [this] {
        for (auto it = m_testStep.begin(); it != m_testStep.end();) {
            if (++it.value() > 3) {
                const char zone = it.key();
                it = m_testStep.erase(it);
                changed(zone);  // back to its look
            } else {
                changed(it.key());  // next colour
                ++it;
            }
        }
        if (m_testStep.isEmpty()) m_testTimer.stop();
        flush();
    });

    m_net.setTransferTimeout(kTimeoutMs);
    connect(&m_port, &QSerialPort::readyRead, this, &WledController::onReadyRead);
    connect(&m_port, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError error) {
        if (error == QSerialPort::ResourceError || error == QSerialPort::DeviceNotFoundError ||
            error == QSerialPort::PermissionError) {
            m_port.close();
            setConnected(false);
        }
    });
}

void WledController::setSegments(const QMap<char, int>& segments) {
    m_segments = segments;
    for (char zone : m_looks.keys()) changed(zone);
}

void WledController::setPortName(const QString& name) {
    m_portName = name;
    if (m_port.isOpen()) {
        m_port.close();
        setConnected(false);
    }
}

void WledController::start() {
    m_started = true;
    m_timer.start();
    tick();
}

void WledController::stop() {
    m_started = false;
    m_timer.stop();
    m_flushTimer.stop();
    m_frameTimer.stop();
    if (m_port.isOpen()) m_port.close();
    setConnected(false);
}

void WledController::setConnected(bool connected) {
    if (connected == m_connected) return;
    m_connected = connected;
    m_misses = 0;
    m_audioChecked = false;
    emit connectedChanged(connected);
    if (m_link == Adalight) {
        if (connected) {
            // Our frames carry the brightness (zone and max brightness): WLED's at full
            post(QJsonObject{{"on", true}, {"bri", 255}, {"lor", 0}});
            updateFrameRate();
            m_frameTimer.start();
        } else {
            m_frameTimer.stop();
        }
    }
    if (connected) {
        // Send every zone's look: WLED may have restarted with another state
        emit boardReady();
        for (char zone : m_looks.keys()) changed(zone);
    }
}

// ---------------------------------------------------------------- link

void WledController::tick() {
    if (!m_started) return;
    if (m_connected && ++m_misses > kMaxMisses) setConnected(false);

    if (m_link == Network) {
        if (m_host.isEmpty()) return;
        get("/json/si", [this](const QJsonDocument& doc) {
            const QJsonObject root = doc.object();
            m_misses = 0;
            const bool first = !m_connected;
            handleState(root);
            if (!first) return;
            // The address for audio sync: the host part of "host[:port]"
            const QString hostName = QUrl("http://" + m_host).host();
            if (m_address.isNull() && !m_address.setAddress(hostName)) {
                QHostInfo::lookupHost(hostName, this, [this](const QHostInfo& host) {
                    if (!host.addresses().isEmpty()) m_address = host.addresses().first();
                });
            }
            // Effect names first, so the looks map to the right effects
            // A bare array of names
            get("/json/effects", [this](const QJsonDocument& doc) {
                m_effects.clear();
                for (const QJsonValue& name : doc.array())
                    m_effects.append(name.toString());
                setConnected(true);
            });
        });
        return;
    }

    if (!m_port.isOpen()) {
        tryOpen();
        return;
    }
    // Ask for state and info: the answer is the keep-alive
    m_port.write("{\"v\":true}\n");
}

// GET http://host<path>; done gets the JSON if it arrived
void WledController::get(const QString& path, std::function<void(const QJsonDocument&)> done) {
    QNetworkRequest request(QUrl(QString("http://%1%2").arg(m_host, path)));
    QNetworkReply* reply = m_net.get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, done] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isNull()) done(doc);
    });
}

void WledController::tryOpen() {
    const QStringList names = m_portName.isEmpty() ? LedController::autoDetectPorts() : QStringList{m_portName};
    for (const QString& name : names) {
        m_port.setPortName(name);
        if (m_port.open(QIODevice::ReadWrite)) break;
    }
    if (!m_port.isOpen()) return;
    m_port.setBaudRate(m_baud);
    m_port.setDataBits(QSerialPort::Data8);
    m_port.setParity(QSerialPort::NoParity);
    m_port.setStopBits(QSerialPort::OneStop);
    m_port.setFlowControl(QSerialPort::NoFlowControl);
    // Many ESP32 boards reset on DTR/RTS toggles
    m_port.setDataTerminalReady(false);
    m_port.setRequestToSend(false);
    m_buf.clear();
    m_port.write("{\"v\":true}\n");
}

void WledController::onReadyRead() {
    m_buf += m_port.readAll();
    if (m_buf.size() > 65536) m_buf.clear();  // runaway line, resync

    int idx;
    while ((idx = m_buf.indexOf('\n')) >= 0) {
        const QByteArray line = m_buf.left(idx).trimmed();
        m_buf.remove(0, idx + 1);
        // WLED answers {"v":true} with {"state":…,"info":…}; anything else
        // (boot messages, Improv) is skipped
        if (!line.startsWith('{')) continue;
        const QJsonDocument doc = QJsonDocument::fromJson(line);
        if (!doc.isObject() || !doc.object().contains("state")) continue;
        m_misses = 0;
        handleState(doc.object());
        setConnected(true);
    }
}

// {state, info}: segments, version, name, LED count
void WledController::handleState(const QJsonObject& root) {
    const QJsonObject info = root.value("info").toObject();
    const QJsonObject state = root.value("state").toObject();

    // WLED restarted (uptime went back): send it the looks again
    const qint64 uptime = info.value("uptime").toVariant().toLongLong();
    const bool restarted = m_connected && uptime < m_info.value("uptime").toLongLong();

    QStringList segments;
    m_ranges.clear();
    for (const QJsonValue& value : state.value("seg").toArray()) {
        const QJsonObject seg = value.toObject();
        const int start = seg.value("start").toInt(), stop = seg.value("stop").toInt();
        m_ranges[seg.value("id").toInt()] = Range{start, stop, seg.value("rev").toBool()};
        QString name = seg.value("n").toString();
        if (name.isEmpty()) name = QString("LEDs %1-%2").arg(start).arg(stop - 1);
        segments.append(QString("%1: %2").arg(seg.value("id").toInt()).arg(name));
    }

    QVariantMap updated{{"fw", QString("WLED %1").arg(info.value("ver").toString())},
                        {"max_leds", info.value("leds").toObject().value("count").toInt()},
                        {"pins", QVariantList()},
                        {"orders", QStringList()},
                        {"name", info.value("name").toString()},
                        {"segments", segments},
                        {"audio", m_audioStatus}};
    const int ledCount = info.value("leds").toObject().value("count").toInt();
    if (ledCount != m_ledCount) {
        m_ledCount = ledCount;
        updateFrameRate();
    }
    if (m_link == Adalight)
        updated["fps"] = m_frameTimer.interval() > 0 ? 1000 / m_frameTimer.interval() : 0;
    const bool changedInfo = updated != m_info.value("public").toMap();
    m_info = updated;
    m_info["public"] = updated;
    m_info["uptime"] = uptime;
    if (changedInfo) emit infoReceived(updated);
    if (restarted) {
        m_audioChecked = false;
        emit boardReady();
        for (char zone : m_looks.keys()) changed(zone);
    }
}

// ---------------------------------------------------------------- looks

void WledController::setColor(char zone, const QColor& c) {
    if (!m_looks.contains(zone)) return;
    m_looks[zone].color = c;
    changed(zone);
}

void WledController::setColor2(char zone, const QColor& c) {
    if (!m_looks.contains(zone)) return;
    m_looks[zone].color2 = c;
    changed(zone);
}

void WledController::setPattern(char zone, int id) {
    if (!m_looks.contains(zone)) return;
    m_looks[zone].pattern = qBound(0, id, kPatterns - 1);
    changed(zone);
}

void WledController::setSpeed(char zone, int value) {
    if (!m_looks.contains(zone)) return;
    m_looks[zone].speed = qBound(0, value, 255);
    changed(zone);
}

void WledController::setBrightness(char zone, int value) {
    if (!m_looks.contains(zone)) return;
    m_looks[zone].brightness = qBound(0, value, 255);
    changed(zone);
}

void WledController::changed(char zone) {
    if (!m_dirty.contains(zone)) m_dirty.append(zone);
    if (!m_flushTimer.isActive()) m_flushTimer.start();
}

int WledController::effect(int pattern) const {
    const Effect& e = kEffects[qBound(0, pattern, kPatterns - 1)];
    for (int i = 0; i < m_effects.size(); ++i) {
        if (m_effects[i].compare(e.name, Qt::CaseInsensitive) == 0) return i;
    }
    return e.id;
}

QJsonObject WledController::segment(char zone, const Look& look) const {
    QJsonObject seg{{"id", m_segments.value(zone, -1)}};
    if (m_testStep.contains(zone)) {
        static const QColor kTest[] = {Qt::red, Qt::green, Qt::blue};
        seg["on"] = true;
        seg["fx"] = 0;
        seg["bri"] = 255;
        seg["col"] = QJsonArray{rgb(kTest[m_testStep.value(zone) - 1])};
        return seg;
    }
    if (look.pattern == 0) {
        seg["on"] = false;
        return seg;
    }
    seg["on"] = true;
    seg["frz"] = false;
    seg["fx"] = effect(look.pattern);
    seg["sx"] = look.speed;
    seg["bri"] = look.brightness;
    seg["pal"] = 0;  // the effect's default palette: most use the colours
    seg["col"] = QJsonArray{rgb(look.color), rgb(look.color2), QJsonArray{0, 0, 0}};
    return seg;
}

void WledController::flush() {
    if (m_link == Adalight) {  // the next frame draws the new looks
        m_dirty.clear();
        return;
    }
    if (!m_connected || m_dirty.isEmpty()) return;
    if (m_busy) {  // the last update is still on its way
        m_flushTimer.start();
        return;
    }
    QJsonArray segs;
    for (char zone : m_dirty) {
        if (m_segments.value(zone, -1) >= 0) segs.append(segment(zone, m_looks.value(zone)));
    }
    m_dirty.clear();
    if (segs.isEmpty()) return;
    // lor 0: take over from a live stream (e.g. a PC running LedFx)
    post(QJsonObject{{"on", true}, {"bri", m_maxBrightness}, {"lor", 0}, {"seg", segs}});
}

void WledController::post(const QJsonObject& state) {
    const QByteArray body = QJsonDocument(state).toJson(QJsonDocument::Compact);
    if (m_link != Network) {  // serial JSON, also alongside Adalight
        if (m_port.isOpen()) m_port.write(body + '\n');
        return;
    }
    QNetworkRequest request(QUrl(QString("http://%1/json/state").arg(m_host)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    m_busy = true;
    QNetworkReply* reply = m_net.post(request, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        m_busy = false;
    });
}

// ---------------------------------------------------------------- music

void WledController::sendAudio(const AudioFrame& frame) {
    if (m_link == Adalight) {  // drawn here, like the Ozark firmware does
        m_music.bands = frame.bands;
        m_music.beat |= frame.beat;
        m_music.last_ms = m_clock.elapsed();
        return;
    }
    if (m_link != Network || !m_connected) return;
    checkAudioSync();
    if (m_address.isNull() || m_audioStatus == "missing") return;
    m_udp.writeDatagram(audioPacket(frame, m_audioFrame++), m_address, m_audioPort);
}

// Once per connection, with music on: is WLED's AudioReactive usermod there,
// and receiving audio sync? Switch it to receive if not.
void WledController::checkAudioSync() {
    if (m_audioChecked) return;
    m_audioChecked = true;
    get("/json/cfg", [this](const QJsonDocument& doc) {
        const QJsonObject cfg = doc.object();
        const QJsonObject audio = cfg.value("um").toObject().value("AudioReactive").toObject();
        const QString before = m_audioStatus;
        if (audio.isEmpty()) {
            qWarning("WLED has no AudioReactive usermod: music patterns won't follow the music");
            m_audioStatus = "missing";
        } else {
            const QJsonObject sync = audio.value("sync").toObject();
            m_audioPort = quint16(sync.value("port").toInt(11988));
            if (!audio.value("enabled").toBool() || sync.value("mode").toInt() != 2) {
                // mode 2: receive. Stored in WLED's config, so this happens once.
                const QJsonObject change{
                    {"um", QJsonObject{{"AudioReactive",
                                        QJsonObject{{"enabled", true}, {"sync", QJsonObject{{"mode", 2}}}}}}}};
                QNetworkRequest request(QUrl(QString("http://%1/json/cfg").arg(m_host)));
                request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
                QNetworkReply* reply = m_net.post(request, QJsonDocument(change).toJson(QJsonDocument::Compact));
                connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
            }
            m_audioStatus = "on";
        }
        if (m_audioStatus != before) {
            QVariantMap info = m_info.value("public").toMap();
            info["audio"] = m_audioStatus;
            m_info["public"] = info;
            emit infoReceived(info);
        }
    });
}

// ---------------------------------------------------------------- config

void WledController::requestInfo() {
    if (m_connected) emit infoReceived(m_info.value("public").toMap());
}

void WledController::requestConfig() {
    emit configReceived({{"G", QVariantMap{{"maxbri", m_maxBrightness}}}});
}

// Only the brightness cap applies: WLED's LED setup is in its own web page
void WledController::stageConfig(char zone, const QString& key, const QString& value) {
    if (zone == 'G' && key.compare("MAXBRI", Qt::CaseInsensitive) == 0) m_stagedMaxBrightness = value.toInt();
}

void WledController::applyConfig() {
    if (m_stagedMaxBrightness > 0) setMaxBrightness(m_stagedMaxBrightness);
    m_stagedMaxBrightness = -1;
    for (char zone : m_looks.keys()) changed(zone);
    emit commandResult(true, "OK APPLIED");
}

void WledController::test(char zone) {
    if (!m_looks.contains(zone)) return;
    if (m_link == Adalight) {
        m_renderers[zone].test(m_clock.elapsed());
        return;
    }
    m_testStep[zone] = 1;
    changed(zone);
    if (!m_testTimer.isActive()) m_testTimer.start();
}

// ---------------------------------------------------------------- Adalight

// As fast as the serial line carries a frame, with some headroom
void WledController::updateFrameRate() {
    const int bytes = 6 + 3 * std::max(m_ledCount, 1);
    const int lineMs = int(std::ceil(bytes * 10.0 * 1000 / m_baud * 1.15));
    m_frameTimer.setInterval(std::max(kMinFrameMs, lineMs));
}

void WledController::renderFrame() {
    if (!m_connected || m_ledCount <= 0) return;
    const int bytes = 6 + 3 * m_ledCount;
    // The line is backed up (e.g. WLED's baud rate is lower than ours): skip a frame
    if (m_port.bytesToWrite() > bytes) return;

    m_frame.resize(bytes);
    quint8* d = reinterpret_cast<quint8*>(m_frame.data());
    const int last = m_ledCount - 1;
    d[0] = 'A';
    d[1] = 'd';
    d[2] = 'a';
    d[3] = quint8(last >> 8);
    d[4] = quint8(last & 0xFF);
    d[5] = quint8(d[3] ^ d[4] ^ 0x55);
    quint8* pixels = d + 6;
    std::memset(pixels, 0, size_t(3 * m_ledCount));

    const qint64 now = m_clock.elapsed();
    for (auto it = m_renderers.begin(); it != m_renderers.end(); ++it) {
        const auto range = m_ranges.constFind(m_segments.value(it.key(), -1));
        if (range == m_ranges.constEnd()) continue;
        const int start = qBound(0, range->start, m_ledCount);
        const int n = qBound(0, range->stop, m_ledCount) - start;
        if (n <= 0) continue;
        m_zonePixels.resize(size_t(3 * n));
        it->render(now, m_looks.value(it.key()), m_music, m_maxBrightness, m_zonePixels.data(), n);
        for (int i = 0; i < n; ++i) {
            const int led = start + (range->reverse ? n - 1 - i : i);
            std::memcpy(pixels + 3 * led, m_zonePixels.data() + 3 * i, 3);
        }
    }
    m_music.beat = false;
    m_port.write(m_frame);
}
