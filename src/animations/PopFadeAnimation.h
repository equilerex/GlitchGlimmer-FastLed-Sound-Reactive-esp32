#pragma once
#include "Animation.h"
#include "BeatClock.h"
#include <FastLED.h>

// Short bars pop in random places and fade. The count per frame rises with the
// build's tension, and every beat adds a small extra burst so the pops land with
// the music instead of dripping at a fixed rate.
class PopFadeAnimation : public Animation {
    uint8_t     gHue;
    BeatClock   clock;
    TensionRamp ramp;

public:
    PopFadeAnimation() : gHue(0) {}

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        clock.update(f, 110.0f);
        const float tension = ramp.update(f, clock.dt);

        fadeToBlackBy(leds, n, 45);

        const uint8_t valHsv = uint8_t(255.0f * f.hsvLevel());
        const int bursts = int(1.0f + tension * 8.0f) + (clock.wrapped ? 3 : 0);
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
