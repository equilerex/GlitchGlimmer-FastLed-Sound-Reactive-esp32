#pragma once
#include "Animation.h"
#include <FastLED.h>

class HyperSpinAnimation : public Animation {
    uint16_t angle;

public:
    HyperSpinAnimation() : angle(0) {}

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        angle += beatsin16(128, 12, 36);
        const uint8_t brightAudio = uint8_t(255.0f * f.pixelLevel());

        for (int i = 0; i < n; ++i) {
            const uint8_t colorIndex = uint8_t((i * 12) - angle);
            leds[i] = ColorFromPalette(RainbowStripeColors_p, colorIndex, brightAudio, LINEARBLEND);
        }
    }
};
