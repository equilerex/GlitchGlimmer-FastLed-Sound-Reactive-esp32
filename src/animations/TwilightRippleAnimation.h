#pragma once
#include "Animation.h"
#include <FastLED.h>

class TwilightRippleAnimation : public Animation {
    uint8_t       gHue;
    CRGBPalette16 pal;

public:
    TwilightRippleAnimation()
        : gHue(0) {
        pal = CRGBPalette16(
            CRGB::Blue,     CRGB::Purple,   CRGB(0, 128, 128), CRGB(0, 80, 40),
            CRGB(0, 0, 140),CRGB(75, 0, 130),CRGB::Cyan,       CRGB(20, 100, 30),
            CRGB::Blue,     CRGB::Purple,   CRGB(0, 128, 128), CRGB(0, 80, 40),
            CRGB(0, 0, 140),CRGB(75, 0, 130),CRGB::Cyan,       CRGB(20, 100, 30)
        );
    }

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        EVERY_N_MILLISECONDS(235) { gHue++; }
        const uint32_t ms = millis();
        const uint8_t brightAudio = uint8_t(255.0f * f.pixelLevel());

        for (int i = 0; i < n; ++i) {
            const uint8_t index = inoise8(i * 20, ms / 50) + gHue;
            leds[i] = ColorFromPalette(pal, index, brightAudio, LINEARBLEND);
        }
    }
};
