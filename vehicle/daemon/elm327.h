// An ELM327 (or clone) OBD adapter, over USB serial or Bluetooth RFCOMM.
// Talks the AT command set with headers on, so replies from several ECUs and
// multi-frame CAN messages can be told apart and put back together here.
#pragma once

#include <QList>
#include <QSocketNotifier>
#include <QTimer>

#include "transport.h"

class Elm327 : public ObdTransport {
    Q_OBJECT

   public:
    // A serial device ("/dev/ttyUSB0", a /dev/serial/by-id path) ...
    static Elm327 *serial(const QString &path, QObject *parent = nullptr);
    // ... or a Bluetooth adapter's address ("AA:BB:CC:DD:EE:FF"), RFCOMM channel 1
    static Elm327 *bluetooth(const QString &address, QObject *parent = nullptr);
    ~Elm327() override;

    void open() override;
    void close() override;
    void request(const QByteArray &service, int timeout_ms, Done done) override;
    QString protocol() const override { return protocol_name; }

   private:
    enum class Link { Serial, Bluetooth };
    Elm327(Link link, const QString &where, QObject *parent);

    // Raw command: text up to the '>' prompt, or an empty string on timeout
    using Raw = std::function<void(const QString &text)>;
    void command(const QString &cmd, int timeout_ms, Raw done);

    bool open_serial(int baud);
    void open_bluetooth();
    void start_session();  // after the link is up: reset and set up the ELM
    void try_next_baud();
    void init_step(int step);
    void fail(const QString &message);
    void read_ready();
    void finish_command(const QString &text);
    void parse_reply(const QString &text, const Done &done);

    Link link;
    QString where;
    int fd = -1;
    QSocketNotifier *reader = nullptr;
    QSocketNotifier *connecting = nullptr;  // Bluetooth connect in progress
    QTimer timeout;
    QByteArray buffer;
    Raw pending;
    QList<int> bauds;
    bool can = false;      // the car speaks CAN (headers are CAN ids)
    bool can29 = false;    // ... with 29-bit ids
    QString protocol_name;
    IsoTpAssembler assembler;
};
