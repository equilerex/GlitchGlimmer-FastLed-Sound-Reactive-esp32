#pragma once
#include "../animations/Animation.h"
#include <FastLED.h>

class PsychedelicTunnelAnimation : public Animation {
public:
    static constexpr MoodType mood = MoodType::FLOATY;
    static constexpr float preferredTempo = 0.5f;  // Slower
    static constexpr float intensity = 0.8f;

    void update(CRGB* leds, int count, const AudioFeatures& f) override {
        float waveSpeed = f.spectrumCentroid * 0.2f + f.bass * 0.8f;
        uint8_t baseHue = millis() / 10;
        for (int i = 0; i < count; i++) {
            float pos = sinf(i * 0.2f + millis() * 0.001f * waveSpeed);
            leds[i] = CHSV(baseHue + pos * 50, 255, 100 + 100 * pos);
        }
    }
};
