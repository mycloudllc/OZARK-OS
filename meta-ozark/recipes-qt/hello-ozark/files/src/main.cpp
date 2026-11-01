// hello-ozark — Ozark OS milestone 3 test app.
//
// Proves the whole graphics path works end to end: KMS/DRM display, GPU
// (EGLFS), fonts (Barlow Condensed via fontconfig), touch/mouse input
// (libinput) and the systemd service that starts it after the splash.
//
// SPDX-License-Identifier: MIT

#include <QApplication>
#include <QDateTime>
#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QScreen>
#include <QSysInfo>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace {

// Ozark brand palette (matches branding/ and psplash-colors.h)
constexpr auto kRidge = "#142A1D";  // background
constexpr auto kCream = "#F1EBDD";  // primary text
constexpr auto kTrail = "#C8602B";  // accent
constexpr auto kMoss  = "#9DB8A6";  // secondary text

QString uptimeText()
{
    QFile f(QStringLiteral("/proc/uptime"));
    if (!f.open(QIODevice::ReadOnly))
        return QStringLiteral("—");
    const qint64 secs = static_cast<qint64>(f.readAll().split(' ').value(0).toDouble());
    return QStringLiteral("%1:%2:%3")
        .arg(secs / 3600)
        .arg((secs / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(secs % 60, 2, 10, QLatin1Char('0'));
}

QFont brandFont(int pixelSize, QFont::Weight weight, qreal letterSpacing = 0)
{
    QFont f(QStringLiteral("Barlow Condensed"));
    f.setPixelSize(pixelSize);
    f.setWeight(weight);
    f.setLetterSpacing(QFont::AbsoluteSpacing, letterSpacing);
    return f;
}

QLabel *makeLabel(const QString &text, const QFont &font, const char *color, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setFont(font);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet(QStringLiteral("color: %1;").arg(QLatin1String(color)));
    return label;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("hello-ozark"));
    QApplication::setOverrideCursor(Qt::BlankCursor);

    // Everything is sized relative to a 480 px tall screen (the official 7"
    // display), so the layout holds up on 1080p HDMI panels too.
    const QSize screen = QGuiApplication::primaryScreen()->size();
    const qreal scale = qMax<qreal>(0.6, screen.height() / 480.0);
    const auto px = [scale](qreal v) { return qRound(v * scale); };

    QWidget window;
    window.setObjectName(QStringLiteral("root"));
    window.setStyleSheet(QStringLiteral("#root { background: %1; }").arg(QLatin1String(kRidge)));

    auto *layout = new QVBoxLayout(&window);
    layout->setContentsMargins(px(24), px(16), px(24), px(12));
    layout->setSpacing(px(4));

    // Badge
    auto *badge = new QLabel(&window);
    badge->setAlignment(Qt::AlignCenter);
    badge->setPixmap(QPixmap(QStringLiteral(":/ozark-badge.png"))
                         .scaled(px(128), px(128), Qt::KeepAspectRatio, Qt::SmoothTransformation));

    // Wordmark
    auto *title = makeLabel(QStringLiteral("OZARK MOUNTAIN"),
                            brandFont(px(44), QFont::Bold, px(1.5)), kCream, &window);
    auto *subtitle = makeLabel(QStringLiteral("TECHNOLOGIES"),
                               brandFont(px(22), QFont::DemiBold, px(7)), kTrail, &window);
    auto *hello = makeLabel(QStringLiteral("Hello, world — Ozark OS is up."),
                            brandFont(px(22), QFont::Medium), kMoss, &window);

    // Touch test: every tap bumps the counter.
    auto *tapButton = new QPushButton(QStringLiteral("TAP TO TEST TOUCH"), &window);
    tapButton->setFont(brandFont(px(22), QFont::Bold, px(2)));
    tapButton->setMinimumHeight(px(56));
    tapButton->setMinimumWidth(px(320));
    tapButton->setFocusPolicy(Qt::NoFocus);
    tapButton->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; color: %2; border: none; border-radius: %3px; padding: 0 %4px; }"
        "QPushButton:pressed { background: #A84E22; }")
        .arg(QLatin1String(kTrail), QLatin1String(kRidge))
        .arg(px(8))
        .arg(px(24)));

    int taps = 0;
    QObject::connect(tapButton, &QPushButton::clicked, tapButton, [tapButton, &taps] {
        ++taps;
        tapButton->setText(QStringLiteral("%1 TAP%2 — TOUCH WORKS")
                               .arg(taps)
                               .arg(taps == 1 ? QString() : QStringLiteral("S")));
    });

    auto *buttonRow = new QHBoxLayout;
    buttonRow->addStretch();
    buttonRow->addWidget(tapButton);
    buttonRow->addStretch();

    // Status line: clock, uptime, resolution, versions
    auto *status = makeLabel(QString(), brandFont(px(15), QFont::Medium, px(0.5)), kMoss, &window);
    const QString staticInfo = QStringLiteral("%1×%2  ·  Qt %3  ·  %4")
                                   .arg(screen.width())
                                   .arg(screen.height())
                                   .arg(QLatin1String(qVersion()), QSysInfo::machineHostName());
    const auto refresh = [status, staticInfo] {
        status->setText(QStringLiteral("%1  ·  up %2  ·  %3")
                            .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")),
                                 uptimeText(), staticInfo));
    };
    refresh();
    QTimer clock;
    QObject::connect(&clock, &QTimer::timeout, status, refresh);
    clock.start(1000);

    layout->addStretch(2);
    layout->addWidget(badge);
    layout->addSpacing(px(8));
    layout->addWidget(title);
    layout->addWidget(subtitle);
    layout->addSpacing(px(10));
    layout->addWidget(hello);
    layout->addStretch(1);
    layout->addLayout(buttonRow);
    layout->addStretch(2);
    layout->addWidget(status);

    // EGLFS is always fullscreen; the explicit geometry also makes desktop
    // test runs (X11 without a window manager) fill the screen.
    window.setGeometry(QGuiApplication::primaryScreen()->geometry());
    window.showFullScreen();
    return app.exec();
}
