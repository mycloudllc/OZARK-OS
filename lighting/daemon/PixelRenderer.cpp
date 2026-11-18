#include "PixelRenderer.h"

#include <QRandomGenerator>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
constexpr qint64 kTestStepMs = 1000;
constexpr qint64 kMusicTimeoutMs = 1000;
constexpr int kBands = 8;
constexpr double kFirmwareFrameMs = 16;  // the firmware's frame time, which its fades count in

// The firmware's pattern ids (../PROTOCOL.md)
enum Pattern {
    Off, Solid, Rainbow, Chase, Breathe, Gradient, Twinkle, Comet, Vu, Spectrum, Beat,
    Fire, Sparkle, Scanner, Wipe, Wave, Fade, Strobe, Theater, RainbowRunner, PatternCount
};

struct Rgb {
    int r = 0, g = 0, b = 0;
};

int lerp(int a, int b, int t)  // t 0..256
{
    return a + ((b - a) * t >> 8);
}

Rgb mix(const Rgb& a, const Rgb& b, int t)
{
    return {lerp(a.r, b.r, t), lerp(a.g, b.g, t), lerp(a.b, b.b, t)};
}

Rgb rgb(const QColor& c)
{
    return {c.red(), c.green(), c.blue()};
}

// 0..255 level as a 0..256 blend amount (255 reaches the far colour)
int amount(int level)
{
    return level + (level >> 7);
}

// Adafruit_NeoPixel::sine8: 0..255 in, 0..255 out
quint8 sine8(quint8 x)
{
    return quint8(std::lround(127.5 + 127.5 * std::sin((x - 64) * 2 * M_PI / 256)));
}

// Adafruit_NeoPixel::ColorHSV at full saturation and value: hue 0..65535
Rgb hsv(quint16 hue)
{
    const int h = (hue * 1530L + 32768) / 65536;
    Rgb c;
    if (h < 510) {
        c.b = 0;
        if (h < 255) { c.r = 255; c.g = h; } else { c.r = 510 - h; c.g = 255; }
    } else if (h < 1020) {
        c.r = 0;
        if (h < 765) { c.g = 255; c.b = h - 510; } else { c.g = 1020 - h; c.b = 255; }
    } else if (h < 1530) {
        c.g = 0;
        if (h < 1275) { c.r = h - 1020; c.b = 255; } else { c.r = 255; c.b = 1530 - h; }
    } else {
        c.r = 255;
    }
    return c;
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

// The firmware's patterns (../firmware/src/zone.cpp), LED for LED
void PixelRenderer::render(qint64 now, const Look& l, const Music& music, int max_bri, quint8* out, int n)
{
    if (n <= 0) return;
    std::memset(out, 0, size_t(n) * 3);
    const int scale = l.brightness * max_bri / 255 + 1;  // 1..256
    auto put = [&](int i, const Rgb& c, int s) { set(out, i, c.r, c.g, c.b, s); };

    if (m_testStart) {
        const qint64 step = (now - m_testStart) / kTestStepMs;
        if (step >= 3) {
            m_testStart = 0;
        } else {
            // Fill from the start over each second: shows the order and direction
            const int lit = int(n * ((now - m_testStart) % kTestStepMs) / kTestStepMs) + 1;
            for (int i = 0; i < n; i++)
                set(out, i, step == 0 && i < lit ? 255 : 0, step == 1 && i < lit ? 255 : 0,
                    step == 2 && i < lit ? 255 : 0, 128);
            return;
        }
    }

    const int pattern = qBound(0, l.pattern, PatternCount - 1);
    if (int(m_level.size()) != n || pattern != m_lastPattern) {
        // A new pattern (or strip length) starts from a clean per-LED state
        m_level.assign(size_t(n), 0);
        m_pulse = 0;
        m_lastPos = -1;
        m_lastPattern = pattern;
    }

    const bool music_live = now - music.last_ms < kMusicTimeoutMs;
    // Firmware frames since the last render (fades and twinkles are per frame there)
    const double frames = m_lastMs < 0 ? 1 : qBound(1.0, (now - m_lastMs) / kFirmwareFrameMs, 30.0);
    m_lastMs = now;
    QRandomGenerator* random = QRandomGenerator::global();
    const Rgb c1 = rgb(l.color), c2 = rgb(l.color2), bg = rgb(l.background);
    auto fadeAll = [this](int fade) {
        for (quint8& v : m_level) v = v > fade ? quint8(v - fade) : 0;
    };

    switch (pattern) {
        case Solid:
            for (int i = 0; i < n; i++) put(i, c1, scale);
            break;

        case Rainbow: {
            const quint16 start = phase(now, l.speed, 20000, 1000);
            for (int i = 0; i < n; i++) put(i, hsv(quint16(start + quint32(i) * 65536 / n)), scale);
            break;
        }

        case Chase: {
            // Groups of 3 lit, 3 in colour 2, moving along
            const int offset = phase(now, l.speed, 6000, 300) * 6 >> 16;
            for (int i = 0; i < n; i++) {
                const bool lit = (i + offset) % 6 < 3;
                put(i, lit ? c1 : c2, lit ? scale : scale / 6);
            }
            break;
        }

        case Breathe: {
            // Background to the full colour and back
            const int t = amount(sine8(quint8(phase(now, l.speed, 8000, 800) >> 8)));
            for (int i = 0; i < n; i++) put(i, mix(bg, c1, t), scale);
            break;
        }

        case Gradient: {
            // Colour to colour 2 and back along the strip, drifting with speed
            const int shift = l.speed ? (phase(now, l.speed, 60000, 2000) * n >> 16) : 0;
            const int half = n / 2 ? n / 2 : 1;
            for (int i = 0; i < n; i++) {
                const int pos = (i + shift) % n;
                const int t = pos < half ? pos * 256 / half : (n - pos) * 256 / half;
                put(i, mix(c1, c2, t), scale);
            }
            break;
        }

        case Twinkle: {
            // Random LEDs flare in colour 2 over a dim colour, then fade
            fadeAll(int(std::lround((2 + l.speed / 16) * frames)));
            const int spawn = int(std::lround((1 + l.speed / 32) * frames));
            for (int s = 0; s < spawn; s++) {
                if (random->bounded(100) < 30) m_level[size_t(random->bounded(n))] = 255;
            }
            const Rgb dim{c1.r / 4, c1.g / 4, c1.b / 4};
            for (int i = 0; i < n; i++) put(i, mix(dim, c2, m_level[size_t(i)] + 1), scale);
            break;
        }

        case Comet: {
            // A head in colour with a tail fading through colour 2 into the background
            const int head = phase(now, l.speed, 8000, 500) * n >> 16;
            const int tail = std::max(4, n / 5);
            for (int i = 0; i < n; i++) {
                const int behind = (head - i + n) % n;
                if (behind > tail) {
                    put(i, bg, scale);
                    continue;
                }
                const int t = behind * 256 / tail;
                put(i, mix(mix(c1, c2, t), bg, t), scale);
            }
            break;
        }

        case Vu: {
            // Bar from the start: colour at the bottom, colour 2 at the top
            int sum = 0;
            for (quint8 band : music.bands) sum += band;
            const int level = music_live ? sum / kBands : 0;
            const int lit = n * level / 255;
            for (int i = 0; i < n; i++) put(i, i < lit ? mix(c1, c2, i * 256 / n) : bg, scale);
            break;
        }

        case Spectrum: {
            // Eight segments, low to high, each as bright as its band, hues across the strip
            for (int i = 0; i < n; i++) {
                const int band = i * kBands / n;
                const int level = music_live ? music.bands[size_t(band)] : 0;
                put(i, mix(bg, hsv(quint16(quint32(band) * 65536 / kBands)), amount(level)), scale);
            }
            break;
        }

        case Beat: {
            // Colour at a low glow; each beat flashes colour 2 and fades back
            if (music_live && music.beat) m_pulse = 255;
            const Rgb dim{c1.r / 5, c1.g / 5, c1.b / 5};
            for (int i = 0; i < n; i++) put(i, mix(dim, c2, m_pulse + 1), scale);
            const int fade = int(std::lround((6 + l.speed / 12) * frames));
            m_pulse = m_pulse > fade ? m_pulse - fade : 0;
            break;
        }

        case Fire: {
            // Fire2012: heat cools, rises and sparks at the start of the strip.
            // Background (cold) -> colour 2 (embers) -> colour (hottest).
            const int cooling = 30 + l.speed / 4;
            const int sparking = 60 + l.speed / 3;
            const int steps = qBound(1, int(std::lround(frames)), 4);
            for (int s = 0; s < steps; s++) {
                for (quint8& h : m_level) {
                    const int cool = random->bounded(cooling * 10 / n + 2);
                    h = h > cool ? quint8(h - cool) : 0;
                }
                for (int k = n - 1; k >= 2; k--)
                    m_level[size_t(k)] = quint8((m_level[size_t(k - 1)] + m_level[size_t(k - 2)] * 2) / 3);
                if (random->bounded(255) < sparking) {
                    const size_t y = size_t(random->bounded(std::min(7, n)));
                    m_level[y] = quint8(std::min(255, m_level[y] + random->bounded(160, 255)));
                }
            }
            for (int i = 0; i < n; i++) {
                const int h = m_level[size_t(i)];
                put(i, h < 128 ? mix(bg, c2, h * 2) : mix(c2, c1, (h - 128) * 2), scale);
            }
            break;
        }

        case Sparkle: {
            // Quick glints of colour 2 over the background
            fadeAll(int(std::lround(48 * frames)));
            const int spawn = int(std::lround((1 + l.speed / 64) * frames));
            for (int s = 0; s < spawn; s++) {
                if (random->bounded(100) < 20 + l.speed / 4) m_level[size_t(random->bounded(n))] = 255;
            }
            for (int i = 0; i < n; i++) put(i, mix(bg, c2, amount(m_level[size_t(i)])), scale);
            break;
        }

        case Scanner: {
            // A dot sweeping end to end and back, leaving a fading trail
            const quint32 p = phase(now, l.speed, 6000, 600);
            const quint32 tri = p < 32768 ? p * 2 : (65535 - p) * 2;
            const int pos = int(tri * quint32(n - 1) / 65535);
            fadeAll(int(std::lround((10 + l.speed / 12) * frames)));
            // Light every LED the dot passed since the last frame: no gaps when it's fast
            const int from = m_lastPos < 0 ? pos : m_lastPos;
            for (int i = std::min(from, pos); i <= std::max(from, pos); i++) m_level[size_t(i)] = 255;
            m_lastPos = pos;
            for (int i = 0; i < n; i++) put(i, mix(bg, c1, amount(m_level[size_t(i)])), scale);
            break;
        }

        case Wipe: {
            // Colour fills the strip end to end, then colour 2, again and again
            const quint16 p = phase(now, l.speed, 8000, 800);
            const bool second = p >= 32768;
            const int filled = int(quint32(p & 0x7FFF) * quint32(n) / 32768);
            for (int i = 0; i < n; i++) put(i, (i < filled) != second ? c1 : c2, scale);
            break;
        }

        case Wave: {
            // Bands of colour rolling along over the background, three on the strip
            const quint8 shift = quint8(phase(now, l.speed, 8000, 800) >> 8);
            for (int i = 0; i < n; i++) {
                const quint8 x = quint8(quint32(i) * 3 * 256 / quint32(n) - shift);
                put(i, mix(bg, c1, amount(sine8(x))), scale);
            }
            break;
        }

        case Fade: {
            // The whole strip blends colour -> colour 2 -> colour
            const Rgb c = mix(c1, c2, amount(sine8(quint8(phase(now, l.speed, 10000, 1000) >> 8))));
            for (int i = 0; i < n; i++) put(i, c, scale);
            break;
        }

        case Strobe: {
            // Short flashes of colour over the background, faster with speed
            const qint64 period = 1000 - (1000 - 80) * l.speed / 255;
            const bool lit = now % period < std::min<qint64>(40, period / 2);
            for (int i = 0; i < n; i++) put(i, lit ? c1 : bg, scale);
            break;
        }

        case Theater: {
            // Every third LED lit, stepping along (marquee lights)
            const int offset = phase(now, l.speed, 3000, 150) * 3 >> 16;
            for (int i = 0; i < n; i++) put(i, (i + 3 - offset) % 3 == 0 ? c1 : bg, scale);
            break;
        }

        case RainbowRunner: {
            // A short rainbow running along over the background
            const int head = phase(now, l.speed, 8000, 800) * n >> 16;
            const int band = std::max(6, n / 4);
            for (int i = 0; i < n; i++) {
                const int behind = (head - i + n) % n;
                put(i, behind < band ? hsv(quint16(quint32(behind) * 65536 / band)) : bg, scale);
            }
            break;
        }

        case Off:
        default:
            break;
    }
}
