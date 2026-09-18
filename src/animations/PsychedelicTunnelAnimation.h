#pragma once
#include "../animations/Animation.h"
#include <FastLED.h>

class PsychedelicTunnelAnimation : public Animation {
public:
    void update(CRGB* leds, int count, const AudioFeatures& f) override {
        if (count <= 0) return;

        // bassLevel, not bass: the share is 0.001 to 0.049 on the microphone in
        // use and a speed term that far below the centroid term is not a term.
        float waveSpeed = f.spectrumCentroid * 0.2f + f.bassLevel * 0.8f;
        uint8_t baseHue = millis() / 10;
        for (int i = 0; i < count; i++) {
            float pos = sinf(i * 0.2f + millis() * 0.001f * waveSpeed);
            leds[i] = CHSV(baseHue + pos * 50, 255, 100 + 100 * pos);
        }
    }
};
