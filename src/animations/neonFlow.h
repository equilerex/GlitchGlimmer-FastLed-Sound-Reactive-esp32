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
        if (n <= 0) return;

        // Base color from spectrum centroid (shifted a bit)
        uint8_t baseHue = fmod(audio.spectrumCentroid * 2.0 + hueOffset, 255);

        // Brightness from level, not volume. volume is the block's RMS in absolute
        // units, so on the microphone in use it sits near 0.008 and this clamped
        // up to its 10 floor, which is the near-black strip. level is the same
        // loudness as a fraction of the loudest recent block, so it reaches 1 at
        // any gain.
        uint8_t baseBrightness = constrain(audio.hsvLevel() * 255.0f, 10, 255);

        // Sparkle intensity from level. energy is a raw magnitude sum in the
        // hundreds, so dividing it by 30 pinned this at its 20 ceiling on every
        // frame of real audio and it counted nothing.
        int sparkles = constrain(int(audio.pixelLevel() * 20.0f), 0, 20);

        // Smooth rainbow background
        for (int i = 0; i < n; ++i) {
            float offset = sin8((i * audio.trebleLevel * 8) + millis() / 10) / 255.0;
            uint8_t hue = baseHue + offset * 32;
            // Clamped after the subtraction, not before. baseBrightness bottoms
            // out at 10 and (i % 16) reaches 15, so five pixels in every sixteen
            // used to wrap to near-maximum instead of dimming.
            int dimmed = int(baseBrightness) - (i % 16);
            if (dimmed < 10) dimmed = 10;
            uint8_t brightness = uint8_t(dimmed);
            leds[i] = CHSV(hue, 255, brightness);
        }

        // Add bass pulses as wave. bassLevel and not bass, because bass is a share
        // of the spectrum and reads 0.001 to 0.049 on the microphone in use, which
        // put this wave at a fortieth of its range and read as black.
        for (int i = 0; i < n; ++i) {
            float wave = sin8((millis() / 4 + i * 5)) / 255.0;
            leds[i] += CHSV(0, 255, audio.bassLevel * wave * 255);
        }

        // Midrange shimmer
        for (int i = 0; i < n; i += 5) {
            if (random(0, 100) < audio.midLevel * 80) {
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

        // Peak flash on ends. Tested against level rather than peak: peak is the
        // largest single sample in the block, which on this microphone does not
        // reach 0.95 during music, so this never fired.
        if (audio.level > 0.95f) {
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
        // Wrapped rather than left to grow. At roughly 9 units a second this
        // passes float's exact-integer range in a few days of continuous running,
        // after which the increments stop landing.
        // Driven by level, modulated by tempo and activity if available.
        const float tempoFactor = audio.music.initialized ? (0.5f + 0.5f * audio.music.tempo.value) : 1.0f;
        const float activityFactor = audio.music.initialized ? (0.5f + 0.5f * audio.music.activity.value) : 1.0f;
        hueOffset += audio.pixelLevel() * 0.3f * tempoFactor * activityFactor;
        if (hueOffset >= 255.0f || hueOffset < 0.0f) hueOffset = fmodf(hueOffset, 255.0f);
    }

    // Test seam, as on AlienPulse: bound is one hue period.
    float debugHueOffset() const { return hueOffset; }
};
