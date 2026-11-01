#pragma once
// Small building blocks that paint themselves with the Look colours, so they follow Dash's light and dark modes.
#include <QAbstractButton>
#include <QFontInfo>
#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>

#include "gfx.hpp"

namespace tc {

inline qreal basePx() { return QFontInfo(qApp->font()).pixelSize() > 0 ? QFontInfo(qApp->font()).pixelSize() : 16; }

class Text : public QWidget {
   public:
    enum Role { Normal, Muted, Good, Warn, Bad };
    Text(const QString &text, qreal scale = 1.0, bool bold = false, Role role = Normal, Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter, QWidget *parent = nullptr)
        : QWidget(parent), text_(text), scale_(scale), bold_(bold), role_(role), align_(align)
    {
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        setAccessibleName(text);
    }
    void setText(const QString &t)
    {
        if (t == text_) return;
        text_ = t;
        setAccessibleName(t);
        updateGeometry();
        update();
    }
    void setRole(Role r) { role_ = r; update(); }
    QString text() const { return text_; }
    QSize sizeHint() const override
    {
        const QFontMetrics fm(scaled(qApp->font(), basePx() * scale_, bold_));
        return QSize(fm.horizontalAdvance(text_) + 2, fm.height() + 2);
    }
    QSize minimumSizeHint() const override { return QSize(10, sizeHint().height()); }

   protected:
    void paintEvent(QPaintEvent *) override
    {
        const Look &l = Look::get();
        QPainter p(this);
        p.setRenderHint(QPainter::TextAntialiasing);
        p.setFont(scaled(qApp->font(), basePx() * scale_, bold_));
        p.setPen(role_ == Muted ? l.muted : role_ == Good ? l.green : role_ == Warn ? l.yellow : role_ == Bad ? l.red : l.text);
        p.drawText(rect(), align_, p.fontMetrics().elidedText(text_, Qt::ElideRight, width()));
    }

   private:
    QString text_;
    qreal scale_;
    bool bold_;
    Role role_;
    Qt::Alignment align_;
};

// A number over its caption
class Stat : public QWidget {
   public:
    explicit Stat(const QString &caption, qreal valueScale = 1.5, QWidget *parent = nullptr) : QWidget(parent)
    {
        auto *l = new QVBoxLayout(this);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(0);
        value_ = new Text("—", valueScale, true);
        caption_ = new Text(caption, 0.8, false, Text::Muted);
        l->addWidget(value_);
        l->addWidget(caption_);
    }
    void setValue(const QString &v, Text::Role r = Text::Normal)
    {
        value_->setText(v);
        value_->setRole(r);
    }
    void setCaption(const QString &c) { caption_->setText(c); }

   private:
    Text *value_, *caption_;
};

// Thin progress bar
class MeterBar : public QWidget {
   public:
    explicit MeterBar(QWidget *parent = nullptr) : QWidget(parent) { setFixedHeight(8); setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed); }
    void setValue(double percent, const QString &band = QString())
    {
        v_ = qBound(0.0, percent, 100.0);
        band_ = band;
        update();
    }

   protected:
    void paintEvent(QPaintEvent *) override
    {
        const Look &l = Look::get();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(l.line);
        p.drawRoundedRect(rect(), 4, 4);
        if (v_ > 0) {
            p.setBrush(band_.isEmpty() ? (l.accent.lightness() > 20 && l.accent.lightness() < 240 ? l.accent : l.green) : Look::band(band_));
            p.drawRoundedRect(QRectF(0, 0, qMax(8.0, width() * v_ / 100.0), height()), 4, 4);
        }
    }

   private:
    double v_ = 0;
    QString band_;
};

// Page switcher entry: text with an accent underline when selected (like Dash's own tabs)
class NavButton : public QAbstractButton {
   public:
    explicit NavButton(const QString &text, QWidget *parent = nullptr) : QAbstractButton(parent)
    {
        setText(text);
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    }
    QSize sizeHint() const override
    {
        const QFontMetrics fm(scaled(qApp->font(), basePx() * 0.95, true));
        return QSize(fm.horizontalAdvance(text()) + 22, fm.height() + 14);
    }

   protected:
    void paintEvent(QPaintEvent *) override
    {
        const Look &l = Look::get();
        QPainter p(this);
        p.setRenderHint(QPainter::TextAntialiasing);
        p.setFont(scaled(qApp->font(), basePx() * 0.95, isChecked()));
        p.setPen(isChecked() ? l.text : l.muted);
        p.drawText(rect().adjusted(0, 0, 0, -3), Qt::AlignCenter, text());
        if (isChecked()) {
            p.setPen(Qt::NoPen);
            p.setBrush(l.accent.lightness() > 20 && l.accent.lightness() < 240 ? l.accent : l.text);
            p.drawRect(QRect(8, height() - 3, width() - 16, 3));
        }
    }
};

// The settings (sliders) button in the header
class GearButton : public QAbstractButton {
   public:
    explicit GearButton(QWidget *parent = nullptr) : QAbstractButton(parent)
    {
        setCursor(Qt::PointingHandCursor);
        setFixedSize(qRound(basePx() * 2.4), qRound(basePx() * 2.4));
        setToolTip("Hub connection");
    }

   protected:
    void paintEvent(QPaintEvent *) override
    {
        const Look &l = Look::get();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (underMouse() || isDown()) {
            p.setPen(Qt::NoPen);
            p.setBrush(l.line);
            p.drawRoundedRect(rect(), 8, 8);
        }
        QPen pen(l.text, qMax(1.6, width() / 16.0), Qt::SolidLine, Qt::RoundCap);
        p.setPen(pen);
        p.setBrush(l.panel);
        const qreal m = width() * 0.24, w = width() - 2 * m;
        const qreal ys[3] = {height() * 0.3, height() * 0.5, height() * 0.7}, xs[3] = {0.65, 0.3, 0.7};
        for (int i = 0; i < 3; i++) {
            p.drawLine(QPointF(m, ys[i]), QPointF(m + w, ys[i]));
            p.drawEllipse(QPointF(m + w * xs[i], ys[i]), w * 0.11, w * 0.11);
        }
    }
};

}  // namespace tc
