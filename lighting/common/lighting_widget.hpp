#pragma once

#include <functional>

#include <QtDBus>
#include <QtWidgets>

class Arbiter;
class ColorWheel;
class Selector;

// A lighting page for some of the lighting controller's zones ('F' interior
// front, 'R' interior rear, 'E' exterior): pattern, colours, speed and
// brightness. Talks to ozark-lightd (org.ozark.Lighting1 on the system bus),
// which finds the controller itself; its port and strip setup are in Dash's
// Settings > Lighting. Used by the Interior Lighting (F, R) and Exterior
// Lighting (E) plugins.
class LightingWidget : public QWidget {
    Q_OBJECT

   public:
    LightingWidget(Arbiter &arbiter, const QString &title, const QString &zones, QWidget *parent = nullptr);

   private slots:
    void on_look_changed(const QDBusMessage &message);

   private:
    void build_ui();
    QLayout *row(const QString &label, QWidget *control);

    // Calls ozark-lightd without blocking the UI; done gets the reply if it succeeded
    void call(const QString &path, const QString &interface, const QString &method, const QVariantList &args = {},
              std::function<void(const QDBusMessage &)> done = nullptr);

    QString zone_path(QChar zone) const;
    QStringList targets() const;
    void set_target(int target);
    void fetch();
    void load_controls();
    void set_look(const QVariantMap &changes);
    void select_color(int which);
    void show_swatch(int which, const QColor &color);

    Arbiter &arbiter;
    const QString zones;              // e.g. "FR" or "E"
    QMap<QString, QVariantMap> looks;  // zone -> look, as ozark-lightd last reported it
    int target = -1;                  // index into zones, or -1 for all of them
    bool loading = false;

    Selector *pattern_selector;
    ColorWheel *wheel;
    QPushButton *swatches[2];  // "Color", "Color 2": which one the wheel edits
    int editing = 0;
    QTimer *send_timer;        // limits wheel drags to ~20 updates a second
    QSlider *speed_slider;
    QSlider *brightness_slider;
};
