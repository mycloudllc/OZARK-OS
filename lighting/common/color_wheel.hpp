#pragma once

#include <QColor>
#include <QImage>
#include <QWidget>

// Hue/saturation colour wheel, as in most LED apps: hue around the circle,
// white in the middle to full colour at the edge. Brightness is left to a
// separate control, so colours are always at full value.
class ColorWheel : public QWidget {
    Q_OBJECT

   public:
    explicit ColorWheel(QWidget *parent = nullptr);

    QColor color() const;
    void setColor(const QColor &color);  // no signal
    bool dragging() const { return this->drag; }

   signals:
    // While the wheel is being touched or dragged, and once more on release
    void colorChanged(const QColor &color);
    void released(const QColor &color);

   protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    QSize sizeHint() const override { return QSize(200, 200); }

   private:
    QRectF circle() const;
    void pick(const QPointF &position);

    QImage wheel;
    double hue = 0.0;         // 0..1, 0 = red
    double saturation = 1.0;  // 0..1
    bool drag = false;
};
