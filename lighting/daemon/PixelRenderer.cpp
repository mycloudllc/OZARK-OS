#include "PixelRenderer.h"

#include <QRandomGenerator>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
constexpr qint64 kTestStepMs = 1000;
constexpr qint64 kMusicTimeoutMs = 1000;
constexpr int kBands = 8;
constexpr int kPatterns = 11;
constexpr double kFirmwareFrameMs = 16;  // the firmware's frame time, which its fades count in

enum Pattern { Off, Solid, Rainbow, Chase, Breathe, Gradient, Twinkle, Comet, Vu, Spectrum, Beat };

int lerp(int a, int b, int t)  // t 0..256
{
    return a + ((b - a) * t >> 8);
}

// Adafruit_NeoPixel::sine8: 0..255 in, 0..255 out
quint8 sine8(quint8 x)
{
    return quint8(std::lround(127.5 + 127.5 * std::sin((x - 64) * 2 * M_PI / 256)));
}

// Adafruit_NeoPixel::ColorHSV at full saturation and value: hue 0..65535
void hsv(quint16 hue, int& r, int& g, int& b)
{
    const int h = (hue * 1530L + 32768) / 65536;
    if (h < 510) {
        b = 0;
        if (h < 255) { r = 255; g = h; } else { r = 510 - h; g = 255; }
    } else if (h < 1020) {
        r = 0;
        if (h < 765) { g = 255; b = h - 510; } else { g = 1020 - h; b = 255; }
    } else if (h < 1530) {
        g = 0;
        if (h < 1275) { r = h - 1020; b = 255; } else { r = 255; b = 1530 - h; }
    } else {
        r = 255; g = 0; b = 0;
    }
}
}  // namespace

// 0..65535 cycle position; speed 0 runs at slowest_ms per cycle, 255 at fastest_ms
quint16 PixelRenderer::phase(qint64 now, int speed, quint32 slowest_ms, quint32 fastest_ms) const
{
    const quint32 period = slowest_ms - (slowest_ms - fastest_ms) * quint32(speed) / 255;
    return quint16((quint64(now % period) << 16) / period);
}

void PixelRenderer::set(quint8* rgb, int i, int r, int g, int b, int scale) const
{
    rgb[3 * i] = quint8(r * scale >> 8);
    rgb[3 * i + 1] = quint8(g * scale >> 8);
    rgb[3 * i + 2] = quint8(b * scale >> 8);
}

void PixelRenderer::render(qint64 now, const Look& l, const Music& music, int max_bri, quint8* rgb, int n)
{
    if (n <= 0) return;
    std::memset(rgb, 0, size_t(n) * 3);
    const int scale = l.brightness * max_bri / 255 + 1;  // 1..256
    const int r = l.color.red(), g = l.color.green(), b = l.color.blue();
    const int r2 = l.color2.red(), g2 = l.color2.green(), b2 = l.color2.blue();

    if (m_testStart) {
        const qint64 step = (now - m_testStart) / kTestStepMs;
        if (step >= 3) {
            m_testStart = 0;
        } else {
            // Fill from the start over each second: shows the order and direction
            const int lit = int(n * ((now - m_testStart) % kTestStepMs) / kTestStepMs) + 1;
            for (int i = 0; i < n; i++)
                set(rgb, i, step == 0 && i < lit ? 255 : 0, step == 1 && i < lit ? 255 : 0,
                    step == 2 && i < lit ? 255 : 0, 128);
            return;
        }
    }

    const bool music_live = now - music.last_ms < kMusicTimeoutMs;
    // Firmware frames since the last render (fades and twinkles are per frame there)
    const double frames = m_lastMs < 0 ? 1 : qBound(1.0, (now - m_lastMs) / kFirmwareFrameMs, 30.0);
    m_lastMs = now;
    if (int(m_twinkle.size()) != n) m_twinkle.assign(size_t(n), 0);
    QRandomGenerator* random = QRandomGenerator::global();

    switch (qBound(0, l.pattern, kPatterns - 1)) {
        case Solid:
            for (int i = 0; i < n; i++) set(rgb, i, r, g, b, scale);
            break;

        case Rainbow: {
            const quint16 start = phase(now, l.speed, 20000, 1000);
            for (int i = 0; i < n; i++) {
                int cr, cg, cb;
                hsv(quint16(start + quint32(i) * 65536 / n), cr, cg, cb);
                set(rgb, i, cr, cg, cb, scale);
            }
            break;
        }

        case Chase: {
            // Groups of 3 lit, 3 in colour 2, moving along
            const int offset = phase(now, l.speed, 6000, 300) * 6 >> 16;
            for (int i = 0; i < n; i++) {
                const bool lit = (i + offset) % 6 < 3;
                set(rgb, i, lit ? r : r2, lit ? g : g2, lit ? b : b2, lit ? scale : scale / 6);
            }
            break;
        }

        case Breathe: {
            const quint16 p = phase(now, l.speed, 8000, 800);
            const int level = (sine8(quint8(p >> 8)) >> 1) + 8;  // never fully dark
            for (int i = 0; i < n; i++) set(rgb, i, r, g, b, scale * level >> 8);
            break;
        }

        case Gradient: {
            // Colour to colour 2 and back along the strip, drifting with speed
            const int shift = l.speed ? (phase(now, l.speed, 60000, 2000) * n >> 16) : 0;
            const int half = n / 2 ? n / 2 : 1;
            for (int i = 0; i < n; i++) {
                const int pos = (i + shift) % n;
                const int t = pos < half ? pos * 256 / half : (n - pos) * 256 / half;
                set(rgb, i, lerp(r, r2, t), lerp(g, g2, t), lerp(b, b2, t), scale);
            }
            break;
        }

        case Twinkle: {
            // Random LEDs flare in colour 2 over a dim colour, then fade
            const int fade = int(std::lround((2 + l.speed / 16) * frames));
            const int spawn = int(std::lround((1 + l.speed / 32) * frames));
            for (quint8& t : m_twinkle) t = t > fade ? quint8(t - fade) : 0;
            for (int s = 0; s < spawn; s++) {
                if (random->bounded(100) < 30) m_twinkle[size_t(random->bounded(n))] = 255;
            }
            for (int i = 0; i < n; i++) {
                const int t = m_twinkle[size_t(i)] + 1;
                set(rgb, i, lerp(r / 4, r2, t), lerp(g / 4, g2, t), lerp(b / 4, b2, t), scale);
            }
            break;
        }

        case Comet: {
            // A head in colour with a tail fading into colour 2
            const int head = phase(now, l.speed, 8000, 500) * n >> 16;
            const int tail = std::max(4, n / 5);
            for (int i = 0; i < n; i++) {
                const int behind = (head - i + n) % n;
                if (behind > tail) continue;
                const int t = behind * 256 / tail;
                set(rgb, i, lerp(r, r2, t), lerp(g, g2, t), lerp(b, b2, t), scale * (256 - t) >> 8);
            }
            break;
        }

        case Vu: {
            // Bar from the start: colour at the bottom, colour 2 at the top
            int sum = 0;
            for (quint8 band : music.bands) sum += band;
            const int level = music_live ? sum / kBands : 0;
            const int lit = n * level / 255;
            for (int i = 0; i < lit && i < n; i++) {
                const int t = i * 256 / n;
                set(rgb, i, lerp(r, r2, t), lerp(g, g2, t), lerp(b, b2, t), scale);
            }
            break;
        }

        case Spectrum: {
            // Eight segments, low to high, each as bright as its band, hues across the strip
            for (int i = 0; i < n; i++) {
                const int band = i * kBands / n;
                const int level = music_live ? music.bands[size_t(band)] : 0;
                int cr, cg, cb;
                hsv(quint16(quint32(band) * 65536 / kBands), cr, cg, cb);
                set(rgb, i, cr, cg, cb, scale * level >> 8);
            }
            break;
        }

        case Beat: {
            // Colour at a low glow; each beat flashes colour 2 and fades back
            if (music_live && music.beat) m_pulse = 255;
            const int t = m_pulse + 1;
            for (int i = 0; i < n; i++)
                set(rgb, i, lerp(r / 5, r2, t), lerp(g / 5, g2, t), lerp(b / 5, b2, t), scale);
            const int fade = int(std::lround((6 + l.speed / 12) * frames));
            m_pulse = m_pulse > fade ? m_pulse - fade : 0;
            break;
        }

        case Off:
        default:
            break;
    }
}
