#pragma once

#include <FastLED.h>
#include "../audio/AudioFeatures.h"
#include "../config/Config.h"
#include "../animations/Animation.h"

class PsychedelicInkSquirtAnimation : public Animation {
private:
    uint8_t hueBase = 0;
    float offset = 0;
    float velocity = 0.2f;
    unsigned long lastUpdate = 0;

public:
    void begin() override {
        hueBase = 0;
        offset = 0;
        lastUpdate = millis();
    }

    void update(CRGB* leds, int n, const AudioFeatures& audio) override {
        unsigned long now = millis();
        float deltaTime = (now - lastUpdate) / 1000.0f;
        lastUpdate = now;

        hueBase += (audio.beatDetected ? 10 : 1);
        offset += deltaTime * (0.1f + audio.bass * 2.0f + audio.energy * 0.05f);

        float squidWave = sin8(millis() / 8) / 255.0f;
        float blobIntensity = audio.volume + (audio.dynamics * 0.5f);

        for (int i = 0; i < n; ++i) {
            float wave = sinf((i * 0.3f) + offset) * blobIntensity * 255.0f;
            uint8_t brightness = constrain((uint8_t)(wave), 15, 255);
            uint8_t hue = hueBase + (i * 3) + (uint8_t)(audio.spectrumCentroid * 2.0f);
            leds[i] = CHSV(hue, 255, brightness);
        }

        // Add sparkle on beat
        if (audio.beatDetected) {
            for (int s = 0; s < 5; ++s) {
                int pos = random(n);
                leds[pos] += CHSV(hueBase + random8(), 200, 255);
            }
        }
    }
};