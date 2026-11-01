#pragma once
// Hub connection settings: address, port, PIN, and a scan that finds hubs on the network.
#include <QCheckBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QNetworkAccessManager>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>

#include "client.hpp"
#include "scanner.hpp"

namespace tc {

class ConnectionDialog : public QDialog {
    Q_OBJECT

   public:
    ConnectionDialog(const Hub &current, bool rememberPin, bool scanNow, QWidget *parent = nullptr);
    Hub hub() const;
    bool rememberPin() const { return remember_->isChecked(); }

   private slots:
    void startScan();
    void testConnection();

   private:
    void addFound(const FoundHub &f);
    void setStatus(const QString &text, bool bad = false);

    QLineEdit *host_, *pin_;
    QSpinBox *port_;
    QCheckBox *https_, *trust_, *remember_;
    QPushButton *scanBtn_, *testBtn_;
    QProgressBar *progress_;
    QListWidget *results_;
    QLabel *status_, *scanStatus_;
    Scanner *scanner_;
    QNetworkAccessManager nam_;
    QMap<QString, QListWidgetItem *> byId_;
};

}  // namespace tc
