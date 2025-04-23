#pragma once
#include "../animations/Animation.h"
#include <FastLED.h>

class BassPulseStormAnimation : public Animation {
public:
    static constexpr MoodType mood = MoodType::INTENSE;
    static constexpr float preferredTempo = 1.0f;
    static constexpr float intensity = 1.0f;

    void update(CRGB* leds, int count, const AudioFeatures& f) override {
        static uint8_t hue = 0;
        if (f.beatDetected || f.bassHits > 0) hue += 32;

        uint8_t brightness = constrain(f.bass * 255 + f.peak * 128, 50, 255);
        fill_solid(leds, count, CHSV(hue, 255, brightness));
    }
};
