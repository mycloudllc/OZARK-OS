#include "elm327.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>

#include <QDebug>
#include <QRegularExpression>
#include <QStringList>

namespace {

// RFCOMM without BlueZ's headers (libbluetooth isn't needed for this)
constexpr int kAfBluetooth = 31;
constexpr int kBtProtoRfcomm = 3;
struct SockaddrRc {
    sa_family_t family;
    unsigned char bdaddr[6];  // least significant byte first
    unsigned char channel;
};

speed_t speed_for(int baud)
{
    switch (baud) {
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        case 500000: return B500000;
        default: return B38400;
    }
}

// What ATDPN's protocol number means (A in front = picked automatically)
QString protocol_text(QChar p)
{
    switch (p.toUpper().toLatin1()) {
        case '1': return "SAE J1850 PWM";
        case '2': return "SAE J1850 VPW";
        case '3': return "ISO 9141-2";
        case '4': return "ISO 14230-4 KWP (5 baud init)";
        case '5': return "ISO 14230-4 KWP (fast init)";
        case '6': return "ISO 15765-4 CAN (11 bit, 500 kbaud)";
        case '7': return "ISO 15765-4 CAN (29 bit, 500 kbaud)";
        case '8': return "ISO 15765-4 CAN (11 bit, 250 kbaud)";
        case '9': return "ISO 15765-4 CAN (29 bit, 250 kbaud)";
        case 'A': return "SAE J1939 CAN (29 bit, 250 kbaud)";
        case 'B': return "CAN (11 bit, 125 kbaud)";
        case 'C': return "CAN (11 bit, 50 kbaud)";
        default: return QString();
    }
}

bool hex_byte(const QString &token, quint8 &value)
{
    if (token.size() != 2)
        return false;
    bool ok = false;
    value = quint8(token.toUInt(&ok, 16));
    return ok;
}

}  // namespace

Elm327 *Elm327::serial(const QString &path, QObject *parent)
{
    return new Elm327(Link::Serial, path, parent);
}

Elm327 *Elm327::bluetooth(const QString &address, QObject *parent)
{
    return new Elm327(Link::Bluetooth, address, parent);
}

Elm327::Elm327(Link link, const QString &where, QObject *parent)
    : ObdTransport(parent)
    , link(link)
    , where(where)
{
    timeout.setSingleShot(true);
    connect(&timeout, &QTimer::timeout, this, [this] { this->finish_command(QString()); });
}

Elm327::~Elm327()
{
    close();
}

void Elm327::close()
{
    timeout.stop();
    delete reader;
    reader = nullptr;
    delete connecting;
    connecting = nullptr;
    if (fd >= 0)
        ::close(fd);
    fd = -1;
    buffer.clear();
    pending = nullptr;
    assembler.clear();
}

void Elm327::open()
{
    close();
    if (link == Link::Bluetooth) {
        open_bluetooth();
        return;
    }
    // USB adapters ship at 38400 or 115200 (clones vary): try them in turn
    bauds = {38400, 115200, 9600, 230400, 500000, 57600};
    try_next_baud();
}

bool Elm327::open_serial(int baud)
{
    if (fd >= 0)
        ::close(fd);
    fd = ::open(where.toLocal8Bit().constData(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        return false;
    termios tio {};
    if (tcgetattr(fd, &tio) != 0) {
        ::close(fd);
        fd = -1;
        return false;
    }
    cfmakeraw(&tio);
    tio.c_cflag |= CLOCAL | CREAD;
    tio.c_cflag &= ~CRTSCTS;
    // VMIN 1: with O_NONBLOCK, "no data yet" is EAGAIN, so a read of 0
    // really means the adapter hung up (with VMIN 0 it would mean either)
    tio.c_cc[VMIN] = 1;
    tio.c_cc[VTIME] = 0;
    cfsetispeed(&tio, speed_for(baud));
    cfsetospeed(&tio, speed_for(baud));
    tcsetattr(fd, TCSANOW, &tio);
    tcflush(fd, TCIOFLUSH);
    delete reader;
    reader = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(reader, &QSocketNotifier::activated, this, [this] { this->read_ready(); });
    return true;
}

void Elm327::try_next_baud()
{
    if (bauds.isEmpty()) {
        fail(QString("No ELM327 answered on %1").arg(where));
        return;
    }
    const int baud = bauds.takeFirst();
    if (!open_serial(baud)) {
        fail(QString("Can't open %1: %2").arg(where, QString::fromLocal8Bit(strerror(errno))));
        return;
    }
    // A stray CR clears anything half typed; ATI answers "ELM327 v1.5"
    ::write(fd, "\r", 1);
    QTimer::singleShot(200, this, [this, baud] {
        buffer.clear();
        command("ATI", 1000, [this, baud](const QString &text) {
            if (text.contains("ELM", Qt::CaseInsensitive)) {
                qInfo().noquote() << "[elm327]" << where << "at" << baud << "baud:" << text.simplified();
                start_session();
            } else {
                try_next_baud();
            }
        });
    });
}

void Elm327::open_bluetooth()
{
    const QStringList parts = where.split(':');
    if (parts.size() != 6) {
        fail(QString("Not a Bluetooth address: %1").arg(where));
        return;
    }
    SockaddrRc addr {};
    addr.family = kAfBluetooth;
    for (int i = 0; i < 6; i++)
        addr.bdaddr[i] = quint8(parts[5 - i].toUInt(nullptr, 16));
    addr.channel = 1;  // the SPP channel on every ELM327 seen so far

    fd = ::socket(kAfBluetooth, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, kBtProtoRfcomm);
    if (fd < 0) {
        fail(QString("No Bluetooth sockets: %1").arg(QString::fromLocal8Bit(strerror(errno))));
        return;
    }
    if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0) {
        reader = new QSocketNotifier(fd, QSocketNotifier::Read, this);
        connect(reader, &QSocketNotifier::activated, this, [this] { this->read_ready(); });
        start_session();
        return;
    }
    if (errno != EINPROGRESS) {
        fail(QString("Can't reach %1 over Bluetooth: %2").arg(where, QString::fromLocal8Bit(strerror(errno))));
        return;
    }
    connecting = new QSocketNotifier(fd, QSocketNotifier::Write, this);
    connect(connecting, &QSocketNotifier::activated, this, [this] {
        connecting->setEnabled(false);
        connecting->deleteLater();
        connecting = nullptr;
        int err = 0;
        socklen_t len = sizeof(err);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
        if (err != 0) {
            fail(QString("Can't reach %1 over Bluetooth: %2").arg(where, QString::fromLocal8Bit(strerror(err))));
            return;
        }
        reader = new QSocketNotifier(fd, QSocketNotifier::Read, this);
        connect(reader, &QSocketNotifier::activated, this, [this] { this->read_ready(); });
        QTimer::singleShot(300, this, [this] { this->start_session(); });
    });
    // A Bluetooth connect can hang for half a minute when the dongle is off
    QTimer::singleShot(20000, this, [this] {
        if (connecting)
            fail(QString("No answer from %1 over Bluetooth").arg(where));
    });
}

void Elm327::start_session()
{
    can = can29 = false;
    protocol_name.clear();
    init_step(0);
}

// Warm start, then: no echo, no linefeeds, spaces on (easier to split),
// headers on (to tell ECUs apart and see CAN frame boundaries), adaptive
// timing, automatic protocol. Then wake the car up with "0100".
void Elm327::init_step(int step)
{
    static const QList<QPair<QString, int>> steps = {
        {"ATWS", 1500}, {"ATE0", 1000}, {"ATL0", 1000}, {"ATS1", 1000},
        {"ATH1", 1000}, {"ATAT1", 1000}, {"ATSP0", 1000},
    };
    if (step < steps.size()) {
        command(steps[step].first, steps[step].second, [this, step](const QString &text) {
            if (text.isEmpty() && step > 0) {
                fail(QString("The adapter on %1 stopped answering").arg(where));
                return;
            }
            init_step(step + 1);
        });
        return;
    }
    // Searching every protocol can take 10 s or so; no answer just means the
    // ignition is off, which is fine: the caller keeps asking
    request(QByteArray::fromHex("0100"), 15000, [this](bool, const EcuReplies &, const QString &) {
        emit opened(true, protocol_name.isEmpty() ? QString("Adapter ready, no answer from the car yet")
                                                  : QString("Talking to the car: %1").arg(protocol_name));
    });
}

void Elm327::fail(const QString &message)
{
    qWarning().noquote() << "[elm327]" << message;
    close();
    emit opened(false, message);
}

void Elm327::command(const QString &cmd, int timeout_ms, Raw done)
{
    if (fd < 0) {
        done(QString());
        return;
    }
    pending = std::move(done);
    buffer.clear();
    const QByteArray line = cmd.toLatin1() + '\r';
    if (::write(fd, line.constData(), size_t(line.size())) != line.size()) {
        auto cb = std::move(pending);
        pending = nullptr;
        emit lost(QString("Lost the adapter on %1").arg(where));
        cb(QString());
        return;
    }
    timeout.start(timeout_ms);
}

void Elm327::read_ready()
{
    char chunk[512];
    for (;;) {
        const ssize_t n = ::read(fd, chunk, sizeof(chunk));
        if (n > 0) {
            buffer.append(chunk, int(n));
            continue;
        }
        if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
            // Unplugged (serial) or out of range (Bluetooth)
            const QString message = QString("Lost the adapter on %1").arg(where);
            close();
            emit lost(message);
            return;
        }
        break;
    }
    const int prompt = buffer.indexOf('>');
    if (prompt < 0)
        return;
    const QString text = QString::fromLatin1(buffer.left(prompt)).remove(QChar('\0'));
    buffer.remove(0, prompt + 1);
    finish_command(text.trimmed());
}

void Elm327::finish_command(const QString &text)
{
    timeout.stop();
    if (!pending)
        return;
    auto cb = std::move(pending);
    pending = nullptr;
    cb(text);
}

void Elm327::request(const QByteArray &service, int timeout_ms, Done done)
{
    command(QString::fromLatin1(service.toHex()).toUpper(), timeout_ms, [this, done](const QString &text) {
        if (text.isEmpty()) {
            done(false, {}, "The adapter didn't answer in time");
            return;
        }
        // First answer since the car woke up: find out what it speaks, so
        // the headers can be read
        const bool has_data = !text.contains("NO DATA") && !text.contains("UNABLE TO CONNECT") &&
                              !text.contains("ERROR") && text != "?";
        if (protocol_name.isEmpty() && has_data) {
            command("ATDPN", 1000, [this, text, done](const QString &dpn) {
                const QString p = dpn.trimmed().toUpper().remove('A');
                if (!p.isEmpty()) {
                    // "A" was the automatic flag, but protocol A (J1939) is "A" alone
                    const QChar n = dpn.trimmed().toUpper() == "A" ? QChar('A') : p.at(0);
                    protocol_name = protocol_text(n);
                    can = QString("6789ABC").contains(n);
                    can29 = QString("79A").contains(n);
                    qInfo().noquote() << "[elm327] Protocol:" << protocol_name;
                }
                parse_reply(text, done);
            });
            return;
        }
        parse_reply(text, done);
    });
}

void Elm327::parse_reply(const QString &text, const Done &done)
{
    EcuReplies replies;
    assembler.clear();
    const QStringList lines = text.split(QRegularExpression("[\r\n]+"), Qt::SkipEmptyParts);
    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty() || line == "OK" || line.startsWith("SEARCHING") || line.startsWith("BUS INIT"))
            continue;
        if (line.contains("NO DATA") || line.contains("UNABLE TO CONNECT"))
            continue;  // no answer: ignition off, or the ECU doesn't do this
        if (line == "?") {
            done(false, {}, "The adapter didn't understand the request");
            return;
        }
        if (line.contains("ERROR") || line.contains("BUFFER FULL") || line.contains("STOPPED") ||
            line.contains("LV RESET") || line.contains("ACT ALERT")) {
            done(false, {}, QString("Adapter: %1").arg(line));
            return;
        }

        const QStringList t = line.split(' ', Qt::SkipEmptyParts);
        if (can) {
            // "7E8 06 41 0C 1A F8" or, 29 bit, "18 DA F1 10 06 41 0C 1A F8"
            quint32 id = 0;
            int first = 0;
            if (!can29 && t.size() >= 2 && t[0].size() == 3) {
                id = t[0].toUInt(nullptr, 16);
                first = 1;
            } else if (can29 && t.size() >= 5) {
                for (int i = 0; i < 4; i++)
                    id = (id << 8) | t[i].toUInt(nullptr, 16);
                first = 4;
            } else {
                continue;
            }
            QByteArray frame;
            for (int i = first; i < t.size(); i++) {
                quint8 b;
                if (hex_byte(t[i], b))
                    frame.append(char(b));
            }
            QByteArray message;
            if (assembler.feed(id, frame, message))
                replies[id].append(message);
        } else {
            // "48 6B 10 41 0C 1A F8 xx": priority, target, source, data, checksum
            if (t.size() < 5)
                continue;
            QByteArray data;
            for (int i = 3; i < t.size() - 1; i++) {
                quint8 b;
                if (hex_byte(t[i], b))
                    data.append(char(b));
            }
            replies[t[2].toUInt(nullptr, 16)].append(data);
        }
    }
    done(true, replies, QString());
}
