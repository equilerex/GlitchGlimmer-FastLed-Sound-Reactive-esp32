#pragma once
#include "Animation.h"
#include "BeatClock.h"
#include <FastLED.h>

class BeatScannerAnimation : public Animation {
    uint8_t gHue;
    BeatClock clock;

public:
    BeatScannerAnimation() : gHue(0) {}

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        fadeToBlackBy(leds, n, 40);

        const uint8_t val = uint8_t(255.0f * f.hsvLevel());
        // Out on one beat, back on the next, so the ends land on the beat. The
        // cosine ease slows the head into each end instead of reversing at speed.
        // Unlocked it falls back to 110 BPM, which is the old beatsin16(55) cycle.
        clock.update(f, 110.0f);
        const float sweep = 0.5f - 0.5f * cosf(6.2831853f * clock.phase2());
        const uint16_t pos = uint16_t(sweep * float(n - 1) + 0.5f);
        const uint16_t mirrorPos = (n - 1) - pos;

        leds[pos] += CHSV(gHue, 255, val);
        leds[mirrorPos] += CHSV(gHue + 128, 255, val);

        EVERY_N_MILLISECONDS(30) { gHue += 2; }
    }
};
