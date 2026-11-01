#include "pages.hpp"

#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <cmath>

namespace tc {

namespace {
struct Dot : QWidget {
    QColor c;
    explicit Dot(const QColor &col) : c(col) { setFixedSize(10, 10); }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawEllipse(rect().adjusted(1, 1, -1, -1));
    }
};
QColor accentOr(const Look &l) { return l.accent.lightness() > 20 && l.accent.lightness() < 240 ? l.accent : l.green; }
}  // namespace

Card *makeCard(QWidget *parent, QLayout *layout)
{
    auto *c = new Card(parent);
    layout->setContentsMargins(14, 12, 14, 12);
    c->setLayout(layout);
    return c;
}

/* ---------- overview ---------- */

OverviewPage::OverviewPage(Client *c, QWidget *parent) : Page(c, parent)
{
    auto *root = new QGridLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(10);

    auto *hv = new QVBoxLayout;
    houseTitle_ = new Text("House battery", 1.0, false, Text::Muted);
    houseBat_ = new BatteryWidget;
    houseBat_->setMinimumWidth(96);
    hVolts_ = new Stat("Voltage");
    hTemp_ = new Stat("Temperature");
    auto *hh = new QHBoxLayout;
    auto *hs = new QVBoxLayout;
    hs->addStretch(1); hs->addWidget(hVolts_); hs->addSpacing(8); hs->addWidget(hTemp_); hs->addStretch(1);
    hh->addWidget(houseBat_, 0); hh->addSpacing(14); hh->addLayout(hs, 1);
    hv->addWidget(houseTitle_); hv->addLayout(hh, 1);
    houseCard_ = makeCard(this, hv);

    auto *sv = new QVBoxLayout;
    starterTitle_ = new Text("Starter battery", 1.0, false, Text::Muted);
    starterBat_ = new BatteryWidget;
    starterBat_->setMinimumWidth(96);
    sVolts_ = new Stat("Voltage");
    sState_ = new Stat("State");
    sTemp_ = new Stat("Temperature");
    auto *sh = new QHBoxLayout;
    auto *ss = new QVBoxLayout;
    ss->addStretch(1); ss->addWidget(sVolts_); ss->addSpacing(6); ss->addWidget(sState_); ss->addSpacing(6); ss->addWidget(sTemp_); ss->addStretch(1);
    sh->addWidget(starterBat_, 0); sh->addSpacing(14); sh->addLayout(ss, 1);
    sv->addWidget(starterTitle_); sv->addLayout(sh, 1);
    starterCard_ = makeCard(this, sv);

    auto *pv = new QVBoxLayout;
    pv->addWidget(new Text("Power", 1.0, false, Text::Muted));
    auto *ph = new QHBoxLayout;
    solar_ = new Stat("Solar input"); draw_ = new Stat("Power draw"); current_ = new Stat("Current"); left_ = new Stat("Time left");
    for (Stat *s : {solar_, draw_, current_, left_}) ph->addWidget(s, 1);
    pv->addLayout(ph);
    auto *powerCard = makeCard(this, pv);

    auto *lv = new QVBoxLayout;
    lv->addWidget(new Text("Loads", 1.0, false, Text::Muted));
    loadsTotal_ = new Text("0.0 A", 1.9, true);
    loadsBar_ = new MeterBar;
    loadsInfo_ = new Text("", 0.8, false, Text::Muted);
    lv->addWidget(loadsTotal_); lv->addWidget(loadsBar_); lv->addWidget(loadsInfo_); lv->addStretch(1);
    auto *loadsCard = makeCard(this, lv);

    auto *av = new QVBoxLayout;
    av->addWidget(new Text("Needs attention", 1.0, false, Text::Muted));
    alerts_ = new QVBoxLayout;
    alerts_->setSpacing(6);
    av->addLayout(alerts_);
    av->addStretch(1);
    alertsCard_ = makeCard(this, av);

    root->addWidget(houseCard_, 0, 0);
    root->addWidget(starterCard_, 0, 1);
    root->addWidget(powerCard, 1, 0, 1, 2);
    root->addWidget(loadsCard, 2, 0);
    root->addWidget(alertsCard_, 2, 1);
    root->setColumnStretch(0, 1);
    root->setColumnStretch(1, 1);
}

void OverviewPage::onSettings()
{
    houseCard_->setVisible(client->flag("show_house"));
    starterCard_->setVisible(client->flag("show_starter"));
    alertsCard_->setVisible(client->flag("alerts_overview"));
    houseTitle_->setText(client->setting("house_name", "House battery"));
    starterTitle_->setText(client->setting("starter_name", "Starter battery"));
    if (!client->state().isEmpty()) onState(client->state());
}

void OverviewPage::onState(const QJsonObject &s)
{
    const Units u = client->units();
    const QJsonObject h = s.value("house").toObject(), st = s.value("starter").toObject(), pw = s.value("power").toObject();
    houseBat_->setCharge(h.value("soc").toDouble(), h.value("band").toString());
    hVolts_->setValue(QString("%1 V").arg(h.value("volts").toDouble(), 0, 'f', 1));
    hTemp_->setValue(u.temperature(h.value("temp_c").toDouble()));
    starterBat_->setCharge(st.value("soc").toDouble(), st.value("band").toString());
    sVolts_->setValue(QString("%1 V").arg(st.value("volts").toDouble(), 0, 'f', 1));
    QString state = st.value("state").toString();
    sState_->setValue(state.isEmpty() ? "—" : state.left(1).toUpper() + state.mid(1));
    sTemp_->setValue(u.temperature(st.value("temp_c").toDouble()));

    const double volts = h.value("volts").toDouble(12);
    solar_->setValue(QString("+%1 A").arg(pw.value("solar_w").toDouble() / (volts > 0 ? volts : 12), 0, 'f', 1));
    draw_->setValue(QString("%1 W").arg(qRound(std::fabs(h.value("watts").toDouble()))));
    current_->setValue(QString("%1 A").arg(h.value("amps").toDouble(), 0, 'f', 1));
    const QJsonValue tl = h.value("time_left_h");
    left_->setValue(tl.isNull() ? (h.value("charging").toBool() ? "Charging" : "—") : hours(tl.toDouble()));

    double total = 0;
    int on = 0, n = 0;
    for (const QJsonValue &v : s.value("controls").toArray()) {
        const QJsonObject c = v.toObject();
        total += c.value("amps").toDouble();
        n++;
        if (c.value("on").toBool()) on++;
    }
    const double limit = client->setting("controller_limit_a", "100").toDouble() > 0 ? client->setting("controller_limit_a", "100").toDouble() : 100;
    loadsTotal_->setText(QString("%1 A total").arg(total, 0, 'f', 1));
    loadsBar_->setValue(total / limit * 100.0);
    loadsInfo_->setText(QString("%1 of %2 controls on, %3 A capacity").arg(on).arg(n).arg(limit));
    renderAlerts(s);
}

void OverviewPage::renderAlerts(const QJsonObject &s)
{
    const Look &look = Look::get();
    const Units u = client->units();
    QList<QPair<bool, QString>> a;  // bad?, text
    auto num = [this](const char *k, double d) { return client->setting(k, QString::number(d)).toDouble(); };
    const QJsonObject h = s.value("house").toObject();
    const double soc = h.value("soc").toDouble();
    if (soc <= num("alert_critical_pct", 15)) a << qMakePair(true, QString("House battery is critically low (%1%)").arg(qRound(soc)));
    else if (soc <= num("alert_low_pct", 30)) a << qMakePair(false, QString("House battery is low (%1%)").arg(qRound(soc)));
    if (h.value("volts").toDouble() < num("alert_low_v", 11.8)) a << qMakePair(true, QString("House battery voltage is low (%1 V)").arg(h.value("volts").toDouble(), 0, 'f', 1));
    if (h.value("temp_c").toDouble() > num("alert_batt_temp_c", 45)) a << qMakePair(true, QString("House battery is hot (%1)").arg(u.temperature(h.value("temp_c").toDouble())));
    static const QMap<QString, QString> names = {{"front_left", "Front left"}, {"front_right", "Front right"}, {"rear_left", "Rear left"}, {"rear_right", "Rear right"}, {"spare", "Spare"}, {"trailer_left", "Trailer left"}, {"trailer_right", "Trailer right"}};
    for (const QJsonValue &v : s.value("tires").toArray()) {
        const QJsonObject t = v.toObject();
        const QString n = names.value(t.value("pos").toString(), t.value("pos").toString());
        if (t.value("low").toBool()) a << qMakePair(true, QString("%1 tire pressure is low (%2 %3)").arg(n, u.pressureValue(t.value("psi").toDouble()), u.pressureUnit()));
        if (t.value("hot").toBool()) a << qMakePair(false, QString("%1 tire is hot (%2)").arg(n, u.temperature(t.value("temp_c").toDouble())));
    }
    for (const QJsonValue &v : s.value("controls").toArray()) {
        const QJsonObject c = v.toObject();
        if (c.value("on").toBool() && c.value("load_pct").toDouble() >= num("alert_control_pct", 90)) a << qMakePair(false, QString("Near fuse limit: %1").arg(c.value("name").toString()));
    }
    for (const QJsonValue &v : maint_.value("items").toArray()) {
        const QJsonObject i = v.toObject();
        if (i.value("status").toString() == "due") a << qMakePair(true, i.value("name").toString() + " is overdue");
        else if (i.value("status").toString() == "soon") a << qMakePair(false, i.value("name").toString() + " is due soon");
    }
    const QJsonObject m = s.value("modules").toObject();
    if (!m.value("victron").toObject().value("connected").toBool(true)) a << qMakePair(false, QString("The Victron module is not responding"));
    if (!m.value("control").toObject().value("connected").toBool(true)) a << qMakePair(true, QString("The control module is not responding"));

    QString key;
    for (auto &x : a) key += (x.first ? "1" : "0") + x.second + "|";
    if (alertsCard_->property("key").toString() == key + QString::number(look.dark)) return;  // unchanged: leave the rows alone
    alertsCard_->setProperty("key", key + QString::number(look.dark));
    while (QLayoutItem *it = alerts_->takeAt(0)) {
        delete it->widget();
        delete it;
    }
    if (a.isEmpty()) alerts_->addWidget(new Text("Nothing needs attention.", 0.95, false, Text::Muted));
    for (auto &x : a) {
        auto *row = new QHBoxLayout;
        row->setSpacing(8);
        row->addWidget(new Dot(x.first ? look.red : look.yellow), 0, Qt::AlignVCenter);
        row->addWidget(new Text(x.second, 0.95), 1);
        alerts_->addLayout(row);
    }
}

/* ---------- controls ---------- */

ControlTile::ControlTile(int id, QWidget *parent) : QAbstractButton(parent), id_(id)
{
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QSize ControlTile::sizeHint() const { return QSize(160, qRound(basePx() * 6.2)); }

void ControlTile::setData(const QString &name, bool on, double amps, double fuse, int pct, const QString &band, bool near)
{
    name_ = name; on_ = on; amps_ = amps; fuse_ = fuse; pct_ = pct; band_ = band; near_ = near;
    setAccessibleName(QString("%1 %2 %3 A").arg(name, on ? "on" : "off").arg(amps, 0, 'f', 1));
    update();
}

void ControlTile::paintEvent(QPaintEvent *)
{
    const Look &l = Look::get();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor fill = on_ ? accentOr(l) : l.panel;
    const QColor fg = on_ ? (fill.lightness() > 140 ? QColor(0, 0, 0) : QColor(255, 255, 255)) : l.text;
    const QColor sub = on_ ? fg : l.muted;
    card(p, QRectF(rect()), fill, on_ ? fill : l.line);
    if (pending_ || underMouse()) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(fg.red(), fg.green(), fg.blue(), pending_ ? 40 : 22));
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);
    }
    const qreal u = basePx(), m = u * 0.8;
    p.setPen(fg);
    p.setFont(scaled(qApp->font(), u * 1.15, false, true));
    const QFontMetricsF f1(p.font());
    p.drawText(QPointF(m, m + f1.ascent()), f1.elidedText(name_, Qt::ElideRight, width() - 2 * m));
    p.setFont(scaled(qApp->font(), u * 0.95, true));
    p.setPen(on_ ? fg : l.muted);
    const QFontMetricsF f2(p.font());
    const qreal y2 = m + f1.height() + f2.ascent();
    p.drawText(QPointF(m, y2), QString("%1 A").arg(amps_, 0, 'f', 1));
    p.setFont(scaled(qApp->font(), u * 0.78));
    p.setPen(sub);
    const QFontMetricsF f3(p.font());
    p.drawText(QPointF(m, y2 + f3.height() * 1.05),
               (on_ ? "On, " : "Off, ") + QString("%1 A fuse").arg(fuse_, 0, 'f', fuse_ == qRound(fuse_) ? 0 : 1) + (on_ && near_ ? ", near limit" : ""));
    const QRectF bar(m, height() - m - 6, width() - 2 * m, 6);
    p.setPen(Qt::NoPen);
    p.setBrush(on_ ? QColor(0, 0, 0, 120) : l.line);
    p.drawRoundedRect(bar, 3, 3);
    if (on_ && pct_ > 0) {
        p.setBrush(on_ ? Look::band(band_) : l.green);
        p.drawRoundedRect(QRectF(bar.left(), bar.top(), qMax(6.0, bar.width() * qMin(pct_, 100) / 100.0), bar.height()), 3, 3);
    }
}

ControlsPage::ControlsPage(Client *c, QWidget *parent) : Page(c, parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(10);
    auto *head = new QHBoxLayout;
    auto *left = new QVBoxLayout;
    left->setSpacing(0);
    left->addWidget(new Text("Total draw", 1.0, false, Text::Muted));
    auto *line = new QHBoxLayout;
    total_ = new Text("0.0", 1.9, true);
    totalUnit_ = new Text("A on 0 controls", 1.1, false, Text::Muted);
    line->addWidget(total_, 0);
    line->addWidget(totalUnit_, 1);
    left->addLayout(line);
    auto *off = new QPushButton("All off");
    connect(off, &QPushButton::clicked, this, [this] {
        client->post("/api/controls/all-off", QJsonObject(), [this](int, const QJsonDocument &, const QString &err) {
            if (!err.isEmpty()) client->report(err);
        });
    });
    head->addLayout(left, 1);
    head->addWidget(off, 0, Qt::AlignVCenter);
    root->addWidget(makeCard(this, head));
    auto *gridHost = new QWidget;
    grid_ = new QGridLayout(gridHost);
    grid_->setContentsMargins(0, 0, 0, 0);
    grid_->setSpacing(10);
    root->addWidget(gridHost);
    root->addStretch(1);
}

void ControlsPage::relayout()
{
    const int cols = qMax(2, width() / qRound(basePx() * 11));
    if (cols == cols_ && grid_->count() == tiles_.size()) return;
    cols_ = cols;
    while (grid_->count()) delete grid_->takeAt(0);
    for (int i = 0; i < tiles_.size(); i++) grid_->addWidget(tiles_[i], i / cols, i % cols);
    for (int i = 0; i < cols; i++) grid_->setColumnStretch(i, 1);
}

void ControlsPage::onState(const QJsonObject &s)
{
    const QJsonArray list = s.value("controls").toArray();
    QSet<int> seen;
    double total = 0;
    int on = 0;
    bool changed = false;
    for (const QJsonValue &v : list) {
        const QJsonObject c = v.toObject();
        const int id = c.value("id").toInt();
        seen.insert(id);
        ControlTile *tile = nullptr;
        for (ControlTile *t : tiles_)
            if (t->id() == id) tile = t;
        if (!tile) {
            tile = new ControlTile(id, this);
            tiles_ << tile;
            connect(tile, &QAbstractButton::clicked, this, [this, tile] {
                const bool want = !tile->isOn();
                tile->setPending(true);
                client->post(QString("/api/controls/%1/state").arg(tile->id()), QJsonObject{{"on", want}}, [this, tile](int, const QJsonDocument &, const QString &err) {
                    tile->setPending(false);
                    if (!err.isEmpty()) client->report(err);
                });
            });
            changed = true;
        }
        tile->setData(c.value("name").toString(), c.value("on").toBool(), c.value("amps").toDouble(), c.value("fuse_a").toDouble(), c.value("load_pct").toInt(), c.value("band").toString(), c.value("near_limit").toBool());
        total += c.value("amps").toDouble();
        if (c.value("on").toBool()) on++;
    }
    for (int i = tiles_.size() - 1; i >= 0; i--)
        if (!seen.contains(tiles_[i]->id())) {
            delete tiles_.takeAt(i);
            changed = true;
        }
    if (changed) {
        cols_ = 0;
        relayout();
    }
    total_->setText(QString::number(total, 'f', 1));
    totalUnit_->setText(QString("A on %1 control%2").arg(on).arg(on == 1 ? "" : "s"));
}

/* ---------- history ---------- */

HistoryPage::HistoryPage(Client *c, QWidget *parent) : Page(c, parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(10);
    auto *chips = new QHBoxLayout;
    auto *group = new QButtonGroup(this);
    const QList<QPair<QString, QString>> ranges = {{"6h", "6 h"}, {"24h", "24 h"}, {"7d", "7 days"}, {"30d", "30 days"}};
    for (const auto &r : ranges) {
        auto *b = new NavButton(r.second);
        b->setChecked(r.first == range_);
        group->addButton(b);
        chips->addWidget(b);
        connect(b, &QAbstractButton::clicked, this, [this, r] {
            range_ = r.first;
            load();
        });
    }
    group->setExclusive(true);
    chips->addStretch(1);
    root->addLayout(chips);
    auto *cv = new QVBoxLayout;
    title_ = new Text("Charge level, last 24 hours", 1.0, false, Text::Muted);
    chart_ = new LineChart;
    cv->addWidget(title_);
    cv->addWidget(chart_, 1);
    root->addWidget(makeCard(this, cv), 1);
    auto *dv = new QVBoxLayout;
    dv->addWidget(new Text("Amp-hours used per day", 1.0, false, Text::Muted));
    days_ = new DayBars;
    dv->addWidget(days_, 1);
    root->addWidget(makeCard(this, dv), 1);
    timer_.setInterval(60000);
    connect(&timer_, &QTimer::timeout, this, [this] { load(); });
}

void HistoryPage::activate()
{
    load();
    timer_.start();
}

void HistoryPage::load()
{
    if (!client->configured()) return;
    struct R { double secs; const char *label, *a, *b; };
    static const QMap<QString, R> info = {{"6h", {21600, "last 6 hours", "6 h ago", "3 h ago"}}, {"24h", {86400, "last 24 hours", "24 h ago", "12 h ago"}}, {"7d", {604800, "last 7 days", "7 days ago", "3 days ago"}}, {"30d", {2592000, "last 30 days", "30 days ago", "15 days ago"}}};
    const R r = info.value(range_);
    const QString range = range_;
    client->get("/api/history?range=" + range, [this, r, range](int status, const QJsonDocument &doc, const QString &) {
        if (status != 200 || range != range_) return;
        QList<LineChart::Pt> pts;
        for (const QJsonValue &v : doc.object().value("points").toArray()) pts.append({v.toObject().value("ts").toDouble(), v.toObject().value("soc").toDouble()});
        title_->setText(QString("Charge level, %1").arg(r.label));
        chart_->setData(pts, r.secs, {r.a, r.b, "Now"});
    });
    client->get("/api/history/daily?days=7", [this](int status, const QJsonDocument &doc, const QString &) {
        if (status != 200) return;
        QList<DayBars::Day> days;
        for (const QJsonValue &v : doc.object().value("days").toArray()) {
            const QJsonObject o = v.toObject();
            days.append({QDate::fromString(o.value("date").toString(), Qt::ISODate).toString("ddd"), o.value("amp_hours").toDouble()});
        }
        days_->setDays(days);
    });
}

/* ---------- tires ---------- */

TiresPage::TiresPage(Client *c, QWidget *parent) : Page(c, parent)
{
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(10);
    auto *vl = new QVBoxLayout;
    vl->addWidget(new Text("Vehicle", 1.0, false, Text::Muted));
    vehicle_ = new RigWidget(RigWidget::Vehicle);
    vl->addWidget(vehicle_, 1);
    vehicleCard_ = makeCard(this, vl);
    auto *tl = new QVBoxLayout;
    tl->addWidget(new Text("Trailer", 1.0, false, Text::Muted));
    trailer_ = new RigWidget(RigWidget::Trailer);
    tl->addWidget(trailer_, 1);
    trailerCard_ = makeCard(this, tl);
    root->addWidget(vehicleCard_, 1);
    root->addWidget(trailerCard_, 1);
}

void TiresPage::onSettings()
{
    vehicleCard_->setVisible(client->flag("show_vehicle"));
    trailerCard_->setVisible(client->flag("show_trailer"));
    if (!client->state().isEmpty()) onState(client->state());
}

void TiresPage::onState(const QJsonObject &s)
{
    const Units u = client->units();
    QList<TireInfo> v, t;
    for (const QJsonValue &x : s.value("tires").toArray()) {
        const QJsonObject o = x.toObject();
        TireInfo ti;
        ti.pos = o.value("pos").toString();
        ti.value = u.pressureValue(o.value("psi").toDouble());
        ti.low = o.value("low").toBool();
        ti.hot = o.value("hot").toBool();
        ti.text = u.pressureUnit() + ", " + u.temperature(o.value("temp_c").toDouble()) + (ti.low ? ", low" : "") + (ti.hot ? ", hot" : "");
        (ti.pos.startsWith("trailer") ? t : v).append(ti);
    }
    vehicle_->setTires(v);
    trailer_->setTires(t);
}

/* ---------- service ---------- */

ServicePage::ServicePage(Client *c, QWidget *parent) : Page(c, parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(10);
    auto *head = new QHBoxLayout;
    auto *left = new QVBoxLayout;
    left->setSpacing(0);
    left->addWidget(new Text("Odometer", 1.0, false, Text::Muted));
    auto *line = new QHBoxLayout;
    odo_ = new Text("—", 1.9, true);
    odoUnit_ = new Text("mi", 1.2, false, Text::Muted);
    line->addWidget(odo_, 0);
    line->addWidget(odoUnit_, 1);
    left->addLayout(line);
    auto *log = new QPushButton("Log service");
    connect(log, &QPushButton::clicked, this, [this] { openLog(0); });
    head->addLayout(left, 1);
    head->addWidget(log, 0, Qt::AlignVCenter);
    root->addWidget(makeCard(this, head));
    auto *lv = new QVBoxLayout;
    lv->addWidget(new Text("Maintenance schedule", 1.0, false, Text::Muted));
    list_ = new QVBoxLayout;
    list_->setSpacing(8);
    lv->addLayout(list_);
    root->addWidget(makeCard(this, lv));
    root->addStretch(1);
    timer_.setInterval(60000);
    connect(&timer_, &QTimer::timeout, this, [this] { load(); });
}

void ServicePage::activate()
{
    load();
    timer_.start();
}

void ServicePage::load()
{
    if (!client->configured()) return;
    client->get("/api/maintenance", [this](int status, const QJsonDocument &doc, const QString &) {
        if (status != 200 || !doc.isObject()) return;
        data_ = doc.object();
        render();
        if (loaded) loaded(data_);
    });
}

void ServicePage::render()
{
    if (data_.isEmpty()) return;
    const Units u = client->units();
    const double odo = data_.value("odometer_mi").toDouble();
    odo_->setText(u.distanceValue(odo));
    odoUnit_->setText(u.distanceUnit());
    while (QLayoutItem *it = list_->takeAt(0)) {
        delete it->widget();
        delete it;
    }
    const QJsonArray items = data_.value("items").toArray();
    if (items.isEmpty()) list_->addWidget(new Text("No maintenance items yet.", 0.95, false, Text::Muted));
    for (const QJsonValue &v : items) {
        const QJsonObject i = v.toObject();
        QStringList parts;
        if (!i.value("interval_miles").isNull()) {
            QString t = QString("Every %1 %2").arg(u.distanceValue(i.value("interval_miles").toDouble()), u.distanceUnit());
            if (!i.value("last_miles").isNull()) t += QString(", %1 %2 since last").arg(u.distanceValue(qMax(0.0, odo - i.value("last_miles").toDouble())), u.distanceUnit());
            parts << t;
        }
        if (!i.value("interval_months").isNull()) {
            QString t = QString("Every %1 months").arg(i.value("interval_months").toInt());
            const QDate last = QDate::fromString(i.value("last_date").toString(), Qt::ISODate);
            if (last.isValid()) {
                const int mo = qMax<qint64>(0, qint64(last.daysTo(QDate::currentDate()) / 30.4375));
                t += QString(", %1 month%2 since last").arg(mo).arg(mo == 1 ? "" : "s");
            }
            parts << t;
        }
        if (parts.isEmpty()) parts << "No interval set";
        const QString status = i.value("status").toString();
        auto *box = new QWidget;
        auto *bl = new QVBoxLayout(box);
        bl->setContentsMargins(0, 0, 0, 0);
        bl->setSpacing(4);
        // Name over its intervals, one line each: side by side on one row, the
        // intervals were cut off with large text
        auto *row = new QHBoxLayout;
        auto *info = new QVBoxLayout;
        info->setSpacing(2);
        info->addWidget(new Text(i.value("name").toString(), 1.0, true));
        const Text::Role role = status == "due" ? Text::Bad : status == "soon" ? Text::Warn : Text::Muted;
        for (const QString &part : parts) info->addWidget(new Text(part, 0.85, false, role));
        row->addLayout(info, 1);
        auto *b = new QPushButton("Log");
        const int id = i.value("id").toInt();
        connect(b, &QPushButton::clicked, this, [this, id] { openLog(id); });
        row->addWidget(b, 0, Qt::AlignVCenter);
        bl->addLayout(row);
        auto *bar = new MeterBar;
        bar->setValue(i.value("percent_used").toDouble(), status == "due" ? "red" : status == "soon" ? "yellow" : "green");
        bl->addWidget(bar);
        list_->addWidget(box);
    }
}

void ServicePage::openLog(int preselect)
{
    const Units u = client->units();
    const QJsonArray items = data_.value("items").toArray();
    QDialog dlg(window());
    dlg.setWindowTitle("Log a service");
    dlg.setStyleSheet("QLabel { border: none; background: transparent; padding: 0; }");
    auto *form = new QFormLayout;
    auto *which = new QComboBox;
    for (const QJsonValue &v : items) which->addItem(v.toObject().value("name").toString(), v.toObject().value("id").toInt());
    if (preselect) which->setCurrentIndex(qMax(0, which->findData(preselect)));
    auto *miles = new QLineEdit(QString::number(qRound(u.fromMiles(data_.value("odometer_mi").toDouble()))));
    miles->setValidator(new QIntValidator(0, 10000000, miles));
    auto *date = new QDateEdit(QDate::currentDate());
    date->setCalendarPopup(true);
    date->setDisplayFormat("yyyy-MM-dd");
    form->addRow("Service item", which);
    form->addRow(QString("Odometer at service (%1)").arg(u.distanceUnit()), miles);
    form->addRow("Date", date);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    auto *lay = new QVBoxLayout(&dlg);
    lay->addLayout(form);
    lay->addWidget(buttons);
    buttons->button(QDialogButtonBox::Save)->setEnabled(which->count() > 0);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted || which->count() == 0) return;
    const QJsonObject body{{"miles", qRound(u.toMiles(miles->text().toDouble()))}, {"date", date->date().toString("yyyy-MM-dd")}};
    client->post(QString("/api/maintenance/%1/log").arg(which->currentData().toInt()), body, [this](int, const QJsonDocument &, const QString &err) {
        if (!err.isEmpty()) client->report(err);
        else load();
    });
}

/* ---------- power flow ---------- */

PowerPage::PowerPage(Client *c, QWidget *parent) : Page(c, parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(10);
    auto *v = new QVBoxLayout;
    v->addWidget(new Text("Power flow", 1.0, false, Text::Muted));
    auto *h = new QHBoxLayout;
    in_ = new Stat("Coming in");
    out_ = new Stat("Going out");
    net_ = new Stat("Battery net");
    for (Stat *s : {in_, out_, net_}) h->addWidget(s, 1);
    v->addLayout(h);
    flow_ = new FlowWidget;
    flow_->setMinimumHeight(qRound(basePx() * 11));
    v->addWidget(flow_, 1);
    root->addWidget(makeCard(this, v), 1);
}

void PowerPage::onState(const QJsonObject &s)
{
    const QJsonObject p = s.value("power").toObject(), h = s.value("house").toObject();
    FlowData d;
    d.valid = true;
    d.solarW = p.value("solar_w").toDouble();
    d.altW = p.value("alternator_w").toDouble();
    d.controlsW = p.value("controls_w").toDouble();
    d.inverterW = p.value("inverter_w").toDouble();
    d.inW = p.value("in_w").toDouble();
    d.outW = p.value("out_w").toDouble();
    d.netW = p.value("net_w").toDouble();
    d.volts = h.value("volts").toDouble(12);
    d.solarA = d.solarW / (d.volts > 0 ? d.volts : 12);
    d.altA = d.altW / (d.volts > 0 ? d.volts : 12);
    d.controlsA = d.controlsW / (d.volts > 0 ? d.volts : 12);
    for (const QJsonValue &v : s.value("controls").toArray())
        if (v.toObject().value("on").toBool()) d.controlsOn++;
    d.soc = h.value("soc").toDouble();
    d.band = h.value("band").toString();
    d.charging = h.value("charging").toBool();
    d.houseName = client->setting("house_name", "House battery");
    flow_->setData(d);
    in_->setValue(QString("%1 W").arg(qRound(d.inW)));
    out_->setValue(QString("%1 W").arg(qRound(d.outW)));
    net_->setValue(QString("%1 W").arg(qRound(d.netW)));
    flow_->update();
    // battery child widget follows the live charge
    if (auto *b = flow_->findChild<BatteryWidget *>()) b->setCharge(d.soc, d.band);
}

}  // namespace tc
