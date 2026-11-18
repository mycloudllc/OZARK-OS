#include "clients.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSaveFile>

namespace {
QString random_text(int bytes)
{
    QByteArray data(bytes, Qt::Uninitialized);
    QRandomGenerator *rng = QRandomGenerator::system();  // the kernel's CSPRNG
    for (int i = 0; i < bytes; i++)
        data[i] = char(rng->bounded(256));
    return QString::fromLatin1(data.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}
}  // namespace

ClientStore::ClientStore(const QString &path)
    : path(path)
{
    this->load();
}

QByteArray ClientStore::hash(const QString &token)
{
    return QCryptographicHash::hash(token.toUtf8(), QCryptographicHash::Sha256).toHex();
}

QString ClientStore::add(const QString &name, const QString &platform, QString *id)
{
    Client client;
    do {
        client.id = random_text(6);
    } while (this->clients.contains(client.id));
    client.name = name.left(64).trimmed();
    if (client.name.isEmpty())
        client.name = "Phone";
    client.platform = platform.left(16);
    const QString token = random_text(32);
    client.token_hash = hash(token);
    client.created = client.last_seen = QDateTime::currentDateTimeUtc();
    this->clients.insert(client.id, client);
    this->save();
    if (id)
        *id = client.id;
    return token;
}

QString ClientStore::find(const QString &token) const
{
    if (token.isEmpty())
        return QString();
    const QByteArray h = hash(token);
    for (const Client &c : this->clients) {
        // Constant-time compare of two equal-length hex strings
        if (c.token_hash.size() != h.size())
            continue;
        int diff = 0;
        for (int i = 0; i < h.size(); i++)
            diff |= c.token_hash[i] ^ h[i];
        if (diff == 0)
            return c.id;
    }
    return QString();
}

void ClientStore::touch(const QString &id)
{
    auto it = this->clients.find(id);
    if (it == this->clients.end())
        return;
    // Saved at most every 10 minutes: the card shouldn't be written on every connect
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const bool save = it->last_seen.secsTo(now) > 600;
    it->last_seen = now;
    if (save)
        this->save();
}

bool ClientStore::remove(const QString &id)
{
    if (!this->clients.remove(id))
        return false;
    this->save();
    return true;
}

QVariantMap ClientStore::describe(const QString &id) const
{
    const Client c = this->clients.value(id);
    return {
        {"id", c.id},
        {"name", c.name},
        {"platform", c.platform},
        {"created", c.created.toString(Qt::ISODate)},
        {"last_seen", c.last_seen.toString(Qt::ISODate)},
    };
}

void ClientStore::load()
{
    QFile file(this->path);
    if (!file.open(QFile::ReadOnly))
        return;
    const QJsonArray list = QJsonDocument::fromJson(file.readAll()).object().value("clients").toArray();
    for (const QJsonValue &v : list) {
        const QJsonObject o = v.toObject();
        Client c;
        c.id = o.value("id").toString();
        c.name = o.value("name").toString();
        c.platform = o.value("platform").toString();
        c.token_hash = o.value("token_sha256").toString().toLatin1();
        c.created = QDateTime::fromString(o.value("created").toString(), Qt::ISODate);
        c.last_seen = QDateTime::fromString(o.value("last_seen").toString(), Qt::ISODate);
        if (!c.id.isEmpty() && c.token_hash.size() == 64)
            this->clients.insert(c.id, c);
    }
}

void ClientStore::save() const
{
    QJsonArray list;
    for (const Client &c : this->clients) {
        list.append(QJsonObject{
            {"id", c.id},
            {"name", c.name},
            {"platform", c.platform},
            {"token_sha256", QString::fromLatin1(c.token_hash)},
            {"created", c.created.toString(Qt::ISODate)},
            {"last_seen", c.last_seen.toString(Qt::ISODate)},
        });
    }
    QDir().mkpath(QFileInfo(this->path).absolutePath());
    QSaveFile file(this->path);
    if (!file.open(QFile::WriteOnly))
        return;
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    file.write(QJsonDocument(QJsonObject{{"clients", list}}).toJson());
    file.commit();
}
