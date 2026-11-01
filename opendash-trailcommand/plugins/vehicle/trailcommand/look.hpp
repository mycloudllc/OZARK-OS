#pragma once
// Colours for custom-painted widgets. Dash themes standard widgets with its own style sheet
// (assets/stylesheets), which does not reach QPainter code, so the same palette is mirrored here.
#include <QApplication>
#include <QColor>
#include <QElapsedTimer>
#include <QPalette>
#include <QSettings>

namespace tc {

struct Look {
    bool dark = true;
    QColor bg, panel, line, text, muted, accent, onAccent, green, yellow, red, onFill;

    static const Look &get()
    {
        static Look look;
        static QElapsedTimer age;
        static bool ready = false;
        if (!ready || age.elapsed() > 500) {
            look = compute();
            age.restart();
            ready = true;
        }
        return look;
    }

    static QColor band(const QString &name)
    {
        const Look &l = get();
        return name == "red" ? l.red : name == "yellow" ? l.yellow : l.green;
    }

   private:
    static Look compute()
    {
        Look l;
        QSettings s;  // same organisation and application name as Dash's own session settings
        l.dark = s.value("Theme/mode", 0).toInt() == 1;
        if (l.dark) {
            l.bg = QColor(18, 18, 18);
            l.panel = QColor(30, 30, 30);
            l.line = QColor(66, 66, 66);
            l.text = QColor(255, 255, 255);
            l.muted = QColor(179, 179, 179);
            l.green = QColor("#4CC77A");
            l.yellow = QColor("#F5C542");
            l.red = QColor("#EE5A47");
            l.onFill = QColor(15, 21, 18);
        }
        else {
            l.bg = QColor(250, 250, 250);
            l.panel = QColor(255, 255, 255);
            l.line = QColor(206, 206, 206);
            l.text = QColor(0, 0, 0);
            l.muted = QColor(97, 97, 97);
            l.green = QColor("#1F8F4E");
            l.yellow = QColor("#C58A00");
            l.red = QColor("#C0392B");
            l.onFill = QColor(255, 255, 255);
        }
        l.accent = qApp->palette().color(QPalette::Base);  // Dash stores its accent colour here
        l.onAccent = l.accent.lightness() > 140 ? QColor(0, 0, 0) : QColor(255, 255, 255);
        return l;
    }
};

}  // namespace tc
