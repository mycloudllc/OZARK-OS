#pragma once
// The TrailCommand tab: settings button, page switcher and pages. Status icons go to Dash's control bar (statusbar.hpp).
#include <QStackedWidget>
#include <QTimer>
#include <functional>

#include "client.hpp"
#include "pages.hpp"
#include "statusbar.hpp"

namespace tc {


class Main : public QWidget {
    Q_OBJECT

   public:
    explicit Main(QWidget *parent = nullptr);
    Client *client() const { return client_; }
    // called after the user saves new connection settings
    std::function<void(const Hub &hub, bool rememberPin)> persist;
    bool rememberPin = false;

   public slots:
    void showConnectionDialog(bool scanNow = false);
    void switchTo(int index);

   protected:
    bool event(QEvent *e) override;

   private:
    void onLink();
    void onState(const QJsonObject &s);
    void updateSignals(const QJsonObject &net);
    void toast(const QString &text);
    void updateClock();

    Client *client_;
    QStackedWidget *outer_, *pages_;
    QList<Page *> list_;
    QList<NavButton *> nav_;
    Text *welcomeText_;
    StatusBar *bar_;
    QWidget *navRow_;
    ServicePage *service_;
    OverviewPage *overview_;
    QTimer maintTimer_, toastTimer_, clockTimer_;
    QString clockShown_;
    Text *toast_;
    int current_ = 0;
};

}  // namespace tc
