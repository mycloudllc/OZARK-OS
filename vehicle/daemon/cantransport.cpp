#include "cantransport.h"

#include <errno.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <QDebug>
#include <QProcess>

namespace {
constexpr quint32 kFunctional11 = 0x7DF;
constexpr quint32 kFunctional29 = 0x18DB33F1;
}  // namespace

CanTransport::CanTransport(const QString &interface, int bitrate, QObject *parent)
    : ObdTransport(parent)
    , interface(interface)
    , bitrate(bitrate)
{
    timeout.setSingleShot(true);
    settle.setSingleShot(true);
    connect(&timeout, &QTimer::timeout, this, [this] { this->finish(true); });
    connect(&settle, &QTimer::timeout, this, [this] { this->finish(true); });
}

CanTransport::~CanTransport()
{
    close();
}

void CanTransport::close()
{
    timeout.stop();
    settle.stop();
    delete reader;
    reader = nullptr;
    if (fd >= 0)
        ::close(fd);
    fd = -1;
    pending = nullptr;
    assembler.clear();
}

QString CanTransport::protocol() const
{
    if (id_bits == 0)
        return QString();
    return QString("ISO 15765-4 CAN (%1 bit) on %2").arg(id_bits).arg(interface);
}

void CanTransport::open()
{
    close();
    id_bits = 0;
    probing_bits = 11;

    if (bitrate > 0) {
        // Needs the interface down to change; restart-ms brings it back by
        // itself after bus-off (wiring trouble, or the wrong bitrate)
        QProcess::execute("ip", {"link", "set", interface, "down"});
        const int set = QProcess::execute("ip", {"link", "set", interface, "type", "can", "bitrate",
                                                 QString::number(bitrate), "restart-ms", "100"});
        if (set != 0)
            qWarning().noquote() << "[can] Couldn't set" << interface << "to" << bitrate << "bit/s";
    }
    QProcess::execute("ip", {"link", "set", interface, "up"});

    fd = ::socket(PF_CAN, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC, CAN_RAW);
    if (fd < 0) {
        emit opened(false, QString("No CAN sockets: %1").arg(QString::fromLocal8Bit(strerror(errno))));
        return;
    }
    ifreq ifr {};
    strncpy(ifr.ifr_name, interface.toLocal8Bit().constData(), IFNAMSIZ - 1);
    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
        const QString message = QString("No CAN interface called %1").arg(interface);
        close();
        emit opened(false, message);
        return;
    }
    sockaddr_can addr {};
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        const QString message = QString("Can't use %1: %2").arg(interface, QString::fromLocal8Bit(strerror(errno)));
        close();
        emit opened(false, message);
        return;
    }
    reader = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(reader, &QSocketNotifier::activated, this, [this] { this->read_frames(); });

    // Which ids does the car use? Ask with 11 bit first (nearly every car),
    // then 29 bit (some GM, Mazda and trucks)
    request(QByteArray::fromHex("0100"), 300, [this](bool, const EcuReplies &r11, const QString &) {
        if (!r11.isEmpty()) {
            id_bits = 11;
            emit opened(true, QString("Talking to the car: %1").arg(protocol()));
            return;
        }
        probing_bits = 29;
        request(QByteArray::fromHex("0100"), 300, [this](bool, const EcuReplies &r29, const QString &) {
            if (!r29.isEmpty())
                id_bits = 29;
            else
                probing_bits = 11;  // nobody yet (ignition off): try again from the start next time
            emit opened(true, id_bits ? QString("Talking to the car: %1").arg(protocol())
                                      : QString("%1 is up, no answer from the car yet").arg(interface));
        });
    });
}

bool CanTransport::is_reply_id(quint32 id) const
{
    const int bits = id_bits ? id_bits : probing_bits;
    if (bits == 11)
        return id >= 0x7E8 && id <= 0x7EF;
    return (id & 0x1FFFFF00) == 0x18DAF100;
}

void CanTransport::send_frame(quint32 id, const QByteArray &data)
{
    can_frame frame {};
    frame.can_id = id > 0x7FF ? (id | CAN_EFF_FLAG) : id;
    frame.can_dlc = 8;
    // Padding to 8 bytes: some ECUs ignore short frames
    memset(frame.data, 0x00, 8);
    memcpy(frame.data, data.constData(), size_t(qMin(8, data.size())));
    if (::write(fd, &frame, sizeof(frame)) != sizeof(frame)) {
        if (errno == ENETDOWN || errno == ENODEV)
            emit lost(QString("%1 went down").arg(interface));
    }
}

void CanTransport::request(const QByteArray &service, int timeout_ms, Done done)
{
    if (fd < 0) {
        done(false, {}, QString("%1 isn't open").arg(interface));
        return;
    }
    if (service.size() > 7) {
        done(false, {}, "Request too long for a single frame");
        return;
    }
    pending = std::move(done);
    replies.clear();
    assembler.clear();
    QByteArray sf;
    sf.append(char(service.size()));
    sf.append(service);
    const int bits = id_bits ? id_bits : probing_bits;
    send_frame(bits == 11 ? kFunctional11 : kFunctional29, sf);
    timeout.start(timeout_ms);
}

void CanTransport::read_frames()
{
    for (;;) {
        can_frame frame {};
        const ssize_t n = ::read(fd, &frame, sizeof(frame));
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return;
            const QString message = QString("%1 went down: %2").arg(interface, QString::fromLocal8Bit(strerror(errno)));
            close();
            emit lost(message);
            return;
        }
        if (n != sizeof(frame) || (frame.can_id & (CAN_RTR_FLAG | CAN_ERR_FLAG)))
            continue;
        const quint32 id = frame.can_id & ((frame.can_id & CAN_EFF_FLAG) ? CAN_EFF_MASK : CAN_SFF_MASK);
        if (!pending || !is_reply_id(id))
            continue;

        const QByteArray data(reinterpret_cast<const char *>(frame.data), frame.can_dlc);
        QByteArray message;
        bool first = false;
        if (assembler.feed(id, data, message, &first)) {
            replies[id].append(message);
            // Others may still answer: give them 50 ms
            timeout.stop();
            settle.start(50);
        } else if (first) {
            // Flow control to that ECU's physical address: send it all, no gaps
            const quint32 target = id <= 0x7FF ? id - 8 : (0x18DA00F1 | ((id & 0xFF) << 8));
            send_frame(target, QByteArray::fromHex("300000"));
            settle.stop();
            timeout.start(1000);
        }
    }
}

void CanTransport::finish(bool ok, const QString &error)
{
    timeout.stop();
    settle.stop();
    if (!pending)
        return;
    auto cb = std::move(pending);
    pending = nullptr;
    cb(ok, replies, error);
}
