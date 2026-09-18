#pragma once
#include "Animation.h"
#include <FastLED.h>

class BeatScannerAnimation : public Animation {
    uint8_t gHue;

public:
    BeatScannerAnimation() : gHue(0) {}

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        fadeToBlackBy(leds, n, 40);

        const uint8_t val = uint8_t(255.0f * f.hsvLevel());
        const uint16_t pos = beatsin16(55, 0, n - 1);
        const uint16_t mirrorPos = (n - 1) - pos;

        leds[pos] += CHSV(gHue, 255, val);
        leds[mirrorPos] += CHSV(gHue + 128, 255, val);

        EVERY_N_MILLISECONDS(30) { gHue += 2; }
    }
};
