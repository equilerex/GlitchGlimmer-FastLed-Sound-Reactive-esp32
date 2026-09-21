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

        const float texture = f.music.initialized ? f.music.texture.value : 0.5f;
        const float activity = f.music.initialized ? f.music.activity.value : f.dynamics;

        EVERY_N_MILLISECONDS(235) { gHue++; }
        // Scale ripple progression speed by activity and texture
        const uint32_t speedDiv = (activity > 0.6f) ? 30 : ((activity < 0.2f) ? 70 : 50);
        const uint32_t ms = millis();
        const uint8_t brightAudio = uint8_t(255.0f * f.pixelLevel());
        const uint16_t spatialScale = uint16_t(15 + texture * 20.0f);

        for (int i = 0; i < n; ++i) {
            const uint8_t index = inoise8(i * spatialScale, ms / speedDiv) + gHue;
            leds[i] = ColorFromPalette(pal, index, brightAudio, LINEARBLEND);
        }
    }
};
