// Plain-English names for the common generic (SAE J2012) trouble codes.
// Manufacturer codes (P1xxx, and the like) differ per make: those get the
// system they belong to instead.
#include <QHash>
#include <QString>

#include "obd.h"

namespace {

QHash<QString, QString> make_table()
{
    QHash<QString, QString> t = {
        {"P0010", "Camshaft position actuator A circuit (bank 1)"},
        {"P0011", "Camshaft A timing over-advanced or system performance (bank 1)"},
        {"P0012", "Camshaft A timing over-retarded (bank 1)"},
        {"P0016", "Crankshaft/camshaft position correlation (bank 1 sensor A)"},
        {"P0030", "O2 sensor heater control circuit (bank 1 sensor 1)"},
        {"P0036", "O2 sensor heater control circuit (bank 1 sensor 2)"},
        {"P0087", "Fuel rail/system pressure too low"},
        {"P0088", "Fuel rail/system pressure too high"},
        {"P0100", "Mass air flow (MAF) circuit"},
        {"P0101", "Mass air flow (MAF) circuit range/performance"},
        {"P0102", "Mass air flow (MAF) circuit low"},
        {"P0103", "Mass air flow (MAF) circuit high"},
        {"P0106", "Manifold pressure (MAP) circuit range/performance"},
        {"P0107", "Manifold pressure (MAP) circuit low"},
        {"P0108", "Manifold pressure (MAP) circuit high"},
        {"P0110", "Intake air temperature sensor circuit"},
        {"P0112", "Intake air temperature sensor circuit low"},
        {"P0113", "Intake air temperature sensor circuit high"},
        {"P0115", "Engine coolant temperature sensor circuit"},
        {"P0116", "Engine coolant temperature sensor range/performance"},
        {"P0117", "Engine coolant temperature sensor circuit low"},
        {"P0118", "Engine coolant temperature sensor circuit high"},
        {"P0120", "Throttle/pedal position sensor A circuit"},
        {"P0121", "Throttle/pedal position sensor A range/performance"},
        {"P0122", "Throttle/pedal position sensor A circuit low"},
        {"P0123", "Throttle/pedal position sensor A circuit high"},
        {"P0125", "Coolant too cold for closed-loop fuel control"},
        {"P0128", "Thermostat: coolant below regulating temperature"},
        {"P0130", "O2 sensor circuit (bank 1 sensor 1)"},
        {"P0131", "O2 sensor low voltage (bank 1 sensor 1)"},
        {"P0132", "O2 sensor high voltage (bank 1 sensor 1)"},
        {"P0133", "O2 sensor slow response (bank 1 sensor 1)"},
        {"P0134", "O2 sensor no activity (bank 1 sensor 1)"},
        {"P0135", "O2 sensor heater circuit (bank 1 sensor 1)"},
        {"P0136", "O2 sensor circuit (bank 1 sensor 2)"},
        {"P0137", "O2 sensor low voltage (bank 1 sensor 2)"},
        {"P0138", "O2 sensor high voltage (bank 1 sensor 2)"},
        {"P0139", "O2 sensor slow response (bank 1 sensor 2)"},
        {"P0140", "O2 sensor no activity (bank 1 sensor 2)"},
        {"P0141", "O2 sensor heater circuit (bank 1 sensor 2)"},
        {"P0150", "O2 sensor circuit (bank 2 sensor 1)"},
        {"P0151", "O2 sensor low voltage (bank 2 sensor 1)"},
        {"P0152", "O2 sensor high voltage (bank 2 sensor 1)"},
        {"P0155", "O2 sensor heater circuit (bank 2 sensor 1)"},
        {"P0156", "O2 sensor circuit (bank 2 sensor 2)"},
        {"P0161", "O2 sensor heater circuit (bank 2 sensor 2)"},
        {"P0171", "System too lean (bank 1)"},
        {"P0172", "System too rich (bank 1)"},
        {"P0174", "System too lean (bank 2)"},
        {"P0175", "System too rich (bank 2)"},
        {"P0191", "Fuel rail pressure sensor range/performance"},
        {"P0200", "Injector circuit/open"},
        {"P0217", "Engine coolant over-temperature"},
        {"P0218", "Transmission fluid over-temperature"},
        {"P0219", "Engine overspeed"},
        {"P0220", "Throttle/pedal position sensor B circuit"},
        {"P0234", "Turbo/supercharger overboost"},
        {"P0299", "Turbo/supercharger underboost"},
        {"P0300", "Random/multiple cylinder misfire detected"},
        {"P0315", "Crankshaft position variation not learned"},
        {"P0325", "Knock sensor 1 circuit (bank 1)"},
        {"P0327", "Knock sensor 1 circuit low (bank 1)"},
        {"P0328", "Knock sensor 1 circuit high (bank 1)"},
        {"P0335", "Crankshaft position sensor A circuit"},
        {"P0336", "Crankshaft position sensor A range/performance"},
        {"P0340", "Camshaft position sensor A circuit (bank 1)"},
        {"P0341", "Camshaft position sensor A range/performance (bank 1)"},
        {"P0380", "Glow plug/heater circuit A"},
        {"P0400", "EGR flow"},
        {"P0401", "EGR flow insufficient"},
        {"P0402", "EGR flow excessive"},
        {"P0404", "EGR circuit range/performance"},
        {"P0405", "EGR sensor A circuit low"},
        {"P0410", "Secondary air injection system"},
        {"P0411", "Secondary air injection incorrect flow"},
        {"P0420", "Catalyst efficiency below threshold (bank 1)"},
        {"P0430", "Catalyst efficiency below threshold (bank 2)"},
        {"P0440", "Evaporative emission (EVAP) system"},
        {"P0441", "EVAP system incorrect purge flow"},
        {"P0442", "EVAP system small leak"},
        {"P0443", "EVAP purge control valve circuit"},
        {"P0446", "EVAP vent control circuit"},
        {"P0449", "EVAP vent valve/solenoid circuit"},
        {"P0450", "EVAP pressure sensor/switch"},
        {"P0451", "EVAP pressure sensor range/performance"},
        {"P0452", "EVAP pressure sensor low"},
        {"P0453", "EVAP pressure sensor high"},
        {"P0455", "EVAP system large leak"},
        {"P0456", "EVAP system very small leak"},
        {"P0457", "EVAP leak: fuel cap loose or off"},
        {"P0460", "Fuel level sensor A circuit"},
        {"P0461", "Fuel level sensor A range/performance"},
        {"P0480", "Cooling fan 1 control circuit"},
        {"P0491", "Secondary air injection insufficient flow (bank 1)"},
        {"P0496", "EVAP system high purge flow"},
        {"P0500", "Vehicle speed sensor A"},
        {"P0505", "Idle air control system"},
        {"P0506", "Idle speed lower than expected"},
        {"P0507", "Idle speed higher than expected"},
        {"P0520", "Engine oil pressure sensor/switch A circuit"},
        {"P0521", "Engine oil pressure sensor/switch A range/performance"},
        {"P0522", "Engine oil pressure sensor/switch A low"},
        {"P0523", "Engine oil pressure sensor/switch A high"},
        {"P0530", "A/C refrigerant pressure sensor A circuit"},
        {"P0562", "System voltage low"},
        {"P0563", "System voltage high"},
        {"P0571", "Brake switch A circuit"},
        {"P0600", "Serial communication link"},
        {"P0601", "Control module memory checksum error"},
        {"P0603", "Control module keep-alive memory (KAM) error"},
        {"P0606", "Control module processor"},
        {"P0700", "Transmission control system (see the transmission's codes)"},
        {"P0705", "Transmission range sensor A circuit"},
        {"P0715", "Input/turbine speed sensor A circuit"},
        {"P0720", "Output speed sensor circuit"},
        {"P0730", "Incorrect gear ratio"},
        {"P0740", "Torque converter clutch solenoid circuit"},
        {"P0741", "Torque converter clutch performance or stuck off"},
        {"P0750", "Shift solenoid A"},
        {"P0755", "Shift solenoid B"},
        {"P2002", "Diesel particulate filter efficiency below threshold (bank 1)"},
        {"P2096", "Post-catalyst fuel trim too lean (bank 1)"},
        {"P2097", "Post-catalyst fuel trim too rich (bank 1)"},
        {"P2135", "Throttle/pedal position sensor A/B voltage correlation"},
        {"P2138", "Throttle/pedal position sensor D/E voltage correlation"},
        {"P2187", "System too lean at idle (bank 1)"},
        {"P2195", "O2 sensor stuck lean (bank 1 sensor 1)"},
        {"P2196", "O2 sensor stuck rich (bank 1 sensor 1)"},
        {"P2270", "O2 sensor stuck lean (bank 1 sensor 2)"},
        {"P2271", "O2 sensor stuck rich (bank 1 sensor 2)"},
        {"P242F", "Diesel particulate filter restriction: ash"},
        {"P2463", "Diesel particulate filter restriction: soot"},
        {"U0001", "High-speed CAN bus"},
        {"U0073", "Control module communication bus A off"},
        {"U0100", "Lost communication with the engine computer (ECM/PCM)"},
        {"U0101", "Lost communication with the transmission computer (TCM)"},
        {"U0121", "Lost communication with the ABS module"},
        {"U0140", "Lost communication with the body control module"},
        {"U0155", "Lost communication with the instrument cluster"},
    };
    for (int n = 1; n <= 8; n++) {
        t.insert(QString("P020%1").arg(n), QString("Injector circuit/open, cylinder %1").arg(n));
        t.insert(QString("P030%1").arg(n), QString("Cylinder %1 misfire detected").arg(n));
        t.insert(QString("P035%1").arg(n), QString("Ignition coil %1 circuit").arg(n));
    }
    return t;
}

}  // namespace

QString dtc_description(const QString &code)
{
    static const QHash<QString, QString> table = make_table();
    const auto it = table.find(code);
    if (it != table.end())
        return it.value();
    if (code.size() != 5)
        return QString();

    const QChar system = code[0];
    const bool generic = code[1] == '0' || (system == 'P' && code[1] == '2') ||
                         (system == 'P' && code[1] == '3' && code[2] >= 'A');
    const QString kind = generic ? "generic" : "manufacturer specific";
    if (system == 'P') {
        QString area;
        switch (code[2].toLatin1()) {
            case '0':
            case '1':
            case '2': area = "Fuel and air metering"; break;
            case '3': area = "Ignition system or misfire"; break;
            case '4': area = "Emission controls"; break;
            case '5': area = "Speed, idle control or auxiliary inputs"; break;
            case '6': area = "Computer or output circuits"; break;
            case '7':
            case '8':
            case '9': area = "Transmission"; break;
            default: area = "Powertrain";
        }
        return QString("%1 (%2)").arg(area, kind);
    }
    if (system == 'C')
        return QString("Chassis: brakes, steering or suspension (%1)").arg(kind);
    if (system == 'B')
        return QString("Body: airbags, seats, lights or climate (%1)").arg(kind);
    return QString("Network/communication between modules (%1)").arg(kind);
}
