#pragma once
#include "Animation.h"
#include "BeatClock.h"
#include <FastLED.h>

class ColorSlamAnimation : public Animation {
    uint32_t lastSlamMs;
    uint8_t currentHue;
    BeatClock clock;

public:
    ColorSlamAnimation() : lastSlamMs(0), currentHue(0) {}

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        clock.update(f, 130.0f);
        const uint32_t now = millis();
        const uint8_t valHsv = uint8_t(255.0f * f.hsvLevel());

        // The clock wrap is the beat while the tracker is locked and a 130 BPM
        // stand-in while it is not. A detected onset can land next to a wrap, so
        // the refractory keeps one beat from slamming twice.
        if ((clock.wrapped || f.beatDetected) && now - lastSlamMs > 200) {
            lastSlamMs = now;
            currentHue += 64;
            fill_solid(leds, n, CHSV(currentHue, 240, valHsv));
        } else {
            fadeToBlackBy(leds, n, 40);
        }
    }
};
