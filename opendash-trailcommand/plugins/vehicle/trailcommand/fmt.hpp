#pragma once
// Number formatting that follows the hub's unit settings (Settings, Appearance on the web dashboard).
#include <QString>
#include <cmath>

namespace tc {

struct Units {
    QString pressure = "psi", temp = "c", distance = "mi";

    QString pressureUnit() const { return pressure; }
    QString pressureValue(double psi) const
    {
        if (pressure == "bar") return QString::number(psi * 0.0689476, 'f', 2);
        if (pressure == "kPa") return QString::number(qRound(psi * 6.89476));
        return QString::number(qRound(psi));
    }
    QString temperature(double c) const
    {
        return temp == "f" ? QString("%1 °F").arg(qRound(c * 9 / 5 + 32)) : QString("%1 °C").arg(qRound(c));
    }
    QString distanceUnit() const { return distance == "km" ? "km" : "mi"; }
    QString distanceValue(double miles) const
    {
        return QLocale().toString(qRound(distance == "km" ? miles * 1.609344 : miles));
    }
    double toMiles(double v) const { return distance == "km" ? v / 1.609344 : v; }
    double fromMiles(double mi) const { return distance == "km" ? mi * 1.609344 : mi; }
};

inline QString hours(double h)
{
    if (h < 0) return QString();
    if (h >= 100) return "99+ h";
    int m = qRound(h * 60);
    return QString("%1 h %2 m").arg(m / 60).arg(m % 60);
}

inline QString bytes(double b)
{
    if (b >= 1073741824.0) return QString::number(b / 1073741824.0, 'f', 2) + " GB";
    if (b >= 1048576.0) return QString::number(b / 1048576.0, 'f', 1) + " MB";
    return QString::number(qRound(b / 1024.0)) + " KB";
}

}  // namespace tc
