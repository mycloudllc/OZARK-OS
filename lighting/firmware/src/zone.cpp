#include "zone.h"

const char *const kOrderNames[6] = {"RGB", "RBG", "GRB", "GBR", "BRG", "BGR"};

namespace {
constexpr neoPixelType kOrders[6] = {NEO_RGB, NEO_RBG, NEO_GRB, NEO_GBR, NEO_BRG, NEO_BGR};
constexpr neoPixelType kOrdersW[6] = {NEO_RGBW, NEO_RBGW, NEO_GRBW, NEO_GBRW, NEO_BRGW, NEO_BGRW};
constexpr uint32_t kTestStepMs = 1000;
constexpr uint32_t kMusicTimeoutMs = 1000;
constexpr float kFrameMs = 16;              // the frame time the fades are tuned for
constexpr uint32_t kStillRefreshMs = 1000;  // Off / Solid: sent again this often anyway

neoPixelType strip_type(const StripConfig &c)
{
    return (c.rgbw ? kOrdersW[c.order] : kOrders[c.order]) | (c.khz400 ? NEO_KHZ400 : NEO_KHZ800);
}

uint8_t lerp(uint8_t a, uint8_t b, uint16_t t)  // t 0..256
{
    return a + ((int(b) - int(a)) * int(t) >> 8);
}

// 0..255 level as a 0..256 blend amount (255 reaches the far colour)
uint16_t amount(uint8_t level)
{
    return level + (level >> 7);
}
}  // namespace

Zone::Zone(char name, const StripConfig &config)
    : name(name)
    , config(config)
    , look{255, 0, 80, 0, 120, 255, PAT_SOLID, 128, 255, 0, 0, 0}
    , strip(config.count, config.pin, strip_type(config))
{
    memset(level, 0, sizeof(level));
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
    shown_valid = false;
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

    // 16 ms frames since the last render. The strips are written one after
    // another (about 30 us an LED), so with long strips a frame takes longer:
    // fades step that much further to keep their speed.
    const float frames = last_render ? constrain((now - last_render) / kFrameMs, 1.0f, 30.0f) : 1.0f;
    last_render = now;

    // Off and Solid don't move: write them only when they change
    const bool still = !test_start && (look.pattern == PAT_OFF || look.pattern == PAT_SOLID);
    if (still && shown_valid && shown_max_bri == max_bri && now - shown_at < kStillRefreshMs &&
        !memcmp(&shown_look, &look, sizeof(Look)))
        return;

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
            shown_valid = false;
            return;
        }
    }

    // A new pattern starts from a clean per-LED state (heat, trails...)
    if (look.pattern != last_pattern) {
        memset(level, 0, sizeof(level));
        pulse = 0;
        last_pos = -1;
        last_pattern = look.pattern;
    }

    const bool music_live = now - music.last_ms < kMusicTimeoutMs;
    const Look &l = look;
    const Rgb c1{l.r, l.g, l.b}, c2{l.r2, l.g2, l.b2}, bg{l.r3, l.g3, l.b3};
    auto mix = [](const Rgb &a, const Rgb &b, uint16_t t) {
        return Rgb{lerp(a.r, b.r, t), lerp(a.g, b.g, t), lerp(a.b, b.b, t)};
    };
    auto hsv = [](uint16_t hue) {
        const uint32_t c = Adafruit_NeoPixel::ColorHSV(hue);
        return Rgb{uint8_t(c >> 16), uint8_t(c >> 8), uint8_t(c)};
    };

    switch (l.pattern) {
        case PAT_SOLID:
            for (int i = 0; i < n; i++)
                set(i, c1, scale);
            break;

        case PAT_RAINBOW: {
            const uint16_t start = phase(now, 20000, 1000);
            for (int i = 0; i < n; i++)
                set(i, hsv(start + uint32_t(i) * 65536 / n), scale);
            break;
        }

        case PAT_CHASE: {
            // Groups of 3 lit, 3 in colour 2, moving along
            const int offset = phase(now, 6000, 300) * 6 >> 16;
            for (int i = 0; i < n; i++) {
                const bool lit = (i + offset) % 6 < 3;
                set(i, lit ? c1 : c2, lit ? scale : scale / 6);
            }
            break;
        }

        case PAT_BREATHE: {
            // Background to the full colour and back
            const uint16_t p = phase(now, 8000, 800);
            const uint16_t t = amount(Adafruit_NeoPixel::sine8(p >> 8));
            for (int i = 0; i < n; i++)
                set(i, mix(bg, c1, t), scale);
            break;
        }

        case PAT_GRADIENT: {
            // Colour to colour 2 and back along the strip, drifting with speed
            const int shift = l.speed ? (phase(now, 60000, 2000) * n >> 16) : 0;
            const int half = n / 2 ? n / 2 : 1;
            for (int i = 0; i < n; i++) {
                const int pos = (i + shift) % n;
                const uint16_t t = pos < half ? pos * 256 / half : (n - pos) * 256 / half;
                set(i, mix(c1, c2, t), scale);
            }
            break;
        }

        case PAT_TWINKLE: {
            // Random LEDs flare in colour 2 over a dim colour, then fade
            const int fade = lroundf((2 + l.speed / 16) * frames);
            const int spawn = lroundf((1 + l.speed / 32) * frames);
            for (int i = 0; i < n; i++)
                level[i] = level[i] > fade ? level[i] - fade : 0;
            for (int s = 0; s < spawn; s++) {
                if (random(100) < 30)
                    level[random(n)] = 255;
            }
            const Rgb dim{uint8_t(l.r / 4), uint8_t(l.g / 4), uint8_t(l.b / 4)};
            for (int i = 0; i < n; i++)
                set(i, mix(dim, c2, level[i] + 1), scale);
            break;
        }

        case PAT_COMET: {
            // A head in colour with a tail fading through colour 2 into the background
            const int head = phase(now, 8000, 500) * n >> 16;
            const int tail = max(4, n / 5);
            for (int i = 0; i < n; i++) {
                const int behind = (head - i + n) % n;
                if (behind > tail) {
                    set(i, bg, scale);
                    continue;
                }
                const uint16_t t = behind * 256 / tail;
                set(i, mix(mix(c1, c2, t), bg, t), scale);
            }
            break;
        }

        case PAT_VU: {
            // Bar from the start: colour at the bottom, colour 2 at the top
            int sum = 0;
            for (int b = 0; b < kBands; b++)
                sum += music.bands[b];
            const int lvl = music_live ? sum / kBands : 0;
            const int lit = n * lvl / 255;
            for (int i = 0; i < n; i++)
                set(i, i < lit ? mix(c1, c2, i * 256 / n) : bg, scale);
            break;
        }

        case PAT_SPECTRUM: {
            // Eight segments, low to high, each as bright as its band, hues across the strip
            for (int i = 0; i < n; i++) {
                const int band = i * kBands / n;
                const uint8_t lvl = music_live ? music.bands[band] : 0;
                set(i, mix(bg, hsv(uint32_t(band) * 65536 / kBands), amount(lvl)), scale);
            }
            break;
        }

        case PAT_BEAT: {
            // Colour at a low glow; each beat flashes colour 2 and fades back
            if (music_live && music.beat)
                pulse = 255;
            const Rgb dim{uint8_t(l.r / 5), uint8_t(l.g / 5), uint8_t(l.b / 5)};
            for (int i = 0; i < n; i++)
                set(i, mix(dim, c2, pulse + 1), scale);
            const int fade = lroundf((6 + l.speed / 12) * frames);
            pulse = pulse > fade ? pulse - fade : 0;
            break;
        }

        case PAT_FIRE: {
            // Fire2012: heat cools, rises and sparks at the start of the strip.
            // Background (cold) -> colour 2 (embers) -> colour (hottest).
            const int cooling = 30 + l.speed / 4;
            const int sparking = 60 + l.speed / 3;
            const int steps = constrain(lroundf(frames), 1, 4);
            for (int s = 0; s < steps; s++) {
                for (int i = 0; i < n; i++) {
                    const int cool = random(0, cooling * 10 / n + 2);
                    level[i] = level[i] > cool ? level[i] - cool : 0;
                }
                for (int k = n - 1; k >= 2; k--)
                    level[k] = (level[k - 1] + level[k - 2] * 2) / 3;
                if (random(255) < sparking) {
                    const int y = random(min(7, n));
                    level[y] = min(255, level[y] + int(random(160, 255)));
                }
            }
            for (int i = 0; i < n; i++) {
                const uint8_t h = level[i];
                set(i, h < 128 ? mix(bg, c2, h * 2) : mix(c2, c1, (h - 128) * 2), scale);
            }
            break;
        }

        case PAT_SPARKLE: {
            // Quick glints of colour 2 over the background
            const int fade = lroundf(48 * frames);
            const int spawn = lroundf((1 + l.speed / 64) * frames);
            for (int i = 0; i < n; i++)
                level[i] = level[i] > fade ? level[i] - fade : 0;
            for (int s = 0; s < spawn; s++) {
                if (random(100) < 20 + l.speed / 4)
                    level[random(n)] = 255;
            }
            for (int i = 0; i < n; i++)
                set(i, mix(bg, c2, amount(level[i])), scale);
            break;
        }

        case PAT_SCANNER: {
            // A dot sweeping end to end and back, leaving a fading trail
            const uint16_t p = phase(now, 6000, 600);
            const uint32_t tri = p < 32768 ? uint32_t(p) * 2 : uint32_t(65535 - p) * 2;
            const int pos = tri * (n - 1) / 65535;
            const int fade = lroundf((10 + l.speed / 12) * frames);
            for (int i = 0; i < n; i++)
                level[i] = level[i] > fade ? level[i] - fade : 0;
            // Light every LED the dot passed since the last frame: no gaps when it's fast
            const int from = last_pos < 0 ? pos : last_pos;
            for (int i = min(from, pos); i <= max(from, pos); i++)
                level[i] = 255;
            last_pos = pos;
            for (int i = 0; i < n; i++)
                set(i, mix(bg, c1, amount(level[i])), scale);
            break;
        }

        case PAT_WIPE: {
            // Colour fills the strip end to end, then colour 2, again and again
            const uint16_t p = phase(now, 8000, 800);
            const bool second = p >= 32768;
            const int filled = uint32_t(p & 0x7FFF) * n / 32768;
            for (int i = 0; i < n; i++)
                set(i, (i < filled) != second ? c1 : c2, scale);
            break;
        }

        case PAT_WAVE: {
            // Bands of colour rolling along over the background, three on the strip
            const uint8_t shift = phase(now, 8000, 800) >> 8;
            for (int i = 0; i < n; i++) {
                const uint8_t x = uint8_t(uint32_t(i) * 3 * 256 / n - shift);
                set(i, mix(bg, c1, amount(Adafruit_NeoPixel::sine8(x))), scale);
            }
            break;
        }

        case PAT_FADE: {
            // The whole strip blends colour -> colour 2 -> colour
            const uint16_t p = phase(now, 10000, 1000);
            const Rgb c = mix(c1, c2, amount(Adafruit_NeoPixel::sine8(p >> 8)));
            for (int i = 0; i < n; i++)
                set(i, c, scale);
            break;
        }

        case PAT_STROBE: {
            // Short flashes of colour over the background, faster with speed
            const uint32_t period = 1000 - (1000 - 80) * uint32_t(l.speed) / 255;
            const bool lit = now % period < min<uint32_t>(40, period / 2);
            for (int i = 0; i < n; i++)
                set(i, lit ? c1 : bg, scale);
            break;
        }

        case PAT_THEATER: {
            // Every third LED lit, stepping along (marquee lights)
            const int offset = phase(now, 3000, 150) * 3 >> 16;
            for (int i = 0; i < n; i++)
                set(i, (i + 3 - offset) % 3 == 0 ? c1 : bg, scale);
            break;
        }

        case PAT_RAINBOW_RUNNER: {
            // A short rainbow running along over the background
            const int head = phase(now, 8000, 800) * n >> 16;
            const int band = max(6, n / 4);
            for (int i = 0; i < n; i++) {
                const int behind = (head - i + n) % n;
                set(i, behind < band ? hsv(uint32_t(behind) * 65536 / band) : bg, scale);
            }
            break;
        }

        case PAT_OFF:
        default:
            strip.clear();
            break;
    }

    strip.show();
    shown_valid = still;
    if (still) {
        shown_look = look;
        shown_max_bri = max_bri;
        shown_at = now;
    }
}
