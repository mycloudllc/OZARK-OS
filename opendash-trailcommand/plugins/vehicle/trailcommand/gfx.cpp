#include "gfx.hpp"

#include <QDateTime>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainterPath>
#include <QResizeEvent>
#include <QToolTip>
#include <cmath>

namespace tc {

QFont scaled(const QFont &base, qreal px, bool bold, bool medium)
{
    QFont f = base;
    f.setPixelSize(qMax(8, qRound(px)));
    f.setBold(bold);
    if (medium && !bold) f.setWeight(QFont::Medium);
    return f;
}

// Axis labels and values: about 70% of the base text. Dash sets fonts in
// points, so QFont::pixelSize() is -1; QFontInfo gives the real pixel size
// (which follows the screen's DPI).
QFont smallFont(const QFont &base)
{
    return scaled(base, qMax<qreal>(10, QFontInfo(base).pixelSize() * 0.7));
}

void card(QPainter &p, const QRectF &r, const QColor &fill, const QColor &border)
{
    p.setPen(QPen(border, 1));
    p.setBrush(fill);
    p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);
}

void Card::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    card(p, rect(), Look::get().panel, Look::get().line);
}

/* ---------- battery ---------- */

BatteryWidget::BatteryWidget(QWidget *parent) : QWidget(parent)
{
    anim_.setDuration(900);
    anim_.setEasingCurve(QEasingCurve::OutCubic);
    connect(&anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        shown_ = v.toDouble();
        update();
    });
    pulse_.setInterval(60);
    connect(&pulse_, &QTimer::timeout, this, [this] {
        phase_ = (phase_ + 1) % 40;
        update();
    });
}

void BatteryWidget::setCharge(double percent, const QString &band)
{
    percent = qBound(0.0, percent, 100.0);
    band_ = band;
    if (qFuzzyCompare(percent + 1, target_ + 1) && !first_) return;
    target_ = percent;
    anim_.stop();
    if (!isVisible()) {
        shown_ = percent;
    }
    else {
        anim_.setStartValue(first_ ? 0.0 : shown_);
        anim_.setEndValue(percent);
        anim_.start();
    }
    first_ = false;
    update();
}

void BatteryWidget::showEvent(QShowEvent *)
{
    pulse_.start();
    if (!qFuzzyCompare(shown_ + 1, target_ + 1)) {
        anim_.setStartValue(0.0);  // fills up when the page is shown
        anim_.setEndValue(target_);
        anim_.start();
    }
}
void BatteryWidget::hideEvent(QHideEvent *) { pulse_.stop(); }

void BatteryWidget::paintEvent(QPaintEvent *)
{
    const Look &look = Look::get();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    qreal h = qMin<qreal>(height(), width() * 1.9), w = h / 1.9;
    const qreal x = (width() - w) / 2, y = (height() - h) / 2, term = h * 0.06;
    const qreal pen = qMax(2.0, w * 0.045), pad = pen * 1.7;
    const QRectF body(x + pen / 2, y + term + pen / 2, w - pen, h - term - pen);
    p.setPen(Qt::NoPen);
    p.setBrush(look.text);
    p.drawRoundedRect(QRectF(x + w * 0.3, y, w * 0.4, term + pen), 3, 3);  // terminal
    p.setPen(QPen(look.text, pen));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(body, w * 0.12, w * 0.12);

    const QRectF inner = body.adjusted(pad, pad, -pad, -pad);
    const qreal fillH = inner.height() * shown_ / 100.0;
    const QRectF fill(inner.left(), inner.bottom() - fillH, inner.width(), fillH);
    const QColor c = Look::band(band_);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    if (fillH > 0.5) p.drawRoundedRect(fill, pad, pad);
    if (fillH > 6) {  // soft pulse at the leading edge of the fill
        const qreal a = 0.15 + 0.35 * (0.5 + 0.5 * std::sin(phase_ / 40.0 * 6.2832));
        QLinearGradient g(fill.topLeft(), QPointF(fill.left(), fill.top() + qMin(fillH, inner.height() * 0.14)));
        g.setColorAt(0, QColor(255, 255, 255, int(255 * a)));
        g.setColorAt(1, QColor(255, 255, 255, 0));
        p.setBrush(g);
        p.drawRoundedRect(fill, pad, pad);
    }

    // percentage, drawn in two colours so it reads on the fill and on the empty part
    const QString num = QString::number(qRound(shown_));
    const QFont big = scaled(font(), inner.width() * 0.5, true), small = scaled(font(), inner.width() * 0.22, true);
    const QFontMetricsF fb(big), fs(small);
    const qreal tw = fb.horizontalAdvance(num) + fs.horizontalAdvance("%") + 2;
    const QPointF base(inner.center().x() - tw / 2, inner.center().y() + fb.ascent() / 2 - fb.descent() / 2);
    auto draw = [&](const QColor &col) {
        p.setPen(col);
        p.setFont(big);
        p.drawText(base, num);
        p.setFont(small);
        p.drawText(QPointF(base.x() + fb.horizontalAdvance(num) + 2, base.y()), "%");
    };
    draw(look.text);
    if (fillH > 0.5) {
        p.save();
        p.setClipRect(fill);
        draw(look.onFill);
        p.restore();
    }
}

/* ---------- charts ---------- */

void LineChart::setData(const QList<Pt> &pts, double windowSeconds, const QStringList &axis)
{
    pts_ = pts;
    window_ = windowSeconds;
    axis_ = axis;
    update();
}

void LineChart::paintEvent(QPaintEvent *)
{
    const Look &look = Look::get();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QFont f = smallFont(font());
    p.setFont(f);
    const QFontMetrics fm(f);
    const QRectF a(6, 6, width() - 12, height() - 12 - fm.height());
    p.setPen(QPen(look.line, 1));
    for (int i = 1; i <= 3; i++) p.drawLine(QPointF(a.left(), a.top() + a.height() * i / 4), QPointF(a.right(), a.top() + a.height() * i / 4));
    if (pts_.isEmpty()) {
        p.setPen(look.muted);
        p.drawText(a, Qt::AlignCenter, "No history yet");
    }
    else {
        const double now = QDateTime::currentSecsSinceEpoch();
        QPainterPath path;
        for (int i = 0; i < pts_.size(); i++) {
            const QPointF pt(a.left() + qBound(0.0, (pts_[i].t - (now - window_)) / window_, 1.0) * a.width(), a.bottom() - qBound(0.0, pts_[i].v, 100.0) / 100.0 * a.height());
            if (i == 0) path.moveTo(pt); else path.lineTo(pt);
        }
        p.setPen(QPen(look.accent.isValid() && look.accent.lightness() > 20 && look.accent.lightness() < 240 ? look.accent : look.green, 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }
    p.setPen(look.muted);
    const int y = height() - 2 - fm.descent();  // room for descenders ("ago")
    if (axis_.size() > 0) p.drawText(QPointF(6, y), axis_[0]);
    if (axis_.size() > 1) p.drawText(QPointF(width() / 2.0 - fm.horizontalAdvance(axis_[1]) / 2.0, y), axis_[1]);
    if (axis_.size() > 2) p.drawText(QPointF(width() - 6 - fm.horizontalAdvance(axis_[2]), y), axis_[2]);
}

void DayBars::paintEvent(QPaintEvent *)
{
    const Look &look = Look::get();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QFont f = smallFont(font());
    p.setFont(f);
    const QFontMetrics fm(f);
    if (days_.isEmpty()) {
        p.setPen(look.muted);
        p.drawText(rect(), Qt::AlignCenter, "No data yet");
        return;
    }
    double max = 1;
    for (const Day &d : days_) max = qMax(max, d.value);
    const int n = days_.size();
    const qreal gap = 8, bw = (width() - 12 - gap * (n - 1)) / n, top = fm.height() + 4, bottom = fm.height() + 4;
    const QColor c = look.accent.lightness() > 20 && look.accent.lightness() < 240 ? look.accent : look.green;
    for (int i = 0; i < n; i++) {
        const qreal x = 6 + i * (bw + gap), h = (height() - top - bottom) * days_[i].value / max;
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawRoundedRect(QRectF(x, height() - bottom - h, bw, h), 3, 3);
        p.setPen(look.text);
        p.drawText(QRectF(x - 4, height() - bottom - h - fm.height() - 2, bw + 8, fm.height()), Qt::AlignCenter, QString::number(qRound(days_[i].value)));
        p.setPen(look.muted);
        p.drawText(QRectF(x - 4, height() - fm.height() - 1, bw + 8, fm.height()), Qt::AlignCenter, days_[i].label);
    }
}

/* ---------- power flow ---------- */

FlowWidget::FlowWidget(QWidget *parent) : QWidget(parent)
{
    bat_ = new BatteryWidget(this);
    t0_ = QDateTime::currentMSecsSinceEpoch();
    timer_.setInterval(40);
    connect(&timer_, &QTimer::timeout, this, [this] { update(); });
}

QRectF FlowWidget::batteryRect() const
{
    const qreal w = width(), h = height();
    return QRectF(w * 0.355, 0, w * 0.29, h);
}

void FlowWidget::resizeEvent(QResizeEvent *)
{
    const QRectF b = batteryRect();
    const QFontMetrics fm(scaled(font(), qMax(12, height() / 12)));
    bat_->setGeometry(QRectF(b.left() + 4, b.top() + 6, b.width() - 8, b.height() - fm.height() * 2.6 - 10).toRect());
    if (d_.valid) bat_->setCharge(d_.soc, d_.band);
}

void FlowWidget::node(QPainter &p, const QRectF &r, const QString &name, const QString &value, const QString &sub, const QColor &edge)
{
    const Look &look = Look::get();
    card(p, r, look.bg, edge);
    const qreal u = qMax<qreal>(11, qMin(r.height() * 0.17, r.width() * 0.11));
    p.setPen(look.text);
    p.setFont(scaled(font(), u * 1.15, false, true));
    const QFontMetricsF f1(p.font());
    qreal y = r.top() + (r.height() - (f1.height() + u * 2.4)) / 2 + f1.ascent();
    p.drawText(QPointF(r.left() + 10, y), f1.elidedText(name, Qt::ElideRight, r.width() - 16));
    p.setFont(scaled(font(), u, false, true));
    y += QFontMetricsF(p.font()).height() * 1.05;
    p.drawText(QPointF(r.left() + 10, y), value);
    p.setPen(look.muted);
    p.setFont(scaled(font(), u * 0.8));
    const QFontMetricsF f3(p.font());
    y += f3.height() * 1.15;
    p.drawText(QPointF(r.left() + 10, y), f3.elidedText(sub, Qt::ElideRight, r.width() - 16));
}

void FlowWidget::line(QPainter &p, const QPointF &a, const QPointF &b, double watts, const QColor &c)
{
    const Look &look = Look::get();
    if (watts < 1) {
        p.setPen(QPen(look.line, 3, Qt::SolidLine, Qt::FlatCap));
        p.drawLine(a + QPointF(6, 0), b - QPointF(6, 0));
        return;
    }
    const double cycle = qBound(0.5, 600.0 / (watts + 100.0), 3.0);  // seconds for one dash to move one dash length
    const double pxPerSec = 18.0 / cycle, elapsed = (QDateTime::currentMSecsSinceEpoch() - t0_) / 1000.0;
    QPen pen(c, 3, Qt::CustomDashLine, Qt::FlatCap);
    pen.setDashPattern({2.6, 3.4});  // in units of the pen width: 8 px dash, 10 px gap
    pen.setDashOffset(-std::fmod(elapsed * pxPerSec, 18.0) / 3.0);
    p.setPen(pen);
    const QPointF s = a + QPointF(6, 0), e = b - QPointF(12, 0);
    p.drawLine(s, e);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawPolygon(QPolygonF({QPointF(e.x() + 1, e.y() - 6), QPointF(e.x() + 11, e.y()), QPointF(e.x() + 1, e.y() + 6)}));
}

void FlowWidget::paintEvent(QPaintEvent *)
{
    const Look &look = Look::get();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (!d_.valid) {
        p.setPen(look.muted);
        p.drawText(rect(), Qt::AlignCenter, "Waiting for the hub…");
        return;
    }
    const qreal w = width(), h = height(), gap = qMax<qreal>(8, h * 0.04), rh = (h - gap) / 2, nw = w * 0.285;
    const QRectF solar(0, 0, nw, rh), alt(0, rh + gap, nw, rh), ctl(w - nw, 0, nw, rh), inv(w - nw, rh + gap, nw, rh);
    const QColor in = look.green, out = look.accent.lightness() > 20 && look.accent.lightness() < 240 ? look.accent : look.yellow;
    node(p, solar, "Solar", QString("%1 W").arg(qRound(d_.solarW)), d_.solarW >= 1 ? QString("%1 A from solar charger").arg(d_.solarA, 0, 'f', 1) : "No solar input", d_.solarW >= 1 ? in : look.line);
    node(p, alt, "Alternator", QString("%1 W").arg(qRound(d_.altW)), d_.altW >= 1 ? QString("%1 A from alternator").arg(d_.altA, 0, 'f', 1) : "Engine off", d_.altW >= 1 ? in : look.line);
    node(p, ctl, "Controls", QString("%1 W").arg(qRound(d_.controlsW)), QString("%1 A, %2 on").arg(d_.controlsA, 0, 'f', 1).arg(d_.controlsOn), d_.controlsW >= 1 ? out : look.line);
    node(p, inv, "Inverter", QString("%1 W").arg(qRound(d_.inverterW)), d_.inverterW >= 1 ? "On" : "Off", d_.inverterW >= 1 ? out : look.line);

    const QRectF b = batteryRect();
    card(p, b, look.bg, look.line);
    const qreal u = qMax<qreal>(12, h / 12);
    p.setPen(look.text);
    p.setFont(scaled(font(), u * 1.1, false, true));
    const QFontMetricsF f1(p.font());
    qreal y = b.bottom() - f1.height() * 2.3 + f1.ascent();
    p.drawText(QRectF(b.left(), y - f1.ascent(), b.width(), f1.height()), Qt::AlignCenter, d_.houseName);
    p.setFont(scaled(font(), u, false, true));
    p.drawText(QRectF(b.left(), y - f1.ascent() + f1.height(), b.width(), f1.height()), Qt::AlignCenter, QString("%1%2 W").arg(d_.netW > 0 ? "+" : "").arg(qRound(d_.netW)));
    p.setPen(look.muted);
    p.setFont(scaled(font(), u * 0.8));
    p.drawText(QRectF(b.left(), y - f1.ascent() + f1.height() * 1.9, b.width(), f1.height()), Qt::AlignCenter, QString("%1 at %2 V").arg(d_.charging ? "Charging" : "Discharging").arg(d_.volts, 0, 'f', 1));

    const qreal y1 = rh / 2, y2 = rh + gap + rh / 2;
    line(p, QPointF(nw, y1), QPointF(b.left(), y1), d_.solarW, in);
    line(p, QPointF(nw, y2), QPointF(b.left(), y2), d_.altW, in);
    line(p, QPointF(b.right(), y1), QPointF(w - nw, y1), d_.controlsW, out);
    line(p, QPointF(b.right(), y2), QPointF(w - nw, y2), d_.inverterW, out);
}

/* ---------- vehicle and trailer drawings (same shapes as the web dashboard) ---------- */

static QPainterPath flareLeft(qreal y, qreal h)
{
    QPainterPath p;  // opens toward the body, which is on the right at x = 16
    p.moveTo(16, y);
    p.lineTo(11, y);
    p.arcTo(QRectF(8, y, 6, 6), 90, 90);
    p.lineTo(8, y + h - 3);
    p.arcTo(QRectF(8, y + h - 6, 6, 6), 180, 90);
    p.lineTo(16, y + h);
    return p;
}
static QPainterPath flareRight(qreal y, qreal h, qreal bodyRight = 64)
{
    QPainterPath p;
    const qreal x = bodyRight;
    p.moveTo(x, y);
    p.lineTo(x + 5, y);
    p.arcTo(QRectF(x + 2, y, 6, 6), 90, -90);
    p.lineTo(x + 8, y + h - 3);
    p.arcTo(QRectF(x + 2, y + h - 6, 6, 6), 0, -90);
    p.lineTo(x, y + h);
    return p;
}

void drawJeep(QPainter &p, const QRectF &box, const QColor &line, const QColor &wheel)
{
    p.save();
    const qreal s = qMin(box.width() / 80.0, box.height() / 160.0);
    p.translate(box.center().x() - 40 * s, box.center().y() - 80 * s);
    p.scale(s, s);
    QPen pen(line, 1.6);
    pen.setCosmetic(true);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(16, 10, 48, 136), 8, 8);
    for (int x : {30, 35, 40, 45, 50}) p.drawLine(QPointF(x, 10), QPointF(x, 15));
    p.drawEllipse(QPointF(22.5, 18), 3, 3);
    p.drawEllipse(QPointF(57.5, 18), 3, 3);
    p.drawRoundedRect(QRectF(23, 24, 34, 26), 3, 3);
    p.drawPath(flareLeft(26, 30));
    p.drawPath(flareRight(26, 30));
    p.drawPath(flareLeft(106, 30));
    p.drawPath(flareRight(106, 30));
    p.setPen(Qt::NoPen);
    p.setBrush(wheel);
    for (qreal y : {30.0, 110.0}) {
        p.drawRoundedRect(QRectF(9.5, y, 6, 22), 2, 2);
        p.drawRoundedRect(QRectF(64.5, y, 6, 22), 2, 2);
    }
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(20, 50, 40, 8), 1.5, 1.5);
    p.drawRoundedRect(QRectF(10, 58, 5, 4), 1, 1);
    p.drawRoundedRect(QRectF(65, 58, 5, 4), 1, 1);
    p.drawRoundedRect(QRectF(18, 62, 44, 64), 5, 5);
    p.drawRoundedRect(QRectF(21, 74, 17, 16), 4, 4);
    p.drawRoundedRect(QRectF(42, 74, 17, 16), 4, 4);
    p.drawEllipse(QPointF(29.5, 68), 3.5, 3.5);
    p.drawRoundedRect(QRectF(21, 100, 38, 14), 4, 4);
    p.drawLine(QPointF(19, 140), QPointF(22, 140));
    p.drawLine(QPointF(58, 140), QPointF(61, 140));
    p.drawRoundedRect(QRectF(27, 148, 26, 9), 4, 4);
    p.restore();
}

void drawTrailer(QPainter &p, const QRectF &box, const QColor &line, const QColor &wheel)
{
    p.save();
    const qreal s = qMin(box.width() / 80.0, box.height() / 150.0);
    p.translate(box.center().x() - 40 * s, box.center().y() - 75 * s);
    p.scale(s, s);
    QPen pen(line, 1.6);
    pen.setCosmetic(true);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawLine(QPointF(26, 36), QPointF(38, 9));
    p.drawLine(QPointF(54, 36), QPointF(42, 9));
    p.drawEllipse(QPointF(40, 6), 3.5, 3.5);
    p.drawRoundedRect(QRectF(33, 21, 14, 10), 2, 2);
    p.drawRoundedRect(QRectF(17, 36, 46, 108), 6, 6);
    p.drawRoundedRect(QRectF(22, 42, 36, 46), 3, 3);
    p.drawLine(QPointF(22, 56), QPointF(58, 56));
    p.drawRoundedRect(QRectF(22, 94, 36, 42), 3, 3);
    p.drawLine(QPointF(22, 115), QPointF(58, 115));
    p.drawPath(flareLeft(76, 34).translated(1, 0));
    p.drawPath(flareRight(76, 34, 63));
    p.setPen(Qt::NoPen);
    p.setBrush(wheel);
    p.drawRoundedRect(QRectF(10.5, 80, 6, 26), 2.5, 2.5);
    p.drawRoundedRect(QRectF(63.5, 80, 6, 26), 2.5, 2.5);
    p.setPen(pen);
    p.drawLine(QPointF(18, 139), QPointF(22, 139));
    p.drawLine(QPointF(58, 139), QPointF(62, 139));
    p.restore();
}

/* ---------- tire tiles around the drawing ---------- */

void RigWidget::tile(QPainter &p, const QRectF &r, const QString &name, const TireInfo *t)
{
    const Look &look = Look::get();
    const bool low = t && t->low;
    card(p, r, look.bg, low ? look.red : look.line);
    const qreal u = qMax<qreal>(10, qMin(r.height() * 0.2, r.width() * 0.13));
    p.setPen(look.muted);
    p.setFont(scaled(font(), u * 0.85));
    const QFontMetricsF f1(p.font());
    p.drawText(QRectF(r.left(), r.top() + 4, r.width(), f1.height()), Qt::AlignCenter, name);
    p.setPen(low ? look.red : look.text);
    p.setFont(scaled(font(), u * 2.1, true));
    const QFontMetricsF f2(p.font());
    p.drawText(QRectF(r.left(), r.top() + 4 + f1.height(), r.width(), f2.height()), Qt::AlignCenter, t ? t->value : "—");
    p.setPen(look.muted);
    p.setFont(scaled(font(), u * 0.8));
    p.drawText(QRectF(r.left() + 2, r.bottom() - QFontMetricsF(p.font()).height() - 4, r.width() - 4, QFontMetricsF(p.font()).height()), Qt::AlignCenter, t ? t->text : QString());
}

void RigWidget::paintEvent(QPaintEvent *)
{
    const Look &look = Look::get();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    auto find = [this](const QString &pos) -> const TireInfo * {
        for (const TireInfo &t : tires_)
            if (t.pos == pos) return &t;
        return nullptr;
    };
    const qreal w = width(), h = height();
    if (kind_ == Vehicle) {
        const qreal spareH = qMin<qreal>(66, h * 0.22), gap = 8, bodyH = h - spareH - gap, tileH = qMin<qreal>(78, bodyH * 0.34), tileW = qMin<qreal>(w * 0.34, 130);
        const QRectF body(w / 2 - qMin<qreal>(bodyH * 0.25, w * 0.13), 0, qMin<qreal>(bodyH * 0.5, w * 0.26), bodyH);
        drawJeep(p, body, look.muted, look.line);
        const qreal yFront = bodyH * 0.25, yRear = bodyH * 0.75;
        tile(p, QRectF(w / 2 - body.width() / 2 - 6 - tileW, yFront - tileH / 2, tileW, tileH), "Front left", find("front_left"));
        tile(p, QRectF(w / 2 + body.width() / 2 + 6, yFront - tileH / 2, tileW, tileH), "Front right", find("front_right"));
        tile(p, QRectF(w / 2 - body.width() / 2 - 6 - tileW, yRear - tileH / 2, tileW, tileH), "Rear left", find("rear_left"));
        tile(p, QRectF(w / 2 + body.width() / 2 + 6, yRear - tileH / 2, tileW, tileH), "Rear right", find("rear_right"));
        tile(p, QRectF(w / 2 - tileW * 0.6, h - spareH, tileW * 1.2, spareH), "Spare", find("spare"));
    }
    else {
        const qreal tileH = qMin<qreal>(78, h * 0.42), tileW = qMin<qreal>(w * 0.34, 130);
        const QRectF body(w / 2 - qMin<qreal>(h * 0.27, w * 0.13), 0, qMin<qreal>(h * 0.53, w * 0.26), h);
        drawTrailer(p, body, look.muted, look.line);
        const qreal y = h * 0.62;
        tile(p, QRectF(w / 2 - body.width() / 2 - 6 - tileW, y - tileH / 2, tileW, tileH), "Left", find("trailer_left"));
        tile(p, QRectF(w / 2 + body.width() / 2 + 6, y - tileH / 2, tileW, tileH), "Right", find("trailer_right"));
    }
}

}  // namespace tc
