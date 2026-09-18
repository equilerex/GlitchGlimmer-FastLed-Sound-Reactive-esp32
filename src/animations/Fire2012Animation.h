#pragma once
#include "Animation.h"
#include <FastLED.h>
#include "fx/1d/fire2012.h"

// Include this header from one translation unit only (AnimationCatalog.cpp).
//
// FastLED ships Fire2012. The default HeatColors_p ramp ends in white, which
// fails the catalog's mid-contrast rule (full white is a flash and a power
// spike). The cooling/sparking simulation is still the right engine: the
// constructor takes a palette, so the restyle is that argument, not a second
// fire. This ramp stops at amber.
//
// The simulation is stateless in time. DrawContext's now is unused. Rebuild
// when the strip length changes, because the heat vector is sized at
// construction.
class Fire2012Animation : public Animation {
    fl::Fire2012Ptr fx;
    int             fxLen = -1;

    static CRGBPalette16 emberPalette() {
        return CRGBPalette16(
            CRGB(0, 0, 0),      CRGB(20, 0, 0),     CRGB(60, 4, 0),     CRGB(100, 12, 0),
            CRGB(140, 24, 0),   CRGB(180, 40, 4),   CRGB(210, 64, 8),   CRGB(230, 90, 16),
            CRGB(240, 110, 20), CRGB(250, 130, 24), CRGB(255, 150, 32), CRGB(255, 165, 40),
            CRGB(255, 175, 48), CRGB(255, 180, 52), CRGB(255, 170, 40), CRGB(255, 140, 24));
    }

public:
    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        if (!fx || fxLen != n) {
            fx    = fl::make_shared<fl::Fire2012>(uint16_t(n), 55, 120, false, emberPalette());
            fxLen = n;
        }

        fx->draw(fl::Fx::DrawContext(millis(), leds));

        const uint8_t bright = uint8_t(255.0f * f.pixelLevel());
        for (int i = 0; i < n; ++i) leds[i].nscale8_video(bright);
    }
};
