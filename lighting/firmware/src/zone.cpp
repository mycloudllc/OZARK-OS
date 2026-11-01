#include "zone.h"

const char *const kOrderNames[6] = {"RGB", "RBG", "GRB", "GBR", "BRG", "BGR"};

namespace {
constexpr neoPixelType kOrders[6] = {NEO_RGB, NEO_RBG, NEO_GRB, NEO_GBR, NEO_BRG, NEO_BGR};
constexpr neoPixelType kOrdersW[6] = {NEO_RGBW, NEO_RBGW, NEO_GRBW, NEO_GBRW, NEO_BRGW, NEO_BGRW};
constexpr uint32_t kTestStepMs = 1000;
constexpr uint32_t kMusicTimeoutMs = 1000;

neoPixelType strip_type(const StripConfig &c)
{
    return (c.rgbw ? kOrdersW[c.order] : kOrders[c.order]) | (c.khz400 ? NEO_KHZ400 : NEO_KHZ800);
}

uint8_t lerp(uint8_t a, uint8_t b, uint16_t t)  // t 0..256
{
    return a + ((int(b) - int(a)) * int(t) >> 8);
}
}  // namespace

Zone::Zone(char name, const StripConfig &config)
    : name(name)
    , config(config)
    , look{255, 0, 80, 0, 120, 255, PAT_SOLID, 128, 255}
    , strip(config.count, config.pin, strip_type(config))
{
    memset(twinkle, 0, sizeof(twinkle));
}

void Zone::apply(const StripConfig &c)
{
    // Blank the old strip before moving away from it
    if (started) {
        strip.clear();
        strip.show();
    }

    config = c;
    strip.updateType(strip_type(c));
    strip.updateLength(c.count);
    strip.setPin(c.pin);
    strip.begin();
    strip.clear();
    strip.show();
    started = true;
}

void Zone::test(uint32_t now)
{
    test_start = now ? now : 1;
}

// 0..65535 cycle position; speed 0 runs at slowest_ms per cycle, 255 at fastest_ms
uint16_t Zone::phase(uint32_t now, uint32_t slowest_ms, uint32_t fastest_ms) const
{
    const uint32_t period = slowest_ms - (slowest_ms - fastest_ms) * look.speed / 255;
    return uint16_t((uint64_t(now % period) << 16) / period);
}

// Colour for LED i (in pattern order), scaled by scale 0..256
void Zone::set(int i, uint8_t r, uint8_t g, uint8_t b, uint16_t scale)
{
    const int led = config.reverse ? config.count - 1 - i : i;
    r = r * scale >> 8;
    g = g * scale >> 8;
    b = b * scale >> 8;
    if (config.rgbw) {
        // Move the shared part of the three channels to the white LED
        const uint8_t w = min(r, min(g, b));
        strip.setPixelColor(led, r - w, g - w, b - w, w);
    }
    else {
        strip.setPixelColor(led, r, g, b);
    }
}

void Zone::render(uint32_t now, const Music &music, uint8_t max_bri)
{
    const int n = config.count;
    const uint16_t scale = (uint16_t(look.bri) * max_bri / 255) + 1;  // 1..256

    if (test_start) {
        const uint32_t step = (now - test_start) / kTestStepMs;
        if (step >= 3) {
            test_start = 0;
        }
        else {
            // Fill from the start of the strip over each second: shows the order and direction
            const int lit = n * ((now - test_start) % kTestStepMs) / kTestStepMs + 1;
            for (int i = 0; i < n; i++)
                set(i, step == 0 && i < lit ? 255 : 0, step == 1 && i < lit ? 255 : 0, step == 2 && i < lit ? 255 : 0, 128);
            strip.show();
            return;
        }
    }

    const bool music_live = now - music.last_ms < kMusicTimeoutMs;
    const Look &l = look;

    switch (l.pattern) {
        case PAT_SOLID:
            for (int i = 0; i < n; i++)
                set(i, l.r, l.g, l.b, scale);
            break;

        case PAT_RAINBOW: {
            const uint16_t start = phase(now, 20000, 1000);
            for (int i = 0; i < n; i++) {
                const uint32_t c = Adafruit_NeoPixel::ColorHSV(start + uint32_t(i) * 65536 / n);
                set(i, c >> 16, c >> 8, c, scale);
            }
            break;
        }

        case PAT_CHASE: {
            // Groups of 3 lit, 3 in colour 2, moving along
            const int offset = phase(now, 6000, 300) * 6 >> 16;
            for (int i = 0; i < n; i++) {
                const bool lit = (i + offset) % 6 < 3;
                set(i, lit ? l.r : l.r2, lit ? l.g : l.g2, lit ? l.b : l.b2, lit ? scale : scale / 6);
            }
            break;
        }

        case PAT_BREATHE: {
            const uint16_t p = phase(now, 8000, 800);
            const uint8_t level = (Adafruit_NeoPixel::sine8(p >> 8) >> 1) + 8;  // never fully dark
            for (int i = 0; i < n; i++)
                set(i, l.r, l.g, l.b, scale * level >> 8);
            break;
        }

        case PAT_GRADIENT: {
            // Colour to colour 2 and back along the strip, drifting with speed
            const int shift = l.speed ? (phase(now, 60000, 2000) * n >> 16) : 0;
            for (int i = 0; i < n; i++) {
                const int pos = (i + shift) % n;
                const int half = n / 2 ? n / 2 : 1;
                const uint16_t t = pos < half ? pos * 256 / half : (n - pos) * 256 / half;
                set(i, lerp(l.r, l.r2, t), lerp(l.g, l.g2, t), lerp(l.b, l.b2, t), scale);
            }
            break;
        }

        case PAT_TWINKLE: {
            // Random LEDs flare in colour 2 over a dim colour, then fade
            const int fade = 2 + l.speed / 16;
            const int spawn = 1 + l.speed / 32;
            for (int i = 0; i < n; i++)
                twinkle[i] = twinkle[i] > fade ? twinkle[i] - fade : 0;
            for (int s = 0; s < spawn; s++) {
                if (random(100) < 30)
                    twinkle[random(n)] = 255;
            }
            for (int i = 0; i < n; i++) {
                const uint16_t t = twinkle[i] + 1;
                set(i, lerp(l.r / 4, l.r2, t), lerp(l.g / 4, l.g2, t), lerp(l.b / 4, l.b2, t), scale);
            }
            break;
        }

        case PAT_COMET: {
            // A head in colour with a tail fading into colour 2
            const int head = phase(now, 8000, 500) * n >> 16;
            const int tail = max(4, n / 5);
            for (int i = 0; i < n; i++) {
                const int behind = (head - i + n) % n;
                if (behind > tail) {
                    set(i, 0, 0, 0, 0);
                    continue;
                }
                const uint16_t t = behind * 256 / tail;
                set(i, lerp(l.r, l.r2, t), lerp(l.g, l.g2, t), lerp(l.b, l.b2, t), scale * (256 - t) >> 8);
            }
            break;
        }

        case PAT_VU: {
            // Bar from the start: colour at the bottom, colour 2 at the top
            int sum = 0;
            for (int b = 0; b < kBands; b++)
                sum += music.bands[b];
            const int level = music_live ? sum / kBands : 0;
            const int lit = n * level / 255;
            for (int i = 0; i < n; i++) {
                const uint16_t t = i * 256 / n;
                if (i < lit)
                    set(i, lerp(l.r, l.r2, t), lerp(l.g, l.g2, t), lerp(l.b, l.b2, t), scale);
                else
                    set(i, 0, 0, 0, 0);
            }
            break;
        }

        case PAT_SPECTRUM: {
            // Eight segments, low to high, each as bright as its band, hues across the strip
            for (int i = 0; i < n; i++) {
                const int band = i * kBands / n;
                const uint8_t level = music_live ? music.bands[band] : 0;
                const uint32_t c = Adafruit_NeoPixel::ColorHSV(uint32_t(band) * 65536 / kBands);
                set(i, c >> 16, c >> 8, c, scale * level >> 8);
            }
            break;
        }

        case PAT_BEAT: {
            // Colour at a low glow; each beat flashes colour 2 and fades back
            if (music_live && music.beat)
                pulse = 255;
            const uint16_t t = pulse + 1;
            for (int i = 0; i < n; i++)
                set(i, lerp(l.r / 5, l.r2, t), lerp(l.g / 5, l.g2, t), lerp(l.b / 5, l.b2, t), scale);
            const int fade = 6 + l.speed / 12;
            pulse = pulse > fade ? pulse - fade : 0;
            break;
        }

        case PAT_OFF:
        default:
            strip.clear();
            break;
    }

    strip.show();
}
