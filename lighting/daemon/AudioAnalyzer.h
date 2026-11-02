#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>
#include <array>
#include <complex>
#include <vector>

#include "Controller.h"

// Captures whatever the head unit is playing (Bluetooth, Android Auto, local
// files...) by reading the default output's monitor source through `parec`,
// then emits frames at ~43 Hz: 8 and 16 log-spaced band levels (0-255), a beat
// flag, the volume and the loudest frequency.
//
// Needs: pulseaudio-utils (parec). Works with PipeWire via pipewire-pulse.
// Emits from the object's own thread, so connect with a direct/auto connection.
class AudioAnalyzer : public QObject {
    Q_OBJECT
public:
    explicit AudioAnalyzer(QObject* parent = nullptr);
    ~AudioAnalyzer() override;

    // Default "@DEFAULT_MONITOR@". Or a name from `pactl list short sources`.
    void setSource(const QString& source);
    void setSensitivity(float gain);  // 0.25 - 4.0, default 1.0
    void start();
    void stop();
    bool isRunning() const { return m_wanted; }

signals:
    void frame(const AudioFrame& frame);

private:
    void launch();
    void scheduleRestart();
    void onReadyRead();
    void process();

    static constexpr int kRate = 22050;
    static constexpr int kFft = 1024;
    static constexpr int kHop = 512;

    QProcess m_proc;
    QTimer m_restart;
    QString m_source = "@DEFAULT_MONITOR@";
    float m_gain = 1.0f;
    bool m_wanted = false;

    QByteArray m_rem;
    std::vector<float> m_samples;
    std::vector<float> m_window;
    std::vector<std::complex<float>> m_buf;

    float m_gpeak = 0.0f;
    float m_bassAvg = 0.0f;
    float m_smoothVolume = 0.0f;
    QElapsedTimer m_beatTimer;
};
