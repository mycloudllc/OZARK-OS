#pragma once
// Finds TrailCommand hubs on the local network: tries each address of every connected IPv4
// subnet (and this machine) for the hub's /api/health reply.
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

namespace tc {

struct FoundHub {
    QString ip;
    int port = 0;
    QString version, id, hostname;
    int build = 0;
    QString label() const { return QString("%1:%2").arg(ip).arg(port); }
};

class Scanner : public QObject {
    Q_OBJECT

   public:
    explicit Scanner(QObject *parent = nullptr) : QObject(parent) {}

    void start(const QList<int> &ports);
    void cancel();
    bool running() const { return running_; }

    // every address worth trying, own addresses first (the hub may be on this very machine)
    static QStringList candidateAddresses(int maxHosts = 1024);

   signals:
    void progress(int done, int total);
    void found(const tc::FoundHub &hub);
    void finished(bool cancelled);

   private:
    void pump();
    void probe(const QString &ip, int port);
    void probeDone();

    struct Target {
        QString ip;
        int port;
    };
    QList<Target> targets_;
    QStringList seen_;
    int next_ = 0, done_ = 0, active_ = 0, generation_ = 0;
    bool running_ = false;
};

}  // namespace tc

Q_DECLARE_METATYPE(tc::FoundHub)
