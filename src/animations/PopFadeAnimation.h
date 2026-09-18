#pragma once
#include "Animation.h"
#include <FastLED.h>

class PopFadeAnimation : public Animation {
    uint8_t gHue;
    uint16_t timer;

public:
    PopFadeAnimation() : gHue(0), timer(0) {}

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        timer = (timer + 1) % 360;
        const float ramp = float(timer) / 360.0f;

        fadeToBlackBy(leds, n, 45);

        const uint8_t valHsv = uint8_t(255.0f * f.hsvLevel());
        const int bursts = int(2.0f + ramp * 8.0f);

        for (int b = 0; b < bursts; ++b) {
            const int idx = random16(n);
            const int barlen = random8(1, 4);
            const uint8_t hue = gHue + random8(64);
            for (int i = 0; i < barlen && (idx + i < n); ++i) {
                leds[idx + i] += CHSV(hue, 220, valHsv);
            }
        }

        EVERY_N_MILLISECONDS(40) { gHue += 2; }
    }
};
