#pragma once
#include "../animations/Animation.h"
#include <FastLED.h>

class AlienBreathAnimation : public Animation {
public:
    void update(CRGB* leds, int count, const AudioFeatures& f) override {
        if (count <= 0) return;

        // Modulate breath frequency and hue via music coordinates if available
        const float presence = f.music.initialized ? f.music.presence.value : f.gateGain;
        const float brightnessCoord = f.music.initialized ? f.music.brightness.value : (f.spectrumCentroid * 0.01f);

        const float rate = 0.0008f + presence * 0.0008f;
        const float breath = sinf(millis() * rate) * 0.5f + 0.5f;

        const uint8_t hue = uint8_t(150 + brightnessCoord * 40.0f + f.spectrumCentroid * 0.1f);
        // Base brightness driven by hsvLevel to preserve perceptual visibility across gains
        const float drive = f.hsvLevel() * (0.6f + 0.4f * breath);
        const uint8_t val = constrain(uint8_t(drive * 255.0f * (0.4f + 0.6f * presence)), 0, 255);

        CRGB color = CHSV(hue, 200, val);
        fill_solid(leds, count, color);
    }
};
