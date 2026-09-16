#pragma once

#include <FastLED.h>
#include "../audio/AudioFeatures.h"
#include <cmath>

#include "Animation.h"

class AlienPulseAnimation : public Animation {
private:
    float wavePhase = 0;
    float flashStrength = 0;

public:
    void update(CRGB* leds, int n, const AudioFeatures& audio) override {
        if (n <= 0) return;

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
            // Both arguments are clamped at zero. fadeToBlackBy takes a uint8_t,
            // and both expressions go negative on real audio: spectrum bins pass
            // 0.5 (so 1 - spectrumMod does) and energy passes 1800, which takes
            // energyPulse past 1 and the lerp fraction with it. A negative float
            // converted to uint8_t wraps to near-maximum, so these faded to black
            // when they were meant to barely fade at all.
            const float fadeAmt = (1.0f - spectrumMod) * 80.0f;
            const float lerpAmt = (1.0f - wobble * energyPulse) * 255.0f;
            c.fadeToBlackBy(uint8_t(constrain(fadeAmt, 0.0f, 255.0f)));
            leds[i] = c.lerp8(CRGB::Black, uint8_t(constrain(lerpAmt, 0.0f, 255.0f)));
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

        // Slow phase advance, wrapped. Unbounded it loses float precision over a
        // long run, and this one adds roughly 0.1 a frame.
        wavePhase += audio.volume * 0.1f + 0.01f;
        if (wavePhase >= 6.2831853f) wavePhase -= 6.2831853f;
    }

    // Test seam. The soak drives this for 20000 frames and asserts the value
    // never leaves one sine period, which is the invariant the wrap establishes.
    float debugWavePhase() const { return wavePhase; }
};
