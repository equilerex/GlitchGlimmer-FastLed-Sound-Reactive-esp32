#pragma once
#include "Animation.h"
#include <FastLED.h>

class ColorWavesAnimation : public Animation {
    uint16_t sPseudotime;
    uint16_t sLastMillis;
    uint16_t sHue16;

public:
    ColorWavesAnimation() : sPseudotime(0), sLastMillis(0), sHue16(0) {}

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        const uint16_t ms = millis();
        const uint16_t deltams = (sLastMillis == 0) ? 1 : (ms - sLastMillis);
        sLastMillis = ms;

        const uint8_t valScale = uint8_t(255.0f * f.hsvLevel());
        const uint8_t brightdepth = beatsin88(341, 96, 224);
        const uint16_t brightnessthetainc16 = beatsin88(203, 25 * 256, 40 * 256);
        const uint8_t msmultiplier = beatsin88(147, 23, 60);

        sPseudotime += deltams * msmultiplier;
        sHue16 += deltams * beatsin88(400, 5, 9);
        uint16_t hue16 = sHue16;
        const uint16_t hueinc16 = beatsin88(113, 1, 3000);
        uint16_t brightnesstheta16 = sPseudotime;

        for (int i = 0; i < n; ++i) {
            hue16 += hueinc16;
            uint8_t hue8 = hue16 / 256;
            if ((hue16 >> 7) & 0x100) hue8 = 255 - hue8;
            const uint8_t rawBright = qadd8(brightdepth, sin8(brightnesstheta16));
            const uint8_t bright = scale8(rawBright, valScale);
            brightnesstheta16 += brightnessthetainc16;
            leds[i] = CHSV(hue8, 240, bright);
        }
    }
};
