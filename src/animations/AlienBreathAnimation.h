#pragma once
#include "../animations/Animation.h"
#include <FastLED.h>

class AlienBreathAnimation : public Animation {
public:
    static constexpr MoodType mood = MoodType::CALM;
    static constexpr float preferredTempo = 0.3f;
    static constexpr float intensity = 0.4f;

    void update(CRGB* leds, int count, const AudioFeatures& f) override {
        float breath = sinf(millis() * 0.001f) * 0.5f + 0.5f;
        CRGB color = CHSV(160 + f.spectrumCentroid * 0.2f, 200, 80 + breath * 80);
        fill_solid(leds, count, color);
    }
};
