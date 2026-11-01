// ozark-lightd: the lighting controller's serial link, served on D-Bus.
#include <QCoreApplication>
#include <QDebug>

#include "lightd.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("ozark-lightd");

    Lighting lighting;
    if (!lighting.start()) {
        qCritical() << "Could not register org.ozark.Lighting1 on the system bus";
        return 1;
    }
    return app.exec();
}
