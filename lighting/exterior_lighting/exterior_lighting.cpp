#include "lighting_widget.hpp"

#include "exterior_lighting.hpp"

QList<QWidget *> ExteriorLighting::widgets()
{
    // One instance only: a second load request returns no extra tab
    if (!this->loaded_widgets.isEmpty())
        return QList<QWidget *>{};

    this->loaded_widgets.append(new LightingWidget(*this->arbiter, "Exterior Lighting", "E"));
    return this->loaded_widgets;
}

void ExteriorLighting::remove_widget(int)
{
    // the launcher already deleted the tab widget
    this->loaded_widgets.clear();
}
