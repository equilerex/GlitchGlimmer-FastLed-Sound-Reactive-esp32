#pragma once
#include "Animation.h"
#include <FastLED.h>

class TwoSinAnimation : public Animation {
    uint8_t thisphase;
    uint8_t thatphase;
    uint8_t thisrot;

public:
    TwoSinAnimation() : thisphase(0), thatphase(0), thisrot(0) {}

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        thisphase += 2;
        thatphase += 1;
        EVERY_N_MILLISECONDS(50) { thisrot++; }

        const uint8_t valScale = uint8_t(255.0f * f.hsvLevel());
        const uint8_t thishue = thisrot;
        const uint8_t thathue = thisrot + 128;
        const uint8_t allfreq = 24;

        for (int k = 0; k < n; ++k) {
            const uint8_t b1 = scale8(qsub8(cubicwave8((k * allfreq) + thisphase), 30), valScale);
            const uint8_t b2 = scale8(qsub8(cubicwave8((k * allfreq) + 128 + thatphase), 30), valScale);
            leds[k] = CHSV(thishue, 240, b1);
            leds[k] += CHSV(thathue, 240, b2);
        }
    }
};
