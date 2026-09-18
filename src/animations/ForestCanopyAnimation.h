#pragma once
#include "Animation.h"
#include <FastLED.h>

class ForestCanopyAnimation : public Animation {
    uint16_t t;

public:
    ForestCanopyAnimation() : t(0) {}

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        t += 3;
        const uint8_t brightAudio = uint8_t(255.0f * f.pixelLevel());

        for (int i = 0; i < n; ++i) {
            const uint8_t green = scale8(inoise8(i * 18, t), brightAudio);
            const uint8_t blue  = scale8(inoise8(i * 18 + 8000, t), brightAudio);
            leds[i] = CRGB(0, green, scale8(blue, 180));
        }
    }
};
