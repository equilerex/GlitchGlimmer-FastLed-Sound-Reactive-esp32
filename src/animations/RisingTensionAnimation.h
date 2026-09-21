#pragma once
#include "Animation.h"
#include "BeatClock.h"
#include <FastLED.h>

// Buildup riser. Two heads sweep from the ends toward the middle and back. The
// sweep is one out-and-back per two beats while nothing is building, and speeds
// up to four times that as tension rises, and the hue, the centre flash and a
// final white-out all follow the same tension. Tension is f.buildup's own
// progress, not a timer, so the riser tightens when the music does and lets go
// when it stops.
class RisingTensionAnimation : public Animation {
    BeatClock   clock;
    TensionRamp ramp;
    float       sweep;

public:
    RisingTensionAnimation() : sweep(0.0f) {}

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        fadeToBlackBy(leds, n, 40);

        clock.update(f, 90.0f);
        const float tension = ramp.update(f, clock.dt);

        // An accumulator, not a multiple of the beat phase: multiplying a phase
        // by a changing factor jumps it, and this one changes every frame.
        sweep += clock.dt * clock.bpmUsed * (1.0f / 60.0f) * 0.5f * (1.0f + 3.0f * tension);
        while (sweep >= 1.0f) sweep -= 1.0f;

        const float travel = 0.5f - 0.5f * cosf(6.2831853f * sweep);
        const uint16_t headPos = uint16_t(travel * float(n / 2));
        const uint16_t left = headPos;
        const uint16_t right = (n - 1) - headPos;

        const uint8_t valHsv = uint8_t(255.0f * f.hsvLevel());
        const uint8_t brightAudio = uint8_t(255.0f * f.pixelLevel());
        const uint8_t hue = uint8_t(30.0f + tension * 130.0f);

        leds[left]  += CHSV(hue, 240, valHsv);
        leds[right] += CHSV(hue + 32, 240, valHsv);

        // Late in the build the centre flashes on every beat.
        if (tension > 0.65f) {
            const uint8_t flashVal = scale8(uint8_t(255.0f * clock.pulse(3.0f)), brightAudio);
            leds[n / 2] += CRGB(flashVal, flashVal, flashVal);
        }

        // And the whole strip lifts toward white just before it lets go.
        if (tension > 0.85f) {
            const uint8_t lift = scale8(uint8_t((tension - 0.85f) * 6.0f * 255.0f * 0.4f), brightAudio);
            for (int i = 0; i < n; ++i) leds[i] += CRGB(lift, lift, lift);
        }
    }
};
