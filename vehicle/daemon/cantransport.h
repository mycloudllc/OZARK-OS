// The car's CAN bus directly, through a SocketCAN interface (a USB-CAN
// adapter, or an MCP2515 on SPI) wired to the OBD port's CAN pins. OBD-II
// over ISO 15765-4: functional requests to 0x7DF (11 bit) or 0x18DB33F1
// (29 bit), ISO-TP replies reassembled here, with our own flow control.
#pragma once

#include <QSocketNotifier>
#include <QTimer>

#include "transport.h"

class CanTransport : public ObdTransport {
    Q_OBJECT

   public:
    // interface: "can0"; bitrate: set the interface to this first (needs
    // root and iproute2), 0 to leave it as it is (vcan, or set elsewhere)
    CanTransport(const QString &interface, int bitrate, QObject *parent = nullptr);
    ~CanTransport() override;

    void open() override;
    void close() override;
    void request(const QByteArray &service, int timeout_ms, Done done) override;
    QString protocol() const override;

   private:
    void read_frames();
    void send_frame(quint32 id, const QByteArray &data);
    void finish(bool ok, const QString &error = QString());
    bool is_reply_id(quint32 id) const;

    QString interface;
    int bitrate;
    int fd = -1;
    QSocketNotifier *reader = nullptr;
    // 11 or 29 bit ids, once a car has answered; 0 until then
    int id_bits = 0;
    int probing_bits = 11;

    Done pending;
    EcuReplies replies;
    IsoTpAssembler assembler;
    QTimer timeout;   // no answer at all
    QTimer settle;    // an answer came: wait a little for other ECUs
};
