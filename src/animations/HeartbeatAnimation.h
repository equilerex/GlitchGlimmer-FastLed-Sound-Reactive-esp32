#pragma once
#include "Animation.h"
#include <FastLED.h>

class HeartbeatAnimation : public Animation {
public:
    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        // Double-beat pattern like a heart: thump-thump ... thump-thump
        const uint8_t beat1 = beatsin8(50, 0, 255, 0, 0);
        const uint8_t beat2 = beatsin8(50, 0, 255, 0, 45); // offset phase
        const uint8_t pulse = max(beat1 > 180 ? beat1 : 0, beat2 > 200 ? beat2 : 0);

        const uint8_t brightAudio = uint8_t(255.0f * f.pixelLevel());
        const uint8_t val = scale8(pulse, brightAudio);

        // Warm crimson/ruby glow
        for (int i = 0; i < n; ++i) {
            leds[i] = CRGB(val, scale8(val, 20), scale8(val, 40));
        }
    }
};
