#pragma once
#include "Animation.h"
#include <FastLED.h>
#include "fx/1d/pacifica.h"

// Include this header from one translation unit only (AnimationCatalog.cpp).
// FastLED's pacifica.h defines Pacifica's members out of line with no inline,
// so a second include becomes a second strong definition at link.

// FastLED ships Pacifica at fx/1d/pacifica.h, and it is a better drifting field
// than anything worth writing here: four wave layers at independent scales and
// speeds, whitecaps where they line up, and a palette that stays blue-green at the
// low end instead of washing out to grey. What gets written is the interface, not
// the effect. The same is true of the rest of what fl/fx ships, which is the first
// place to look before writing a field by hand.
//
// Two things the wrapper has to supply, both of them the cost of adapting an effect
// that was written to own the loop.
//
// Pacifica is constructed with a length and rebuilt when the strip length changes,
// because it is handed the buffer on every draw rather than holding it.
//
// And it measures elapsed time itself, as `now - sLastms` with sLastms starting at
// zero, so handing it `millis()` makes its first frame a delta of however long the
// board has been up and throws the wave phase by that much. The clock here starts
// when the effect does. That is the same defect `Animation::begin()` was wired to
// fix, in a class that cannot be given a begin() because it is the library's.
//
// The audio curve is the only thing this adds. Pacifica renders at full scale and
// knows nothing about music, so the finished buffer is scaled by pixelLevel(). Every
// shipped effect assumes it is the only thing driving the strip, which is exactly
// the assumption the silence gate has to break.
class PacificaAnimation : public Animation {
    fl::PacificaPtr fx;
    int             fxLen   = -1;
    fl::u32         clock   = 0;
    uint32_t        lastMs  = 0;
    bool            started = false;

public:
    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        const uint32_t now = millis();
        if (!started) {
            started = true;
            lastMs  = now;
        }
        clock += now - lastMs;
        lastMs = now;

        if (!fx || fxLen != n) {
            fx    = fl::make_shared<fl::Pacifica>(uint16_t(n));
            fxLen = n;
        }

        fx->draw(fl::Fx::DrawContext(clock, leds));

        const uint8_t bright = uint8_t(255.0f * f.pixelLevel());
        for (int i = 0; i < n; ++i) {
            leds[i].r = qadd8(leds[i].r, leds[i].r >> 2);
            leds[i].g = qadd8(leds[i].g, leds[i].g >> 2);
            leds[i].b = qadd8(leds[i].b, leds[i].b >> 2);
            leds[i].nscale8_video(bright);
        }
    }
};
