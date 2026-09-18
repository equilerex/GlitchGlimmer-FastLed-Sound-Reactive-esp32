#pragma once
#include "Animation.h"
#include <FastLED.h>

class ColorSlamAnimation : public Animation {
    uint8_t lastBeat;
    uint8_t currentHue;

public:
    ColorSlamAnimation() : lastBeat(0), currentHue(0) {}

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        const uint8_t beat = beatsin8(130, 0, 100);
        const uint8_t valHsv = uint8_t(255.0f * f.hsvLevel());

        if ((beat < 15 && lastBeat >= 15) || f.beatDetected) {
            currentHue += 64;
            fill_solid(leds, n, CHSV(currentHue, 240, valHsv));
        } else {
            fadeToBlackBy(leds, n, 40);
        }
        lastBeat = beat;
    }
};
