#pragma once
#include "Animation.h"
#include "BeatClock.h"
#include <FastLED.h>

class GentlePulseWaveAnimation : public Animation {
    uint8_t wavePhase;
    BeatClock clock;

public:
    GentlePulseWaveAnimation() : wavePhase(0) {}

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        wavePhase += 1;
        const uint8_t valHsv = uint8_t(255.0f * f.hsvLevel());
        // Swells on the beat and settles to 140 by the next. 45 BPM unlocked.
        clock.update(f, 45.0f);
        const uint8_t pulse = uint8_t(140.0f + 115.0f * clock.pulse(2.0f));

        for (int i = 0; i < n; ++i) {
            const uint8_t w = sin8(i * 16 + wavePhase);
            const uint8_t val = scale8(scale8(w, pulse), valHsv);
            // Deep magenta/indigo to warm violet
            leds[i] = CHSV(200 + (w >> 3), 240, val);
        }
    }
};
