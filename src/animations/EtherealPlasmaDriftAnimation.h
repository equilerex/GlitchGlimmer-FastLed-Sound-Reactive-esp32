#pragma once
#include "Animation.h"
#include <FastLED.h>

class EtherealPlasmaDriftAnimation : public Animation {
    uint8_t       gHue;
    uint16_t      noiseX;
    uint16_t      noiseY;
    uint8_t       pulseBeat;
    CRGBPalette16 pal;
    CRGB          targetColor;

public:
    EtherealPlasmaDriftAnimation()
        : gHue(0), noiseX(0), noiseY(0), pulseBeat(0) {
        pal = CRGBPalette16(
            CRGB(20, 60, 255),  CRGB(60, 100, 255), CRGB(120, 40, 255), CRGB(180, 40, 255),
            CRGB(220, 60, 255), CRGB(100, 80, 255), CRGB(40, 160, 255),CRGB(20, 210, 255),
            CRGB(20, 60, 255),  CRGB(60, 100, 255), CRGB(120, 40, 255), CRGB(180, 40, 255),
            CRGB(220, 60, 255), CRGB(100, 80, 255), CRGB(40, 160, 255),CRGB(20, 210, 255)
        );
        targetColor = CHSV(128, 180, 240);
    }

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        EVERY_N_MILLISECONDS(30) { gHue++; noiseY += 2; }
        EVERY_N_MILLISECONDS(50) { noiseX++; }

        EVERY_N_MILLISECONDS(100) {
            for (uint8_t i = 0; i < 16; ++i) {
                nblend(pal[i], targetColor, 8);
            }
            targetColor = CHSV(gHue + random8(64), 180 + random8(76), 180 + random8(75));
        }

        const uint8_t brightAudio = uint8_t(255.0f * f.pixelLevel());
        for (int i = 0; i < n; ++i) {
            const uint8_t noise = inoise8(i * 12 + noiseX, noiseY + sin8(i * 3) * 2);
            leds[i] = ColorFromPalette(pal, noise + gHue / 2, brightAudio, LINEARBLEND);
        }
    }
};
