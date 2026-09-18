#pragma once
#include "Animation.h"
#include <FastLED.h>

class BeatDropAnimation : public Animation {
    uint8_t dropPhase;
    uint8_t gHue;

public:
    BeatDropAnimation() : dropPhase(0), gHue(0) {}

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        if (f.dropDetected) {
            dropPhase = 1;
        }

        const uint8_t brightAudio = uint8_t(255.0f * f.pixelLevel());
        const uint8_t valHsv      = uint8_t(255.0f * f.hsvLevel());

        if (dropPhase > 0 && dropPhase <= 4) {
            // High-impact white/hot flash on drop trigger
            fill_solid(leds, n, CRGB(brightAudio, brightAudio, brightAudio));
            ++dropPhase;
        } else {
            fadeToBlackBy(leds, n, 40);
            // Searing decay sparkles
            const int sparks = (n > 20) ? 6 : 2;
            for (int i = 0; i < sparks; ++i) {
                const int idx = random16(n);
                leds[idx] = CHSV(gHue + random8(64), 220, valHsv);
            }
            if (dropPhase > 4) {
                if (++dropPhase > 25) dropPhase = 0;
            }
        }

        EVERY_N_MILLISECONDS(60) { gHue += 8; }
    }
};
