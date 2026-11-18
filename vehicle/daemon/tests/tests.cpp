// Host tests for ozark-vehicled: a fake ELM327 on a pseudo-terminal answering
// like a CAN car (two ECUs, multi-frame replies), plus the decoders.
//   cmake -S . -B build -DVEHICLED_TESTS=ON && make -C build vehicled-tests && build/vehicled-tests
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSocketNotifier>
#include <QTimer>
#include <cstdio>
#include <functional>

#include "elm327.h"
#include "obd.h"
#include "tpms.h"

namespace {
int failures = 0, checks = 0;

void check(bool ok, const QString &what)
{
    checks++;
    if (!ok)
        failures++;
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.toUtf8().constData());
}

// Run the event loop until `done` or `ms` pass
bool wait_for(std::function<bool()> done, int ms)
{
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

// A CAN car behind an ELM327: answers what the daemon asks
class FakeElm : public QObject {
   public:
    int master = -1;
    QString slave;
    QByteArray line;
    bool cleared = false;
    QStringList seen;

    FakeElm()
    {
        master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
        grantpt(master);
        unlockpt(master);
        slave = QString::fromLatin1(ptsname(master));
        auto n = new QSocketNotifier(master, QSocketNotifier::Read, this);
        connect(n, &QSocketNotifier::activated, this, [this] { this->read(); });
    }

    void reply(const QString &text)
    {
        const QByteArray out = text.toLatin1() + "\r\r>";
        ::write(master, out.constData(), size_t(out.size()));
    }

    void read()
    {
        char buf[256];
        ssize_t n;
        while ((n = ::read(master, buf, sizeof(buf))) > 0) {
            line.append(buf, int(n));
            int cr;
            while ((cr = line.indexOf('\r')) >= 0) {
                const QString cmd = QString::fromLatin1(line.left(cr)).trimmed().toUpper();
                line.remove(0, cr + 1);
                if (!cmd.isEmpty())
                    answer(cmd);
            }
        }
    }

    void answer(const QString &cmd)
    {
        seen << cmd;
        if (cmd == "ATI" || cmd == "ATWS")
            reply("ELM327 v1.5");
        else if (cmd == "ATDPN")
            reply("A6");
        else if (cmd.startsWith("AT"))
            reply("OK");
        // 01, 04, 05, 0C, 0D, 0F, 11 supported; nothing past 0x20
        else if (cmd == "0100")
            reply("SEARCHING...\r7E8 06 41 00 98 1A 80 00");
        else if (cmd == "0101")
            reply(cleared ? "7E8 06 41 01 00 07 E5 00" : "7E8 06 41 01 84 07 E5 00");
        else if (cmd == "010C")
            reply("7E8 04 41 0C 1A F8");
        else if (cmd == "010D")
            reply("7E8 03 41 0D 32");
        else if (cmd == "0105")
            reply("7E8 03 41 05 7B");
        // Three codes from the engine (multi-frame), one from another ECU
        else if (cmd == "03")
            reply(cleared ? "7E8 02 43 00"
                          : "7E8 10 08 43 03 01 33 03 01\r7E8 21 04 20 00 00 00 00 00\r7E9 04 43 01 C1 00");
        else if (cmd == "07")
            reply("7E8 02 47 00");
        else if (cmd == "0902")
            reply("7E8 10 14 49 02 01 31 47 31\r7E8 21 4A 43 35 34 34 34 52\r7E8 22 37 32 35 32 33 36 37");
        else if (cmd == "04") {
            cleared = true;
            reply("7E8 01 44");
        } else
            reply("NO DATA");
    }
};

QStringList codes(const QVariantMap &result, const char *key)
{
    QStringList out;
    for (const QVariant &v : result.value(key).toList())
        out << v.toMap().value("code").toString();
    return out;
}
}  // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // ---- decoders
    check(dtc_code(0x01, 0x33) == "P0133", "DTC 01 33 is P0133");
    check(dtc_code(0xC1, 0x00) == "U0100", "DTC C1 00 is U0100");
    check(dtc_code(0x44, 0x20) == "C0420", "DTC 44 20 is C0420");
    check(dtc_description("P0301") == "Cylinder 1 misfire detected", "P0301 has a name");
    check(dtc_description("P1234").contains("manufacturer specific"), "P1234 falls back to its system");

    {
        // A real advertisement: 80:EA:CA:10:8A:78, 28131 Pa, 27.90 °C, 91 %
        const QByteArray ad = QByteArray::fromHex("80EACA108A78E36D0000E60A00005B00");
        double kpa = 0, temp = 0;
        int battery = 0;
        bool alarm = true;
        const bool ok = BleTpms::decode(0x0100, ad, kpa, temp, battery, alarm);
        check(ok && qAbs(kpa - 28.131) < 0.001 && qAbs(temp - 27.9) < 0.001 && battery == 91 && !alarm,
              QString("BLE TPMS decode: %1 kPa, %2 C, %3 %").arg(kpa).arg(temp).arg(battery));
        check(!BleTpms::decode(0x004C, ad, kpa, temp, battery, alarm), "other companies' data is ignored");
    }

    {
        IsoTpAssembler a;
        QByteArray out;
        bool first = false;
        check(!a.feed(0x7E8, QByteArray::fromHex("1008430301330301"), out, &first) && first, "ISO-TP first frame");
        check(a.feed(0x7E8, QByteArray::fromHex("2104200000000000"), out) &&
                  out == QByteArray::fromHex("4303013303010420"),
              "ISO-TP message reassembled");
        check(!a.feed(0x7E8, QByteArray::fromHex("2204"), out), "stray consecutive frame ignored");
    }

    // ---- the fake car
    FakeElm car;
    Obd obd;
    QVariantMap dtc_result;
    bool cleared = false;
    QString clear_message;
    QObject::connect(&obd, &Obd::dtcs_read, [&](const QVariantMap &r) { dtc_result = r; });
    QObject::connect(&obd, &Obd::dtcs_cleared, [&](bool ok, const QString &m) {
        cleared = ok;
        clear_message = m;
    });
    obd.set_transport(Elm327::serial(car.slave), "fake ELM327");

    check(wait_for([&] { return obd.state() == Obd::State::Connected; }, 15000),
          "connects: " + obd.status().value("message").toString());
    check(obd.status().value("protocol").toString().contains("CAN (11 bit, 500"), "protocol is CAN 11 bit 500k");
    check(car.seen.contains("ATH1") && car.seen.contains("ATSP0"), "headers on, automatic protocol");

    check(wait_for([&] {
              const QVariantMap v = obd.values();
              return v.contains("rpm") && v.contains("speed_kmh") && v.contains("coolant_c");
          }, 8000),
          "live values arrive");
    const QVariantMap v = obd.values();
    check(qAbs(v.value("rpm").toDouble() - 1726) < 0.01, QString("rpm 1726 (got %1)").arg(v.value("rpm").toDouble()));
    check(v.value("speed_kmh").toDouble() == 50, "speed 50 km/h");
    check(v.value("coolant_c").toDouble() == 83, "coolant 83 C");
    check(!v.contains("fuel_pct"), "unsupported PIDs aren't asked for");

    check(wait_for([&] { return obd.status().value("vin").toString() == "1G1JC5444R7252367"; }, 5000),
          "VIN read (multi-frame): " + obd.status().value("vin").toString());

    // read at connect time already; read again on request
    dtc_result.clear();
    obd.read_dtcs();
    check(wait_for([&] { return !dtc_result.isEmpty(); }, 8000), "trouble codes read");
    check(dtc_result.value("ok").toBool() && dtc_result.value("mil").toBool() && dtc_result.value("count").toInt() == 4,
          "MIL on, 4 codes");
    const QStringList stored = codes(dtc_result, "stored");
    check(stored == QStringList({"P0133", "P0301", "P0420", "U0100"}) ||
              (stored.size() == 4 && stored.contains("U0100") && stored.contains("P0420")),
          "stored: " + stored.join(", "));
    check(codes(dtc_result, "pending").isEmpty() && codes(dtc_result, "permanent").isEmpty(),
          "no pending or permanent codes");

    dtc_result.clear();
    obd.clear_dtcs();
    check(wait_for([&] { return cleared; }, 5000), "codes cleared: " + clear_message);
    check(wait_for([&] { return !dtc_result.isEmpty(); }, 8000) && codes(dtc_result, "stored").isEmpty() &&
              !dtc_result.value("mil").toBool(),
          "read again after clearing: none, MIL off");

    std::printf("\n%d of %d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
