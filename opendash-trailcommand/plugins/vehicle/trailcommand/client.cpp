#include "client.hpp"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslError>
#include <QUrl>

namespace tc {

Client::Client(QObject *parent) : QObject(parent)
{
    reconnect_.setSingleShot(true);
    reconnect_.setInterval(2000);
    connect(&reconnect_, &QTimer::timeout, this, [this] { openSocket(); });
    pollTimer_.setInterval(3000);
    connect(&pollTimer_, &QTimer::timeout, this, [this] { poll(); });
    settingsTimer_.setInterval(20000);
    connect(&settingsTimer_, &QTimer::timeout, this, [this] { if (link_ == Link::Live) reloadSettings(); });
    settingsTimer_.start();
}

QString Client::baseUrl() const
{
    const bool defaultPort = (hub_.https && hub_.port == 443) || (!hub_.https && hub_.port == 80);
    return QString("%1://%2%3").arg(hub_.https ? "https" : "http", hub_.host, defaultPort ? QString() : QString(":%1").arg(hub_.port));
}

QString Client::linkText() const
{
    switch (link_) {
        case Link::Live: return "Connected";
        case Link::Connecting: return "Connecting…";
        case Link::Lost: return lastError_.isEmpty() ? "Reconnecting…" : lastError_;
        default: return "Not set up";
    }
}

Units Client::units() const
{
    Units u;
    u.pressure = setting("units_pressure", "psi");
    u.temp = setting("units_temp", "c");
    u.distance = setting("units_distance", "mi");
    return u;
}

void Client::setLink(Link l)
{
    if (link_ == l) return;
    link_ = l;
    if (l == Link::Live) {
        pollTimer_.stop();
        lastError_.clear();
    }
    else if (l == Link::Lost || l == Link::Connecting) {
        if (!pollTimer_.isActive() && configured()) pollTimer_.start();
    }
    emit linkChanged(l);
}

void Client::setHub(const Hub &hub)
{
    if (hub == hub_) return;
    hub_ = hub;
    reconnect_.stop();
    pollTimer_.stop();
    if (ws_) {
        ws_->disconnect(this);
        ws_->abort();
        ws_->deleteLater();
    }
    state_ = QJsonObject();
    settings_ = QJsonObject();
    if (!configured()) {
        setLink(Link::Idle);
        return;
    }
    setLink(Link::Connecting);
    reloadSettings();
    openSocket();
}

void Client::openSocket()
{
    if (!configured()) return;
    const bool defaultPort = (hub_.https && hub_.port == 443) || (!hub_.https && hub_.port == 80);
    const QString origin = baseUrl();
    Q_UNUSED(defaultPort)
    ws_ = new QWebSocket(origin, QWebSocketProtocol::VersionLatest, this);  // Origin must match Host or the hub refuses it
    connect(ws_, &QWebSocket::connected, this, [this] {
        setLink(Link::Live);
        reloadSettings();
    });
    connect(ws_, &QWebSocket::textMessageReceived, this, [this](const QString &msg) {
        const QJsonDocument doc = QJsonDocument::fromJson(msg.toUtf8());
        if (!doc.isObject()) return;
        state_ = doc.object();
        emit stateChanged(state_);
    });
    connect(ws_, &QWebSocket::disconnected, this, [this] {
        if (!configured()) return;
        setLink(Link::Lost);
        reconnect_.start();
    });
    connect(ws_, &QWebSocket::sslErrors, this, [this](const QList<QSslError> &) {
        if (hub_.trustCertificate) ws_->ignoreSslErrors();
    });
    connect(ws_, static_cast<void (QWebSocket::*)(QAbstractSocket::SocketError)>(&QWebSocket::error), this, [this](QAbstractSocket::SocketError) {
        lastError_ = ws_ ? ws_->errorString() : QString();
    });
    QString url = baseUrl() + "/ws";
    url.replace(0, url.indexOf("://"), hub_.https ? "wss" : "ws");
    ws_->open(QUrl(url));
}

void Client::poll()
{
    // while the WebSocket is down, keep the numbers moving with plain requests
    get("/api/state", [this](int status, const QJsonDocument &doc, const QString &err) {
        if (status == 200 && doc.isObject()) {
            state_ = doc.object();
            emit stateChanged(state_);
        }
        else if (!err.isEmpty()) {
            lastError_ = err;
            emit linkChanged(link_);
        }
    });
}

void Client::reloadSettings()
{
    get("/api/settings", [this](int status, const QJsonDocument &doc, const QString &) {
        if (status != 200 || !doc.isObject()) return;
        settings_ = doc.object();
        emit settingsChanged();
    });
}

void Client::send(const QByteArray &verb, const QString &path, const QJsonObject &body, Reply cb, bool hasBody, bool retried)
{
    if (!configured()) {
        if (cb) cb(0, QJsonDocument(), "No hub is set up");
        return;
    }
    QNetworkRequest req(QUrl(baseUrl() + path));
    req.setRawHeader("X-TC-Client", "opendash");  // the hub requires this on writes (it blocks other websites)
    if (!hub_.pin.isEmpty()) req.setRawHeader("X-TC-Pin", hub_.pin.toUtf8());
    if (hasBody) req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = nam_.sendCustomRequest(req, verb, hasBody ? QJsonDocument(body).toJson(QJsonDocument::Compact) : QByteArray());
    QTimer::singleShot(8000, reply, [reply] { if (reply->isRunning()) reply->abort(); });  // works on Qt older than 5.15 too
    connect(reply, &QNetworkReply::sslErrors, reply, [this, reply](const QList<QSslError> &) {
        if (hub_.trustCertificate) reply->ignoreSslErrors();
    });
    connect(reply, &QNetworkReply::finished, this, [=] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QString error;
        if (status >= 400) error = doc.object().value("error").toString(QString("Request failed (%1)").arg(status));
        else if (reply->error() != QNetworkReply::NoError) error = reply->errorString();
        reply->deleteLater();
        if (status == 401 && !retried && pinPrompt_) {
            QString pin;
            if (pinPrompt_(&pin) && !pin.isEmpty()) {
                hub_.pin = pin;
                send(verb, path, body, cb, hasBody, true);
                return;
            }
        }
        if (cb) cb(status, doc, error);
        else if (!error.isEmpty() && verb != "GET") emit rejected(error);
    });
}

}  // namespace tc
