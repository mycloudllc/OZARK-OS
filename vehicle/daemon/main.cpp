// ozark-vehicled: the car (OBD-II, illumination input, tire sensors), served
// on D-Bus as org.ozark.Vehicle1 and forwarded to a TrailCommand hub.
#include <QCoreApplication>
#include <QDebug>

#include "vehicled.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("ozark-vehicled");

    Vehicle vehicle;
    if (!vehicle.start()) {
        qCritical() << "Could not register org.ozark.Vehicle1 on the system bus";
        return 1;
    }
    return app.exec();
}
