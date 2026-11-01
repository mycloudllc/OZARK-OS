#include "scanner.hpp"

#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAddressEntry>
#include <QNetworkInterface>
#include <QPointer>
#include <QSharedPointer>
#include <QTcpSocket>
#include <QTimer>

namespace tc {

QStringList Scanner::candidateAddresses(int maxHosts)
{
    QStringList own, others;
    for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp) || !(iface.flags() & QNetworkInterface::IsRunning)) continue;
        if (iface.flags() & QNetworkInterface::IsLoopBack) continue;
        for (const QNetworkAddressEntry &e : iface.addressEntries()) {
            if (e.ip().protocol() != QAbstractSocket::IPv4Protocol) continue;
            const quint32 ip = e.ip().toIPv4Address();
            if ((ip >> 16) == 0xA9FE) continue;  // 169.254.x.x link-local
            int prefix = e.prefixLength();
            if (prefix < 22) prefix = 22;  // never sweep more than a /22
            if (prefix > 30) { own << e.ip().toString(); continue; }
            const quint32 mask = prefix == 0 ? 0 : (0xFFFFFFFFu << (32 - prefix));
            const quint32 net = ip & mask, count = ~mask + 1;
            own << e.ip().toString();
            for (quint32 h = 1; h + 1 < count; h++)
                if (net + h != ip) others << QHostAddress(net + h).toString();
        }
    }
    QStringList all;
    all << "127.0.0.1" << own;
    for (const QString &a : others)
        if (all.size() < maxHosts) all << a;
    all.removeDuplicates();
    return all;
}

void Scanner::start(const QList<int> &ports)
{
    cancel();
    targets_.clear();
    seen_.clear();
    for (const QString &ip : candidateAddresses())
        for (int p : ports) targets_.append({ip, p});
    next_ = done_ = active_ = 0;
    running_ = true;
    ++generation_;
    emit progress(0, targets_.size());
    pump();
    if (targets_.isEmpty()) {
        running_ = false;
        emit finished(false);
    }
}

void Scanner::cancel()
{
    if (!running_) return;
    running_ = false;
    ++generation_;  // outstanding probes ignore their results
    emit finished(true);
}

void Scanner::pump()
{
    while (running_ && active_ < 48 && next_ < targets_.size()) {
        const Target t = targets_[next_++];
        probe(t.ip, t.port);
    }
}

void Scanner::probeDone()
{
    --active_;
    ++done_;
    if (!running_) return;
    emit progress(done_, targets_.size());
    if (done_ >= targets_.size()) {
        running_ = false;
        emit finished(false);
        return;
    }
    pump();
}

void Scanner::probe(const QString &ip, int port)
{
    ++active_;
    const int gen = generation_;
    auto *sock = new QTcpSocket(this);
    auto *timer = new QTimer(sock);
    auto buffer = QSharedPointer<QByteArray>::create();
    timer->setSingleShot(true);
    auto finish = [=](bool ok) {
        // runs at most once per probe
        if (!sock->property("done").isNull()) return;
        sock->setProperty("done", true);
        timer->stop();
        Q_UNUSED(ok)
        if (gen == generation_ && running_) {  // use whatever arrived, even if the hub keeps the connection open
            const int split = buffer->indexOf("\r\n\r\n");
            const QJsonObject o = QJsonDocument::fromJson(split >= 0 ? buffer->mid(split + 4) : QByteArray()).object();
            if (o.value("app").toString() == "trailcommand" && !seen_.contains(ip + QString(":%1").arg(port))) {
                seen_ << ip + QString(":%1").arg(port);
                FoundHub h;
                h.ip = ip; h.port = port;
                h.version = o.value("version").toString();
                h.build = o.value("build").toInt();
                h.id = o.value("id").toString();
                h.hostname = o.value("hostname").toString();
                emit found(h);
            }
        }
        sock->abort();
        sock->deleteLater();
        if (gen == generation_) probeDone();
    };
    connect(timer, &QTimer::timeout, sock, [=] { finish(false); });
    connect(sock, &QTcpSocket::connected, sock, [=] {
        timer->start(1500);  // connected: allow a little longer for the answer
        sock->write(QString("GET /api/health HTTP/1.0\r\nHost: %1\r\nX-TC-Client: scan\r\nConnection: close\r\n\r\n").arg(ip).toUtf8());
    });
    connect(sock, &QTcpSocket::readyRead, sock, [=] {
        buffer->append(sock->readAll());
        const int split = buffer->indexOf("\r\n\r\n");
        if (split >= 0 && !QJsonDocument::fromJson(buffer->mid(split + 4)).isNull()) finish(true);  // complete answer: no need to wait for the hub to hang up
    });
    connect(sock, &QTcpSocket::disconnected, sock, [=] {
        if (!sock->property("done").isNull()) return;
        buffer->append(sock->readAll());
        finish(true);
    });
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(sock, &QAbstractSocket::errorOccurred, sock, [=](QAbstractSocket::SocketError) {
#else
    connect(sock, static_cast<void (QAbstractSocket::*)(QAbstractSocket::SocketError)>(&QAbstractSocket::error), sock, [=](QAbstractSocket::SocketError) {
#endif
        if (sock->state() == QAbstractSocket::UnconnectedState) finish(false);
    });
    timer->start(700);
    sock->connectToHost(ip, static_cast<quint16>(port));
}

}  // namespace tc
