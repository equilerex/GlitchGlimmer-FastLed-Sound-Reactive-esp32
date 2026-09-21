// ThemeAnimations.h
#pragma once
#include "Animation.h"
#include "BeatClock.h"
#include <FastLED.h>

// Animations ported from the Serenity controller's themes folder. Each was a free
// running effect there (a fixed beatsin, a constant spawn rate, a constant hue
// step) and none read the audio. Here each one keeps its look and takes its speed,
// density and pulse from the music: tempo and beat phase for motion, activity for
// how much happens, brightness and treble for colour, level for brightness.
//
// Kept in one header because they are small and share the same shape. A port that
// grows past a screen belongs in its own file.

namespace theme {

// 0 at rest, 1 at the far end, eased at both ends. `phase` is 0..1 per cycle.
inline float pingPong(float phase) {
    return 0.5f - 0.5f * cosf(6.2831853f * phase);
}

inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

}  // namespace theme

// ---------------------------------------------------------------------------
// Juggle: several dots weaving through each other. Dot count follows activity,
// speed follows tempo, and each beat flares every dot.
class JuggleAnimation : public Animation {
    static const int kMaxDots = 8;
    float     dotPhase[kMaxDots];
    BeatClock clock;

public:
    JuggleAnimation() {
        for (int i = 0; i < kMaxDots; ++i) dotPhase[i] = i * 0.125f;
    }

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        clock.update(f, 100.0f);
        const float tempoMul = 0.5f + (clock.bpmUsed / 120.0f);   // 1.0 at 120 bpm
        const int dots = 3 + int(theme::clamp01(f.music.activity.value) * float(kMaxDots - 3) + 0.5f);

        fadeToBlackBy(leds, n, 20);
        const uint8_t val = uint8_t(255.0f * f.hsvLevel() * (0.7f + 0.3f * clock.pulse(3.0f)));

        uint8_t hue = 0;
        for (int i = 0; i < dots; ++i) {
            // Each dot has its own rate, so the weave never repeats quickly.
            dotPhase[i] += clock.dt * 0.12f * float(i + 7) * (1.0f / 7.0f) * tempoMul;
            if (dotPhase[i] >= 1.0f) dotPhase[i] -= 1.0f;
            const int pos = int(theme::pingPong(dotPhase[i]) * float(n - 1) + 0.5f);
            leds[pos] |= CHSV(hue, 200, val);
            hue += 32;
        }
    }
};

// ---------------------------------------------------------------------------
// Sinelon: one head sweeping the strip, leaving a fading tail. The sweep is one
// beat out and one beat back. A busy passage shortens the tail so it stays legible.
class SinelonAnimation : public Animation {
    uint8_t   gHue;
    BeatClock clock;

public:
    SinelonAnimation() : gHue(0) {}

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        clock.update(f, 52.0f);   // the original 13 cycles a minute is 26 bpm out-and-back
        const uint8_t tail = uint8_t(14.0f + 40.0f * theme::clamp01(f.music.activity.value));
        fadeToBlackBy(leds, n, tail);

        const int pos = int(theme::pingPong(clock.phase2()) * float(n - 1) + 0.5f);
        leds[pos] += CHSV(gHue, 255, uint8_t(255.0f * f.hsvLevel()));

        // Colour drifts faster on brighter timbre.
        EVERY_N_MILLISECONDS(20) { gHue += 1 + uint8_t(3.0f * theme::clamp01(f.music.brightness.value)); }
    }
};

// ---------------------------------------------------------------------------
// Confetti: random coloured specks that fade. The rate follows activity and every
// beat throws an extra handful.
class ConfettiAnimation : public Animation {
    uint8_t   gHue;
    BeatClock clock;

public:
    ConfettiAnimation() : gHue(0) {}

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        clock.update(f, 110.0f);
        fadeToBlackBy(leds, n, 10);

        const uint8_t val = uint8_t(255.0f * f.hsvLevel());
        const int specks = 1 + int(theme::clamp01(f.music.activity.value) * 6.0f) + (clock.wrapped ? 4 : 0);
        for (int i = 0; i < specks; ++i) {
            leds[random16(n)] += CHSV(gHue + random8(64), 200, val);
        }
        EVERY_N_MILLISECONDS(20) { gHue++; }
    }
};

// ---------------------------------------------------------------------------
// Twinkle Stars: sparse points in a cool palette. Treble decides how many, so
// hats and shimmer light the sky and a bassline alone leaves it dark.
class TwinkleStarsAnimation : public Animation {
public:
    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        fadeToBlackBy(leds, n, 10);

        const uint8_t val = uint8_t(255.0f * f.hsvLevel());
        const float rate = 0.15f + 5.0f * theme::clamp01(f.trebleLevel);
        int stars = int(rate);
        if (random8() < uint8_t((rate - float(stars)) * 255.0f)) ++stars;   // the fractional part
        for (int i = 0; i < stars; ++i) {
            leds[random16(n)] += CHSV(random8(64, 192), 200, val);
        }
    }
};

// ---------------------------------------------------------------------------
// Rainbow March: a rainbow scrolling along the strip. Scroll speed follows tempo
// and the brightness swells gently on each beat.
class RainbowMarchAnimation : public Animation {
    float     hue;
    BeatClock clock;

public:
    RainbowMarchAnimation() : hue(0.0f) {}

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        clock.update(f, 100.0f);
        // The original stepped 5 hue units every 10 ms at a fixed pace.
        hue += clock.dt * 120.0f * (clock.bpmUsed / 120.0f);
        while (hue >= 256.0f) hue -= 256.0f;

        fill_rainbow(leds, n, uint8_t(hue), 2);
        const float level = f.hsvLevel() * (0.7f + 0.3f * clock.pulse(2.0f));
        for (int i = 0; i < n; ++i) leds[i].nscale8_video(uint8_t(255.0f * level));
    }
};

// ---------------------------------------------------------------------------
// Breathing: one colour swelling and settling over four beats, a bar of music per
// breath. Loudness sets how deep the swell goes.
class BreathingAnimation : public Animation {
    BeatClock clock;

public:
    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        clock.update(f, 24.0f);   // the original 6 breaths a minute, four beats each
        const float bar = (float(clock.beatCount & 3u) + clock.phase) * 0.25f;
        const float swell = theme::pingPong(bar);
        const float top = 255.0f * f.hsvLevel();
        const uint8_t val = uint8_t(20.0f + (top > 20.0f ? top - 20.0f : 0.0f) * swell);
        fill_solid(leds, n, CHSV(140, 150, val));
    }
};

// ---------------------------------------------------------------------------
// Beat Trails: a head that moves with the beat and leaves a long coloured trail.
// The original never faded, so the strip filled up. The trail fades slowly here
// and the hue steps on every beat.
class BeatTrailsAnimation : public Animation {
    uint8_t   gHue;
    BeatClock clock;

public:
    BeatTrailsAnimation() : gHue(0) {}

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        clock.update(f, 60.0f);
        fadeToBlackBy(leds, n, 12);

        const int pos = int(theme::pingPong(clock.phase2()) * float(n - 1) + 0.5f);
        leds[pos] += CHSV(gHue, 255, uint8_t(255.0f * f.hsvLevel()));
        if (clock.wrapped) gHue += 12;
    }
};

// ---------------------------------------------------------------------------
// BPM: party-palette stripes pulsing at the tempo. The pulse is the beat phase, so
// it lands on the beat rather than at a fixed 62 bpm.
class BpmAnimation : public Animation {
    uint8_t   gHue;
    BeatClock clock;

public:
    BpmAnimation() : gHue(0) {}

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        clock.update(f, 62.0f);
        const float peak = 255.0f * f.hsvLevel();
        const uint8_t beat = uint8_t(64.0f + (peak > 64.0f ? peak - 64.0f : 0.0f) * clock.pulse(2.0f));
        for (int i = 0; i < n; ++i) {
            leds[i] = ColorFromPalette(PartyColors_p, gHue + (i * 2), uint8_t(beat - gHue + (i * 10)));
        }
        EVERY_N_MILLISECONDS(20) { gHue++; }
    }
};
