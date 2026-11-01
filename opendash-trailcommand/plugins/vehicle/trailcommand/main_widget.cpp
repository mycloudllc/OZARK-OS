#include "main_widget.hpp"

#include <QDateTime>
#include <QInputDialog>
#include <QLocale>
#include <QScrollArea>
#include <QScroller>

#include "connection_dialog.hpp"

namespace tc {

Main::Main(QWidget *parent) : QWidget(parent)
{
    setObjectName("TrailCommand");  // the Vehicle page uses this as the tab title
    client_ = new Client(this);
    client_->setPinPrompt([this](QString *pin) {
        bool ok = false;
        const QString p = QInputDialog::getText(window(), "PIN required", "The hub asks for its PIN before changes:", QLineEdit::Password, QString(), &ok);
        if (ok) *pin = p;
        return ok;
    });

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 4, 0, 0);
    root->setSpacing(6);

    // status icons live in Dash's bottom control bar, in this order
    bar_ = new StatusBar(this);
    bar_->add("clock", StatusIcon::Glyph::Clock, "Date and time");
    bar_->add("hub", StatusIcon::Glyph::Hub, "TrailCommand hub");
    bar_->add("wifi", StatusIcon::Glyph::Wifi, "Wi-Fi");
    bar_->add("cell", StatusIcon::Glyph::Cell, "Cell");
    connect(bar_, &StatusBar::clicked, this, [this](const QString &id) {
        if (id == "hub") showConnectionDialog(false);
    });

    outer_ = new QStackedWidget;
    root->addWidget(outer_, 1);

    // page 0: first run, nothing configured yet
    auto *welcome = new QWidget;
    auto *wl = new QVBoxLayout(welcome);
    wl->addStretch(1);
    wl->addWidget(new Text("Connect to your TrailCommand hub", 1.5, true, Text::Normal, Qt::AlignCenter));
    welcomeText_ = new Text("Find it on this network, or type its address.", 1.0, false, Text::Muted, Qt::AlignCenter);
    wl->addWidget(welcomeText_);
    auto *wb = new QHBoxLayout;
    auto *find = new QPushButton("Find my hub");
    auto *manual = new QPushButton("Enter address");
    wb->addStretch(1);
    wb->addWidget(find);
    wb->addWidget(manual);
    wb->addStretch(1);
    wl->addSpacing(10);
    wl->addLayout(wb);
    wl->addStretch(2);
    connect(find, &QPushButton::clicked, this, [this] { showConnectionDialog(true); });
    connect(manual, &QPushButton::clicked, this, [this] { showConnectionDialog(false); });
    outer_->addWidget(welcome);

    // page 1: the dashboard
    auto *content = new QWidget;
    auto *cl = new QVBoxLayout(content);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->setSpacing(6);
    navRow_ = new QWidget;
    auto *nl = new QHBoxLayout(navRow_);
    nl->setContentsMargins(0, 0, 0, 0);
    nl->setSpacing(2);
    pages_ = new QStackedWidget;
    overview_ = new OverviewPage(client_);
    service_ = new ServicePage(client_);
    const QList<QPair<QString, Page *>> defs = {{"Overview", overview_}, {"Controls", new ControlsPage(client_)}, {"History", new HistoryPage(client_)},
                                                {"Tires", new TiresPage(client_)}, {"Service", service_}, {"Power", new PowerPage(client_)}};
    for (int i = 0; i < defs.size(); i++) {
        auto *b = new NavButton(defs[i].first);
        b->setChecked(i == 0);
        nl->addWidget(b);
        nav_ << b;
        list_ << defs[i].second;
        connect(b, &QAbstractButton::clicked, this, [this, i] { switchTo(i); });
        auto *sa = new QScrollArea;
        sa->setWidgetResizable(true);
        sa->setFrameShape(QFrame::NoFrame);
        sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        sa->setStyleSheet("QScrollArea { background: transparent; border: none; } QScrollArea > QWidget > QWidget { background: transparent; }");
        sa->setWidget(defs[i].second);
        QScroller::grabGesture(sa->viewport(), QScroller::LeftMouseButtonGesture);  // drag to scroll on a touch screen
        pages_->addWidget(sa);
    }
    nl->addStretch(1);
    auto *gear = new GearButton;  // the hub connection settings, at the end of the tab row
    nl->addWidget(gear);
    connect(gear, &QAbstractButton::clicked, this, [this] { showConnectionDialog(false); });
    cl->addWidget(navRow_);
    cl->addWidget(pages_, 1);
    outer_->addWidget(content);

    toast_ = new Text("", 0.95, true, Text::Normal, Qt::AlignCenter, this);
    toast_->hide();
    toastTimer_.setSingleShot(true);
    connect(&toastTimer_, &QTimer::timeout, toast_, &QWidget::hide);

    service_->loaded = [this](const QJsonObject &m) {
        for (Page *p : list_) p->onMaintenance(m);
    };
    maintTimer_.setInterval(300000);
    connect(&maintTimer_, &QTimer::timeout, service_, [this] { service_->load(); });

    connect(client_, &Client::stateChanged, this, [this](const QJsonObject &s) { onState(s); });
    connect(client_, &Client::settingsChanged, this, [this] {
        for (Page *p : list_) p->onSettings();
        updateClock();  // the 12/24 hour choice comes from the hub
    });
    clockTimer_.setInterval(1000);
    connect(&clockTimer_, &QTimer::timeout, this, [this] { updateClock(); });
    clockTimer_.start();
    updateClock();
    connect(client_, &Client::linkChanged, this, [this](Client::Link) { onLink(); });
    connect(client_, &Client::rejected, this, [this](const QString &m) { toast(m); });
    onLink();
}

bool Main::event(QEvent *e)
{
    if (e->type() == QEvent::ApplicationPaletteChange || e->type() == QEvent::StyleChange) update();
    return QWidget::event(e);
}

// Date and time of the device Dash runs on, in the 12 or 24 hour style chosen in the hub's settings
void Main::updateClock()
{
    const QDateTime now = QDateTime::currentDateTime();
    const QLocale loc;
    const bool h24 = client_->setting("clock", "12") == "24";
    const QString text = loc.toString(now, h24 ? "HH:mm" : "h:mm AP") + "\n" + loc.toString(now, "ddd, MMM d");
    if (text == clockShown_) return;
    clockShown_ = text;
    bar_->set("clock", 0, StatusIcon::Tone::Dim, text, loc.toString(now, "dddd, MMMM d, yyyy") + ", " + loc.toString(now, h24 ? "HH:mm" : "h:mm AP"));
}

void Main::toast(const QString &text)
{
    toast_->setText(text);
    toast_->adjustSize();
    const QSize s = toast_->sizeHint() + QSize(24, 12);
    toast_->setGeometry((width() - s.width()) / 2, height() - s.height() - 10, s.width(), s.height());
    toast_->setStyleSheet(QString("background: %1; color: %2; border-radius: 8px;").arg(Look::get().red.name(), Look::get().dark ? "#000" : "#fff"));
    toast_->show();
    toast_->raise();
    toastTimer_.start(4500);
}

void Main::switchTo(int index)
{
    if (index < 0 || index >= list_.size()) return;
    list_[current_]->deactivate();
    current_ = index;
    pages_->setCurrentIndex(index);
    for (int i = 0; i < nav_.size(); i++) nav_[i]->setChecked(i == index);
    list_[index]->activate();
}

void Main::showConnectionDialog(bool scanNow)
{
    ConnectionDialog dlg(client_->hub(), rememberPin, scanNow, window());
    if (dlg.exec() != QDialog::Accepted) return;
    rememberPin = dlg.rememberPin();
    const Hub hub = dlg.hub();
    if (persist) persist(hub, rememberPin);
    client_->setHub(hub);
    onLink();
}

void Main::onLink()
{
    const Client::Link l = client_->link();
    outer_->setCurrentIndex(client_->configured() ? 1 : 0);
    using Tone = StatusIcon::Tone;
    const QString host = client_->hub().host;
    if (!client_->configured() || l == Client::Link::Idle) bar_->hide("hub");
    else if (l == Client::Link::Live) bar_->set("hub", 0, Tone::Good, "", QString("TrailCommand hub: connected to %1").arg(host));
    else if (l == Client::Link::Connecting) bar_->set("hub", 0, Tone::Warn, "", QString("TrailCommand hub: connecting to %1…").arg(host));
    else bar_->set("hub", 0, Tone::Bad, "", QString("Can't reach the hub at %1:%2. Retrying… Tap to change the address or scan.").arg(host).arg(client_->hub().port));
    if (l != Client::Link::Live) {
        bar_->hide("wifi");
        bar_->hide("cell");
        maintTimer_.stop();
    }
    else {
        service_->load();
        maintTimer_.start();
        list_[current_]->activate();
    }
}

void Main::onState(const QJsonObject &s)
{
    for (Page *p : list_) p->onState(s);
    updateSignals(s.value("net").toObject());
}

static StatusIcon::Tone toneFor(int bars)
{
    return bars <= 0 ? StatusIcon::Tone::Dim : bars == 1 ? StatusIcon::Tone::Bad : bars == 2 ? StatusIcon::Tone::Warn : StatusIcon::Tone::Good;
}

void Main::updateSignals(const QJsonObject &net)
{
    const QJsonObject w = net.value("wifi").toObject(), l = net.value("lte").toObject();
    if (!w.value("present").toBool()) bar_->hide("wifi");
    else {
        const bool up = w.value("connected").toBool();
        const QString ssid = w.value("ssid").toString();
        const int bars = up ? w.value("bars").toInt() : 0;
        bar_->set("wifi", bars, toneFor(bars), QString(),
                  up ? QString("Wi-Fi: %1, %2 dBm").arg(ssid.isEmpty() ? "connected" : ssid).arg(w.value("dbm").toInt()) : "Wi-Fi: not connected");
    }
    if (!l.value("present").toBool()) bar_->hide("cell");
    else {
        const bool known = l.value("data_known").toBool(), sim = l.value("sim_ready").toBool(true), up = l.value("connected").toBool();
        const bool live = known && sim && up;
        const int bars = live ? l.value("bars").toInt() : 0;
        const QString label = !known ? QString() : !sim ? "No SIM" : up ? l.value("tech").toString("LTE") : QString();
        const QString tip = !known ? "Cellular modem detected, details not available"
                          : !sim   ? "Cellular: no SIM card"
                          : up     ? QString("Cellular: %1, %2, %3 dBm").arg(l.value("carrier").toString("connected"), l.value("tech").toString(), QString::number(l.value("dbm").toInt()))
                                   : "Cellular: no service";
        bar_->set("cell", bars, toneFor(bars), label, tip);
    }
}

}  // namespace tc
