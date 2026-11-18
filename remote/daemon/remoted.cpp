#include "remoted.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QSysInfo>

#include "bridge.h"

namespace {
// How long the code stays on the radio's screen, and how many tries a phone gets
constexpr int kPairSeconds = 120;
constexpr int kPairAttempts = 5;
// A new pairing at most this often (each one puts a code on the screen)
constexpr int kPairEverySeconds = 5;
// Bigger messages are refused (nothing in the protocol comes close)
constexpr int kMaxMessage = 64 * 1024;

// Methods a phone may use before it has said hello
const QStringList kOpen = {"hello", "pair.request", "pair.confirm", "info", "ping"};

QJsonValue to_json(const QVariant &value)
{
    if (value.type() == QVariant::ByteArray)
        return QString::fromLatin1(value.toByteArray().toBase64());
    return QJsonValue::fromVariant(value);
}

QString log_name(Session *s)
{
    return QString("%1 %2").arg(s->transport, s->peer);
}
}  // namespace

Remote::Remote(const QString &store_path, QObject *parent)
    : QObject(parent)
    , store(store_path)
    , bridge(new Bridge(this))
{
    connect(this->bridge, &Bridge::event, this, &Remote::broadcast);
    this->pairing_timer.setSingleShot(true);
    connect(&this->pairing_timer, &QTimer::timeout, this, [this] {
        qInfo().noquote() << "[pair] Code expired";
        this->end_pairing(false);
    });
}

void Remote::add(Session *session)
{
    this->sessions.append(session);
    qInfo().noquote() << "[session] Connected:" << log_name(session);
    connect(session, &Session::received, this, [this, session](const QByteArray &m) { this->handle(session, m); });
    connect(session, &Session::closed, this, [this, session] {
        qInfo().noquote() << "[session] Closed:" << log_name(session);
        this->sessions.removeAll(session);
        if (this->pairing.session == session)
            this->end_pairing(false);
        session->deleteLater();
        emit StatusChanged();
    });
    emit StatusChanged();
}

void Remote::set_transport_state(const QString &transport, const QString &state)
{
    if (this->transports.value(transport).toString() == state)
        return;
    this->transports[transport] = state;
    qInfo().noquote() << QString("[%1]").arg(transport) << state;
    emit StatusChanged();
}

void Remote::handle(Session *session, const QByteArray &message)
{
    if (message.size() > kMaxMessage) {
        this->error(session, QJsonValue(), "too_big", "Message too big");
        return;
    }
    QJsonParseError parse;
    const QJsonDocument doc = QJsonDocument::fromJson(message, &parse);
    if (!doc.isObject()) {
        this->error(session, QJsonValue(), "bad_message", "Not a JSON object: " + parse.errorString());
        return;
    }
    const QJsonObject request = doc.object();
    const QJsonValue id = request.value("id");
    const QString method = request.value("method").toString();
    const QVariantMap params = request.value("params").toObject().toVariantMap();
    if (method.isEmpty()) {
        this->error(session, id, "bad_message", "No method");
        return;
    }

    if (session->client_id.isEmpty() && !kOpen.contains(method)) {
        this->error(session, id, "unauthorized", "Say hello with your token, or pair first");
        return;
    }

    // ---- Always open
    if (method == "ping") {
        this->reply(session, id, QVariantMap{{"pong", true}});
        return;
    }
    if (method == "info") {
        this->reply(session, id, QVariantMap{{"name", QSysInfo::machineHostName()},
                                             {"protocol", kProtocol},
                                             {"product", "Ozark OS"}});
        return;
    }

    // ---- Pairing: the radio shows a code, the phone sends it back, gets a token
    if (method == "pair.request") {
        const QDateTime now = QDateTime::currentDateTimeUtc();
        if (this->pairing.session && this->pairing.session != session) {
            this->error(session, id, "busy", "Another phone is pairing; try again in a moment");
            return;
        }
        if (this->last_pair_request.isValid() && this->last_pair_request.secsTo(now) < kPairEverySeconds &&
            this->pairing.session != session) {
            this->error(session, id, "busy", "Wait a few seconds and try again");
            return;
        }
        this->last_pair_request = now;
        this->pairing.session = session;
        this->pairing.name = params.value("name").toString().left(64).trimmed();
        if (this->pairing.name.isEmpty())
            this->pairing.name = "Phone";
        this->pairing.platform = params.value("platform").toString().left(16);
        this->pairing.code = QString("%1").arg(QRandomGenerator::system()->bounded(1000000), 6, 10, QChar('0'));
        this->pairing.attempts = 0;
        this->pairing_timer.start(kPairSeconds * 1000);
        qInfo().noquote() << "[pair] Code on screen for" << this->pairing.name << "(" << log_name(session) << ")";
        emit PairingStarted(this->pairing.code, this->pairing.name, kPairSeconds);
        this->reply(session, id, QVariantMap{{"expires_in", kPairSeconds}, {"digits", 6}});
        return;
    }
    if (method == "pair.confirm") {
        if (this->pairing.session != session || this->pairing.code.isEmpty()) {
            this->error(session, id, "no_pairing", "Ask for a code first (pair.request)");
            return;
        }
        const QString code = params.value("code").toString().remove(' ');
        if (code != this->pairing.code) {
            const int left = kPairAttempts - ++this->pairing.attempts;
            if (left <= 0) {
                this->end_pairing(false);
                this->error(session, id, "wrong_code", "Wrong code too many times; start again");
            }
            else {
                this->error(session, id, "wrong_code", QString("Wrong code, %1 tries left").arg(left));
            }
            return;
        }
        QString client_id;
        const QString token = this->store.add(this->pairing.name, this->pairing.platform, &client_id);
        session->client_id = client_id;
        qInfo().noquote() << "[pair] Paired" << this->pairing.name << "as" << client_id;
        this->end_pairing(true);
        emit ClientsChanged();
        emit StatusChanged();
        QVariantMap result = this->hello_result(session);
        result["token"] = token;
        this->reply(session, id, result);
        return;
    }
    if (method == "hello") {
        const QString client = this->store.find(params.value("token").toString());
        if (client.isEmpty()) {
            this->error(session, id, "unauthorized", "Unknown token: pair again");
            return;
        }
        session->client_id = client;
        this->store.touch(client);
        qInfo().noquote() << "[session]" << log_name(session) << "is" << this->store.all().value(client).name;
        emit StatusChanged();
        this->reply(session, id, this->hello_result(session));
        return;
    }

    // ---- Phones the radio knows
    if (method == "clients.list") {
        QVariantList list;
        for (const QVariant &c : this->ListClients())
            list << c;
        this->reply(session, id, QVariantMap{{"clients", list}, {"you", session->client_id}});
        return;
    }
    if (method == "clients.revoke") {
        const QString target = params.value("id").toString();
        if (!this->store.all().contains(target)) {
            this->error(session, id, "bad_params", "No such phone");
            return;
        }
        this->reply(session, id, QVariantMap{{"ok", true}});
        this->RevokeClient(target);
        return;
    }

    // ---- Everything else: the radio's services
    if (!Bridge::methods().contains(method)) {
        this->error(session, id, "unknown_method", QString("No method %1").arg(method));
        return;
    }
    QPointer<Session> guard(session);
    this->bridge->call(method, params,
        [this, guard, id](const QVariant &result) {
            if (guard)
                this->reply(guard, id, result);
        },
        [this, guard, id](const QString &code, const QString &message) {
            if (guard)
                this->error(guard, id, code, message);
        });
}

QVariantMap Remote::hello_result(Session *session) const
{
    QVariantList methods;
    for (const QString &m : Bridge::methods())
        methods << m;
    methods << "clients.list" << "clients.revoke";
    return {{"client_id", session->client_id},
            {"name", QSysInfo::machineHostName()},
            {"protocol", kProtocol},
            {"methods", methods}};
}

void Remote::reply(Session *session, const QJsonValue &id, const QVariant &result)
{
    QJsonObject o{{"id", id}, {"result", to_json(result)}};
    session->send(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

void Remote::error(Session *session, const QJsonValue &id, const QString &code, const QString &message)
{
    QJsonObject o{{"id", id}, {"error", QJsonObject{{"code", code}, {"message", message}}}};
    session->send(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

void Remote::broadcast(const QString &event, const QVariant &data)
{
    const QByteArray message = QJsonDocument(QJsonObject{{"event", event}, {"data", to_json(data)}})
                                   .toJson(QJsonDocument::Compact);
    for (const auto &s : this->sessions)
        if (s && !s->client_id.isEmpty())
            s->send(message);
}

void Remote::end_pairing(bool paired)
{
    if (this->pairing.code.isEmpty())
        return;
    const QString name = this->pairing.name;
    this->pairing = Pairing();
    this->pairing_timer.stop();
    emit PairingEnded(name, paired);
}

// ---------------------------------------------------------------- D-Bus

QVariantMap Remote::GetStatus() const
{
    QVariantList sessions;
    for (const auto &s : this->sessions) {
        if (!s)
            continue;
        sessions << QVariantMap{{"transport", s->transport},
                                {"peer", s->peer},
                                {"client", s->client_id},
                                {"name", this->store.all().value(s->client_id).name}};
    }
    return {{"transports", this->transports},
            {"sessions", sessions},
            {"pairing", !this->pairing.code.isEmpty()},
            {"protocol", kProtocol}};
}

QVariantMap Remote::ListClients() const
{
    QVariantMap out;
    for (const auto &c : this->store.all()) {
        QVariantMap d = this->store.describe(c.id);
        bool connected = false;
        for (const auto &s : this->sessions)
            connected |= s && s->client_id == c.id;
        d["connected"] = connected;
        out.insert(c.id, d);
    }
    return out;
}

bool Remote::RevokeClient(const QString &id)
{
    if (!this->store.remove(id))
        return false;
    qInfo().noquote() << "[clients] Revoked" << id;
    // Its open connections end now
    for (const auto &s : this->sessions) {
        if (s && s->client_id == id) {
            s->client_id.clear();
            s->close();
        }
    }
    emit ClientsChanged();
    emit StatusChanged();
    return true;
}

void Remote::CancelPairing()
{
    this->end_pairing(false);
}
