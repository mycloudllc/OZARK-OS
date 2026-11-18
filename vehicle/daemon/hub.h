// The TrailCommand hub, when there is one: vehicle data goes to it
// (POST /api/vehicle), and its own tire sensors (TireCommander) come back
// from GET /api/tires. Same hub, address and PIN as Dash's TrailCommand tab
// uses, unless the vehicle settings name another.
#pragma once

#include <functional>

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QTimer>
#include <QVariantMap>

class HubLink : public QObject {
    Q_OBJECT

   public:
    struct Hub {
        QString base;  // "http://10.42.0.5:8080"
        QString pin;
        bool trust = false;  // accept the hub's self-signed certificate
    };

    // `snapshot` builds what to send; `hub` says where (it's asked every time,
    // so changes in Dash's TrailCommand settings apply at once)
    HubLink(std::function<QJsonObject()> snapshot, std::function<Hub()> hub, QObject *parent = nullptr);

    void set_forwarding(bool on);
    void set_fetch_tires(bool on);
    // Something changed: send soon (at most every 2 s)
    void changed();

    QVariantMap status() const;
    // The hub's own tires (not ours sent back): [{pos, pressure_kpa, temp_c, low, hot}]
    QJsonArray tires() const { return hub_tires; }

   signals:
    void status_changed();
    void tires_changed();

   private:
    void send();
    void fetch_tires();
    QNetworkRequest request(const Hub &hub, const QString &path) const;
    void set_state(const QString &state, const QString &message);

    std::function<QJsonObject()> snapshot;
    std::function<Hub()> hub;
    QNetworkAccessManager net;
    bool forwarding = false;
    bool fetching = false;
    bool dirty = true;
    bool in_flight = false;
    QTimer send_timer;   // every 2 s if something changed, 15 s otherwise
    QTimer tires_timer;  // every 10 s
    QDateTime last_sent;
    int idle_ticks = 0;
    QString state = "off";
    QString message;
    QString base_used;
    QJsonArray hub_tires;
};
