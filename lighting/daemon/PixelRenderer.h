#pragma once

#include <QColor>
#include <QtGlobal>
#include <array>
#include <vector>

// Draws the lighting patterns on the head unit, LED by LED: the same patterns,
// timing and music response as the Ozark firmware (../firmware/src/zone.cpp),
// for controllers that only display pixels (WLED over Adalight).

// Latest music frame (shared by all zones)
struct Music {
    std::array<quint8, 8> bands{};
    bool beat = false;  // set by a frame, cleared once rendered
    qint64 last_ms = -100000;
};

class PixelRenderer {
public:
    struct Look {
        QColor color{255, 0, 80}, color2{0, 120, 255};
        int pattern = 1, speed = 128, brightness = 255;
    };

    void test(qint64 now) { m_testStart = now ? now : 1; }
    bool testing() const { return m_testStart != 0; }

    // Draws n LEDs into rgb (3 bytes each, pattern order; the caller places
    // and reverses them). max_bri caps the zone's brightness, like the
    // firmware's MAXBRI.
    void render(qint64 now, const Look& look, const Music& music, int max_bri, quint8* rgb, int n);

private:
    void set(quint8* rgb, int i, int r, int g, int b, int scale) const;
    quint16 phase(qint64 now, int speed, quint32 slowest_ms, quint32 fastest_ms) const;

    std::vector<quint8> m_twinkle;  // per-LED twinkle level
    int m_pulse = 0;                // beat pulse level
    qint64 m_testStart = 0;         // 0: not testing
    qint64 m_lastMs = -1;           // last render: fades keep the firmware's speed at any frame rate
};
