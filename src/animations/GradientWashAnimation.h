#pragma once
#include "Animation.h"
#include <FastLED.h>

class GradientWashAnimation : public Animation {
    uint8_t phase;

public:
    GradientWashAnimation() : phase(0) {}

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        EVERY_N_MILLISECONDS(120) { ++phase; }

        const uint8_t val = uint8_t(255.0f * f.hsvLevel());
        const uint8_t shift = sin8(phase);

        // Deep blue to violet gradient wash
        const CRGB startCol = CHSV(160 + (shift >> 3), 220, val);
        const CRGB endCol   = CHSV(200 + (shift >> 3), 200, val);

        fill_gradient_RGB(leds, n, startCol, endCol);
    }
};
