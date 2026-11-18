#pragma once

#include <QObject>

class QWebSocketServer;
class Remote;

// Phones on the radio's Wi-Fi hotspot (or the same wired network): a
// WebSocket on port 54546, one JSON message per text frame. Only local
// addresses get in: the LTE side never reaches it.
class WsTransport : public QObject {
    Q_OBJECT

   public:
    static constexpr quint16 kPort = 54546;

    WsTransport(Remote &remote, QObject *parent = nullptr);
    bool start();

   private:
    Remote &remote;
    QWebSocketServer *server;
};
