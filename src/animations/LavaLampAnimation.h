#pragma once
#include "Animation.h"
#include <FastLED.h>

class LavaLampAnimation : public Animation {
    uint32_t x;

public:
    LavaLampAnimation() : x(0) {}

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        x += 10;
        const uint8_t valScale = uint8_t(255.0f * f.hsvLevel());

        for (int i = 0; i < n; ++i) {
            const uint8_t noise = inoise8(i * 45, x);
            // Warm ember/lava hues: red-orange to golden amber (10..34)
            const uint8_t hue = map(noise, 0, 255, 10, 34);
            const uint8_t val = scale8(noise, valScale);
            leds[i] = CHSV(hue, 245, val);
        }
    }
};
