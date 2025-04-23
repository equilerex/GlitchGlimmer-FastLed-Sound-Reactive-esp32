#pragma once
#include <FastLED.h>

#include "Animation.h"
#include "../audio/AudioFeatures.h"

class NeonFlowAnimation : public Animation {
private:
    float hueOffset = 0;
    uint8_t sparkleCountdown = 0;

public:
    void update(CRGB* leds, int n, const AudioFeatures& audio) override {
        // Base color from spectrum centroid (shifted a bit)
        uint8_t baseHue = fmod(audio.spectrumCentroid * 2.0 + hueOffset, 255);

        // Brightness from volume
        uint8_t baseBrightness = constrain(audio.volume * 255, 10, 255);

        // Dynamics affect flicker intensity
        float flicker = audio.dynamics * 255;

        // Sparkle intensity from energy
        int sparkles = constrain(audio.energy / 30, 0, 20);

        // Smooth rainbow background
        for (int i = 0; i < n; ++i) {
            float offset = sin8((i * audio.treble * 8) + millis() / 10) / 255.0;
            uint8_t hue = baseHue + offset * 32;
            uint8_t brightness = baseBrightness - (i % 16);
            leds[i] = CHSV(hue, 255, brightness);
        }

        // Add bass pulses as wave
        for (int i = 0; i < n; ++i) {
            float wave = sin8((millis() / 4 + i * 5)) / 255.0;
            leds[i] += CHSV(0, 255, audio.bass * wave * 255);
        }

        // Midrange shimmer
        for (int i = 0; i < n; i += 5) {
            if (random(0, 100) < audio.mid * 80) {
                leds[i] += CHSV(96, 255, 200);
            }
        }

        // Treble sparkles
        for (int i = 0; i < sparkles; ++i) {
            int pos = random(0, n);
            leds[pos] = CHSV(160 + random(30), 255, 255);
        }

        // Beat flash (pulse all)
        if (audio.beatDetected) {
            for (int i = 0; i < n; ++i) {
                leds[i] += CHSV(random8(), 255, 255);
            }
        }

        // Peak flash on ends
        if (audio.peak > 0.95f) {
            leds[0] = CRGB::White;
            leds[n - 1] = CRGB::White;
        }

        // Center spike from dominant frequency band
        int center = map(audio.dominantBand, 0, NUM_SAMPLES / 2, 0, n);
        for (int i = -2; i <= 2; ++i) {
            int pos = constrain(center + i, 0, n - 1);
            leds[pos] = CHSV(200, 255, 255);
        }

        // Fade effect
        for (int i = 0; i < n; ++i) {
            leds[i].nscale8(240);
        }

        // Advance hue slowly
        hueOffset += audio.loudness / 100.0f;
    }
};
