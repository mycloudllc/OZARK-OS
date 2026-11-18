// OBD-II over whichever transport is configured: finds what the car
// supports, polls live values (fast ones like speed and rpm often, slow ones
// like coolant temperature every couple of seconds), reads and clears
// trouble codes, and reads the VIN. Values are SI (km/h, °C, kPa, V).
#pragma once

#include <functional>

#include <QDateTime>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QVariantMap>

#include "transport.h"

class Obd : public QObject {
    Q_OBJECT

   public:
    enum class State { Off, Connecting, NoCar, Connected, Error };

    explicit Obd(QObject *parent = nullptr);
    ~Obd() override;

    // Takes ownership; nullptr turns OBD off
    void set_transport(ObdTransport *transport, const QString &description);

    State state() const { return state_; }
    QVariantMap status() const;     // {state, link, protocol, vin, message, ecus, supported}
    QVariantMap values() const { return values_; }
    QVariantMap dtcs() const { return dtcs_; }

    // Mode 01 MIL and count, then stored (03), pending (07) and permanent (0A)
    // codes; the answer is dtcs_read()
    void read_dtcs();
    // Mode 04: clears stored and pending codes and turns the MIL off (the
    // car keeps permanent ones until it has re-tested); then reads again
    void clear_dtcs();

   signals:
    void status_changed();
    void values_changed(const QVariantMap &changed);
    void dtcs_read(const QVariantMap &result);
    void dtcs_cleared(bool ok, const QString &message);

   private:
    struct Job {
        QByteArray request;
        int timeout_ms;
        ObdTransport::Done done;
    };

    void set_state(State state, const QString &message);
    void enqueue(const QByteArray &request, int timeout_ms, ObdTransport::Done done, bool urgent = false);
    void run_next();
    void on_opened(bool ok, const QString &message);
    void on_lost(const QString &message);
    void reopen_later(int ms);
    void discover(int range = 0);
    void read_vin();
    void poll();
    void probe();
    void set_value(const QString &key, const QVariant &value);
    void flush_values();
    void dtc_step(int step, QVariantMap result);
    // Messages answering `service` (first byte service + 0x40), all ECUs
    static QList<QByteArray> answers(const EcuReplies &replies, quint8 service);

    ObdTransport *transport = nullptr;
    QString link_description;
    State state_ = State::Off;
    QString message;
    QString vin;
    QList<quint32> ecus;

    QList<Job> queue;
    bool busy = false;

    QSet<int> supported;  // mode 01 PIDs the car answers
    QList<int> fast, slow;
    int fast_index = 0, slow_index = 0, cycle = 0;
    int misses = 0;       // polls in a row with no answer
    QTimer poll_timer;
    QTimer probe_timer;   // car asleep: ask now and then
    QTimer reopen_timer;  // adapter gone: try again

    QVariantMap values_;
    QVariantMap changed;
    QTimer flush_timer;
    QVariantMap dtcs_;
    bool reading_dtcs = false;
};

// "P0301" from the two bytes of a DTC
QString dtc_code(quint8 a, quint8 b);
// What it means, for the common generic codes; otherwise the system it
// belongs to ("Powertrain, manufacturer specific")
QString dtc_description(const QString &code);
