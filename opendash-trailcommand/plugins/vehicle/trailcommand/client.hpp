#pragma once
// Connection to a TrailCommand hub: REST requests plus the live state over a WebSocket.
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QWebSocket>
#include <functional>

#include "fmt.hpp"

namespace tc {

struct Hub {
    QString host;
    int port = 8080;
    bool https = false;
    bool trustCertificate = false;  // accept a self-signed certificate
    QString pin;

    bool operator==(const Hub &o) const
    {
        return host == o.host && port == o.port && https == o.https && trustCertificate == o.trustCertificate && pin == o.pin;
    }
};

class Client : public QObject {
    Q_OBJECT

   public:
    enum class Link { Idle, Connecting, Live, Lost };
    using Reply = std::function<void(int status, const QJsonDocument &doc, const QString &error)>;

    explicit Client(QObject *parent = nullptr);

    void setHub(const Hub &hub);  // (re)connects when anything changed
    const Hub &hub() const { return hub_; }
    bool configured() const { return !hub_.host.isEmpty(); }
    Link link() const { return link_; }
    QString linkText() const;

    const QJsonObject &state() const { return state_; }
    QString setting(const QString &key, const QString &fallback = QString()) const
    {
        return settings_.contains(key) ? settings_.value(key).toString() : fallback;
    }
    bool flag(const QString &key, bool fallback = true) const
    {
        return settings_.contains(key) ? settings_.value(key).toString() != "0" : fallback;
    }
    Units units() const;

    void get(const QString &path, Reply cb) { send("GET", path, QJsonObject(), cb, false); }
    void post(const QString &path, const QJsonObject &body, Reply cb = nullptr) { send("POST", path, body, cb, true); }
    void put(const QString &path, const QJsonObject &body, Reply cb = nullptr) { send("PUT", path, body, cb, true); }
    void reloadSettings();
    void report(const QString &message) { emit rejected(message); }

    // Called on a 401 (the hub has a PIN). Return true after filling in the PIN.
    void setPinPrompt(std::function<bool(QString *pin)> f) { pinPrompt_ = f; }
    QString baseUrl() const;

   signals:
    void stateChanged(const QJsonObject &state);
    void settingsChanged();
    void linkChanged(Link link);
    void rejected(const QString &message);  // a write failed: show it to the user

   private:
    void send(const QByteArray &verb, const QString &path, const QJsonObject &body, Reply cb, bool hasBody, bool retried = false);
    void openSocket();
    void setLink(Link l);
    void poll();

    Hub hub_;
    Link link_ = Link::Idle;
    QNetworkAccessManager nam_;
    QPointer<QWebSocket> ws_;
    QTimer reconnect_, pollTimer_, settingsTimer_;
    QJsonObject state_, settings_;
    std::function<bool(QString *)> pinPrompt_;
    QString lastError_;
};

}  // namespace tc
