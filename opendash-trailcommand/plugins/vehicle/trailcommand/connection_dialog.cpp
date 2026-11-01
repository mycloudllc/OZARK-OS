#include "connection_dialog.hpp"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QVBoxLayout>

namespace tc {

ConnectionDialog::ConnectionDialog(const Hub &current, bool rememberPin, bool scanNow, QWidget *parent) : QDialog(parent)
{
    setWindowTitle("TrailCommand hub");
    setStyleSheet("QLabel { border: none; background: transparent; padding: 0; }");  // Dash boxes plain labels; keep this dialog tidy
    scanner_ = new Scanner(this);

    host_ = new QLineEdit(current.host);
    host_->setPlaceholderText("192.168.1.50");
    port_ = new QSpinBox;
    port_->setRange(1, 65535);
    port_->setValue(current.port);
    https_ = new QCheckBox("Secure connection (HTTPS)");
    https_->setChecked(current.https);
    trust_ = new QCheckBox("Trust the hub's own certificate");
    trust_->setChecked(current.trustCertificate);
    trust_->setEnabled(current.https);
    connect(https_, &QCheckBox::toggled, trust_, &QWidget::setEnabled);
    pin_ = new QLineEdit(current.pin);
    pin_->setEchoMode(QLineEdit::Password);
    pin_->setPlaceholderText("Only if the hub asks for one");
    remember_ = new QCheckBox("Remember the PIN on this device");
    remember_->setChecked(rememberPin);
    testBtn_ = new QPushButton("Test connection");
    status_ = new QLabel;
    status_->setWordWrap(true);
    status_->setMinimumHeight(status_->fontMetrics().height() + 4);  // one line; grows when a message wraps

    auto *form = new QFormLayout;
    form->addRow("Address", host_);
    form->addRow("Port", port_);
    form->addRow(QString(), https_);
    form->addRow(QString(), trust_);
    form->addRow("PIN", pin_);
    form->addRow(QString(), remember_);
    auto *left = new QVBoxLayout;
    left->addLayout(form);
    left->addWidget(testBtn_, 0, Qt::AlignLeft);
    left->addWidget(status_);
    left->addStretch(1);

    scanBtn_ = new QPushButton("Scan network");
    scanBtn_->setMinimumWidth(scanBtn_->fontMetrics().horizontalAdvance("Scan network") + 36);  // it also says "Stop"
    progress_ = new QProgressBar;
    progress_->setTextVisible(false);
    progress_->setFixedHeight(8);
    progress_->setRange(0, 1);
    progress_->setValue(0);
    results_ = new QListWidget;
    results_->setMinimumHeight(110);
    scanStatus_ = new QLabel("Looks for TrailCommand hubs on this network.");
    scanStatus_->setWordWrap(true);
    scanStatus_->setMinimumHeight(scanStatus_->fontMetrics().height() * 2 + 4);
    auto *right = new QVBoxLayout;
    right->addWidget(scanBtn_, 0, Qt::AlignLeft);
    right->addWidget(progress_);
    right->addWidget(results_, 1);
    right->addWidget(scanStatus_);

    auto *cols = new QHBoxLayout;
    cols->addLayout(left, 1);
    cols->addSpacing(14);
    cols->addLayout(right, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    auto *root = new QVBoxLayout(this);
    root->addLayout(cols, 1);
    root->addWidget(buttons);
    buttons->button(QDialogButtonBox::Save)->setText("Connect");
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (host_->text().trimmed().isEmpty()) {
            setStatus("Enter the hub's address, or scan for it.", true);
            return;
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(scanBtn_, &QPushButton::clicked, this, &ConnectionDialog::startScan);
    connect(testBtn_, &QPushButton::clicked, this, &ConnectionDialog::testConnection);
    connect(scanner_, &Scanner::progress, this, [this](int done, int total) {
        progress_->setRange(0, qMax(1, total));
        progress_->setValue(done);
        scanStatus_->setText(QString("Scanning… %1 of %2 addresses").arg(done).arg(total));
    });
    connect(scanner_, &Scanner::found, this, [this](const FoundHub &f) { addFound(f); });
    connect(scanner_, &Scanner::finished, this, [this](bool cancelled) {
        scanBtn_->setText("Scan network");
        if (cancelled) return;
        progress_->setValue(progress_->maximum());
        scanStatus_->setText(results_->count() ? QString("Found %1 hub%2. Choose one to fill in the address.").arg(results_->count()).arg(results_->count() == 1 ? "" : "s")
                                               : "No hub found. Check that it is powered, on this network, and enter its address by hand.");
    });
    connect(results_, &QListWidget::itemClicked, this, [this](QListWidgetItem *it) {
        host_->setText(it->data(Qt::UserRole).toString());
        port_->setValue(it->data(Qt::UserRole + 1).toInt());
        https_->setChecked(false);
        setStatus(QString());
    });
    connect(results_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *it) {
        host_->setText(it->data(Qt::UserRole).toString());
        port_->setValue(it->data(Qt::UserRole + 1).toInt());
        https_->setChecked(false);
        accept();
    });
    setMinimumWidth(640);
    if (scanNow) QTimer::singleShot(0, this, &ConnectionDialog::startScan);
}

Hub ConnectionDialog::hub() const
{
    Hub h;
    h.host = host_->text().trimmed();
    h.port = port_->value();
    h.https = https_->isChecked();
    h.trustCertificate = trust_->isChecked();
    h.pin = pin_->text();
    return h;
}

void ConnectionDialog::setStatus(const QString &text, bool bad)
{
    status_->setText(text);
    status_->setStyleSheet(bad ? "color: #EE5A47;" : QString());
}

void ConnectionDialog::addFound(const FoundHub &f)
{
    // The same hub answers on 127.0.0.1 and on its LAN address when it runs on this machine: list it once,
    // preferring the loopback address (it keeps working with no network).
    const QString text = QString("%1\n%2%3").arg(f.hostname.isEmpty() ? QString("TrailCommand") : f.hostname, f.label(), f.version.isEmpty() ? QString() : ", v" + f.version);
    if (!f.id.isEmpty() && byId_.contains(f.id)) {
        QListWidgetItem *old = byId_[f.id];
        if (f.ip.startsWith("127.") && !old->data(Qt::UserRole).toString().startsWith("127.")) {
            old->setText(text);
            old->setData(Qt::UserRole, f.ip);
            old->setData(Qt::UserRole + 1, f.port);
            if (results_->count() == 1) { host_->setText(f.ip); port_->setValue(f.port); }
        }
        return;
    }
    auto *it = new QListWidgetItem(text, results_);
    it->setData(Qt::UserRole, f.ip);
    it->setData(Qt::UserRole + 1, f.port);
    if (!f.id.isEmpty()) byId_[f.id] = it;
    if (results_->count() == 1) {  // first hub found: fill it in so a single hub needs no extra tap
        host_->setText(f.ip);
        port_->setValue(f.port);
        https_->setChecked(false);
    }
}

void ConnectionDialog::startScan()
{
    if (scanner_->running()) {
        scanner_->cancel();
        scanStatus_->setText("Scan stopped.");
        return;
    }
    results_->clear();
    byId_.clear();
    scanBtn_->setText("Stop");
    scanStatus_->setText("Scanning…");
    QList<int> ports{8080, 80};
    if (!ports.contains(port_->value()) && !https_->isChecked()) ports.prepend(port_->value());
    scanner_->start(ports);
}

void ConnectionDialog::testConnection()
{
    const Hub h = hub();
    if (h.host.isEmpty()) {
        setStatus("Enter the hub's address first.", true);
        return;
    }
    const bool defaultPort = (h.https && h.port == 443) || (!h.https && h.port == 80);
    const QString url = QString("%1://%2%3/api/health").arg(h.https ? "https" : "http", h.host, defaultPort ? QString() : QString(":%1").arg(h.port));
    setStatus("Connecting…");
    testBtn_->setEnabled(false);
    QNetworkRequest req{QUrl(url)};
    QNetworkReply *reply = nam_.get(req);
    QTimer::singleShot(4000, reply, [reply] { if (reply->isRunning()) reply->abort(); });
    connect(reply, &QNetworkReply::sslErrors, reply, [this, reply](const QList<QSslError> &) {
        if (trust_->isChecked()) reply->ignoreSslErrors();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        testBtn_->setEnabled(true);
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        if (reply->error() == QNetworkReply::NoError && o.value("app").toString() == "trailcommand")
            setStatus(QString("Connected to TrailCommand %1 (build %2).").arg(o.value("version").toString()).arg(o.value("build").toInt()));
        else if (reply->error() == QNetworkReply::NoError)
            setStatus("Something answered, but it is not a TrailCommand hub.", true);
        else
            setStatus("Could not connect: " + reply->errorString(), true);
        reply->deleteLater();
    });
}

}  // namespace tc
