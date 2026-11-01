#pragma once
// Custom-painted widgets: battery, charts, power-flow diagram, vehicle and trailer views.
#include <QDate>
#include <QJsonObject>
#include <QList>
#include <QPainter>
#include <QTimer>
#include <QVariantAnimation>
#include <QWidget>

#include "fmt.hpp"
#include "look.hpp"

namespace tc {

QFont scaled(const QFont &base, qreal px, bool bold = false, bool medium = false);
QFont smallFont(const QFont &base);
void card(QPainter &p, const QRectF &r, const QColor &fill, const QColor &border);

// A rounded panel that other widgets sit in
class Card : public QWidget {
   public:
    explicit Card(QWidget *parent = nullptr) : QWidget(parent) { setAttribute(Qt::WA_StyledBackground, false); }

   protected:
    void paintEvent(QPaintEvent *) override;
};

// Upright battery with the charge percentage inside, animated fill and a colour band
class BatteryWidget : public QWidget {
   public:
    explicit BatteryWidget(QWidget *parent = nullptr);
    void setCharge(double percent, const QString &band);
    QSize sizeHint() const override { return QSize(110, 190); }
    QSize minimumSizeHint() const override { return QSize(70, 120); }

   protected:
    void paintEvent(QPaintEvent *) override;
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;

   private:
    double target_ = 0, shown_ = 0;
    QString band_ = "green";
    QVariantAnimation anim_;
    QTimer pulse_;
    int phase_ = 0;
    bool first_ = true;
};

class LineChart : public QWidget {
   public:
    explicit LineChart(QWidget *parent = nullptr) : QWidget(parent) { setMinimumHeight(130); }
    struct Pt { double t, v; };
    void setData(const QList<Pt> &pts, double windowSeconds, const QStringList &axis);
    QSize sizeHint() const override { return QSize(400, 170); }

   protected:
    void paintEvent(QPaintEvent *) override;

   private:
    QList<Pt> pts_;
    double window_ = 86400;
    QStringList axis_;
};

class DayBars : public QWidget {
   public:
    explicit DayBars(QWidget *parent = nullptr) : QWidget(parent) { setMinimumHeight(120); }
    struct Day { QString label; double value; };
    void setDays(const QList<Day> &d) { days_ = d; update(); }
    QSize sizeHint() const override { return QSize(400, 150); }

   protected:
    void paintEvent(QPaintEvent *) override;

   private:
    QList<Day> days_;
};

struct FlowData {
    bool valid = false;
    double solarW = 0, altW = 0, controlsW = 0, inverterW = 0, inW = 0, outW = 0, netW = 0, solarA = 0, altA = 0, controlsA = 0;
    int controlsOn = 0;
    double soc = 0, volts = 0;
    QString band = "green", houseName = "House battery";
    bool charging = false;
};

// Power-flow diagram: sources, the house battery and the loads, with dashes moving along live paths
class FlowWidget : public QWidget {
   public:
    explicit FlowWidget(QWidget *parent = nullptr);
    void setData(const FlowData &d) { d_ = d; update(); }
    QSize sizeHint() const override { return QSize(560, 250); }
    QSize minimumSizeHint() const override { return QSize(360, 200); }

   protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void showEvent(QShowEvent *) override { timer_.start(); }
    void hideEvent(QHideEvent *) override { timer_.stop(); }

   private:
    QRectF batteryRect() const;
    BatteryWidget *bat_ = nullptr;
    void node(QPainter &p, const QRectF &r, const QString &name, const QString &value, const QString &sub, const QColor &edge);
    void line(QPainter &p, const QPointF &a, const QPointF &b, double watts, const QColor &c);
    FlowData d_;
    QTimer timer_;
    qint64 t0_;
};

struct TireInfo {
    QString pos, text, value;  // value: pressure number, text: second line
    bool low = false;
    bool hot = false;
};

// Top-down vehicle or trailer outline with a reading at each wheel
class RigWidget : public QWidget {
   public:
    enum Kind { Vehicle, Trailer };
    explicit RigWidget(Kind kind, QWidget *parent = nullptr) : QWidget(parent), kind_(kind) { setMinimumHeight(kind == Vehicle ? 230 : 170); }
    void setTires(const QList<TireInfo> &t) { tires_ = t; update(); }
    QSize sizeHint() const override { return QSize(300, kind_ == Vehicle ? 270 : 190); }

   protected:
    void paintEvent(QPaintEvent *) override;

   private:
    void tile(QPainter &p, const QRectF &r, const QString &name, const TireInfo *t);
    Kind kind_;
    QList<TireInfo> tires_;
};

void drawJeep(QPainter &p, const QRectF &box, const QColor &line, const QColor &wheel);
void drawTrailer(QPainter &p, const QRectF &box, const QColor &line, const QColor &wheel);

}  // namespace tc
