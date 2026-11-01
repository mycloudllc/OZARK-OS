#include "statusbar.hpp"

#include <QApplication>
#include <QBoxLayout>
#include <QEvent>
#include <QFontInfo>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QtMath>

#include "gfx.hpp"

namespace tc {

/* ---------- one icon ---------- */

StatusIcon::StatusIcon(Glyph glyph, const QString &name, QWidget *parent) : QWidget(parent), glyph_(glyph), name_(name)
{
    // the clock takes the bar's full height and sizes its text to fit inside it
    setSizePolicy(QSizePolicy::Fixed, glyph_ == Glyph::Clock ? QSizePolicy::Expanding : QSizePolicy::Fixed);
    if (glyph_ == Glyph::Hub) setCursor(Qt::PointingHandCursor);
    setVisible(false);
}

void StatusIcon::set(int level, Tone tone, const QString &label, const QString &tip)
{
    level_ = qBound(0, level, 4);
    tone_ = tone;
    label_ = label;
    setToolTip(tip);
    setAccessibleName(tip.isEmpty() ? name_ : tip);
    updateGeometry();
    update();
}

QFont StatusIcon::labelFont() const
{
    // QFontInfo: Dash's point-sized fonts have no pixelSize(), so this stays in
    // proportion with the bar's text at any DPI
    return scaled(font(), qMax<qreal>(11, QFontInfo(font()).pixelSize() * 0.75), true);
}

// Time over date, as large as fits in the given height with a margin above and
// below, so the text never runs into the control bar's edges
void StatusIcon::clockFonts(qreal height, QFont &time, QFont &date) const
{
    const qreal room = height - 2 * qMax<qreal>(4, height * 0.14);
    for (qreal px = 17 * height / 42.0; ; px -= 0.5) {
        time = scaled(font(), px, true);
        date = scaled(font(), px * 0.75, false);
        if (px <= 9 || QFontMetricsF(time).height() + 1 + QFontMetricsF(date).height() <= room) return;
    }
}

void StatusIcon::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    if (glyph_ == Glyph::Clock && e->size().height() != e->oldSize().height()) updateGeometry();  // width follows the text size
}

QSize StatusIcon::sizeHint() const
{
    const int h = qMax(28, QFontMetrics(font()).height() + 8);
    const qreal s = h / 28.0;
    if (glyph_ == Glyph::Clock) {
        // fixed to the widest likely text, so the icons beside it do not shift when the minute changes
        QFont time, date;
        clockFonts(qMax(h, height()), time, date);
        const QFontMetrics big(time), small(date);
        const QStringList lines = label_.split('\n');
        int w1 = qMax(big.horizontalAdvance("88:88 AM"), big.horizontalAdvance(lines.value(0)));
        int w2 = qMax(small.horizontalAdvance("Wed, May 28"), small.horizontalAdvance(lines.value(1)));
        return QSize(qMax(w1, w2) + 4, h);
    }
    int w = qCeil((glyph_ == Glyph::Cell ? 28 : 26) * s);
    if (!label_.isEmpty()) w += 6 + QFontMetrics(labelFont()).horizontalAdvance(label_);
    return QSize(w, h);
}

QColor StatusIcon::color() const
{
    const Look &look = Look::get();
    switch (tone_) {
        case Tone::Good: return look.green;
        case Tone::Warn: return look.yellow;
        case Tone::Bad: return look.red;
        default: return look.muted;
    }
}

void StatusIcon::paintEvent(QPaintEvent *)
{
    const Look &look = Look::get();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal s = height() / 28.0, cy = height() / 2.0;
    const QColor on = color();
    QColor off = look.muted;
    off.setAlpha(90);
    qreal x = 0;

    if (glyph_ == Glyph::Clock) {
        const QStringList lines = label_.split('\n');
        QFont big, small;
        clockFonts(height(), big, small);
        const QFontMetrics fb(big), fs(small);
        const qreal gap = 1, total = fb.height() + gap + fs.height(), top = (height() - total) / 2.0;
        p.setFont(big);
        p.setPen(look.text);
        p.drawText(QPointF(0, top + fb.ascent()), lines.value(0));
        p.setFont(small);
        p.setPen(look.muted);
        p.drawText(QPointF(0, top + fb.height() + gap + fs.ascent()), lines.value(1));
        return;
    }
    if (glyph_ == Glyph::Wifi) {
        // a dot plus three arcs; strength lights them from the bottom up
        const qreal cx = 13 * s, base = cy + 8 * s;
        const int lit = level_ == 0 ? 0 : qMin(3, level_);
        p.setPen(Qt::NoPen);
        p.setBrush(level_ > 0 ? on : off);
        p.drawEllipse(QPointF(cx, base), 2.2 * s, 2.2 * s);
        p.setBrush(Qt::NoBrush);
        for (int i = 0; i < 3; i++) {
            const qreal r = (6.5 + 4.5 * i) * s;
            p.setPen(QPen(i < lit ? on : off, 2.2 * s, Qt::SolidLine, Qt::RoundCap));
            p.drawArc(QRectF(cx - r, base - r, 2 * r, 2 * r), 48 * 16, 84 * 16);
        }
        if (level_ == 0) {  // not connected: strike through
            p.setPen(QPen(look.muted, 2 * s, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(cx - 10 * s, cy - 9 * s), QPointF(cx + 10 * s, cy + 11 * s));
        }
        x = 26 * s;
    }
    else if (glyph_ == Glyph::Cell) {
        const qreal bw = 4 * s, gap = 2 * s, maxH = 17 * s;
        p.setPen(Qt::NoPen);
        for (int i = 0; i < 4; i++) {
            const qreal h = maxH * (0.3 + 0.7 * i / 3.0);
            p.setBrush(i < level_ ? on : off);
            p.drawRoundedRect(QRectF(2 + i * (bw + gap), cy + maxH / 2 - h, bw, h), 1, 1);
        }
        x = 4 * (bw + gap) + 4 * s;
    }
    else {
        // two interlocked links: the hub connection
        const qreal w = 13 * s, h = 8 * s, r = h / 2;
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(on, 2 * s, Qt::SolidLine, Qt::RoundCap));
        p.drawRoundedRect(QRectF(2, cy - h / 2, w, h), r, r);
        p.drawRoundedRect(QRectF(2 + w - 5 * s, cy - h / 2, w, h), r, r);
        if (tone_ == Tone::Bad) {
            p.setPen(QPen(look.red, 2 * s, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(4 * s, cy + 10 * s), QPointF(22 * s, cy - 10 * s));
        }
        x = 26 * s;
    }

    if (!label_.isEmpty()) {
        const QFont f = labelFont();
        const QFontMetrics fm(f);
        p.setFont(f);
        p.setPen(look.muted);
        p.drawText(QPointF(x + 4, cy + (fm.ascent() - fm.descent()) / 2.0), label_);
    }
}

void StatusIcon::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && rect().contains(e->pos())) emit clicked();
    QWidget::mouseReleaseEvent(e);
}

bool StatusIcon::event(QEvent *e)
{
    if (e->type() == QEvent::ApplicationPaletteChange || e->type() == QEvent::StyleChange) update();
    return QWidget::event(e);
}

/* ---------- the model, and putting it into Dash ---------- */

StatusBar::StatusBar(QObject *parent) : QObject(parent)
{
    watch_.setInterval(2000);  // the bar may not exist yet at plugin start, and Dash can rebuild it later
    connect(&watch_, &QTimer::timeout, this, [this] { attach(); });
    watch_.start();
    QTimer::singleShot(0, this, [this] { attach(); });
}

StatusBar::~StatusBar()
{
    delete box_;  // removes the icons from Dash's bar
}

StatusBar::Item *StatusBar::find(const QString &id)
{
    for (Item &it : items_)
        if (it.id == id) return &it;
    return nullptr;
}

void StatusBar::add(const QString &id, StatusIcon::Glyph glyph, const QString &name)
{
    if (find(id)) return;
    Item it;
    it.id = id;
    it.name = name;
    it.glyph = glyph;
    items_.append(it);
    if (box_) {  // bar already there: rebuild so the order stays as added
        delete box_;
        for (Item &i : items_) i.icon = nullptr;
    }
    attach();
}

void StatusBar::set(const QString &id, int level, StatusIcon::Tone tone, const QString &label, const QString &tip)
{
    Item *it = find(id);
    if (!it) return;
    it->level = level;
    it->tone = tone;
    it->label = label;
    it->tip = tip;
    it->visible = true;
    push(*it);
}

void StatusBar::hide(const QString &id)
{
    Item *it = find(id);
    if (!it) return;
    it->visible = false;
    push(*it);
}

void StatusBar::push(Item &it)
{
    if (!it.icon) return;
    it.icon->set(it.level, it.tone, it.label, it.tip);
    it.icon->setVisible(it.visible);
    if (box_) box_->updateGeometry();
}

bool StatusBar::attached() const
{
    return box_ && box_->parentWidget() && box_->parentWidget()->objectName() == "ControlBar";
}

QWidget *StatusBar::controlBar() const
{
    for (QWidget *top : QApplication::topLevelWidgets()) {
        if (top->objectName() == "ControlBar") return top;
        if (QWidget *w = top->findChild<QWidget *>("ControlBar")) return w;
    }
    return nullptr;
}

void StatusBar::attach()
{
    QWidget *bar = controlBar();
    if (!bar || (box_ && box_->parentWidget() == bar)) return;
    auto *lay = qobject_cast<QBoxLayout *>(bar->layout());
    if (!lay) return;

    if (!box_) {
        box_ = new QWidget;
        box_->setObjectName("TrailCommandStatus");
        box_->setStyleSheet("background: transparent;");
        auto *bl = new QHBoxLayout(box_);
        bl->setContentsMargins(8, 0, 8, 0);
        bl->setSpacing(12);
        for (Item &it : items_) {
            it.icon = new StatusIcon(it.glyph, it.name, box_);
            const QString id = it.id;
            connect(it.icon, &StatusIcon::clicked, this, [this, id] { emit clicked(id); });
            // the clock fills the bar's height (it centres its own text); the rest centre in it
            if (it.glyph == StatusIcon::Glyph::Clock)
                bl->addWidget(it.icon);
            else
                bl->addWidget(it.icon, 0, Qt::AlignVCenter);
            push(it);
        }
    }
    else if (box_->parentWidget() && box_->parentWidget()->layout()) {
        box_->parentWidget()->layout()->removeWidget(box_);
    }

    // Dash's bar reads [quick views][stretch][power][close]: sit right after the stretch
    int at = lay->count();
    for (int i = 0; i < lay->count(); i++) {
        if (lay->itemAt(i)->spacerItem()) {
            at = i + 1;
            break;
        }
    }
    lay->insertWidget(at, box_);
    box_->show();
}

}  // namespace tc
