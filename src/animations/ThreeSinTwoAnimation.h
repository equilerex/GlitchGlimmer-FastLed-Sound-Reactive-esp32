#pragma once
#include "Animation.h"
#include <FastLED.h>

class ThreeSinTwoAnimation : public Animation {
    uint8_t wave1;
    uint8_t wave2;
    uint8_t wave3;

public:
    ThreeSinTwoAnimation() : wave1(0), wave2(0), wave3(0) {}

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        wave1 += 2;
        wave2 += 1;
        wave3 -= 3;

        const uint8_t brightAudio = uint8_t(255.0f * f.pixelLevel());

        for (int k = 0; k < n; ++k) {
            const uint8_t r = scale8(sin8(5 * k + wave1), brightAudio);
            const uint8_t g = scale8(sin8(8 * k + wave2), brightAudio);
            const uint8_t b = scale8(sin8(7 * k + wave3), brightAudio);
            leds[k] = CRGB(r, g, b);
        }
    }
};
