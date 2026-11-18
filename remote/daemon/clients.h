#pragma once

#include <QDateTime>
#include <QMap>
#include <QString>
#include <QVariantMap>

// Phones allowed to control the radio. Each got a random token when it was
// paired (the code on the radio's screen); only a hash of the token is kept,
// in /etc/ozark/remote/clients.json (the data partition: survives updates).
class ClientStore {
   public:
    struct Client {
        QString id;        // short random id, shown and used to revoke
        QString name;      // what the phone called itself ("Pixel 8")
        QString platform;  // "android", "ios", ...
        QByteArray token_hash;
        QDateTime created;
        QDateTime last_seen;
    };

    explicit ClientStore(const QString &path);

    // A new client; returns the token to hand to the phone (only time it exists in clear)
    QString add(const QString &name, const QString &platform, QString *id);
    // The client the token belongs to, or an empty id
    QString find(const QString &token) const;
    void touch(const QString &id);
    bool remove(const QString &id);
    const QMap<QString, Client> &all() const { return this->clients; }
    QVariantMap describe(const QString &id) const;

   private:
    QString path;
    QMap<QString, Client> clients;

    void load();
    void save() const;
    static QByteArray hash(const QString &token);
};
