#pragma once
#include "Animation.h"
#include <FastLED.h>

class MoonlightAnimation : public Animation {
    uint8_t       starPos[8];
    CRGBPalette16 nightPal;

public:
    MoonlightAnimation() {
        nightPal = CRGBPalette16(
            CRGB(0, 0, 30),   CRGB(0, 0, 80),   CRGB(10, 0, 120),  CRGB(20, 0, 160),
            CRGB(30, 20, 180),CRGB(40, 40, 200),CRGB(50, 60, 220), CRGB(70, 80, 255),
            CRGB(0, 0, 30),   CRGB(0, 0, 80),   CRGB(10, 0, 120),  CRGB(20, 0, 160),
            CRGB(30, 20, 180),CRGB(40, 40, 200),CRGB(50, 60, 220), CRGB(70, 80, 255)
        );
        for (int i = 0; i < 8; ++i) starPos[i] = i * 12;
    }

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        fadeToBlackBy(leds, n, 20);

        const uint32_t ms = millis();
        const uint8_t brightAudio = uint8_t(255.0f * f.pixelLevel());

        // Slow night clouds
        for (int i = 0; i < n; ++i) {
            const uint8_t noise = inoise8(i * 12, ms / 120);
            leds[i] = ColorFromPalette(nightPal, noise, brightAudio, LINEARBLEND);
        }

        // Soft twinkling stars
        for (int i = 0; i < 8; ++i) {
            const int p = starPos[i] % n;
            const uint8_t twinkle = beatsin8(8 + i * 2, 40, 255);
            const uint8_t starVal = scale8(twinkle, brightAudio);
            leds[p] += CRGB(starVal, starVal, starVal);
        }
    }
};
