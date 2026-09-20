#pragma once
#include "Animation.h"
#include <FastLED.h>
#include "fl/fx/1d/noisewave.h"

// Include this header from one translation unit only (AnimationCatalog.cpp).
// Same rule as PacificaAnimation.h, even though NoiseWave's methods are inline
// in the class: keep every shipped fx include off AnimationCatalog.h.
//
// FastLED ships this as a two-channel noise field (red and a quieter blue,
// green held at zero). That is a 2-hue wash with one travelling gesture, which
// is why it is in the catalog rather than rewritten. It is not Pacifica: after
// the first draw it times itself with millis(), so the DrawContext's now is
// only the sentinel that arms start_time. Pass wall millis() in, not a
// synthetic clock, or start_time latches a small number and the first frame
// jumps by however long the board has been up.
//
// Brightness is ours. The shipped draw writes full scale.
class NoiseWaveAnimation : public Animation {
    fl::NoiseWavePtr fx;
    int              fxLen = -1;

public:
    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        if (!fx || fxLen != n) {
            fx    = fl::make_shared<fl::NoiseWave>(uint16_t(n));
            fxLen = n;
        }

        fx->draw(fl::Fx::DrawContext(millis(), fl::span<CRGB>(leds, n)));

        const uint8_t bright = uint8_t(255.0f * f.pixelLevel());
        for (int i = 0; i < n; ++i) {
            leds[i].r = qadd8(leds[i].r, leds[i].r >> 1);
            leds[i].b = qadd8(leds[i].b, leds[i].b >> 1);
            leds[i].nscale8_video(bright);
        }
    }
};
