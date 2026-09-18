#pragma once
#include "Animation.h"
#include <FastLED.h>

class TomorrowlandStageAnimation : public Animation {
    uint8_t       gHue;
    uint16_t      noiseOffset;
    uint8_t       laserPos;
    bool          laserActive;
    CRGBPalette16 pal;

public:
    TomorrowlandStageAnimation()
        : gHue(0), noiseOffset(0), laserPos(0), laserActive(false) {
        pal = CRGBPalette16(
            CRGB::Purple,    CRGB::HotPink,   CRGB::Cyan,       CRGB::Lime,
            CRGB::BlueViolet,CRGB::Magenta,   CRGB::Turquoise,  CRGB::Yellow,
            CRGB::Indigo,    CRGB::DeepPink,  CRGB::SkyBlue,    CRGB::SpringGreen,
            CRGB::DarkViolet,CRGB::Fuchsia,   CRGB::DodgerBlue, CRGB::GreenYellow
        );
    }

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        EVERY_N_MILLISECONDS(20) { gHue += 2; noiseOffset += 3; }
        fadeToBlackBy(leds, n, 25);

        const uint8_t brightAudio = uint8_t(255.0f * f.pixelLevel());

        // Stage LED waves
        for (int i = 0; i < n; ++i) {
            const uint8_t noise = inoise8(i * 18 + noiseOffset, gHue * 2);
            leds[i] = ColorFromPalette(pal, noise + gHue, brightAudio, LINEARBLEND);
        }

        // Fast laser sweeps
        if (!laserActive && random8() < 25) {
            laserActive = true;
            laserPos = 0;
        }
        if (laserActive) {
            if (laserPos < n) {
                leds[laserPos] = CRGB(brightAudio, brightAudio, brightAudio);
                laserPos += 4;
            } else {
                laserActive = false;
            }
        }

        // Pyro flashes
        if (random8() < 8) {
            const int p = random16(n);
            leds[p] = CRGB(brightAudio, brightAudio, brightAudio);
        }

        // Symmetry mirror for stage feel
        const int half = n / 2;
        for (int i = 0; i < half; ++i) {
            leds[n - 1 - i] = leds[i];
        }
    }
};
