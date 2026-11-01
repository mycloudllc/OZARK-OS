#include "lighting_widget.hpp"

#include "interior_lighting.hpp"

QList<QWidget *> InteriorLighting::widgets()
{
    // One instance only: a second load request returns no extra tab
    if (!this->loaded_widgets.isEmpty())
        return QList<QWidget *>{};

    this->loaded_widgets.append(new LightingWidget(*this->arbiter, "Interior Lighting", "FR"));
    return this->loaded_widgets;
}

void InteriorLighting::remove_widget(int)
{
    // the launcher already deleted the tab widget
    this->loaded_widgets.clear();
}
