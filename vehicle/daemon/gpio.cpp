#include "gpio.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <QDebug>
#include <QDir>
#include <QRegularExpression>

namespace {
constexpr int kDebounceMs = 30;

// Rockchip names pins by bank and group: line 13 of gpio3 is GPIO3_B5. The
// Orange Pi's pin tables use those names.
QString rockchip_name(const QString &chip_label, int line)
{
    const QRegularExpression bank("^gpio([0-9])$");
    const auto m = bank.match(chip_label);
    if (!m.hasMatch() || line > 31)
        return QString();
    return QString("GPIO%1_%2%3").arg(m.captured(1)).arg(QChar('A' + line / 8)).arg(line % 8);
}
}  // namespace

GpioInput::GpioInput(QObject *parent)
    : QObject(parent)
{
    settle.setSingleShot(true);
    connect(&settle, &QTimer::timeout, this, [this] {
        const bool before = value_;
        if (read_value() && value_ != before)
            emit changed(value_);
    });
}

GpioInput::~GpioInput()
{
    close();
}

void GpioInput::close()
{
    settle.stop();
    delete notifier;
    notifier = nullptr;
    if (line_fd >= 0)
        ::close(line_fd);
    line_fd = -1;
}

bool GpioInput::open(const QString &chip, int line, bool active_low, Bias bias, QString *error)
{
    close();
    const QString path = "/dev/" + chip;
    const int chip_fd = ::open(path.toLocal8Bit().constData(), O_RDWR | O_CLOEXEC);
    if (chip_fd < 0) {
        if (error)
            *error = QString("Can't open %1: %2").arg(path, QString::fromLocal8Bit(strerror(errno)));
        return false;
    }

    gpio_v2_line_request req {};
    req.offsets[0] = quint32(line);
    req.num_lines = 1;
    strncpy(req.consumer, "ozark-vehicled", sizeof(req.consumer) - 1);
    req.config.flags = GPIO_V2_LINE_FLAG_INPUT | GPIO_V2_LINE_FLAG_EDGE_RISING | GPIO_V2_LINE_FLAG_EDGE_FALLING;
    if (active_low)
        req.config.flags |= GPIO_V2_LINE_FLAG_ACTIVE_LOW;
    if (bias == Bias::PullUp)
        req.config.flags |= GPIO_V2_LINE_FLAG_BIAS_PULL_UP;
    else if (bias == Bias::PullDown)
        req.config.flags |= GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN;
    else
        req.config.flags |= GPIO_V2_LINE_FLAG_BIAS_DISABLED;
    // The kernel debounces in software if the controller can't
    req.config.num_attrs = 1;
    req.config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_DEBOUNCE;
    req.config.attrs[0].attr.debounce_period_us = kDebounceMs * 1000;
    req.config.attrs[0].mask = 1;

    int rc = ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &req);
    if (rc < 0 && errno == EINVAL) {
        // No debounce, or no bias setting on this controller: plain input,
        // debounced here instead
        req.config.num_attrs = 0;
        req.config.flags &= ~quint64(GPIO_V2_LINE_FLAG_BIAS_DISABLED);
        rc = ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &req);
    }
    const int saved = errno;
    ::close(chip_fd);
    if (rc < 0) {
        if (error)
            *error = QString("Can't use %1 line %2: %3")
                         .arg(chip)
                         .arg(line)
                         .arg(saved == EBUSY ? QString("something else is using it")
                                             : QString::fromLocal8Bit(strerror(saved)));
        return false;
    }
    line_fd = req.fd;
    read_value();
    notifier = new QSocketNotifier(line_fd, QSocketNotifier::Read, this);
    connect(notifier, &QSocketNotifier::activated, this, [this] { this->read_events(); });
    return true;
}

bool GpioInput::read_value()
{
    if (line_fd < 0)
        return false;
    gpio_v2_line_values values {};
    values.mask = 1;
    if (ioctl(line_fd, GPIO_V2_LINE_GET_VALUES_IOCTL, &values) < 0)
        return false;
    value_ = values.bits & 1;
    return true;
}

void GpioInput::read_events()
{
    gpio_v2_line_event events[16];
    const ssize_t n = ::read(line_fd, events, sizeof(events));
    if (n <= 0)
        return;
    // Read the level once it has settled, rather than trusting edge order
    settle.start(kDebounceMs);
}

QVariantList GpioInput::lines()
{
    QVariantList out;
    const QStringList chips =
        QDir("/dev").entryList({"gpiochip*"}, QDir::System | QDir::Files | QDir::Readable, QDir::Name);
    for (const QString &chip : chips) {
        const int fd = ::open(("/dev/" + chip).toLocal8Bit().constData(), O_RDONLY | O_CLOEXEC);
        if (fd < 0)
            continue;
        gpiochip_info info {};
        if (ioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &info) == 0) {
            const QString label = QString::fromLatin1(info.label);
            for (quint32 i = 0; i < info.lines; i++) {
                gpio_v2_line_info li {};
                li.offset = i;
                if (ioctl(fd, GPIO_V2_GET_LINEINFO_IOCTL, &li) < 0)
                    continue;
                const QString consumer = QString::fromLatin1(li.consumer);
                const bool used = (li.flags & GPIO_V2_LINE_FLAG_USED) && consumer != "ozark-vehicled";
                QString name = QString::fromLatin1(li.name);
                if (name.isEmpty())
                    name = rockchip_name(label, int(i));
                const QString where = QString("%1 line %2").arg(chip).arg(i);
                out << QVariantMap {
                    {"id", QString("%1:%2").arg(chip).arg(i)},
                    {"label", name.isEmpty() ? where : QString("%1 (%2)").arg(name, where)},
                    {"used", used},
                    {"consumer", consumer},
                };
            }
        }
        ::close(fd);
    }
    return out;
}
