#pragma once

#include <FastLED.h>
#include "../audio/AudioFeatures.h"
#include <cmath>

#include "Animation.h"

class AlienPulseAnimation : public Animation {
private:
    float hueShift = 0;
    float lastEnergy = 0;
    float wavePhase = 0;
    float flashStrength = 0;

public:
    void update(CRGB* leds, int n, const AudioFeatures& audio) override {
        // Basic color hue from spectrum centroid
        float baseHue = fmod(audio.spectrumCentroid * 2.0f, 255.0f);

        // Calculate brightness boost from dynamics and volume
        float pulse = audio.volume * 0.6f + audio.dynamics * 0.4f;
        uint8_t brightness = constrain((int)(pulse * 255.0f), 30, 255);

        // Color based on bass/mid/treble blend
        uint8_t r = (uint8_t)(audio.bass * 255);
        uint8_t g = (uint8_t)(audio.mid * 255);
        uint8_t b = (uint8_t)(audio.treble * 255);
        CRGB blendColor = CRGB(r, g, b);

        // Fade wave trail effect using sine modulation and spectrum
        for (int i = 0; i < n; i++) {
            float t = (float)i / (float)n;
            float wobble = sinf(t * 10.0f + wavePhase) * 0.5f + 0.5f;
            float spectrumMod = audio.spectrum[i % (NUM_SAMPLES / 2)] * 2.0f;
            float energyPulse = powf(audio.energy / 1800.0f, 1.5f);
            CRGB c = blendColor;
            c.fadeToBlackBy((1.0f - spectrumMod) * 80);
            leds[i] = c.lerp8(CRGB::Black, (1.0f - wobble * energyPulse) * 255);
        }

        // Beat flash
        if (audio.beatDetected) {
            flashStrength = 255;
        } else {
            flashStrength *= 0.9f;
        }

        if (flashStrength > 5) {
            for (int i = 0; i < n; i += 6) {
                leds[(i + (millis() / 20) % 6) % n] += CHSV(baseHue, 255, (uint8_t)flashStrength);
            }
        }

        // Slow hue shift
        hueShift += audio.frequency * 0.001f;
        wavePhase += audio.volume * 0.1f + 0.01f;
    }
};
