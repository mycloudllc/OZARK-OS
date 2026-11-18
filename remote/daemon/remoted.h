#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantMap>

#include "clients.h"

class Bridge;

// One connected phone, over Wi-Fi (WebSocket) or Bluetooth LE. A transport
// turns its bytes into whole JSON messages and back.
class Session : public QObject {
    Q_OBJECT

   public:
    Session(const QString &transport, const QString &peer, QObject *parent = nullptr)
        : QObject(parent), transport(transport), peer(peer) {}
    virtual void send(const QByteArray &message) = 0;
    virtual void close() = 0;

    const QString transport;  // "wifi" or "ble"
    const QString peer;       // address
    QString client_id;        // set once it has said hello with a valid token

   signals:
    void received(const QByteArray &message);
    void closed();
};

// The remote control service, org.ozark.Remote1 on the system bus: the
// protocol (PROTOCOL.md), pairing with a code on the radio's screen, and the
// list of phones allowed in. Dash shows the code (PairingStarted) and lists
// the phones in Settings > Bluetooth.
class Remote : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.ozark.Remote1")

   public:
    static constexpr int kProtocol = 1;

    Remote(const QString &store_path, QObject *parent = nullptr);
    void add(Session *session);
    void set_transport_state(const QString &transport, const QString &state);

   public slots:
    Q_SCRIPTABLE QVariantMap GetStatus() const;
    // id -> {id, name, platform, created, last_seen, connected}
    Q_SCRIPTABLE QVariantMap ListClients() const;
    Q_SCRIPTABLE bool RevokeClient(const QString &id);
    Q_SCRIPTABLE void CancelPairing();

   signals:
    Q_SCRIPTABLE void PairingStarted(const QString &code, const QString &name, int seconds);
    Q_SCRIPTABLE void PairingEnded(const QString &name, bool paired);
    Q_SCRIPTABLE void ClientsChanged();
    Q_SCRIPTABLE void StatusChanged();

   private:
    struct Pairing {
        QPointer<Session> session;
        QString code, name, platform;
        int attempts = 0;
    };

    ClientStore store;
    Bridge *bridge;
    QList<QPointer<Session>> sessions;
    Pairing pairing;
    QTimer pairing_timer;
    QDateTime last_pair_request;
    QVariantMap transports;  // "wifi" / "ble" -> state

    void handle(Session *session, const QByteArray &message);
    void reply(Session *session, const QJsonValue &id, const QVariant &result);
    void error(Session *session, const QJsonValue &id, const QString &code, const QString &message);
    void broadcast(const QString &event, const QVariant &data);
    void end_pairing(bool paired);
    QVariantMap hello_result(Session *session) const;
};
