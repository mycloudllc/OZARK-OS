#include <QCoreApplication>
#include <QDebug>
#include <QTimer>
#include "scanner.hpp"
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    qRegisterMetaType<tc::FoundHub>();
    qDebug() << "candidates:" << tc::Scanner::candidateAddresses().mid(0, 6) << "count" << tc::Scanner::candidateAddresses().size();
    tc::Scanner s;
    QObject::connect(&s, &tc::Scanner::found, [](const tc::FoundHub &h) { qDebug() << "FOUND" << h.label() << h.version << h.id << h.hostname; });
    int last = -1;
    QObject::connect(&s, &tc::Scanner::progress, [&](int d, int t) { if (d / 50 != last) { last = d / 50; qDebug() << "progress" << d << t; } });
    QObject::connect(&s, &tc::Scanner::finished, [&](bool c) { qDebug() << "finished cancelled=" << c; app.quit(); });
    s.start({18800});
    QTimer::singleShot(60000, &app, &QCoreApplication::quit);
    return app.exec();
}
