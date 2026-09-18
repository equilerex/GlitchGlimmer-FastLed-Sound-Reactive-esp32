#pragma once
#include "../animations/Animation.h"
#include <FastLED.h>

// Ported from digital-rgb-led-universal-controller's AuroraAnimation. The motion is
// the original's: a Perlin field walked along the strip one step per 50 ms, slow
// enough to read as one drifting gesture rather than as shimmer. The palette is
// written out rather than keeping the original's four trailing black entries, which
// put a quarter of the strip at zero whenever the field crossed them.
//
// The one change the port makes is brightness. Nothing in Serenity reads audio, so
// the original rendered at full scale in silence. Here the palette lookup runs at
// full value and the audio curve is what brings it down. That is the shape of every
// port: no spatial reaction to the music, but a strip that is dark when nothing is
// playing.
class AuroraAnimation : public Animation {
    uint16_t       t = 0;
    CRGBPalette16  pal;

public:
    AuroraAnimation() {
        pal = CRGBPalette16(
            CRGB(0, 60, 30),   CRGB(0, 120, 60),   CRGB(0, 180, 90),   CRGB(20, 230, 140),
            CRGB(40, 90, 200), CRGB(70, 60, 235),  CRGB(130, 80, 255), CRGB(180, 120, 255),
            CRGB(0, 160, 220), CRGB(0, 110, 190),  CRGB(10, 70, 150),  CRGB(20, 30, 100),
            CRGB(60, 20, 110), CRGB(110, 40, 170), CRGB(150, 70, 210), CRGB(90, 150, 245));
    }

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;
        EVERY_N_MILLISECONDS(50) { ++t; }
        const uint8_t bright = uint8_t(255.0f * f.pixelLevel());
        for (int i = 0; i < n; ++i) {
            const uint8_t noise = inoise8(uint16_t(i * 20), t);
            leds[i] = ColorFromPalette(pal, noise, bright, LINEARBLEND);
        }
    }
};
