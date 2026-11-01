#include "app/arbiter.hpp"
#include "app/widgets/selector.hpp"

#include "color_wheel.hpp"
#include "lighting_widget.hpp"

namespace {
// ozark-lightd on the system bus
const char *kService = "org.ozark.Lighting1";
const char *kPath = "/org/ozark/Lighting1";
const char *kZoneInterface = "org.ozark.Lighting1.Zone";

// Must match the firmware's pattern ids (../PROTOCOL.md)
const QList<QString> kPatterns = {"Off",     "Solid",    "Rainbow",  "Chase",    "Breathe", "Gradient",
                                  "Twinkle", "Comet",    "VU Meter", "Spectrum", "Beat Pulse"};

// Look keys of the two colours, in swatch order
const char *kColorKeys[2] = {"color", "color2"};

QString zone_name(QChar zone)
{
    switch (zone.toLatin1()) {
        case 'F': return "Front";
        case 'R': return "Rear";
        default: return "Exterior";
    }
}

// Nested maps arrive from D-Bus as QDBusArgument, not QVariantMap
QVariantMap to_map(const QVariant &value)
{
    if (value.userType() == qMetaTypeId<QDBusArgument>())
        return qdbus_cast<QVariantMap>(value.value<QDBusArgument>());
    return value.toMap();
}
}  // namespace

LightingWidget::LightingWidget(Arbiter &arbiter, const QString &title, const QString &zones, QWidget *parent)
    : QWidget(parent)
    , arbiter(arbiter)
    , zones(zones)
{
    this->setObjectName(title);  // launcher uses this as the tab title
    this->build_ui();

    QDBusConnection bus = QDBusConnection::systemBus();
    bus.connect(kService, QString(), kZoneInterface, "LookChanged", this, SLOT(on_look_changed(QDBusMessage)));

    // Catch up whenever ozark-lightd (re)starts
    auto watcher = new QDBusServiceWatcher(kService, bus, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceRegistered, this, [this] { this->fetch(); });
    this->fetch();
}

void LightingWidget::call(const QString &path, const QString &interface, const QString &method,
                          const QVariantList &args, std::function<void(const QDBusMessage &)> done)
{
    QDBusMessage message = QDBusMessage::createMethodCall(kService, path, interface, method);
    message.setArguments(args);
    auto watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [done](QDBusPendingCallWatcher *watcher) {
        const QDBusMessage reply = watcher->reply();
        if (reply.type() == QDBusMessage::ReplyMessage && done)
            done(reply);
        watcher->deleteLater();
    });
}

QString LightingWidget::zone_path(QChar zone) const
{
    return QString("%1/%2").arg(kPath).arg(zone);
}

// Ask ozark-lightd for the current looks
void LightingWidget::fetch()
{
    for (const QChar &zone : this->zones) {
        this->call(this->zone_path(zone), kZoneInterface, "GetLook", {}, [this, zone](const QDBusMessage &reply) {
            this->looks[zone] = to_map(reply.arguments().value(0));
            this->load_controls();
        });
    }
}

// ---------------------------------------------------------------- ui

QLayout *LightingWidget::row(const QString &label, QWidget *control)
{
    auto layout = new QHBoxLayout();
    layout->addWidget(new QLabel(label), 1);
    if (qobject_cast<QSlider *>(control) || qobject_cast<QSpinBox *>(control))
        layout->addWidget(control, 1);
    else
        layout->addWidget(control, 1, Qt::AlignHCenter);
    return layout;
}

void LightingWidget::build_ui()
{
    const QFont font14 = this->arbiter.forge().font(14);

    auto outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto scroll = new QScrollArea(this);
    Session::Forge::to_touch_scroller(scroll);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll);

    auto content = new QWidget(scroll);
    auto layout = new QVBoxLayout(content);
    scroll->setWidget(content);

    // zone buttons (one per zone, plus "Both"), only with more than one zone
    if (this->zones.size() > 1) {
        auto buttons = new QHBoxLayout();
        auto group = new QButtonGroup(this);
        for (int i = 0; i < this->zones.size(); ++i) {
            auto button = new QPushButton(zone_name(this->zones[i]), content);
            button->setCheckable(true);
            button->setFont(font14);
            group->addButton(button, i);
            buttons->addWidget(button);
        }
        auto all = new QPushButton(this->zones.size() == 2 ? "Both" : "All", content);
        all->setCheckable(true);
        all->setFont(font14);
        all->setChecked(true);
        group->addButton(all, -1);
        buttons->addWidget(all);
        connect(group, QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked), this,
                [this, group](QAbstractButton *button) { this->set_target(group->id(button)); });
        layout->addLayout(buttons);
        layout->addWidget(Session::Forge::br());
    }

    // pattern
    this->pattern_selector = new Selector(kPatterns, kPatterns.value(1), font14, this->arbiter, content);
    connect(this->pattern_selector, &Selector::idx_changed, this, [this](int idx) {
        if (!this->loading)
            this->set_look({{"pattern", idx}});
    });
    layout->addLayout(this->row("Pattern", this->pattern_selector));

    // colours: a wheel for whichever of the two swatches is selected
    const double scale = this->arbiter.layout().scale;
    auto colors = new QHBoxLayout();
    this->wheel = new ColorWheel(content);
    this->wheel->setFixedSize(int(220 * scale), int(220 * scale));
    colors->addWidget(this->wheel);

    auto swatch_column = new QVBoxLayout();
    swatch_column->addStretch();
    auto swatch_group = new QButtonGroup(this);
    const char *labels[2] = {"Color", "Color 2"};
    for (int i = 0; i < 2; ++i) {
        this->swatches[i] = new QPushButton(labels[i], content);
        this->swatches[i]->setCheckable(true);
        this->swatches[i]->setFont(font14);
        this->swatches[i]->setMinimumSize(int(150 * scale), int(56 * scale));
        swatch_group->addButton(this->swatches[i], i);
        swatch_column->addWidget(this->swatches[i]);
    }
    swatch_column->addStretch();
    this->swatches[0]->setChecked(true);
    connect(swatch_group, &QButtonGroup::idClicked, this, [this](int id) { this->select_color(id); });
    colors->addLayout(swatch_column, 1);
    layout->addLayout(colors);

    // Send the wheel's colour at most every 50 ms while dragging, and on release
    this->send_timer = new QTimer(this);
    this->send_timer->setSingleShot(true);
    this->send_timer->setInterval(50);
    connect(this->send_timer, &QTimer::timeout, this, [this] {
        this->set_look({{kColorKeys[this->editing], this->wheel->color().name()}});
    });
    connect(this->wheel, &ColorWheel::colorChanged, this, [this](const QColor &color) {
        this->show_swatch(this->editing, color);
        if (!this->send_timer->isActive())
            this->send_timer->start();
    });
    connect(this->wheel, &ColorWheel::released, this, [this](const QColor &color) {
        this->send_timer->stop();
        this->set_look({{kColorKeys[this->editing], color.name()}});
    });

    // speed / brightness
    this->speed_slider = new QSlider(Qt::Horizontal, content);
    this->speed_slider->setRange(0, 255);
    connect(this->speed_slider, &QSlider::valueChanged, this, [this](int value) {
        if (!this->loading)
            this->set_look({{"speed", value}});
    });
    layout->addLayout(this->row("Speed", this->speed_slider));

    this->brightness_slider = new QSlider(Qt::Horizontal, content);
    this->brightness_slider->setRange(0, 255);
    connect(this->brightness_slider, &QSlider::valueChanged, this, [this](int value) {
        if (!this->loading)
            this->set_look({{"brightness", value}});
    });
    layout->addLayout(this->row("Brightness", this->brightness_slider));

    auto music_hint = new QLabel("VU Meter, Spectrum and Beat Pulse react to the music playing on this unit.", content);
    music_hint->setWordWrap(true);
    music_hint->setFont(this->arbiter.forge().font(11));
    layout->addWidget(music_hint);
    layout->addStretch();
}

// ---------------------------------------------------------------- looks

QStringList LightingWidget::targets() const
{
    QStringList zones;
    for (int i = 0; i < this->zones.size(); ++i) {
        if (this->target < 0 || this->target == i)
            zones.append(this->zones[i]);
    }
    return zones;
}

void LightingWidget::set_target(int target)
{
    this->target = target;
    this->load_controls();
}

// Show the selected zone's look (the first zone's when several are selected)
void LightingWidget::load_controls()
{
    const QVariantMap look = this->looks.value(this->targets().value(0));
    if (look.isEmpty())
        return;

    this->loading = true;
    this->pattern_selector->set_current(kPatterns.value(look.value("pattern").toInt()));
    for (int i = 0; i < 2; ++i)
        this->show_swatch(i, QColor(look.value(kColorKeys[i]).toString()));
    if (!this->wheel->dragging())
        this->wheel->setColor(QColor(look.value(kColorKeys[this->editing]).toString()));
    // Don't fight a finger still on a slider with echoes of its own earlier values
    if (!this->speed_slider->isSliderDown())
        this->speed_slider->setValue(look.value("speed").toInt());
    if (!this->brightness_slider->isSliderDown())
        this->brightness_slider->setValue(look.value("brightness").toInt());
    this->loading = false;
}

// The wheel now edits Color (0) or Color 2 (1)
void LightingWidget::select_color(int which)
{
    this->editing = which;
    const QVariantMap look = this->looks.value(this->targets().value(0));
    this->wheel->setColor(QColor(look.value(kColorKeys[which]).toString()));
}

// A swatch button filled with its colour; the one the wheel edits is outlined
void LightingWidget::show_swatch(int which, const QColor &color)
{
    // Dark text on colours that look light (perceived brightness: green counts most)
    const int luma = (299 * color.red() + 587 * color.green() + 114 * color.blue()) / 1000;
    const QColor text = luma > 150 ? Qt::black : Qt::white;
    this->swatches[which]->setStyleSheet(
        QString("QPushButton { background-color: %1; color: %2; border: 2px solid transparent; border-radius: 8px; }"
                "QPushButton:checked { border: 3px solid palette(text); }")
            .arg(color.name(), text.name()));
}

void LightingWidget::set_look(const QVariantMap &changes)
{
    for (const QString &zone : this->targets()) {
        for (auto it = changes.begin(); it != changes.end(); ++it)
            this->looks[zone][it.key()] = it.value();
        this->call(this->zone_path(zone[0]), kZoneInterface, "SetLook", {changes});
    }
}

// A zone's look changed, from this tab or anything else on the bus
void LightingWidget::on_look_changed(const QDBusMessage &message)
{
    const QString zone = message.path().section('/', -1);
    if (!this->zones.contains(zone))
        return;
    this->looks[zone] = to_map(message.arguments().value(0));
    if (this->targets().value(0) == zone)
        this->load_controls();
}
