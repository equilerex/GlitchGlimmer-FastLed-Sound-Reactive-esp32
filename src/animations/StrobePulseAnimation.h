#pragma once
#include "Animation.h"
#include "BeatClock.h"
#include <FastLED.h>

// Strobe on the beat, denser as a build tightens. One flash per beat while
// nothing is building, up to four per beat at full tension. Alternate pixels
// flash so the strobe reads as a texture rather than a blackout.
class StrobePulseAnimation : public Animation {
    uint8_t     gHue;
    BeatClock   clock;
    TensionRamp ramp;
    float       strobe;

public:
    StrobePulseAnimation() : gHue(0), strobe(0.0f) {}

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        clock.update(f, 120.0f);
        const float tension = ramp.update(f, clock.dt);

        // Accumulator for the same reason as RisingTension: the rate changes with
        // tension every frame, and a multiplied phase would jump.
        strobe += clock.dt * clock.bpmUsed * (1.0f / 60.0f) * (1.0f + 3.0f * tension);
        while (strobe >= 1.0f) strobe -= 1.0f;

        fadeToBlackBy(leds, n, 60);

        // Lit for the first third of each strobe cycle, brightest at its start.
        if (strobe < 0.33f) {
            const uint8_t pulse = uint8_t(255.0f * (1.0f - strobe / 0.33f) * 0.6f + 100.0f);
            const uint8_t brightAudio = uint8_t(255.0f * f.pixelLevel());
            const uint8_t val = scale8(pulse, brightAudio);
            for (int i = 0; i < n; i += 2) {
                leds[i] = ColorFromPalette(PartyColors_p, gHue + i * 4, val, LINEARBLEND);
            }
        }

        EVERY_N_MILLISECONDS(30) { gHue += 3; }
    }
};
