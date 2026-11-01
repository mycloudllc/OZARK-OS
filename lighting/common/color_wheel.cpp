#include <algorithm>
#include <cmath>

#include <QMouseEvent>
#include <QPainter>

#include "color_wheel.hpp"

namespace {
constexpr double kPi = 3.14159265358979323846;
}  // namespace

ColorWheel::ColorWheel(QWidget *parent)
    : QWidget(parent)
{
    this->setAttribute(Qt::WA_OpaquePaintEvent, false);
    this->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

QColor ColorWheel::color() const
{
    return QColor::fromHsvF(this->hue, this->saturation, 1.0);
}

void ColorWheel::setColor(const QColor &color)
{
    // Greys have no hue: keep the marker's angle, move it to the middle
    const double hue = color.hsvHueF();
    if (hue >= 0.0)
        this->hue = hue;
    this->saturation = color.hsvSaturationF();
    this->update();
}

// The largest centred square, minus room for the marker at the edge
QRectF ColorWheel::circle() const
{
    const double margin = 12.0;
    const double size = std::min(this->width(), this->height()) - 2 * margin;
    return QRectF((this->width() - size) / 2.0, (this->height() - size) / 2.0, size, size);
}

// Draw the wheel once per size, pixel by pixel, with a soft 1 px edge
void ColorWheel::resizeEvent(QResizeEvent *)
{
    const QRectF c = this->circle();
    const double r = c.width() / 2.0;
    const QPointF centre = c.center();
    const qreal dpr = this->devicePixelRatioF();

    this->wheel = QImage(this->size() * dpr, QImage::Format_ARGB32_Premultiplied);
    this->wheel.setDevicePixelRatio(dpr);
    this->wheel.fill(Qt::transparent);
    for (int y = 0; y < this->wheel.height(); ++y) {
        auto line = reinterpret_cast<QRgb *>(this->wheel.scanLine(y));
        for (int x = 0; x < this->wheel.width(); ++x) {
            const double dx = (x + 0.5) / dpr - centre.x();
            const double dy = (y + 0.5) / dpr - centre.y();
            const double d = std::sqrt(dx * dx + dy * dy);
            if (d > r + 1.0)
                continue;
            double angle = std::atan2(-dy, dx) / (2 * kPi);
            if (angle < 0)
                angle += 1.0;
            const QColor c = QColor::fromHsvF(angle, std::min(d / r, 1.0), 1.0);
            const double alpha = std::clamp(r + 0.5 - d, 0.0, 1.0);
            line[x] = qPremultiply(qRgba(c.red(), c.green(), c.blue(), int(alpha * 255)));
        }
    }
}

void ColorWheel::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.drawImage(0, 0, this->wheel);

    // Marker: the picked colour in a white ring with a dark outline
    const QRectF c = this->circle();
    const double r = c.width() / 2.0;
    const double angle = this->hue * 2 * kPi;
    const QPointF at(c.center().x() + std::cos(angle) * this->saturation * r,
                     c.center().y() - std::sin(angle) * this->saturation * r);
    const double size = this->drag ? 14.0 : 11.0;
    painter.setPen(QPen(QColor(0, 0, 0, 120), 5));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(at, size, size);
    painter.setPen(QPen(Qt::white, 3));
    painter.setBrush(this->color());
    painter.drawEllipse(at, size, size);
}

void ColorWheel::pick(const QPointF &position)
{
    const QRectF c = this->circle();
    const double dx = position.x() - c.center().x();
    const double dy = position.y() - c.center().y();
    double angle = std::atan2(-dy, dx) / (2 * kPi);
    if (angle < 0)
        angle += 1.0;
    this->hue = angle;
    this->saturation = std::min(std::sqrt(dx * dx + dy * dy) / (c.width() / 2.0), 1.0);
    this->update();
    emit colorChanged(this->color());
}

void ColorWheel::mousePressEvent(QMouseEvent *event)
{
    // Only start on the wheel itself, so the page can still be scrolled around it
    const QRectF c = this->circle();
    const QPointF d = event->pos() - c.center();
    if (std::hypot(d.x(), d.y()) > c.width() / 2.0 + 12.0) {
        event->ignore();
        return;
    }
    this->drag = true;
    this->pick(event->pos());
}

void ColorWheel::mouseMoveEvent(QMouseEvent *event)
{
    if (this->drag)
        this->pick(event->pos());
}

void ColorWheel::mouseReleaseEvent(QMouseEvent *event)
{
    if (!this->drag)
        return;
    this->pick(event->pos());
    this->drag = false;
    this->update();
    emit released(this->color());
}
