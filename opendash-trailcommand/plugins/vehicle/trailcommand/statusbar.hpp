#pragma once
// Status icons in Dash's bottom control bar (hub link, Wi-Fi, cell, ...).
//
// Dash has no plugin hook for that bar, but it is an ordinary widget named "ControlBar" whose layout reads
// [quick views] [stretch] [power] [close]. StatusBar keeps a list of icons (the model) and, once the bar exists,
// slots them in between the stretch and the power button. If Dash ever rebuilds its window the icons are
// recreated from the model, so callers only ever say what an icon should show.
//
//   StatusBar *bar = new StatusBar(this);
//   bar->add("wifi", StatusIcon::Glyph::Wifi, "Wi-Fi");      // once, in the order they should appear
//   bar->set("wifi", 3, StatusIcon::Tone::Good, "", "Wi-Fi: HomeNet, -58 dBm");
//   bar->hide("wifi");
#include <QList>
#include <QPointer>
#include <QTimer>
#include <QWidget>

#include "look.hpp"

namespace tc {

class StatusIcon : public QWidget {
    Q_OBJECT

   public:
    enum class Glyph { Wifi, Cell, Hub, Clock };  // Clock: label is "time\ndate"
    enum class Tone { Dim, Good, Warn, Bad };

    StatusIcon(Glyph glyph, const QString &name, QWidget *parent = nullptr);
    // level 0..4 is the signal strength (ignored by Hub); label is the short text next to the icon
    void set(int level, Tone tone, const QString &label, const QString &tip);
    QSize sizeHint() const override;

   signals:
    void clicked();

   protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    bool event(QEvent *e) override;

   private:
    QColor color() const;
    QFont labelFont() const;
    void clockFonts(qreal height, QFont &time, QFont &date) const;

    Glyph glyph_;
    QString name_, label_;
    int level_ = 0;
    Tone tone_ = Tone::Dim;
};

class StatusBar : public QObject {
    Q_OBJECT

   public:
    explicit StatusBar(QObject *parent = nullptr);
    ~StatusBar() override;

    void add(const QString &id, StatusIcon::Glyph glyph, const QString &name);  // hidden until set()
    void set(const QString &id, int level, StatusIcon::Tone tone, const QString &label, const QString &tip);
    void hide(const QString &id);
    bool attached() const;  // true once the icons are in Dash's control bar

   signals:
    void clicked(const QString &id);

   private:
    struct Item {
        QString id, name, label, tip;
        StatusIcon::Glyph glyph;
        int level = 0;
        StatusIcon::Tone tone = StatusIcon::Tone::Dim;
        bool visible = false;
        QPointer<StatusIcon> icon;
    };

    Item *find(const QString &id);
    void push(Item &it);
    void attach();
    QWidget *controlBar() const;

    QList<Item> items_;
    QPointer<QWidget> box_;
    QTimer watch_;
};

}  // namespace tc
