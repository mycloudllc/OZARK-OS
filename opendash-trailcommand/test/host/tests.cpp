// Drives the TrailCommand plugin like a user would, against a running hub, and saves screenshots.
// usage: tctest <hub-port> <light|dark> [offline]
#include <QApplication>
#include <QComboBox>
#include <QBoxLayout>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QListWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPluginLoader>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QTabWidget>
#include <QTimer>
#include <functional>

#include "plugins/vehicle_plugin.hpp"

static int pass = 0, fail = 0;
static QString shotDir;
static void ok(const QString &name, bool cond, const QString &extra = QString())
{
    (cond ? pass : fail)++;
    printf("  %s %s %s\n", cond ? "ok  " : "FAIL", qPrintable(name), qPrintable(cond ? QString() : extra));
    fflush(stdout);
}
static void wait(int ms)
{
    QEventLoop l;
    QTimer::singleShot(ms, &l, &QEventLoop::quit);
    l.exec();
}
static bool waitFor(const std::function<bool()> &f, int ms = 10000)
{
    for (int t = 0; t < ms; t += 50) {
        if (f()) return true;
        wait(50);
    }
    return f();
}
static QWidget *visibleByName(QWidget *root, const QString &re);
static QWidget *byName(QWidget *root, const QString &re)
{
    const QRegularExpression rx(re);
    for (QWidget *w : root->findChildren<QWidget *>())
        if (!w->accessibleName().isEmpty() && rx.match(w->accessibleName()).hasMatch() && w->isVisibleTo(root->window())) return w;
    return nullptr;
}
static QList<QWidget *> allByName(QWidget *root, const QString &re)
{
    QList<QWidget *> out;
    const QRegularExpression rx(re);
    for (QWidget *w : root->findChildren<QWidget *>())
        if (!w->accessibleName().isEmpty() && rx.match(w->accessibleName()).hasMatch()) out << w;
    return out;
}
static QWidget *visibleByName(QWidget *root, const QString &re)
{
    QWidget *w = byName(root, re);
    return w && w->isVisible() ? w : nullptr;
}
static QPushButton *button(QWidget *root, const QString &text)
{
    for (QPushButton *b : root->findChildren<QPushButton *>())
        if (b->text() == text && b->isVisibleTo(root->window())) return b;
    return nullptr;
}
static QAbstractButton *navButton(QWidget *root, const QString &text)
{
    for (QAbstractButton *b : root->findChildren<QAbstractButton *>())
        if (b->text() == text && b->isVisibleTo(root->window())) return b;
    return nullptr;
}
static void shot(QWidget *w, const QString &name) { w->grab().save(shotDir + "/" + name + ".png"); }

static QNetworkAccessManager nam;
static QString base, pinHeader;
static QJsonDocument http(const QByteArray &verb, const QString &path, const QJsonObject &body = QJsonObject())
{
    QNetworkRequest req(QUrl(base + path));
    req.setRawHeader("X-TC-Client", "test");
    if (!pinHeader.isEmpty()) req.setRawHeader("X-TC-Pin", pinHeader.toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *r = nam.sendCustomRequest(req, verb, body.isEmpty() ? QByteArray() : QJsonDocument(body).toJson());
    QEventLoop l;
    QObject::connect(r, &QNetworkReply::finished, &l, &QEventLoop::quit);
    l.exec();
    const QJsonDocument d = QJsonDocument::fromJson(r->readAll());
    r->deleteLater();
    return d;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    app.setOrganizationName("tc-test");
    app.setApplicationName("dash");
    const QString port = argc > 1 ? argv[1] : "18800";
    const bool dark = argc > 2 && QString(argv[2]) == "dark";
    const bool offline = argc > 3 && QString(argv[3]) == "offline";
    base = "http://127.0.0.1:" + port;
    shotDir = QDir::currentPath() + QString("/shots/%1").arg(dark ? "dark" : "light");
    QDir().mkpath(shotDir);
    {
        QSettings dashSettings;  // what Dash itself would hold
        dashSettings.setValue("Theme/mode", dark ? 1 : 0);
        dashSettings.setValue("Theme/Color/light", "#0288D1");
        dashSettings.setValue("Theme/Color/dark", "#4FC3F7");
        dashSettings.sync();
        QSettings plugins("tc-test", "plugins");
        plugins.clear();
        if (offline) {
            plugins.setValue("Vehicle/TrailCommand/host", "127.0.0.1");
            plugins.setValue("Vehicle/TrailCommand/port", port.toInt());
        }
        plugins.sync();
    }
    app.setPalette([&] {
        QPalette p;
        p.setColor(QPalette::Base, QColor(dark ? "#4FC3F7" : "#0288D1"));  // Dash keeps its accent colour in Base
        return p;
    }());

    {
        // Dash's own style sheet, so dialogs look as they do inside Dash (DASH_DIR = a Dash checkout)
        QFile qss(QString("%1/assets/stylesheets/%2.qss").arg(qEnvironmentVariable("DASH_DIR"), dark ? "dark" : "light"));
        if (qss.open(QFile::ReadOnly)) app.setStyleSheet(QString::fromUtf8(qss.readAll()));
    }
    // Stand-in for Dash's bottom bar: the same object name and layout ([quick view] [stretch] [power] [close])
    QWidget fakeDash;
    {
        auto *fl = new QVBoxLayout(&fakeDash);
        fl->addStretch(1);
        auto *cb = new QWidget;
        cb->setObjectName("ControlBar");
        auto *bl = new QHBoxLayout(cb);
        bl->setContentsMargins(0, 0, 0, 0);
        bl->setSpacing(0);
        bl->addWidget(new QLabel("quick view"));
        bl->addStretch();
        bl->addWidget(new QPushButton("power"));
        bl->addWidget(new QPushButton("close"));
        fl->addWidget(cb);
        fakeDash.resize(820, 64);
        fakeDash.move(0, 520);
        fakeDash.show();
    }
    QPluginLoader loader("plugins/vehicle/libtrailcommand.so");
    auto *plugin = qobject_cast<VehiclePlugin *>(loader.instance());
    ok("plugin loads through QPluginLoader as a VehiclePlugin", plugin != nullptr, loader.errorString());
    if (!plugin) return 1;
    plugin->dashize(nullptr);
    ok("init succeeds", plugin->init(nullptr));
    QTabWidget tabs;
    QWidget *main = plugin->widgets().value(0);
    ok("one tab titled TrailCommand", plugin->widgets().size() == 1 && main && main->objectName() == "TrailCommand");
    tabs.addTab(main, main->objectName());
    tabs.resize(820, 440);
    tabs.show();
    wait(300);

    if (offline) {
        printf("OFFLINE hub (configured, nothing answering)\n");
        ok("control bar shows the hub cannot be reached", waitFor([&] { return visibleByName(&fakeDash, "^Can't reach the hub at 127.0.0.1"); }, 8000));
        ok("no Wi-Fi or cell icon while the hub is down", !visibleByName(&fakeDash, "^Wi-Fi|^Cellular"));
        ok("the old header banner is gone", byName(main, "^Can't reach the hub") == nullptr);
        shot(&tabs, "offline");
        shot(&fakeDash, "controlbar_offline");
        QTimer::singleShot(400, [&] {
            QWidget *dlg = QApplication::activeModalWidget();
            if (!dlg) return;
            auto *scan = button(dlg, "Scan network");
            scan->click();
            auto *status = dlg->findChildren<QLabel *>().last();
            waitFor([&] { return status->text().startsWith("No hub found"); }, 30000);
            ok("scan with no hub reports that nothing was found", status->text().startsWith("No hub found"), status->text());
            ok("result list stays empty", dlg->findChild<QListWidget *>()->count() == 0);
            shot(dlg, "dialog_nohub");
            dlg->close();
        });
        QMetaObject::invokeMethod(main, "showConnectionDialog", Qt::DirectConnection, Q_ARG(bool, false));
        printf("RESULT: %d passed, %d failed\n", pass, fail);
        return fail ? 1 : 0;
    }

    printf("FIRST RUN\n");
    ok("welcome screen offers Find my hub", button(main, "Find my hub") != nullptr);
    shot(&tabs, "welcome");

    printf("SCAN AND CONNECT\n");
    int found = -1;
    QString firstItem;
    QTimer::singleShot(400, [&] {
        QWidget *dlg = QApplication::activeModalWidget();
        auto *list = dlg->findChild<QListWidget *>();
        auto *bar = dlg->findChild<QProgressBar *>();
        waitFor([&] { return list->count() >= 1; }, 20000);
        shot(dlg, "dialog_scanning");
        waitFor([&] { return bar->value() >= bar->maximum() && bar->maximum() > 1; }, 40000);  // let the whole sweep finish
        found = list->count();
        firstItem = list->count() ? list->item(0)->text() : QString();
        shot(dlg, "dialog_found");
        auto *host = dlg->findChildren<QLineEdit *>().first();
        ok("the first hub found fills in the address", host->text() == "127.0.0.1", "host field is [" + host->text() + "], list: " + (list->count() ? list->item(0)->text() : QString("empty")));
        button(dlg, "Test connection")->click();
        const QList<QLabel *> lab = dlg->findChildren<QLabel *>();
        waitFor([&] { for (QLabel *l : lab) if (l->text().startsWith("Connected to TrailCommand")) return true; return false; }, 6000);
        bool tested = false;
        for (QLabel *l : lab) if (l->text().startsWith("Connected to TrailCommand")) tested = true;
        ok("Test connection recognises the hub", tested);
        button(dlg, "Connect")->click();
    });
    QMetaObject::invokeMethod(main, "showConnectionDialog", Qt::DirectConnection, Q_ARG(bool, true));
    ok("scan found the hub exactly once (loopback and LAN address are the same hub)", found == 1, QString("found=%1 %2").arg(found).arg(firstItem));
    ok("hub is listed with its hostname and version", firstItem.contains("v0.1.0") && firstItem.contains("127.0.0.1:" + port), firstItem);
    ok("link goes live", waitFor([&] { return visibleByName(&fakeDash, "^TrailCommand hub: connected to 127\\.0\\.0\\.1"); }, 10000));
    QSettings saved("tc-test", "plugins");
    ok("connection is remembered for next start", saved.value("Vehicle/TrailCommand/host").toString() == "127.0.0.1" && saved.value("Vehicle/TrailCommand/port").toInt() == port.toInt());

    printf("OVERVIEW\n");
    wait(2500);
    ok("loads total shown", waitFor([&] { return byName(main, "^[0-9.]+ A total$"); }, 5000));
    ok("battery stats shown", byName(main, "^1[0-4]\\.[0-9] V$") != nullptr);
    ok("Wi-Fi icon in the control bar", waitFor([&] { return visibleByName(&fakeDash, "^Wi-Fi: .*-[0-9]+ dBm$"); }, 5000));
    ok("cell icon in the control bar", visibleByName(&fakeDash, "^Cellular: .*LTE, -[0-9]+ dBm$") != nullptr);
    ok("nothing left in the tab header but the settings button", byName(main, "^Wi-Fi|^Cellular|^TrailCommand hub") == nullptr);
    {
        QWidget *clock = visibleByName(&fakeDash, "^[A-Z][a-z]+day, [A-Z][a-z]+ [0-9]+, 20[0-9][0-9], ");
        ok("date and time shown in the control bar", clock != nullptr, clock ? clock->accessibleName() : QString());
        QWidget *hubIcon = visibleByName(&fakeDash, "^TrailCommand hub");
        ok("the clock is the first icon", clock && hubIcon && clock->x() < hubIcon->x());
        QAbstractButton *gear = nullptr;
        for (QAbstractButton *b : main->findChildren<QAbstractButton *>()) if (b->toolTip() == "Hub connection") gear = b;
        QAbstractButton *first = navButton(main, "Overview"), *last = navButton(main, "Power");
        ok("settings button sits in the tab row", gear && first && last && gear->parentWidget() == first->parentWidget() && gear->x() > last->x());
        ok("and on the same line as the tabs", gear && first && qAbs(gear->mapTo(main, gear->rect().center()).y() - first->mapTo(main, first->rect().center()).y()) <= 3);
    }
    {
        QWidget *bar = fakeDash.findChild<QWidget *>("ControlBar");
        QWidget *box = bar->findChild<QWidget *>("TrailCommandStatus");
        auto *lay = qobject_cast<QBoxLayout *>(bar->layout());
        ok("icons sit between the stretch and the power button", box && lay->indexOf(box) == 2 && lay->itemAt(1)->spacerItem() && qobject_cast<QPushButton *>(lay->itemAt(3)->widget()));
        QWidget *hub = visibleByName(&fakeDash, "^TrailCommand hub");
        QWidget *wifi = visibleByName(&fakeDash, "^Wi-Fi");
        ok("icons are laid out left to right: hub, Wi-Fi, cell", hub && wifi && visibleByName(&fakeDash, "^Cellular") && hub->x() < wifi->x() && wifi->x() < visibleByName(&fakeDash, "^Cellular")->x());
        shot(&fakeDash, "controlbar");
        bool opened = false;
        QTimer::singleShot(400, [&] {
            QWidget *dlg = QApplication::activeModalWidget();
            opened = dlg != nullptr;
            if (dlg) dlg->close();
        });
        QMouseEvent rel(QEvent::MouseButtonRelease, QPointF(5, 5), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        if (hub) QApplication::sendEvent(hub, &rel);
        ok("tapping the hub icon opens the connection dialog", opened);
    }
    ok("alerts list present", byName(main, "tire pressure is low|Near fuse limit|overdue|due soon|Nothing needs attention") != nullptr);
    shot(&tabs, "overview");

    printf("CONTROLS\n");
    QMetaObject::invokeMethod(main, "switchTo", Q_ARG(int, 1));
    wait(500);
    auto tiles = allByName(main, "(on|off) [0-9.]+ A$");
    ok("ten control tiles", tiles.size() == 10, QString::number(tiles.size()));
    http("POST", "/api/controls/all-off");
    wait(1500);
    QWidget *compressor = byName(main, "^Air compressor off");
    ok("Air compressor tile starts off", compressor != nullptr);
    if (compressor) static_cast<QAbstractButton *>(compressor)->click();
    ok("tapping a tile switches the control on the hub", waitFor([&] { for (auto v : http("GET", "/api/controls").array()) if (v.toObject().value("id").toInt() == 5) return v.toObject().value("on").toBool(); return false; }, 4000));
    ok("tile shows it on with live amps", waitFor([&] { return byName(main, "^Air compressor on [0-9.]+ A$"); }, 4000));
    shot(&tabs, "controls");
    button(main, "All off")->click();
    ok("All off turns everything off", waitFor([&] { for (auto v : http("GET", "/api/controls").array()) if (v.toObject().value("on").toBool()) return false; return true; }, 4000));
    http("POST", QString("/api/controls/%1/state").arg(1), QJsonObject{{"on", true}});
    http("POST", QString("/api/controls/%1/state").arg(2), QJsonObject{{"on", true}});
    http("POST", QString("/api/controls/%1/state").arg(3), QJsonObject{{"on", true}});
    http("POST", QString("/api/controls/%1/state").arg(7), QJsonObject{{"on", true}});

    printf("HISTORY\n");
    QMetaObject::invokeMethod(main, "switchTo", Q_ARG(int, 2));
    wait(1200);
    shot(&tabs, "history_24h");
    navButton(main, "7 days")->click();
    wait(1200);
    shot(&tabs, "history_7d");
    ok("history range buttons respond", navButton(main, "7 days")->isChecked() && !navButton(main, "24 h")->isChecked());

    printf("TIRES\n");
    QMetaObject::invokeMethod(main, "switchTo", Q_ARG(int, 3));
    wait(700);
    shot(&tabs, "tires");

    printf("SERVICE\n");
    QMetaObject::invokeMethod(main, "switchTo", Q_ARG(int, 4));
    wait(1000);
    shot(&tabs, "service");
    QTimer::singleShot(300, [&] {
        QWidget *dlg = QApplication::activeModalWidget();
        shot(dlg, "service_log");
        dlg->findChildren<QLineEdit *>().first()->setText("90000");
        button(dlg, "Save")->click();
    });
    button(main, "Log service")->click();
    ok("Log service writes the odometer reading to the hub", waitFor([&] { return http("GET", "/api/maintenance").object().value("items").toArray().at(0).toObject().value("last_miles").toInt() == 90000; }, 4000));

    printf("POWER FLOW\n");
    QMetaObject::invokeMethod(main, "switchTo", Q_ARG(int, 5));
    wait(1000);
    const QImage a = tabs.grab().toImage();
    wait(260);
    const QImage b = tabs.grab().toImage();
    ok("the power-flow dashes are moving", a != b);
    shot(&tabs, "power");

    printf("PIN\n");
    http("PUT", "/api/settings", QJsonObject{{"pin", "4821"}});
    pinHeader = "4821";
    QMetaObject::invokeMethod(main, "switchTo", Q_ARG(int, 1));
    wait(800);
    QWidget *tile = byName(main, "^Cab fan");
    const bool wasOn = tile->accessibleName().contains(" on ");
    QTimer::singleShot(400, [&] {
        auto *dlg = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
        ok("a PIN prompt appears when the hub wants one", dlg != nullptr);
        if (!dlg) return;
        shot(dlg, "pin_prompt");
        dlg->setTextValue("4821");
        dlg->accept();
    });
    static_cast<QAbstractButton *>(tile)->click();
    ok("the change goes through after entering the PIN", waitFor([&] { for (auto v : http("GET", "/api/controls").array()) if (v.toObject().value("id").toInt() == 7) return v.toObject().value("on").toBool() != wasOn; return false; }, 5000));
    http("PUT", "/api/settings", QJsonObject{{"pin", ""}});
    pinHeader.clear();

    printf("RESULT: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
