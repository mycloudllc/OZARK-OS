#pragma once
// The six pages of the dashboard: Overview, Controls, History, Tires, Service and Power.
#include <QButtonGroup>
#include <QDateEdit>
#include <QGridLayout>
#include <QJsonArray>
#include <QPointer>
#include <QPushButton>
#include <QTimer>

#include "client.hpp"
#include "gfx.hpp"
#include "widgets.hpp"

namespace tc {

class Page : public QWidget {
   public:
    explicit Page(Client *c, QWidget *parent = nullptr) : QWidget(parent), client(c) {}
    virtual void onState(const QJsonObject &) {}
    virtual void onSettings() {}
    virtual void onMaintenance(const QJsonObject &) {}
    virtual void activate() {}
    virtual void deactivate() {}

   protected:
    Client *client;
};

Card *makeCard(QWidget *parent, QLayout *layout);

class OverviewPage : public Page {
   public:
    explicit OverviewPage(Client *c, QWidget *parent = nullptr);
    void onState(const QJsonObject &s) override;
    void onSettings() override;
    void onMaintenance(const QJsonObject &m) override { maint_ = m; if (!client->state().isEmpty()) renderAlerts(client->state()); }

   private:
    void renderAlerts(const QJsonObject &s);
    Card *houseCard_, *starterCard_, *alertsCard_;
    Text *houseTitle_, *starterTitle_, *loadsTotal_, *loadsInfo_;
    BatteryWidget *houseBat_, *starterBat_;
    Stat *hVolts_, *hTemp_, *sVolts_, *sState_, *sTemp_, *solar_, *draw_, *current_, *left_;
    MeterBar *loadsBar_;
    QVBoxLayout *alerts_;
    QJsonObject maint_;
};

class ControlTile : public QAbstractButton {
   public:
    explicit ControlTile(int id, QWidget *parent = nullptr);
    int id() const { return id_; }
    bool isOn() const { return on_; }
    void setData(const QString &name, bool on, double amps, double fuse, int pct, const QString &band, bool near);
    void setPending(bool p) { pending_ = p; update(); }
    bool pending() const { return pending_; }
    QSize sizeHint() const override;

   protected:
    void paintEvent(QPaintEvent *) override;

   private:
    int id_;
    QString name_, band_ = "green";
    bool on_ = false, near_ = false, pending_ = false;
    double amps_ = 0, fuse_ = 0;
    int pct_ = 0;
};

class ControlsPage : public Page {
   public:
    explicit ControlsPage(Client *c, QWidget *parent = nullptr);
    void onState(const QJsonObject &s) override;

   protected:
    void resizeEvent(QResizeEvent *) override { relayout(); }

   private:
    void relayout();
    Text *total_, *totalUnit_;
    QGridLayout *grid_;
    QList<ControlTile *> tiles_;
    int cols_ = 0;
};

class HistoryPage : public Page {
   public:
    explicit HistoryPage(Client *c, QWidget *parent = nullptr);
    void activate() override;
    void deactivate() override { timer_.stop(); }
    void onSettings() override { if (isVisible()) load(); }

   private:
    void load();
    QString range_ = "24h";
    Text *title_;
    LineChart *chart_;
    DayBars *days_;
    QTimer timer_;
};

class TiresPage : public Page {
   public:
    explicit TiresPage(Client *c, QWidget *parent = nullptr);
    void onState(const QJsonObject &s) override;
    void onSettings() override;

   private:
    Card *vehicleCard_, *trailerCard_;
    RigWidget *vehicle_, *trailer_;
};

class ServicePage : public Page {
   public:
    explicit ServicePage(Client *c, QWidget *parent = nullptr);
    void activate() override;
    void deactivate() override { timer_.stop(); }
    void onSettings() override { render(); }
    void load();
    void openLog(int preselect = 0);
    QJsonObject data() const { return data_; }
    std::function<void(const QJsonObject &)> loaded;

   private:
    void render();
    Text *odo_, *odoUnit_;
    QVBoxLayout *list_;
    QJsonObject data_;
    QTimer timer_;
};

class PowerPage : public Page {
   public:
    explicit PowerPage(Client *c, QWidget *parent = nullptr);
    void onState(const QJsonObject &s) override;
    void onSettings() override { if (!client->state().isEmpty()) onState(client->state()); }

   private:
    Stat *in_, *out_, *net_;
    FlowWidget *flow_;
};

}  // namespace tc
