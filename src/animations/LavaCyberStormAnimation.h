#pragma once
#include "Animation.h"
#include <FastLED.h>

class LavaCyberStormAnimation : public Animation {
    uint8_t       gHue;
    uint8_t       mood;
    uint32_t      lastShift;
    CRGBPalette16 emberPal;
    CRGBPalette16 twilightPal;

public:
    LavaCyberStormAnimation()
        : gHue(0), mood(0), lastShift(0) {
        emberPal = CRGBPalette16(
            CRGB(255, 60, 0),  CRGB(255, 120, 0), CRGB(220, 30, 10), CRGB(180, 20, 40),
            CRGB(255, 60, 0),  CRGB(255, 120, 0), CRGB(220, 30, 10), CRGB(180, 20, 40),
            CRGB(255, 60, 0),  CRGB(255, 120, 0), CRGB(220, 30, 10), CRGB(180, 20, 40),
            CRGB(255, 60, 0),  CRGB(255, 120, 0), CRGB(220, 30, 10), CRGB(180, 20, 40)
        );
        twilightPal = CRGBPalette16(
            CRGB(40, 60, 255), CRGB(140, 30, 255),CRGB(0, 180, 255), CRGB(100, 0, 220),
            CRGB(40, 60, 255), CRGB(140, 30, 255),CRGB(0, 180, 255), CRGB(100, 0, 220),
            CRGB(40, 60, 255), CRGB(140, 30, 255),CRGB(0, 180, 255), CRGB(100, 0, 220),
            CRGB(40, 60, 255), CRGB(140, 30, 255),CRGB(0, 180, 255), CRGB(100, 0, 220)
        );
    }

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        const uint32_t now = millis();
        if (now - lastShift > 7000) {
            mood = !mood;
            lastShift = now;
        }

        EVERY_N_MILLISECONDS(40) { ++gHue; }

        const uint8_t brightAudio = uint8_t(255.0f * f.pixelLevel());
        const uint32_t ms = millis();

        for (int i = 0; i < n; ++i) {
            const uint8_t noise = inoise8(i * 15, ms / 60 + gHue);
            leds[i] = ColorFromPalette(mood ? twilightPal : emberPal, noise, brightAudio, LINEARBLEND);
        }
    }
};
