#include "AudioAnalyzer.h"

#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
constexpr float kPi = 3.14159265358979f;

// Tuning knobs
constexpr float kNoiseFloor = 30.0f;     // ignore magnitudes below this
constexpr float kPeakDecay = 0.997f;     // auto-gain release per hop
constexpr float kCurve = 0.8f;           // <1 lifts quiet parts
constexpr float kBeatRatio = 1.35f;      // bass must exceed average by this
constexpr int kBeatMinMs = 250;
const float kEdgesHz[9] = {40, 80, 160, 320, 640, 1280, 2560, 5120, 10240};
const float kTilt[8] = {1.0f, 1.0f, 1.2f, 1.5f, 2.0f, 2.5f, 3.0f, 4.0f};  // lift highs

void fft(std::complex<float>* a, int n) {
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (int len = 2; len <= n; len <<= 1) {
        const float ang = -2.0f * kPi / len;
        const std::complex<float> wl(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (int j = 0; j < len / 2; ++j) {
                const std::complex<float> u = a[i + j];
                const std::complex<float> v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}
}  // namespace

AudioAnalyzer::AudioAnalyzer(QObject* parent) : QObject(parent) {
    m_window.resize(kFft);
    for (int i = 0; i < kFft; ++i)
        m_window[i] = 0.5f * (1.0f - std::cos(2.0f * kPi * i / (kFft - 1)));
    m_buf.resize(kFft);
    m_beatTimer.start();

    connect(&m_proc, &QProcess::readyReadStandardOutput, this, &AudioAnalyzer::onReadyRead);
    connect(&m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int, QProcess::ExitStatus) { scheduleRestart(); });
    connect(&m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) scheduleRestart();
    });

    m_restart.setSingleShot(true);
    m_restart.setInterval(2000);
    connect(&m_restart, &QTimer::timeout, this, &AudioAnalyzer::launch);
}

AudioAnalyzer::~AudioAnalyzer() { stop(); }

void AudioAnalyzer::setSource(const QString& source) {
    m_source = source.isEmpty() ? QStringLiteral("@DEFAULT_MONITOR@") : source;
    if (m_wanted) {  // restart capture on the new source
        m_proc.terminate();
    }
}

void AudioAnalyzer::setSensitivity(float gain) { m_gain = qBound(0.25f, gain, 4.0f); }

void AudioAnalyzer::start() {
    m_wanted = true;
    launch();
}

void AudioAnalyzer::stop() {
    m_wanted = false;
    m_restart.stop();
    if (m_proc.state() != QProcess::NotRunning) {
        m_proc.terminate();
        if (!m_proc.waitForFinished(500)) m_proc.kill();
    }
}

void AudioAnalyzer::scheduleRestart() {
    if (m_wanted) m_restart.start();
}

void AudioAnalyzer::launch() {
    if (!m_wanted || m_proc.state() != QProcess::NotRunning) return;

    m_rem.clear();
    m_samples.clear();
    m_gpeak = 0.0f;

    m_proc.start("parec", {"-d", m_source, "--format=s16le", "--rate=" + QString::number(kRate),
                           "--channels=1", "--latency-msec=20", "--raw",
                           "--client-name=lighting", "--stream-name=lighting-monitor"});
}

void AudioAnalyzer::onReadyRead() {
    QByteArray data = m_rem + m_proc.readAllStandardOutput();
    const int count = data.size() / 2;
    const char* p = data.constData();

    m_samples.reserve(m_samples.size() + count);
    for (int i = 0; i < count; ++i)
        m_samples.push_back(qFromLittleEndian<qint16>(p + 2 * i) / 32768.0f);
    m_rem = data.mid(count * 2);

    // If we fell behind, drop old audio instead of adding latency
    if (m_samples.size() > size_t(kFft) * 8)
        m_samples.erase(m_samples.begin(), m_samples.end() - kFft * 2);

    while (m_samples.size() >= size_t(kFft)) process();
}

void AudioAnalyzer::process() {
    for (int i = 0; i < kFft; ++i) m_buf[i] = {m_samples[i] * m_window[i], 0.0f};
    fft(m_buf.data(), kFft);
    m_samples.erase(m_samples.begin(), m_samples.begin() + kHop);

    const float binHz = float(kRate) / kFft;
    float band[8];
    float mx = 0.0f;
    for (int k = 0; k < 8; ++k) {
        const int lo = std::max(1, int(kEdgesHz[k] / binHz));
        const int hi = std::max(lo + 1, int(kEdgesHz[k + 1] / binHz));
        float sum = 0.0f;
        for (int b = lo; b < hi; ++b) sum += std::abs(m_buf[b]);
        band[k] = (sum / (hi - lo)) * kTilt[k];
        mx = std::max(mx, band[k]);
    }

    m_gpeak = std::max(mx, m_gpeak * kPeakDecay);
    const float denom = std::max(m_gpeak, kNoiseFloor);

    std::array<quint8, 8> out{};
    for (int k = 0; k < 8; ++k) {
        const float v = 255.0f * std::pow(band[k] / denom, kCurve) * m_gain;
        out[k] = quint8(qBound(0.0f, v, 255.0f));
    }

    const float bass = (out[0] + out[1]) * 0.5f;
    m_bassAvg = 0.95f * m_bassAvg + 0.05f * bass;
    bool beat = false;
    if (bass > m_bassAvg * kBeatRatio + 10.0f && bass > 60.0f && m_beatTimer.elapsed() > kBeatMinMs) {
        beat = true;
        m_beatTimer.restart();
    }

    emit frame(out, beat);
}
