#pragma once
#include "Animation.h"
#include <FastLED.h>

class GentlePulseWaveAnimation : public Animation {
    uint8_t wavePhase;

public:
    GentlePulseWaveAnimation() : wavePhase(0) {}

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        wavePhase += 1;
        const uint8_t valHsv = uint8_t(255.0f * f.hsvLevel());
        const uint8_t pulse = beatsin8(45, 100, 255);

        for (int i = 0; i < n; ++i) {
            const uint8_t w = sin8(i * 16 + wavePhase);
            const uint8_t val = scale8(scale8(w, pulse), valHsv);
            // Deep magenta/indigo to warm violet
            leds[i] = CHSV(200 + (w >> 3), 220, val);
        }
    }
};
