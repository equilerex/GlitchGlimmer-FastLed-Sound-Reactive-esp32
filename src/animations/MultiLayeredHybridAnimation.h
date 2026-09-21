#pragma once

#include <FastLED.h>
#include "../audio/AudioFeatures.h"
#include "../audio/AudioHistoryTracker.h"
#include "../animations/AlienPulse.h"
#include "BeatClock.h"
#include "../animations/neonFlow.h"
#include "../animations/PsychedelicInkSquirtAnimation.h"
#include "../audio/AudioSnapshot.h"

class MultiLayeredHybridAnimation : public Animation {
private:
    Animation* layers[3];
    float opacities[3];
    unsigned long lastSwitch = 0;
    size_t currentIndex = 0;
    HoldSelect leadSelect;

public:
    MultiLayeredHybridAnimation() {
        layers[0] = new AlienPulseAnimation();
        layers[1] = new NeonFlowAnimation();
        layers[2] = new PsychedelicInkSquirtAnimation();
        for (int i = 0; i < 3; ++i) opacities[i] = 0;
        opacities[0] = 1.0;
    }

    ~MultiLayeredHybridAnimation() {
        for (int i = 0; i < 3; ++i) delete layers[i];
    }

    // There is no history-consuming variant here. An earlier version kept a
    // function-local static deque to feed a second overload that never read it,
    // which leaked its first node per process and made this the only animation
    // in the catalog with a nonzero steady-state live count.
    void update(CRGB* leds, int n, const AudioFeatures& now) override {
        if (n <= 0) return;

        fill_solid(leds, n, CRGB::Black);

        // The band carrying the most energy leads: bass to Alien Pulse, mid to Neon
        // Flow, treble to the squirts. The other two stay under it at a third, and
        // opacities slew toward their targets so a change of lead is a crossfade.
        // It rotated every 10 s and snapped its opacities before this.
        unsigned long nowTime = millis();
        const float dt = lastSwitch == 0 ? 0.0f : (nowTime - lastSwitch) * 0.001f;
        lastSwitch = nowTime;
        const float scores[3] = {now.bassLevel, now.midLevel, now.trebleLevel};
        currentIndex = size_t(leadSelect.update(scores, 3, 0.15f, 4000));
        for (int i = 0; i < 3; ++i) {
            const float target = (size_t(i) == currentIndex) ? 1.0f : 0.3f;
            const float stepTo = 1.5f * dt;
            if (opacities[i] < target) opacities[i] = fminf(target, opacities[i] + stepTo);
            else                       opacities[i] = fmaxf(target, opacities[i] - stepTo);
        }

        for (int i = 0; i < 3; ++i) {
            CRGB temp[n];
            fill_solid(temp, n, CRGB::Black);
            layers[i]->update(temp, n, now);
            for (int j = 0; j < n; ++j) {
                leds[j].r = qadd8(leds[j].r, temp[j].r * opacities[i]);
                leds[j].g = qadd8(leds[j].g, temp[j].g * opacities[i]);
                leds[j].b = qadd8(leds[j].b, temp[j].b * opacities[i]);
            }
        }
    }
};