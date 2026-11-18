// One way of reaching the car's diagnostic port: an ELM327 adapter (USB
// serial or Bluetooth RFCOMM) or the CAN bus directly (SocketCAN). Requests
// are OBD-II service bytes ("01 0C"); replies come back per ECU, already
// unwrapped from the transport (ISO-TP reassembled on CAN).
#pragma once

#include <functional>

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>

// ECU address (CAN id or K-line/J1850 source byte) -> its messages, each
// starting with the response service byte (0x41 for a mode 01 request)
using EcuReplies = QMap<quint32, QList<QByteArray>>;

class ObdTransport : public QObject {
    Q_OBJECT

   public:
    // ok: the adapter answered (replies may be empty: "NO DATA", engine off)
    // error: why not, for the log and the status line
    using Done = std::function<void(bool ok, const EcuReplies &replies, const QString &error)>;

    using QObject::QObject;
    ~ObdTransport() override = default;

    // Connect to the adapter and get it talking to the car; answer in opened()
    virtual void open() = 0;
    virtual void close() = 0;
    // One request at a time (the caller queues). timeout_ms: how long to wait
    // for the first answer
    virtual void request(const QByteArray &service, int timeout_ms, Done done) = 0;
    // What the adapter says the car speaks, e.g. "ISO 15765-4 CAN (11 bit, 500 kbaud)"
    virtual QString protocol() const = 0;

   signals:
    void opened(bool ok, const QString &message);
    // The adapter went away (unplugged, Bluetooth out of range)
    void lost(const QString &message);
};

// Shared by the CAN-speaking transports: put one ECU's ISO-TP frames back
// together (single frame, or first frame plus consecutive frames)
class IsoTpAssembler {
   public:
    // Feed one frame's data bytes (PCI first). Returns true when a whole
    // message is ready in `out`. `first` is set for a first frame, which the
    // sender must answer with a flow control frame (SocketCAN; an ELM327
    // sends it itself).
    bool feed(quint32 ecu, const QByteArray &data, QByteArray &out, bool *first = nullptr);
    void clear() { partial.clear(); }

   private:
    struct Partial {
        int expected = 0;
        int next_sn = 1;
        QByteArray data;
    };
    QMap<quint32, Partial> partial;
};
