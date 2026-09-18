#pragma once
#include "Animation.h"
#include <FastLED.h>

class SpaceWizardsAndLizardsAnimation : public Animation {
    uint8_t       gHue;
    uint16_t      t;
    uint32_t      wizardSeed;
    bool          isWizardPhase;
    uint32_t      lastPhaseSwitch;
    CRGBPalette16 wizardPal;
    CRGBPalette16 lizardPal;

public:
    SpaceWizardsAndLizardsAnimation()
        : gHue(0), t(0), wizardSeed(1337), isWizardPhase(true), lastPhaseSwitch(0) {
        wizardPal = CRGBPalette16(
            CRGB(0, 180, 255),  CRGB(80, 0, 255),   CRGB(0, 255, 200),  CRGB(120, 20, 255),
            CRGB(0, 140, 255),  CRGB(160, 40, 255), CRGB(0, 200, 240),  CRGB(100, 0, 240),
            CRGB(0, 180, 255),  CRGB(80, 0, 255),   CRGB(0, 255, 200),  CRGB(120, 20, 255),
            CRGB(0, 140, 255),  CRGB(160, 40, 255), CRGB(0, 200, 240),  CRGB(100, 0, 240)
        );
        lizardPal = CRGBPalette16(
            CRGB(50, 255, 50),  CRGB(200, 255, 0),  CRGB(0, 255, 120),  CRGB(255, 180, 0),
            CRGB(20, 200, 40),  CRGB(180, 220, 0),  CRGB(0, 240, 160),  CRGB(240, 160, 0),
            CRGB(50, 255, 50),  CRGB(200, 255, 0),  CRGB(0, 255, 120),  CRGB(255, 180, 0),
            CRGB(20, 200, 40),  CRGB(180, 220, 0),  CRGB(0, 240, 160),  CRGB(240, 160, 0)
        );
    }

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        EVERY_N_MILLISECONDS(40) { ++gHue; ++t; }

        const uint32_t now = millis();
        if (now - lastPhaseSwitch > 6000) {
            isWizardPhase = !isWizardPhase;
            lastPhaseSwitch = now;
            wizardSeed = random16();
        }

        const uint8_t brightAudio = uint8_t(255.0f * f.pixelLevel());

        if (isWizardPhase) {
            // Arcane wizard spell: shifting noise with random bright pops
            for (int i = 0; i < n; ++i) {
                const uint8_t noise = inoise8(i * 15, (now / 4) + wizardSeed);
                leds[i] = ColorFromPalette(wizardPal, noise, brightAudio, LINEARBLEND);
                if (random8() < 6) leds[i] += CRGB(brightAudio, brightAudio, brightAudio);
            }
        } else {
            // Lizard aura: periodic spaced spikes with color stepping
            fadeToBlackBy(leds, n, 30);
            for (int i = 0; i < n; ++i) {
                if (i % 6 == 0) {
                    const uint8_t idx = uint8_t(gHue + i * 4 + t);
                    leds[i] = ColorFromPalette(lizardPal, idx, brightAudio, LINEARBLEND);
                }
            }
            if (random8() < 25) {
                const uint16_t burstPos = beatsin16(8, 0, n - 1);
                leds[burstPos] = CRGB(0, brightAudio, 0);
            }
        }
    }
};
