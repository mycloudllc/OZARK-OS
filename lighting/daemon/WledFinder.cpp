#include "WledFinder.h"

#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QtEndian>

namespace {
const QHostAddress kGroup("224.0.0.251");
constexpr quint16 kMdnsPort = 5353;
const QByteArray kService = "_wled._tcp.local";

QByteArray encodeName(const QByteArray& name) {
    QByteArray out;
    for (const QByteArray& label : name.split('.')) {
        out.append(char(label.size()));
        out.append(label);
    }
    out.append('\0');
    return out;
}

// A DNS name at pos (following compression pointers); pos moves past it.
// Empty if it's malformed.
QByteArray readName(const QByteArray& msg, int& pos) {
    QByteArray name;
    int at = pos, jumps = 0;
    bool jumped = false;
    while (at < msg.size()) {
        const quint8 len = quint8(msg[at]);
        if (len == 0) {
            if (!jumped) pos = at + 1;
            return name;
        }
        if ((len & 0xC0) == 0xC0) {
            if (at + 1 >= msg.size() || ++jumps > 16) return {};
            if (!jumped) pos = at + 2;
            jumped = true;
            at = ((len & 0x3F) << 8) | quint8(msg[at + 1]);
            continue;
        }
        if (at + 1 + len > msg.size()) return {};
        if (!name.isEmpty()) name.append('.');
        name.append(msg.mid(at + 1, len));
        at += 1 + len;
    }
    return {};
}
}  // namespace

WledFinder::WledFinder(QObject* parent) : QObject(parent) {
    m_timer.setSingleShot(true);
    m_timer.setInterval(2500);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        m_socket.close();
        QStringList found;
        for (auto it = m_found.begin(); it != m_found.end(); ++it) found.append(it.key() + ' ' + it.value());
        emit finished(found);
    });
    m_repeat.setSingleShot(true);
    m_repeat.setInterval(800);
    connect(&m_repeat, &QTimer::timeout, this, &WledFinder::query);
    connect(&m_socket, &QUdpSocket::readyRead, this, &WledFinder::onReadyRead);
}

void WledFinder::find() {
    if (isFinding()) return;
    m_found.clear();
    // A one-shot query from our own port: responders answer it directly
    // (RFC 6762 6.7), so this doesn't need port 5353 or a running avahi
    if (!m_socket.bind(QHostAddress(QHostAddress::AnyIPv4), 0)) {
        emit finished({});
        return;
    }
    query();
    m_repeat.start();
    m_timer.start();
}

void WledFinder::query() {
    // id 0, no flags, one question: PTR _wled._tcp.local, unicast reply wanted
    QByteArray q(12, '\0');
    q[5] = 1;
    q.append(encodeName(kService));
    q.append("\x00\x0c\x80\x01", 4);  // type PTR, class IN with the QU bit

    for (const QNetworkInterface& iface : QNetworkInterface::allInterfaces()) {
        const auto flags = iface.flags();
        if (!(flags & QNetworkInterface::IsUp) || (flags & QNetworkInterface::IsLoopBack) ||
            !(flags & QNetworkInterface::CanMulticast))
            continue;
        bool ipv4 = false;
        for (const QNetworkAddressEntry& entry : iface.addressEntries())
            ipv4 |= entry.ip().protocol() == QAbstractSocket::IPv4Protocol;
        if (!ipv4) continue;
        m_socket.setMulticastInterface(iface);
        m_socket.writeDatagram(q, kGroup, kMdnsPort);
    }
}

// Any answer naming a "<name>._wled._tcp.local" instance: the sender is the
// WLED (answers come straight from the device)
void WledFinder::onReadyRead() {
    while (m_socket.hasPendingDatagrams()) {
        const QNetworkDatagram datagram = m_socket.receiveDatagram();
        const QByteArray msg = datagram.data();
        if (msg.size() < 12) continue;
        const int questions = qFromBigEndian<quint16>(msg.constData() + 4);
        const int records = qFromBigEndian<quint16>(msg.constData() + 6) +
                            qFromBigEndian<quint16>(msg.constData() + 8) +
                            qFromBigEndian<quint16>(msg.constData() + 10);
        int pos = 12;
        for (int i = 0; i < questions && pos < msg.size(); ++i) {
            if (readName(msg, pos).isEmpty()) break;
            pos += 4;
        }
        QString name;
        for (int i = 0; i < records && pos + 10 <= msg.size(); ++i) {
            const QByteArray owner = readName(msg, pos);
            if (owner.isEmpty() || pos + 10 > msg.size()) break;
            const quint16 type = qFromBigEndian<quint16>(msg.constData() + pos);
            const quint16 length = qFromBigEndian<quint16>(msg.constData() + pos + 8);
            pos += 10;
            const int next = pos + length;
            if (type == 12 && owner.compare(kService, Qt::CaseInsensitive) == 0) {  // PTR
                int at = pos;
                const QByteArray instance = readName(msg, at);
                if (instance.endsWith("." + kService)) name = QString::fromUtf8(instance.left(instance.size() - kService.size() - 1));
            }
            pos = next;
        }
        if (name.isEmpty()) continue;
        QHostAddress sender = datagram.senderAddress();
        if (sender.protocol() == QAbstractSocket::IPv6Protocol) {
            bool ok = false;
            const QHostAddress v4(sender.toIPv4Address(&ok));
            if (ok) sender = v4;
        }
        m_found[sender.toString()] = name;
    }
}
