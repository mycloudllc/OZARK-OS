#pragma once

#include <QHostAddress>
#include <QMap>
#include <QObject>
#include <QTimer>
#include <QUdpSocket>

// Finds WLED controllers on the head unit's networks (hotspot, Ethernet):
// asks mDNS for _wled._tcp on every interface, and collects the answers for
// a couple of seconds. Each WLED announces itself as "<name>._wled._tcp".
class WledFinder : public QObject {
    Q_OBJECT
public:
    explicit WledFinder(QObject* parent = nullptr);

    void find();  // finished() follows, about 2 s later
    bool isFinding() const { return m_timer.isActive(); }

signals:
    // "address name" for each controller found
    void finished(const QStringList& found);

private:
    void onReadyRead();
    void query();

    QUdpSocket m_socket;
    QTimer m_timer;
    QTimer m_repeat;  // a second query, for answers lost on Wi-Fi
    QMap<QString, QString> m_found;  // address -> name
};
