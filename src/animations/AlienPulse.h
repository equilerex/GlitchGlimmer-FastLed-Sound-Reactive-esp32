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

        // Color based on bass/mid/treble blend. The bandLevels and not the shares:
        // a share is small on broadband audio, measured at 0.001 to 0.049 for bass
        // on the microphone in use, so this blend was three near-black channels and
        // the strip rendered dark with music playing.
        uint8_t r = (uint8_t)(audio.bassLevel * 255);
        uint8_t g = (uint8_t)(audio.midLevel * 255);
        uint8_t b = (uint8_t)(audio.trebleLevel * 255);
        CRGB blendColor = CRGB(r, g, b);

        // Fade wave trail effect using sine modulation and spectrum
        for (int i = 0; i < n; i++) {
            float t = (float)i / (float)n;
            float wobble = sinf(t * 10.0f + wavePhase) * 0.5f + 0.5f;
            float spectrumMod = audio.spectrum[i % (NUM_SAMPLES / 2)] * 2.0f;
            // level, not energy / 1800. The divisor was a guess about the input's
            // absolute scale: energy is a sum of 255 magnitudes and the microphone
            // in use reaches a few hundred, so this landed near 0.01 and the strip
            // rendered black with music playing. Curved as well, because at the
            // level this microphone actually reports it was 16 percent duty.
            const float pulse = audio.pixelLevel();
            CRGB c = blendColor;
            // Both arguments are clamped at zero. fadeToBlackBy takes a uint8_t,
            // and both expressions go negative on real audio: spectrum bins pass
            // 0.5 (so 1 - spectrumMod does) and a pulse over 1 takes the lerp
            // fraction with it. A negative float converted to uint8_t wraps to
            // near-maximum, so these faded to black when they were meant to barely
            // fade at all.
            const float fadeAmt = (1.0f - spectrumMod) * 80.0f;
            // Carries the pulse and the wave separately. The pulse scales the
            // pixel's brightness, which is a multiply on the colour; the wave is
            // what puts the trail in, which is the fraction carried toward black.
            // Multiplying the two into one lerp fraction, as this did, made the
            // trail depth depend on how loud the moment was: at a pulse of 0.16
            // the fraction came out 0.84 and threw away 84 percent of the colour
            // wherever the wave was, so the animation was dark exactly when it was
            // meant to be loud.
            const float lerpAmt = (1.0f - wobble) * 255.0f;
            c.fadeToBlackBy(uint8_t(constrain(fadeAmt, 0.0f, 255.0f)));
            c.nscale8(uint8_t(constrain(pulse * 255.0f, 0.0f, 255.0f)));
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
        wavePhase += audio.pixelLevel() * 0.1f + 0.01f;
        if (wavePhase >= 6.2831853f) wavePhase -= 6.2831853f;
    }

    // Test seam. The soak drives this for 20000 frames and asserts the value
    // never leaves one sine period, which is the invariant the wrap establishes.
    float debugWavePhase() const { return wavePhase; }
};
