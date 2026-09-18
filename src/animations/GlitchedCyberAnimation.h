#pragma once
#include "Animation.h"
#include <FastLED.h>

class GlitchedCyberAnimation : public Animation {
    uint8_t       gHue;
    CRGBPalette16 neonPal;

public:
    GlitchedCyberAnimation() : gHue(0) {
        neonPal = CRGBPalette16(
            CRGB(255, 20, 147), CRGB(0, 255, 240), CRGB(180, 0, 255), CRGB(50, 255, 50),
            CRGB(255, 0, 100),  CRGB(0, 200, 255), CRGB(140, 0, 220), CRGB(100, 255, 0),
            CRGB(255, 20, 147), CRGB(0, 255, 240), CRGB(180, 0, 255), CRGB(50, 255, 50),
            CRGB(255, 0, 100),  CRGB(0, 200, 255), CRGB(140, 0, 220), CRGB(100, 255, 0)
        );
    }

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        EVERY_N_MILLISECONDS(40) { gHue++; }

        fadeToBlackBy(leds, n, 45);

        const uint8_t brightAudio = uint8_t(255.0f * f.pixelLevel());
        const uint8_t density = 35;

        for (int i = 0; i < n; ++i) {
            if (random8() < density) {
                const uint8_t glitchType = random8(3);
                if (glitchType == 0) {
                    leds[i] = ColorFromPalette(neonPal, gHue + random8(64), brightAudio);
                } else if (glitchType == 1) {
                    const int target = i + random8(4);
                    if (target < n) {
                        leds[target] = ColorFromPalette(neonPal, gHue, brightAudio);
                    }
                } else {
                    leds[i] += CHSV(gHue + 96, 220, scale8(random8(120, 255), brightAudio));
                }
            }
        }

        blur1d(leds, n, 40);
    }
};
