#pragma once
#include "../animations/Animation.h"
#include <FastLED.h>

class NeonBeatTunnelAnimation : public Animation {
public:
    void update(CRGB* leds, int count, const AudioFeatures& f) override {
        if (count <= 0) return;

        for (int i = 0; i < count; ++i) {
            uint8_t wave = sin8(i * 8 + millis() / 4);
            // level, not volume. wave already spans 0..255, so multiplying by a
            // 0..1 loudness is the intent, and volume is an absolute RMS near
            // 0.008 on the microphone in use, which left every pixel black. Curved
            // as well: this was the one catalog animation that read as working at
            // the level this microphone reports, and it was working at 16 percent
            // duty.
            leds[i] = CHSV((i * 2 + wave) % 255, 255, wave * f.hsvLevel());
        }
    }
};
