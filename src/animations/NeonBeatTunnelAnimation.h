#pragma once
#include "../animations/Animation.h"
#include <FastLED.h>

class NeonBeatTunnelAnimation : public Animation {
public:
    static constexpr MoodType mood = MoodType::ENERGETIC;
    static constexpr float preferredTempo = 1.2f;
    static constexpr float intensity = 0.9f;

    void update(CRGB* leds, int count, const AudioFeatures& f) override {
        for (int i = 0; i < count; ++i) {
            uint8_t wave = sin8(i * 8 + millis() / 4);
            leds[i] = CHSV((i * 2 + wave) % 255, 255, wave * f.volume);
        }
    }
};
