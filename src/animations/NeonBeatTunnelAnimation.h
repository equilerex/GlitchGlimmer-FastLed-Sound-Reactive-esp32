#pragma once
#include "../animations/Animation.h"
#include "BeatClock.h"
#include <FastLED.h>

class NeonBeatTunnelAnimation : public Animation {
    BeatClock clock;

public:
    void update(CRGB* leds, int count, const AudioFeatures& f) override {
        if (count <= 0) return;

        // The tunnel advances an eighth of a cycle per beat and pumps with the beat,
        // so the motion is the tempo. 100 BPM unlocked.
        clock.update(f, 100.0f);
        const uint8_t scroll = uint8_t(uint32_t(clock.beatCount * 32u) + uint32_t(clock.phase * 32.0f));
        const float pump = 0.65f + 0.35f * clock.pulse(2.0f);

        for (int i = 0; i < count; ++i) {
            uint8_t wave = sin8(i * 8 + scroll);
            // level, not volume. wave already spans 0..255, so multiplying by a
            // 0..1 loudness is the intent, and volume is an absolute RMS near
            // 0.008 on the microphone in use, which left every pixel black. Curved
            // as well: this was the one catalog animation that read as working at
            // the level this microphone reports, and it was working at 16 percent
            // duty.
            leds[i] = CHSV((i * 2 + wave) % 255, 255, wave * f.hsvLevel() * pump);
        }
    }
};
