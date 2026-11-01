#pragma once

#include <QtWidgets>

#include "plugins/launcher_plugin.hpp"

// Interior Lighting: launcher tab for the interior front and rear strips (zones F, R).
class InteriorLighting : public QObject, LauncherPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID LauncherPlugin_iid FILE "interior_lighting.json")
    Q_INTERFACES(LauncherPlugin)

   public:
    InteriorLighting() { this->settings.beginGroup("InteriorLighting"); }
    QList<QWidget *> widgets() override;
    void remove_widget(int idx) override;
};
