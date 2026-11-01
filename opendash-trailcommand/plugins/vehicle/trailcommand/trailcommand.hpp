#pragma once

#include <QObject>
#include <QPointer>

#include "plugins/vehicle_plugin.hpp"

namespace tc { class Main; }

// OpenDash vehicle plugin: adds a "TrailCommand" tab to the Vehicle page.
class TrailCommand : public QObject, VehiclePlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID VehiclePlugin_iid FILE "trailcommand.json")
    Q_INTERFACES(VehiclePlugin)

   public:
    TrailCommand() = default;
    ~TrailCommand();
    QList<QWidget *> widgets() override;
    bool init(ICANBus *) override;

   private:
    QPointer<tc::Main> main_;  // the Vehicle page also owns the tab, so only delete it if it is still alive
};
