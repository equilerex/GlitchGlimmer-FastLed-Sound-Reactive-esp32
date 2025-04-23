#pragma once

#include <FastLED.h>
#include "../audio/AudioFeatures.h"
#include "../scenes/MoodHistory.h"
#include "Animation.h"

class MoodReactiveAnimation : public Animation {
private:
    unsigned long lastUpdate = 0;
    MoodHistory& moodHistory;
    CRGBPalette16 palette;

    enum class MoodType { CALM, BUILD, HYPE, NEUTRAL };

    MoodType detectMood(const MoodSnapshot& mood) {
        if (mood.energy > 1800.0 && mood.centroid > 100.0) return MoodType::HYPE;
        if (mood.energy > 1600.0 && mood.volume > 0.5) return MoodType::BUILD;
        if (mood.volume < 0.1 && mood.centroid < 60.0) return MoodType::CALM;
        return MoodType::NEUTRAL;
    }

public:
    MoodReactiveAnimation(MoodHistory& history) : moodHistory(history) {
        palette = RainbowColors_p;
    }

    void update(CRGB* leds, int numLeds, const AudioFeatures& audio) override {
        unsigned long now = millis();
        MoodSnapshot mood = moodHistory.latest();
        MoodType currentMood = detectMood(mood);

        switch (currentMood) {
            case MoodType::CALM:
                // Calm: Faint cyan breathing
                fill_solid(leds, numLeds, CHSV(160, 100, 20));
                for (int i = 0; i < numLeds; i++) {
                    leds[i].fadeLightBy(sin8((now / 20 + i * 5) % 255) / 3);
                }
                break;

            case MoodType::BUILD:
                // Build-up: Yellow-orange sweeping wave
                for (int i = 0; i < numLeds; i++) {
                    uint8_t offset = (i * 10 + now / 5) % 255;
                    leds[i] = CHSV(32, 255, sin8(offset));
                }
                break;

            case MoodType::HYPE:
                // Hype: Flash all colors with bass hits or beat
                for (int i = 0; i < numLeds; i++) {
                    leds[i] = ColorFromPalette(palette, (i * 5 + now / 3) % 255, 255);
                }
                if (audio.beatDetected || audio.bassHits > 0) {
                    for (int i = 0; i < numLeds; i++) {
                        leds[i].fadeLightBy(random8(10, 80));
                    }
                }
                break;

            case MoodType::NEUTRAL:
                // Neutral: Slow breathing purple
                for (int i = 0; i < numLeds; i++) {
                    uint8_t wave = sin8(now / 15 + i * 2);
                    leds[i] = CHSV(192, 255, wave);
                }
                break;
        }
    }

    const char* getName() const override {
        return "Mood Reactive";
    }
};
