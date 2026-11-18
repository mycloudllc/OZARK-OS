// A digital input on a GPIO line (the illumination wire: on with the
// headlights), through the GPIO character device (/dev/gpiochipN, uAPI v2).
#pragma once

#include <QObject>
#include <QSocketNotifier>
#include <QTimer>
#include <QVariantList>

class GpioInput : public QObject {
    Q_OBJECT

   public:
    enum class Bias { None, PullUp, PullDown };

    explicit GpioInput(QObject *parent = nullptr);
    ~GpioInput() override;

    // chip: "gpiochip3"; active_low: on when the pin reads low (an
    // optocoupler pulling it to ground). False and a message if it can't.
    bool open(const QString &chip, int line, bool active_low, Bias bias, QString *error = nullptr);
    void close();
    bool is_open() const { return line_fd >= 0; }
    bool value() const { return value_; }

    // Every input line there is: [{id: "gpiochip3:13", label: "GPIO3_B5 (gpiochip3 line 13)",
    // used: bool, consumer}]
    static QVariantList lines();

   signals:
    void changed(bool on);

   private:
    void read_events();
    bool read_value();

    int line_fd = -1;
    QSocketNotifier *notifier = nullptr;
    QTimer settle;  // software debounce when the kernel can't
    bool value_ = false;
};
