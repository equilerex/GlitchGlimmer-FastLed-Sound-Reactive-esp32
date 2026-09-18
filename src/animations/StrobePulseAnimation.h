#pragma once
#include "Animation.h"
#include <FastLED.h>

class StrobePulseAnimation : public Animation {
    uint8_t gHue;
    uint16_t timer;

public:
    StrobePulseAnimation() : gHue(0), timer(0) {}

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        timer = (timer + 1) % 400;
        const float ramp = float(timer) / 400.0f;
        const uint8_t bpm = uint8_t(80.0f + ramp * 160.0f); // 80 -> 240 bpm

        fadeToBlackBy(leds, n, 60);

        const uint8_t pulse = beatsin8(bpm, 0, 255);
        const uint8_t brightAudio = uint8_t(255.0f * f.pixelLevel());

        if (pulse > 160) {
            const uint8_t val = scale8(pulse, brightAudio);
            for (int i = 0; i < n; i += 2) {
                leds[i] = ColorFromPalette(PartyColors_p, gHue + i * 4, val, LINEARBLEND);
            }
        }

        EVERY_N_MILLISECONDS(30) { gHue += 3; }
    }
};
