#pragma once

#include <QtWidgets>

#include "plugins/launcher_plugin.hpp"

// Exterior Lighting: launcher tab for the exterior strip (zone E).
class ExteriorLighting : public QObject, LauncherPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID LauncherPlugin_iid FILE "exterior_lighting.json")
    Q_INTERFACES(LauncherPlugin)

   public:
    ExteriorLighting() { this->settings.beginGroup("ExteriorLighting"); }
    QList<QWidget *> widgets() override;
    void remove_widget(int idx) override;
};
