#pragma once
#include "Animation.h"
#include "BeatClock.h"
#include <FastLED.h>

class HeartbeatAnimation : public Animation {
    BeatClock clock;

public:
    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        // Double-beat pattern like a heart: thump-thump ... rest. One lub-dub per
        // beat of the music, so the rest between them is what stretches with the
        // tempo. Unlocked, it falls back to a resting 50 BPM. The dub is a fifth of
        // a beat behind the lub and a little quieter, and both tails are steep
        // enough to reach black before the next beat, which is what makes it a
        // heartbeat rather than a slow throb.
        clock.update(f, 50.0f);
        const float lub = clock.pulse(6.0f);
        const float dub = 0.7f * beatPulseAt(clock.phase, 0.22f, 6.0f);
        const uint8_t pulse = uint8_t(255.0f * (lub > dub ? lub : dub));

        const uint8_t brightAudio = uint8_t(255.0f * f.pixelLevel());
        const uint8_t val = scale8(pulse, brightAudio);

        // Warm crimson/ruby glow
        for (int i = 0; i < n; ++i) {
            leds[i] = CRGB(val, scale8(val, 20), scale8(val, 40));
        }
    }
};
