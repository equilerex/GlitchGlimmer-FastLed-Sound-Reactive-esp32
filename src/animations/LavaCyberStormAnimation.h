#pragma once
#include "Animation.h"
#include "BeatClock.h"
#include <FastLED.h>

class LavaCyberStormAnimation : public Animation {
    uint8_t       gHue;
    HoldLatch     cool;
    float         mix;
    uint32_t      lastMs;
    CRGBPalette16 emberPal;
    CRGBPalette16 twilightPal;

public:
    LavaCyberStormAnimation()
        : gHue(0), mix(0.0f), lastMs(0) {
        emberPal = CRGBPalette16(
            CRGB(255, 60, 0),  CRGB(255, 120, 0), CRGB(220, 30, 10), CRGB(180, 20, 40),
            CRGB(255, 60, 0),  CRGB(255, 120, 0), CRGB(220, 30, 10), CRGB(180, 20, 40),
            CRGB(255, 60, 0),  CRGB(255, 120, 0), CRGB(220, 30, 10), CRGB(180, 20, 40),
            CRGB(255, 60, 0),  CRGB(255, 120, 0), CRGB(220, 30, 10), CRGB(180, 20, 40)
        );
        twilightPal = CRGBPalette16(
            CRGB(40, 60, 255), CRGB(140, 30, 255),CRGB(0, 180, 255), CRGB(100, 0, 220),
            CRGB(40, 60, 255), CRGB(140, 30, 255),CRGB(0, 180, 255), CRGB(100, 0, 220),
            CRGB(40, 60, 255), CRGB(140, 30, 255),CRGB(0, 180, 255), CRGB(100, 0, 220),
            CRGB(40, 60, 255), CRGB(140, 30, 255),CRGB(0, 180, 255), CRGB(100, 0, 220)
        );
    }

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        // Ember for dark, bass-led passages and twilight for bright ones, chosen
        // by the timbre coordinate instead of a 7 s timer, and crossfaded so the
        // change is a drift rather than a cut.
        const uint32_t now = millis();
        const float dt = lastMs == 0 ? 0.0f : (now - lastMs) * 0.001f;
        lastMs = now;
        const bool wantCool = cool.update(f.music.brightness.value, 0.40f, 0.58f, 4000);
        const float step = 0.5f * dt;
        mix += wantCool ? step : -step;
        if (mix < 0.0f) mix = 0.0f;
        if (mix > 1.0f) mix = 1.0f;

        EVERY_N_MILLISECONDS(40) { ++gHue; }

        const uint8_t brightAudio = uint8_t(255.0f * f.pixelLevel());
        const uint32_t ms = millis();

        for (int i = 0; i < n; ++i) {
            const uint8_t noise = inoise8(i * 15, ms / 60 + gHue);
            if (mix <= 0.02f) {
                leds[i] = ColorFromPalette(emberPal, noise, brightAudio, LINEARBLEND);
            } else if (mix >= 0.98f) {
                leds[i] = ColorFromPalette(twilightPal, noise, brightAudio, LINEARBLEND);
            } else {
                leds[i] = blend(ColorFromPalette(emberPal, noise, brightAudio, LINEARBLEND),
                                ColorFromPalette(twilightPal, noise, brightAudio, LINEARBLEND),
                                uint8_t(mix * 255.0f));
            }
        }
    }
};
