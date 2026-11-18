#include "ws.h"

#include <QDebug>
#include <QHostAddress>
#include <QTimer>
#include <QWebSocket>
#include <QWebSocketServer>

#include "remoted.h"

namespace {
// The hotspot (10.42.0.0/24), other private networks, link-local, loopback
bool local(const QHostAddress &address)
{
    QHostAddress a = address;
    bool ok = false;
    const quint32 v4 = a.toIPv4Address(&ok);  // also unwraps ::ffff:a.b.c.d
    if (ok)
        a = QHostAddress(v4);
    static const QList<QPair<QHostAddress, int>> nets = {
        QHostAddress::parseSubnet("10.0.0.0/8"),     QHostAddress::parseSubnet("172.16.0.0/12"),
        QHostAddress::parseSubnet("192.168.0.0/16"), QHostAddress::parseSubnet("169.254.0.0/16"),
        QHostAddress::parseSubnet("127.0.0.0/8"),    QHostAddress::parseSubnet("fe80::/10"),
        QHostAddress::parseSubnet("fc00::/7"),       QHostAddress::parseSubnet("::1/128"),
    };
    for (const auto &net : nets)
        if (a.isInSubnet(net))
            return true;
    return false;
}

class WsSession : public Session {
   public:
    WsSession(QWebSocket *socket, QObject *parent)
        : Session("wifi", socket->peerAddress().toString(), parent), socket(socket)
    {
        socket->setParent(this);
        connect(socket, &QWebSocket::textMessageReceived, this, [this](const QString &m) { emit received(m.toUtf8()); });
        connect(socket, &QWebSocket::binaryMessageReceived, this, [this](const QByteArray &m) { emit received(m); });
        connect(socket, &QWebSocket::disconnected, this, &Session::closed);
        // A phone that went away without closing: pings find out
        auto ping = new QTimer(this);
        ping->setInterval(20000);
        connect(ping, &QTimer::timeout, this, [this] {
            if (this->missed++ >= 2)
                this->socket->abort();
            else
                this->socket->ping();
        });
        connect(socket, &QWebSocket::pong, this, [this] { this->missed = 0; });
        ping->start();
    }
    void send(const QByteArray &message) override { this->socket->sendTextMessage(QString::fromUtf8(message)); }
    void close() override { this->socket->close(); }

   private:
    QWebSocket *socket;
    int missed = 0;
};
}  // namespace

WsTransport::WsTransport(Remote &remote, QObject *parent)
    : QObject(parent)
    , remote(remote)
    , server(new QWebSocketServer("ozark-remoted", QWebSocketServer::NonSecureMode, this))
{
    connect(this->server, &QWebSocketServer::newConnection, this, [this] {
        while (QWebSocket *socket = this->server->nextPendingConnection()) {
            if (!local(socket->peerAddress())) {
                qInfo().noquote() << "[wifi] Refused" << socket->peerAddress().toString() << "(not a local network)";
                socket->abort();
                socket->deleteLater();
                continue;
            }
            this->remote.add(new WsSession(socket, &this->remote));
        }
    });
}

bool WsTransport::start()
{
    if (this->server->listen(QHostAddress::Any, kPort)) {
        this->remote.set_transport_state("wifi", QString("listening on port %1").arg(kPort));
        return true;
    }
    this->remote.set_transport_state("wifi", "error: " + this->server->errorString());
    // The address may be busy for a moment after a restart
    QTimer::singleShot(5000, this, &WsTransport::start);
    return false;
}
