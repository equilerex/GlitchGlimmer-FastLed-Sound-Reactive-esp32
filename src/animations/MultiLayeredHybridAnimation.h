#pragma once

#include <FastLED.h>
#include "../audio/AudioFeatures.h"
#include "../audio/AudioHistoryTracker.h"
#include "../animations/AlienPulse.h"
#include "../animations/neonFlow.h"
#include "../animations/PsychedelicInkSquirtAnimation.h"
#include "../audio/AudioSnapshot.h"

class MultiLayeredHybridAnimation : public Animation {
private:
    Animation* layers[3];
    float opacities[3];
    unsigned long lastSwitch = 0;
    size_t currentIndex = 0;

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

        unsigned long nowTime = millis();
        if (nowTime - lastSwitch > 10000) {
            currentIndex = (currentIndex + 1) % 3;
            for (int i = 0; i < 3; ++i) opacities[i] = 0.3;
            opacities[currentIndex] = 1.0;
            lastSwitch = nowTime;
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