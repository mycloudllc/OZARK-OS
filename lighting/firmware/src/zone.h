// One LED output of the lighting controller: its strip, hardware config and look.
#pragma once

#include <Adafruit_NeoPixel.h>
#include <Arduino.h>

constexpr int kMaxLeds = 600;  // per zone
constexpr int kBands = 8;      // music frame bands

enum Pattern : uint8_t {
    PAT_OFF,
    PAT_SOLID,
    PAT_RAINBOW,
    PAT_CHASE,
    PAT_BREATHE,
    PAT_GRADIENT,
    PAT_TWINKLE,
    PAT_COMET,
    PAT_VU,
    PAT_SPECTRUM,
    PAT_BEAT,
    PAT_COUNT
};

struct StripConfig {
    uint8_t pin;
    uint16_t count;
    uint8_t order;  // index into kOrderNames
    bool rgbw;
    bool khz400;
    bool reverse;
};

struct Look {
    uint8_t r, g, b;
    uint8_t r2, g2, b2;
    uint8_t pattern;
    uint8_t speed;
    uint8_t bri;
};

// Latest music frame from the head unit (shared by all zones)
struct Music {
    uint8_t bands[kBands];
    bool beat;           // set by a frame, cleared once rendered
    uint32_t last_ms;    // when the last frame arrived
};

extern const char *const kOrderNames[6];

class Zone {
   public:
    Zone(char name, const StripConfig &config);

    void apply(const StripConfig &config);  // (re)initialise the strip
    void render(uint32_t now, const Music &music, uint8_t max_bri);
    void test(uint32_t now);                 // red, green, blue sweep

    char name;
    StripConfig config;
    Look look;

   private:
    void set(int i, uint8_t r, uint8_t g, uint8_t b, uint16_t scale);
    uint16_t phase(uint32_t now, uint32_t slowest_ms, uint32_t fastest_ms) const;

    Adafruit_NeoPixel strip;
    uint8_t twinkle[kMaxLeds];  // per-LED twinkle level
    uint8_t pulse = 0;          // beat pulse level
    uint32_t test_start = 0;    // 0: not testing
    bool started = false;
};
