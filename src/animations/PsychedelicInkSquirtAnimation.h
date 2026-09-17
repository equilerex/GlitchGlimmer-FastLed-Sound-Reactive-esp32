#pragma once

#include <FastLED.h>
#include <cmath>
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
        if (n <= 0) return;

        unsigned long now = millis();
        float deltaTime = (now - lastUpdate) / 1000.0f;
        lastUpdate = now;

        hueBase += (audio.beatDetected ? 10 : 1);
        // Wrapped at the sine's own period, so the wave is unchanged. Unbounded,
        // and moving at up to 0.1 + 2.0 + 0.5 units a second, it loses float
        // precision over a long run.
        // level, not energy. energy is a raw FFT magnitude sum in the hundreds,
        // so the old energy * 0.05 term contributed 5 to 50 units a second and
        // swamped the two musical terms beside it.
        offset += deltaTime * (0.1f + audio.bass * 2.0f + audio.pixelLevel() * 0.5f);
        if (offset >= 6.2831853f || offset < 0.0f) offset = fmodf(offset, 6.2831853f);

        // level, not volume, for the gain reason the rest of the sweep shares:
        // volume is an absolute RMS near 0.008 on the microphone in use, which
        // left this at barely its dynamics term. Curved, because the curve is
        // what this expression is: a brightness.
        float blobIntensity = audio.hsvLevel() + (audio.dynamics * 0.5f);

        for (int i = 0; i < n; ++i) {
            float wave = sinf((i * 0.3f) + offset) * blobIntensity * 255.0f;
            // Clamped while still a float. sinf goes negative on half its cycle
            // and blobIntensity reaches 1.5, so wave spans roughly -382 to 382,
            // and converting a negative float to uint8_t is undefined. The
            // constrain was already here, but it ran after the cast.
            uint8_t brightness = uint8_t(constrain(wave, 15.0f, 255.0f));
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

    // Test seam, as on AlienPulse: bound is one sine period.
    float debugOffset() const { return offset; }
};