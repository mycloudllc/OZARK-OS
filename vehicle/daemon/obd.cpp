#include "obd.h"

#include <QDebug>

namespace {

using Decode = double (*)(const QByteArray &d);

double u8(const QByteArray &d, int i) { return d.size() > i ? quint8(d[i]) : 0; }
double u16(const QByteArray &d) { return u8(d, 0) * 256 + u8(d, 1); }

// SAE J1979 mode 01 PIDs worth showing, decoded to SI units. `fast` ones are
// asked for every cycle, the rest in turn every few cycles.
struct Pid {
    int pid;
    const char *key;
    bool fast;
    int min_bytes;
    Decode decode;
};
const Pid kPids[] = {
    {0x0C, "rpm", true, 2, [](const QByteArray &d) { return u16(d) / 4.0; }},
    {0x0D, "speed_kmh", true, 1, [](const QByteArray &d) { return u8(d, 0); }},
    {0x11, "throttle_pct", true, 1, [](const QByteArray &d) { return u8(d, 0) * 100.0 / 255.0; }},
    {0x04, "load_pct", true, 1, [](const QByteArray &d) { return u8(d, 0) * 100.0 / 255.0; }},
    {0x05, "coolant_c", false, 1, [](const QByteArray &d) { return u8(d, 0) - 40; }},
    {0x0F, "intake_c", false, 1, [](const QByteArray &d) { return u8(d, 0) - 40; }},
    {0x2F, "fuel_pct", false, 1, [](const QByteArray &d) { return u8(d, 0) * 100.0 / 255.0; }},
    {0x42, "voltage_v", false, 2, [](const QByteArray &d) { return u16(d) / 1000.0; }},
    {0x46, "ambient_c", false, 1, [](const QByteArray &d) { return u8(d, 0) - 40; }},
    {0x5C, "oil_c", false, 1, [](const QByteArray &d) { return u8(d, 0) - 40; }},
    {0x0B, "map_kpa", false, 1, [](const QByteArray &d) { return u8(d, 0); }},
    {0x10, "maf_gs", false, 2, [](const QByteArray &d) { return u16(d) / 100.0; }},
    {0x33, "baro_kpa", false, 1, [](const QByteArray &d) { return u8(d, 0); }},
    {0x5E, "fuel_rate_lph", false, 2, [](const QByteArray &d) { return u16(d) / 20.0; }},
    {0x1F, "run_time_s", false, 2, [](const QByteArray &d) { return u16(d); }},
    {0xA6, "odometer_km", false, 4,
     [](const QByteArray &d) {
         return (u8(d, 0) * 16777216.0 + u8(d, 1) * 65536.0 + u8(d, 2) * 256.0 + u8(d, 3)) / 10.0;
     }},
    // Monitor status: MIL and how many stored codes
    {0x01, "", false, 1, nullptr},
};

const Pid *pid_info(int pid)
{
    for (const Pid &p : kPids)
        if (p.pid == pid)
            return &p;
    return nullptr;
}

const char *state_name(Obd::State s)
{
    switch (s) {
        case Obd::State::Off: return "off";
        case Obd::State::Connecting: return "connecting";
        case Obd::State::NoCar: return "no-car";
        case Obd::State::Connected: return "connected";
        case Obd::State::Error: return "error";
    }
    return "off";
}

}  // namespace

Obd::Obd(QObject *parent)
    : QObject(parent)
{
    poll_timer.setSingleShot(true);
    connect(&poll_timer, &QTimer::timeout, this, &Obd::poll);
    probe_timer.setInterval(4000);
    connect(&probe_timer, &QTimer::timeout, this, &Obd::probe);
    reopen_timer.setSingleShot(true);
    connect(&reopen_timer, &QTimer::timeout, this, [this] {
        if (transport) {
            set_state(State::Connecting, "Connecting to the adapter");
            transport->open();
        }
    });
    // Values go out at most 4 times a second, all together
    flush_timer.setInterval(250);
    connect(&flush_timer, &QTimer::timeout, this, &Obd::flush_values);
    flush_timer.start();
}

Obd::~Obd()
{
    set_transport(nullptr, QString());
}

void Obd::set_transport(ObdTransport *t, const QString &description)
{
    poll_timer.stop();
    probe_timer.stop();
    reopen_timer.stop();
    queue.clear();
    busy = false;
    if (transport) {
        transport->disconnect(this);
        transport->close();
        transport->deleteLater();
    }
    transport = t;
    link_description = description;
    supported.clear();
    ecus.clear();
    vin.clear();
    values_.clear();
    changed.clear();
    emit values_changed(values_);
    if (!transport) {
        set_state(State::Off, "OBD is off");
        return;
    }
    transport->setParent(this);
    connect(transport, &ObdTransport::opened, this, &Obd::on_opened);
    connect(transport, &ObdTransport::lost, this, &Obd::on_lost);
    set_state(State::Connecting, "Connecting to the adapter");
    transport->open();
}

void Obd::set_state(State state, const QString &text)
{
    if (state == state_ && text == message)
        return;
    if (text != message)
        qInfo().noquote() << "[obd]" << text;
    state_ = state;
    message = text;
    emit status_changed();
}

QVariantMap Obd::status() const
{
    QVariantList pids;
    for (int p : supported)
        pids << p;
    QStringList ecu_list;
    for (quint32 e : ecus)
        ecu_list << QString::number(e, 16).toUpper();
    return {
        {"state", state_name(state_)},
        {"link", link_description},
        {"protocol", transport ? transport->protocol() : QString()},
        {"vin", vin},
        {"message", message},
        {"ecus", ecu_list},
        {"supported", pids},
    };
}

void Obd::enqueue(const QByteArray &request, int timeout_ms, ObdTransport::Done done, bool urgent)
{
    Job job {request, timeout_ms, std::move(done)};
    if (urgent)
        queue.prepend(job);
    else
        queue.append(job);
    run_next();
}

void Obd::run_next()
{
    if (busy || queue.isEmpty() || !transport)
        return;
    busy = true;
    Job job = queue.takeFirst();
    ObdTransport *t = transport;
    t->request(job.request, job.timeout_ms, [this, t, job](bool ok, const EcuReplies &r, const QString &error) {
        if (t != transport)  // switched adapters meanwhile
            return;
        busy = false;
        for (auto it = r.begin(); it != r.end(); ++it)
            if (!ecus.contains(it.key()))
                ecus << it.key();
        job.done(ok, r, error);
        run_next();
    });
}

QList<QByteArray> Obd::answers(const EcuReplies &replies, quint8 service)
{
    QList<QByteArray> out;
    for (const auto &messages : replies)
        for (const QByteArray &m : messages)
            if (!m.isEmpty() && quint8(m[0]) == quint8(service + 0x40))
                out << m;
    return out;
}

void Obd::on_opened(bool ok, const QString &text)
{
    if (!ok) {
        set_state(State::Error, text);
        reopen_later(10000);
        return;
    }
    set_state(State::NoCar, text);
    probe();
}

void Obd::on_lost(const QString &text)
{
    poll_timer.stop();
    probe_timer.stop();
    queue.clear();
    busy = false;
    values_.clear();
    emit values_changed(values_);
    set_state(State::Error, text);
    reopen_later(5000);
}

void Obd::reopen_later(int ms)
{
    poll_timer.stop();
    probe_timer.stop();
    reopen_timer.start(ms);
}

// Is the car awake? "0100" is answered by every OBD-II car with the ignition on
void Obd::probe()
{
    if (!queue.isEmpty() || busy)
        return;
    enqueue(QByteArray::fromHex("0100"), 3000, [this](bool ok, const EcuReplies &r, const QString &error) {
        if (!ok && !error.isEmpty() && state_ != State::NoCar)
            qInfo().noquote() << "[obd]" << error;
        if (answers(r, 0x01).isEmpty()) {
            if (state_ != State::NoCar)
                set_state(State::NoCar, "No answer from the car (ignition off?)");
            if (!probe_timer.isActive())
                probe_timer.start();
            return;
        }
        probe_timer.stop();
        misses = 0;
        discover();
    });
}

// Which mode 01 PIDs exist: "01 00" answers for 01-20 as a bit mask, whose
// last bit says whether "01 20" is worth asking, and so on
void Obd::discover(int range)
{
    if (range == 0)
        supported.clear();
    enqueue(QByteArray::fromHex(QByteArray::number(0x0100 + range, 16).rightJustified(4, '0')), 2000,
            [this, range](bool, const EcuReplies &r, const QString &) {
                bool more = false;
                for (const QByteArray &m : answers(r, 0x01)) {
                    if (m.size() < 6 || quint8(m[1]) != range)
                        continue;
                    for (int byte = 0; byte < 4; byte++)
                        for (int bit = 0; bit < 8; bit++)
                            if (quint8(m[2 + byte]) & (0x80 >> bit))
                                supported.insert(range + byte * 8 + bit + 1);
                    if (quint8(m[5]) & 0x01)
                        more = true;
                }
                if (more && range < 0xC0) {
                    discover(range + 0x20);
                    return;
                }
                fast.clear();
                slow.clear();
                for (const Pid &p : kPids)
                    if (supported.contains(p.pid))
                        (p.fast ? fast : slow) << p.pid;
                qInfo().noquote() << "[obd]" << supported.size() << "PIDs supported;" << fast.size() << "fast,"
                                  << slow.size() << "slow";
                set_state(State::Connected, transport && !transport->protocol().isEmpty()
                                                ? QString("Connected: %1").arg(transport->protocol())
                                                : QString("Connected"));
                if (vin.isEmpty())
                    read_vin();
                // Know about the check-engine light from the start
                read_dtcs();
                poll_timer.start(0);
            });
}

void Obd::read_vin()
{
    const bool can = transport && transport->protocol().contains("CAN");
    enqueue(QByteArray::fromHex("0902"), 2000, [this, can](bool, const EcuReplies &r, const QString &) {
        QByteArray raw;
        const QList<QByteArray> got = answers(r, 0x09);
        if (can) {
            // 49 02 01 then 17 characters
            for (const QByteArray &m : got)
                if (m.size() > 3 && quint8(m[1]) == 0x02) {
                    raw = m.mid(3);
                    break;
                }
        } else {
            // One message per 4 characters: 49 02 <n> c c c c
            QMap<int, QByteArray> parts;
            for (const QByteArray &m : got)
                if (m.size() >= 7 && quint8(m[1]) == 0x02)
                    parts[quint8(m[2])] = m.mid(3, 4);
            for (const QByteArray &p : parts)
                raw += p;
        }
        QString text;
        for (char c : raw)
            if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z'))
                text += QChar(c);
        if (text.size() >= 11) {
            vin = text;
            qInfo().noquote() << "[obd] VIN" << vin;
            emit status_changed();
        }
    });
}

void Obd::poll()
{
    if (state_ != State::Connected || !transport)
        return;
    if (busy || !queue.isEmpty()) {
        poll_timer.start(50);
        return;
    }
    // Three fast PIDs, then one slow one
    int pid = -1;
    if (!slow.isEmpty() && (fast.isEmpty() || cycle % 4 == 3))
        pid = slow[slow_index++ % slow.size()];
    else if (!fast.isEmpty())
        pid = fast[fast_index++ % fast.size()];
    cycle++;
    if (pid < 0) {
        poll_timer.start(2000);
        return;
    }
    QByteArray request;
    request.append(char(0x01));
    request.append(char(pid));
    enqueue(request, 1000, [this, pid](bool ok, const EcuReplies &r, const QString &error) {
        bool got = false;
        for (const QByteArray &m : answers(r, 0x01)) {
            if (m.size() < 2 || quint8(m[1]) != pid)
                continue;
            const QByteArray data = m.mid(2);
            if (pid == 0x01) {
                if (!data.isEmpty()) {
                    set_value("mil", bool(quint8(data[0]) & 0x80));
                    set_value("dtc_count", int(quint8(data[0]) & 0x7F));
                }
            } else if (const Pid *p = pid_info(pid)) {
                if (data.size() >= p->min_bytes)
                    set_value(p->key, qRound(p->decode(data) * 100) / 100.0);
            }
            got = true;
            break;
        }
        if (got) {
            misses = 0;
        } else if (++misses >= 8) {
            // Engine and ignition off: stop polling, wait for it to wake
            values_.clear();
            emit values_changed(values_);
            set_state(State::NoCar, ok ? QString("The car stopped answering (ignition off?)") : error);
            probe_timer.start();
            return;
        }
        poll_timer.start(20);
    });
}

void Obd::set_value(const QString &key, const QVariant &value)
{
    if (values_.value(key) == value)
        return;
    values_[key] = value;
    changed[key] = value;
}

void Obd::flush_values()
{
    if (changed.isEmpty())
        return;
    changed.clear();
    emit values_changed(values_);
}

void Obd::read_dtcs()
{
    if (reading_dtcs)
        return;
    if (state_ != State::Connected) {
        emit dtcs_read({{"ok", false},
                        {"message", state_ == State::NoCar ? "No answer from the car. Turn the ignition on."
                                                           : "Not connected to the car"}});
        return;
    }
    reading_dtcs = true;
    dtc_step(0, {});
}

// 0: MIL and count (01 01), 1: stored (03), 2: pending (07), 3: permanent (0A)
void Obd::dtc_step(int step, QVariantMap result)
{
    static const quint8 kModes[] = {0x01, 0x03, 0x07, 0x0A};
    static const char *kKeys[] = {"", "stored", "pending", "permanent"};
    if (step > 3) {
        reading_dtcs = false;
        result["ok"] = true;
        result["read_at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        const int n = result.value("stored").toList().size();
        result["message"] = n ? QString("%1 stored code%2").arg(n).arg(n == 1 ? "" : "s") : QString("No stored codes");
        dtcs_ = result;
        emit dtcs_read(result);
        return;
    }
    const bool can = transport && transport->protocol().contains("CAN");
    const QByteArray request = step == 0 ? QByteArray::fromHex("0101") : QByteArray(1, char(kModes[step]));
    enqueue(
        request, 3000,
        [this, step, result, can](bool ok, const EcuReplies &r, const QString &error) mutable {
            if (!ok && step == 0) {
                reading_dtcs = false;
                emit dtcs_read({{"ok", false}, {"message", error}});
                return;
            }
            if (step == 0) {
                bool mil = false;
                int count = 0;
                for (const QByteArray &m : answers(r, 0x01))
                    if (m.size() >= 3 && quint8(m[1]) == 0x01) {
                        mil = mil || (quint8(m[2]) & 0x80);
                        count += quint8(m[2]) & 0x7F;
                    }
                result["mil"] = mil;
                result["count"] = count;
            } else {
                QVariantList codes;
                QSet<QString> seen;
                for (auto it = r.begin(); it != r.end(); ++it) {
                    for (const QByteArray &m : it.value()) {
                        if (m.isEmpty() || quint8(m[0]) != quint8(kModes[step] + 0x40))
                            continue;
                        // CAN: a count byte, then pairs; older protocols: pairs only
                        int i = can ? 2 : 1;
                        for (; i + 1 < m.size(); i += 2) {
                            const quint8 a = quint8(m[i]), b = quint8(m[i + 1]);
                            if (a == 0 && b == 0)
                                continue;
                            const QString code = dtc_code(a, b);
                            if (seen.contains(code))
                                continue;
                            seen.insert(code);
                            codes << QVariantMap {{"code", code},
                                                  {"description", dtc_description(code)},
                                                  {"ecu", QString::number(it.key(), 16).toUpper()}};
                        }
                    }
                }
                result[kKeys[step]] = codes;
            }
            dtc_step(step + 1, result);
        },
        true);
}

void Obd::clear_dtcs()
{
    if (state_ != State::Connected) {
        emit dtcs_cleared(false, "Not connected to the car");
        return;
    }
    enqueue(
        QByteArray(1, char(0x04)), 5000,
        [this](bool ok, const EcuReplies &r, const QString &error) {
            if (!ok) {
                emit dtcs_cleared(false, error);
                return;
            }
            if (!answers(r, 0x04).isEmpty()) {
                qInfo() << "[obd] Trouble codes cleared";
                emit dtcs_cleared(true, "Codes cleared");
                // Give the ECUs a moment before asking again
                QTimer::singleShot(1500, this, [this] { this->read_dtcs(); });
                return;
            }
            // 7F 04 22: conditions not correct (usually: the engine is running)
            for (const auto &messages : r)
                for (const QByteArray &m : messages)
                    if (m.size() >= 3 && quint8(m[0]) == 0x7F && quint8(m[1]) == 0x04) {
                        emit dtcs_cleared(false, quint8(m[2]) == 0x22
                                                     ? "The car refused. Turn the engine off, leave the ignition on, "
                                                       "and try again."
                                                     : QString("The car refused (code %1)").arg(quint8(m[2]), 2, 16, QChar('0')));
                        return;
                    }
            emit dtcs_cleared(false, "No answer from the car");
        },
        true);
}

QString dtc_code(quint8 a, quint8 b)
{
    static const char kSystems[] = {'P', 'C', 'B', 'U'};
    return QString("%1%2%3%4%5")
        .arg(kSystems[a >> 6])
        .arg((a >> 4) & 0x03)
        .arg(a & 0x0F, 1, 16)
        .arg(b >> 4, 1, 16)
        .arg(b & 0x0F, 1, 16)
        .toUpper();
}
