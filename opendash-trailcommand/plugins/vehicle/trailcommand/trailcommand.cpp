#include "trailcommand.hpp"

#include "main_widget.hpp"

TrailCommand::~TrailCommand()
{
    delete main_;  // also removes the tab from the Vehicle page
}

QList<QWidget *> TrailCommand::widgets()
{
    QList<QWidget *> tabs;
    if (main_) tabs.append(main_);
    return tabs;
}

bool TrailCommand::init(ICANBus *)
{
    if (main_) return true;
    main_ = new tc::Main;

    // saved connection (Dash keeps plugin settings in its own settings file)
    tc::Hub hub;
    hub.host = settings.value("TrailCommand/host").toString();
    hub.port = settings.value("TrailCommand/port", 8080).toInt();
    hub.https = settings.value("TrailCommand/https", false).toBool();
    hub.trustCertificate = settings.value("TrailCommand/trust", false).toBool();
    main_->rememberPin = settings.value("TrailCommand/remember_pin", false).toBool();
    if (main_->rememberPin) hub.pin = settings.value("TrailCommand/pin").toString();

    main_->persist = [this](const tc::Hub &h, bool remember) {
        settings.setValue("TrailCommand/host", h.host);
        settings.setValue("TrailCommand/port", h.port);
        settings.setValue("TrailCommand/https", h.https);
        settings.setValue("TrailCommand/trust", h.trustCertificate);
        settings.setValue("TrailCommand/remember_pin", remember);
        if (remember) settings.setValue("TrailCommand/pin", h.pin);
        else settings.remove("TrailCommand/pin");
        settings.sync();
    };
    main_->client()->setHub(hub);
    return true;
}
