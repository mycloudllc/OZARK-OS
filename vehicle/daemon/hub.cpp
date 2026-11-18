#include "hub.h"

#include <QDebug>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QSslError>

HubLink::HubLink(std::function<QJsonObject()> snapshot, std::function<Hub()> hub, QObject *parent)
    : QObject(parent)
    , snapshot(std::move(snapshot))
    , hub(std::move(hub))
{
    send_timer.setInterval(2000);
    connect(&send_timer, &QTimer::timeout, this, [this] {
        // A heartbeat every 15 s even when nothing changed, so the hub knows
        // the radio is still there
        if (dirty || ++idle_ticks >= 7)
            send();
    });
    tires_timer.setInterval(10000);
    connect(&tires_timer, &QTimer::timeout, this, &HubLink::fetch_tires);
}

void HubLink::set_forwarding(bool on)
{
    if (on == forwarding)
        return;
    forwarding = on;
    if (on) {
        dirty = true;
        send_timer.start();
        send();
    } else {
        send_timer.stop();
        if (!fetching)
            set_state("off", "Not sending to the hub");
    }
}

void HubLink::set_fetch_tires(bool on)
{
    if (on == fetching)
        return;
    fetching = on;
    if (on) {
        tires_timer.start();
        fetch_tires();
    } else {
        tires_timer.stop();
        if (!hub_tires.isEmpty()) {
            hub_tires = QJsonArray();
            emit tires_changed();
        }
    }
}

void HubLink::changed()
{
    dirty = true;
}

QVariantMap HubLink::status() const
{
    return {
        {"state", state},  // off | ok | no-hub | unreachable | pin
        {"message", message},
        {"url", base_used},
        {"forwarding", forwarding},
        {"last_sent", last_sent.isValid() ? last_sent.toString(Qt::ISODate) : QString()},
    };
}

void HubLink::set_state(const QString &s, const QString &m)
{
    if (s == state && m == message)
        return;
    if (m != message)
        qInfo().noquote() << "[hub]" << m;
    state = s;
    message = m;
    emit status_changed();
}

QNetworkRequest HubLink::request(const Hub &h, const QString &path) const
{
    QNetworkRequest req(QUrl(h.base + path));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    // The hub refuses changes without this header (its CSRF guard)
    req.setRawHeader("X-TC-Client", "ozark-radio");
    if (!h.pin.isEmpty())
        req.setRawHeader("X-TC-Pin", h.pin.toUtf8());
    req.setTransferTimeout(5000);
    return req;
}

void HubLink::send()
{
    if (!forwarding || in_flight)
        return;
    const Hub h = hub();
    base_used = h.base;
    if (h.base.isEmpty()) {
        set_state("no-hub", "No hub set up (connect one on the TrailCommand tab)");
        return;
    }
    dirty = false;
    idle_ticks = 0;
    in_flight = true;
    const QByteArray body = QJsonDocument(snapshot()).toJson(QJsonDocument::Compact);
    QNetworkReply *reply = net.post(request(h, "/api/vehicle"), body);
    if (h.trust)
        connect(reply, &QNetworkReply::sslErrors, reply, [reply](const QList<QSslError> &) { reply->ignoreSslErrors(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        in_flight = false;
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (code == 200) {
            last_sent = QDateTime::currentDateTimeUtc();
            set_state("ok", "Sending vehicle data to the hub");
        } else if (code == 401) {
            set_state("pin", "The hub wants its PIN (set it in the vehicle settings, or remember it on the "
                             "TrailCommand tab)");
        } else if (code == 404) {
            set_state("unreachable", "The hub is too old for vehicle data (update it)");
        } else if (code == 429) {
            set_state("pin", "The hub refused: too many wrong PINs");
        } else {
            dirty = true;  // try again next tick
            set_state("unreachable", code ? QString("The hub answered %1").arg(code)
                                          : QString("Can't reach the hub: %1").arg(reply->errorString()));
        }
    });
}

void HubLink::fetch_tires()
{
    if (!fetching)
        return;
    const Hub h = hub();
    if (h.base.isEmpty())
        return;
    QNetworkReply *reply = net.get(request(h, "/api/tires"));
    if (h.trust)
        connect(reply, &QNetworkReply::sslErrors, reply, [reply](const QList<QSslError> &) { reply->ignoreSslErrors(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;
        QJsonArray mine;
        for (const QJsonValue &v : QJsonDocument::fromJson(reply->readAll()).array()) {
            QJsonObject t = v.toObject();
            // Ours, sent there and coming back: skip
            if (t.value("source").toString() == "radio")
                continue;
            const double psi = t.value("psi").toDouble();
            mine.append(QJsonObject {
                {"pos", t.value("pos")},
                {"pressure_kpa", qRound(psi * 6.89476 * 10) / 10.0},
                {"temp_c", t.value("temp_c")},
                {"low", t.value("low")},
                {"hot", t.value("hot")},
            });
        }
        if (mine != hub_tires) {
            hub_tires = mine;
            emit tires_changed();
        }
    });
}
