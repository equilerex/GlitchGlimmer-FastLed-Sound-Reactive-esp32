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

// ---------------------------------------------------------------------------
// Liquid Dream: slow, dreamy multi-sine wave interference with breathing pulse.
// Speed follows tempo and brightness swells with the music.
class LiquidDreamAnimation : public Animation {
    float     hue;
    float     wavePhaseA;
    float     wavePhaseB;
    float     wavePhaseC;
    BeatClock clock;

public:
    LiquidDreamAnimation()
        : hue(0.0f), wavePhaseA(0.0f), wavePhaseB(0.0f), wavePhaseC(0.0f) {}

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        clock.update(f, 40.0f);
        const float tempoMul = 0.5f + (clock.bpmUsed / 120.0f);
        wavePhaseA += clock.dt * 0.4f * tempoMul;
        wavePhaseB -= clock.dt * 0.3f * tempoMul;
        wavePhaseC += clock.dt * 0.2f * tempoMul;
        while (wavePhaseA >= 6.2831853f) wavePhaseA -= 6.2831853f;
        while (wavePhaseB <= -6.2831853f) wavePhaseB += 6.2831853f;
        while (wavePhaseC >= 6.2831853f) wavePhaseC -= 6.2831853f;

        hue += clock.dt * 6.0f * (0.5f + 1.5f * theme::clamp01(f.music.brightness.value));
        while (hue >= 256.0f) hue -= 256.0f;

        const float pulseCenter = 0.5f + 0.3f * sinf(clock.phase * 6.2831853f);
        const float valMaster = 255.0f * f.hsvLevel();

        for (int i = 0; i < n; ++i) {
            const float posNorm = (n > 1) ? (float)i / (float)(n - 1) : 0.5f;
            const float w1 = sinf(posNorm * 6.28f * 1.5f + wavePhaseA);
            const float w2 = sinf(posNorm * 6.28f * 2.2f + wavePhaseB);
            const float w3 = cosf(posNorm * 6.28f * 3.1f + wavePhaseC);
            const float combined = (w1 + w2 + w3) * 0.333f;

            const float dist = (posNorm - pulseCenter) * 3.5f;
            const float pulse = expf(-dist * dist);

            const uint8_t pixHue = uint8_t(hue + combined * 35.0f + pulse * 25.0f);
            const uint8_t pixVal = uint8_t(valMaster * (0.6f + 0.4f * pulse) * (0.7f + 0.3f * (combined + 1.0f) * 0.5f));
            leds[i] = CHSV(pixHue, 210, pixVal);
        }
    }
};

// ---------------------------------------------------------------------------
// Dreamwave Aurora: noise-driven aurora palette drift with subtle treble shimmer.
// Palette shifts with musical weight and brightness.
class DreamwaveAuroraAnimation : public Animation {
    uint16_t      noiseX;
    uint8_t       colorLoop;
    CRGBPalette16 currentPalette;
    CRGBPalette16 targetPalette;
    BeatClock     clock;

public:
    DreamwaveAuroraAnimation()
        : noiseX(0), colorLoop(0), currentPalette(OceanColors_p), targetPalette(PartyColors_p) {}

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        clock.update(f, 50.0f);
        const float tempoMul = 0.5f + (clock.bpmUsed / 120.0f);
        noiseX += uint16_t(clock.dt * 40.0f * tempoMul + 1.0f);
        colorLoop += uint8_t(clock.dt * 20.0f * tempoMul + 1.0f);

        nblendPaletteTowardPalette(currentPalette, targetPalette, 8);

        if (clock.wrapped && (clock.beatCount % 8 == 0)) {
            if (f.music.weight.value > 0.6f) {
                targetPalette = LavaColors_p;
            } else if (f.music.brightness.value > 0.6f) {
                targetPalette = PartyColors_p;
            } else if (f.music.activity.value < 0.3f) {
                targetPalette = ForestColors_p;
            } else {
                targetPalette = OceanColors_p;
            }
        }

        const uint8_t val = uint8_t(255.0f * f.hsvLevel());
        for (int i = 0; i < n; ++i) {
            const uint8_t noise = inoise8(i * 35, noiseX);
            leds[i] = ColorFromPalette(currentPalette, noise + colorLoop, val, LINEARBLEND);
        }

        const float shimmerRate = 0.05f + 2.0f * theme::clamp01(f.trebleLevel);
        int shimmers = int(shimmerRate);
        if (random8() < uint8_t((shimmerRate - float(shimmers)) * 255.0f)) ++shimmers;
        for (int s = 0; s < shimmers; ++s) {
            leds[random16(n)] += CRGB(val / 2, val / 2, val / 2);
        }
    }
};

// ---------------------------------------------------------------------------
// Fire Tribe Wonderland: layered noise waves in warm flame and sunset palettes.
// Pulse locks to the beat clock and amber sparkles scatter with treble.
class FireTribeAnimation : public Animation {
    uint8_t       gHue;
    uint16_t      t;
    CRGBPalette16 flamePalette;
    CRGBPalette16 sunsetPalette;
    BeatClock     clock;

public:
    FireTribeAnimation()
        : gHue(0), t(0) {
        flamePalette = CRGBPalette16(
            CHSV(0, 255, 100), CHSV(10, 255, 180), CHSV(20, 200, 255), CHSV(25, 180, 255),
            CHSV(30, 180, 200), CHSV(40, 150, 150), CHSV(20, 255, 255), CHSV(10, 200, 180),
            CHSV(0, 255, 100), CHSV(10, 255, 180), CHSV(20, 200, 255), CHSV(25, 180, 255),
            CHSV(30, 180, 200), CHSV(40, 150, 150), CHSV(20, 255, 255), CHSV(10, 200, 180));
        sunsetPalette = CRGBPalette16(
            CHSV(5, 255, 255), CHSV(15, 200, 255), CHSV(25, 150, 230), CHSV(35, 180, 200),
            CHSV(10, 150, 220), CHSV(20, 180, 240), CHSV(30, 160, 255), CHSV(40, 180, 255),
            CHSV(5, 255, 255), CHSV(15, 200, 255), CHSV(25, 150, 230), CHSV(35, 180, 200),
            CHSV(10, 150, 220), CHSV(20, 180, 240), CHSV(30, 160, 255), CHSV(40, 180, 255));
    }

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        clock.update(f, 110.0f);
        const float tempoMul = 0.5f + (clock.bpmUsed / 120.0f);
        t += uint16_t(clock.dt * 60.0f * tempoMul + 1.0f);
        gHue += uint8_t(clock.dt * 15.0f);

        fadeToBlackBy(leds, n, 16);

        const CRGBPalette16& pal = (f.music.weight.value > 0.5f) ? flamePalette : sunsetPalette;
        const float pulse = clock.pulse(2.5f);
        const uint8_t val = uint8_t(255.0f * f.hsvLevel() * (0.65f + 0.35f * pulse));

        for (int i = 0; i < n; ++i) {
            const uint8_t offset = sin8(i * 3 + t / 2);
            const uint8_t index = inoise8(i * 14, t + offset * 2);
            leds[i] = ColorFromPalette(pal, index + gHue, val, LINEARBLEND);
        }

        const int sparks = (clock.wrapped ? 2 : 0) + (random8() < uint8_t(100.0f * theme::clamp01(f.trebleLevel)) ? 1 : 0);
        for (int s = 0; s < sparks; ++s) {
            leds[random16(n)] += CHSV(random8(12, 38), 255, val);
        }
    }
};

// ---------------------------------------------------------------------------
// Cosmic Chaos: multi-state quantum cosmic disturbance. Switches modes on music
// structural shifts (drop, buildup, anomaly) rather than wall-clock timers. Zero
// heap allocations using fixed-size wormhole buffers.
class CosmicChaosAnimation : public Animation {
    enum ChaosMode { SWIRL = 0, BURST, RIFTS, TSUNAMI };
    ChaosMode mode;
    uint8_t   gHue;
    uint16_t  noiseSeed;
    struct Wormhole {
        float pos;
        float vel;
        uint8_t hue;
    };
    static const int kMaxWormholes = 4;
    Wormhole  wormholes[kMaxWormholes];
    BeatClock clock;

public:
    CosmicChaosAnimation()
        : mode(SWIRL), gHue(0), noiseSeed(5338) {
        for (int i = 0; i < kMaxWormholes; ++i) {
            wormholes[i].pos = float(i * 30);
            wormholes[i].vel = (i % 2 == 0 ? 15.0f : -20.0f);
            wormholes[i].hue = uint8_t(i * 64);
        }
    }

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        clock.update(f, 125.0f);
        const float tempoMul = 0.5f + (clock.bpmUsed / 120.0f);
        gHue += uint8_t(clock.dt * 30.0f * tempoMul + 1.0f);

        // The drop window and the buildup episode, not their raw flags: the window
        // stays open for as long as the payoff does and a buildup is one episode for
        // its whole length. f.buildup is kept for a block built by hand.
        if (f.dropDetected || f.episode[SIG_DROP].state == EP_ACTIVE) {
            mode = TSUNAMI;
        } else if (f.episode[SIG_BUILDUP].state == EP_ACTIVE || f.buildup > 0.4f) {
            mode = BURST;
        } else if (f.anomaly) {
            mode = RIFTS;
        } else if (clock.wrapped && (clock.beatCount % 16 == 0)) {
            mode = static_cast<ChaosMode>((static_cast<int>(mode) + 1) % 4);
            noiseSeed += 4096;
        }

        const uint8_t val = uint8_t(255.0f * f.hsvLevel());

        switch (mode) {
            case SWIRL: {
                for (int i = 0; i < n; ++i) {
                    const uint8_t hue = gHue + inoise8(i * 15, noiseSeed);
                    const uint8_t bri = scale8(sin8(i * 4 + uint8_t(clock.phase * 255.0f)), val);
                    leds[i] = CHSV(hue, 240, bri);
                }
                break;
            }
            case BURST: {
                fadeToBlackBy(leds, n, 40);
                const int center = int(theme::pingPong(clock.phase) * float(n - 1) + 0.5f);
                for (int d = -6; d <= 6; ++d) {
                    const int p = center + d;
                    if (p >= 0 && p < n) {
                        const uint8_t b = scale8(uint8_t(255 - abs(d) * 38), val);
                        leds[p] += CHSV(gHue + d * 8, 220, b);
                    }
                }
                break;
            }
            case RIFTS: {
                fadeToBlackBy(leds, n, 25);
                for (int i = 0; i < n; i += 4) {
                    const uint8_t noise = inoise8(i * 20 + noiseSeed, uint16_t(gHue * 4));
                    if (noise > 160) {
                        leds[i] = CHSV(gHue + noise, 180, val);
                        if (i + 1 < n) leds[i + 1] = CHSV(gHue + noise + 32, 220, val / 2);
                    }
                }
                break;
            }
            case TSUNAMI: {
                for (int i = 0; i < n; ++i) {
                    const uint8_t idx = sin8(i * 8 + uint8_t(clock.phase * 255.0f)) + cos8(i * 6 - gHue);
                    leds[i] = ColorFromPalette(PartyColors_p, idx, val, LINEARBLEND);
                }
                break;
            }
        }

        for (int i = 0; i < kMaxWormholes; ++i) {
            wormholes[i].pos += wormholes[i].vel * clock.dt * tempoMul;
            while (wormholes[i].pos < 0.0f) wormholes[i].pos += float(n);
            while (wormholes[i].pos >= float(n)) wormholes[i].pos -= float(n);
            const int p = int(wormholes[i].pos);
            if (p >= 0 && p < n) {
                leds[p] += CHSV(wormholes[i].hue + gHue, 240, val);
            }
        }

        if (random8() < uint8_t(80.0f * theme::clamp01(f.trebleLevel))) {
            leds[random16(n)] += CRGB(val, val, val);
        }
    }
};

// ---------------------------------------------------------------------------
// Cosmic Beast of Many Moods: layered noise body, rhythmic pulse ribs, and
// chaos bursts that ignite on drops and anomalies.
class CosmicBeastAnimation : public Animation {
    uint8_t       gHue;
    uint16_t      t;
    CRGBPalette16 currentPalette;
    CRGBPalette16 altPalette;
    BeatClock     clock;

public:
    CosmicBeastAnimation()
        : gHue(0), t(0), currentPalette(RainbowColors_p), altPalette(LavaColors_p) {}

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        clock.update(f, 130.0f);
        const float tempoMul = 0.5f + (clock.bpmUsed / 120.0f);
        t += uint16_t(clock.dt * 80.0f * tempoMul + 1.0f);
        gHue += uint8_t(clock.dt * 30.0f * tempoMul + 1.0f);

        nblendPaletteTowardPalette(currentPalette, altPalette, 10);

        if (clock.wrapped && (clock.beatCount % 8 == 0)) {
            if (f.music.weight.value > 0.6f) altPalette = LavaColors_p;
            else if (f.music.brightness.value > 0.6f) altPalette = PartyColors_p;
            else if (f.music.activity.value > 0.6f) altPalette = RainbowColors_p;
            else altPalette = OceanColors_p;
        }

        fadeToBlackBy(leds, n, 28);
        const float pulse = clock.pulse(3.0f);
        const uint8_t val = uint8_t(255.0f * f.hsvLevel());

        for (int i = 0; i < n; ++i) {
            const uint8_t index = inoise8(i * 12, t);
            leds[i] += ColorFromPalette(currentPalette, index + gHue, scale8(140, val));
        }

        const int ribStep = 3 + int((1.0f - theme::clamp01(f.music.activity.value)) * 4.0f);
        const uint8_t ribBri = scale8(uint8_t(pulse * 255.0f), val);
        if (ribBri > 10) {
            for (int i = 0; i < n; i += ribStep) {
                leds[i] += CHSV(gHue + i * 4, 255, ribBri);
            }
        }

        if (f.dropDetected || f.anomaly) {
            for (int i = 0; i < n; ++i) {
                leds[i] += CHSV(gHue + i * 2, 220, val);
            }
        } else if (clock.wrapped && f.beatConfidence > 0.4f) {
            for (int k = 0; k < 3; ++k) {
                leds[random16(n)] += CHSV(random8(), 200, val);
            }
        }
    }
};

// ---------------------------------------------------------------------------
// Trippy Hippie Wonderland: pastel flowing rainbow waves, blooming flowers
// spreading with beat phase, and swirling mandala accents.
class TrippyHippieAnimation : public Animation {
    uint8_t       gHue;
    uint16_t      flowOffset;
    BeatClock     clock;
    CRGBPalette16 wonderPalette;
    int           bloomPos;
    float         bloomRadius;
    bool          bloomActive;

public:
    TrippyHippieAnimation()
        : gHue(0), flowOffset(0), bloomPos(0), bloomRadius(0.0f), bloomActive(false) {
        wonderPalette = CRGBPalette16(
            CRGB::Lavender, CRGB::LightPink, CRGB::LightSkyBlue, CRGB::PaleGreen,
            CRGB::Violet, CRGB::Pink, CRGB::SkyBlue, CRGB::MintCream,
            CRGB::Orchid, CRGB::HotPink, CRGB::Turquoise, CRGB::LimeGreen,
            CRGB::Purple, CRGB::DeepPink, CRGB::DodgerBlue, CRGB::SpringGreen
        );
    }

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        clock.update(f, 90.0f);
        const float tempoMul = 0.5f + (clock.bpmUsed / 120.0f);
        flowOffset += uint16_t(clock.dt * 50.0f * tempoMul + 1.0f);
        gHue += uint8_t(clock.dt * 20.0f);

        fadeToBlackBy(leds, n, 12);
        const uint8_t val = uint8_t(255.0f * f.hsvLevel());

        for (int i = 0; i < n; ++i) {
            const uint8_t noise = inoise8(i * 12, flowOffset);
            const uint8_t bright = scale8(sin8(noise) / 2 + 100, val);
            leds[i] = ColorFromPalette(wonderPalette, noise + gHue, bright, LINEARBLEND);
        }

        if (clock.wrapped) {
            bloomActive = true;
            bloomPos = random16(n);
            bloomRadius = 0.0f;
        }

        if (bloomActive) {
            bloomRadius += clock.dt * 18.0f * tempoMul;
            const int rInt = int(bloomRadius);
            const uint8_t bloomVal = (bloomRadius < 15.0f) ? uint8_t((1.0f - bloomRadius / 15.0f) * val) : 0;
            if (bloomVal > 5) {
                const int left = bloomPos - rInt;
                const int right = bloomPos + rInt;
                if (left >= 0 && left < n)   nblend(leds[left],  CHSV(gHue + 64, 200, bloomVal), 140);
                if (right >= 0 && right < n) nblend(leds[right], CHSV(gHue + 64, 200, bloomVal), 140);
            } else {
                bloomActive = false;
            }
        }

        if (f.music.activity.value > 0.4f) {
            for (int i = 0; i < n; i += 4) {
                leds[i] += CHSV(gHue + i * 3, 180, scale8(140, val));
            }
        }

        if (f.beatDetected && f.beatConfidence > 0.5f) {
            for (int b = 0; b < 2; ++b) {
                leds[random16(n)] += CHSV(random8(), 240, val);
            }
        }
    }
};

// ---------------------------------------------------------------------------
// Plasma Effect: classic dual-sine interference in RainbowColors palette.
// Motion tracks tempo and beat pulse flares brightness.
class PlasmaEffectAnimation : public Animation {
    float     plasmaTime;
    BeatClock clock;

public:
    PlasmaEffectAnimation() : plasmaTime(0.0f) {}

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        clock.update(f, 95.0f);
        const float tempoMul = 0.5f + (clock.bpmUsed / 120.0f);
        plasmaTime += clock.dt * 45.0f * tempoMul;
        while (plasmaTime >= 256.0f) plasmaTime -= 256.0f;

        const uint8_t y = uint8_t(plasmaTime);
        const float pulse = clock.pulse(2.5f);
        const uint8_t val = uint8_t(255.0f * f.hsvLevel() * (0.7f + 0.3f * pulse));
        const uint8_t freq = uint8_t(10.0f + 15.0f * theme::clamp01(f.music.activity.value));

        for (int i = 0; i < n; ++i) {
            const uint8_t x = uint8_t(i * freq);
            const uint8_t colorIndex = sin8(x + y) + cos8(x - y);
            leds[i] = ColorFromPalette(RainbowColors_p, colorIndex, val, LINEARBLEND);
        }
    }
};

// ---------------------------------------------------------------------------
// Plasma Effect 2: multi-harmonic party plasma with intense beat flaring.
class PlasmaEffectTwoAnimation : public Animation {
    float     plasmaTime;
    BeatClock clock;

public:
    PlasmaEffectTwoAnimation() : plasmaTime(0.0f) {}

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        clock.update(f, 120.0f);
        const float tempoMul = 0.5f + (clock.bpmUsed / 120.0f);
        plasmaTime += clock.dt * 60.0f * tempoMul;
        while (plasmaTime >= 256.0f) plasmaTime -= 256.0f;

        const uint8_t y = uint8_t(plasmaTime);
        const float pulse = clock.pulse(3.0f);
        const uint8_t val = uint8_t(255.0f * f.hsvLevel() * (0.65f + 0.35f * pulse));

        for (int i = 0; i < n; ++i) {
            const uint8_t x = uint8_t(i * 12);
            const uint8_t colorIndex = sin8(x * 2 + y) + cos8(x - y * 2) + sin8(x + y);
            leds[i] = ColorFromPalette(PartyColors_p, colorIndex, val, LINEARBLEND);
        }
    }
};

// ---------------------------------------------------------------------------
// Lava Lamp 2: dual-octave convection lava with thermal heat flares driven by
// bass weight and beat kicks.
class LavaLampTwoAnimation : public Animation {
    uint32_t  x;
    BeatClock clock;

public:
    LavaLampTwoAnimation() : x(0) {}

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        clock.update(f, 65.0f);
        const float tempoMul = 0.5f + (clock.bpmUsed / 120.0f);
        x += uint32_t(clock.dt * 80.0f * tempoMul + 1.0f);

        const float pulse = clock.pulse(2.0f);
        const uint8_t valScale = uint8_t(255.0f * f.hsvLevel());
        const float bassBoost = theme::clamp01(f.music.weight.value);

        for (int i = 0; i < n; ++i) {
            const uint8_t noise1 = inoise8(i * 35, x);
            const uint8_t noise2 = inoise8(i * 70, x * 2);
            const uint8_t heat = qadd8(scale8(noise1, 180), scale8(noise2, 100));

            const uint8_t hue = uint8_t(5.0f + heat * 0.12f + bassBoost * pulse * 15.0f);
            const uint8_t bri = scale8(heat, valScale);
            leds[i] = CHSV(hue, 255, bri);
        }
    }
};

// ---------------------------------------------------------------------------
// Three Sin: three phase-stepping sine waves with cutoffs mapped across bass (Red),
// mid (Green), and treble (Blue).
class ThreeSinAnimation : public Animation {
    uint8_t   wave1;
    uint8_t   wave2;
    uint8_t   wave3;
    BeatClock clock;

public:
    ThreeSinAnimation() : wave1(0), wave2(0), wave3(0) {}

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        clock.update(f, 90.0f);
        const float tempoMul = 0.5f + (clock.bpmUsed / 120.0f);

        wave1 += uint8_t(clock.dt * 60.0f * tempoMul * (0.5f + 1.5f * theme::clamp01(f.bassLevel)) + 1.0f);
        wave2 += uint8_t(clock.dt * 45.0f * tempoMul * (0.5f + 1.5f * theme::clamp01(f.midLevel)) + 1.0f);
        wave3 -= uint8_t(clock.dt * 70.0f * tempoMul * (0.5f + 1.5f * theme::clamp01(f.trebleLevel)) + 1.0f);

        const uint8_t val = uint8_t(255.0f * f.hsvLevel());
        const uint8_t lvl = uint8_t(60.0f - 30.0f * theme::clamp01(f.music.activity.value));

        for (int k = 0; k < n; ++k) {
            const uint8_t r = scale8(qsub8(sin8(5 * k + wave1), lvl), val);
            const uint8_t g = scale8(qsub8(sin8(8 * k + wave2), lvl), val);
            const uint8_t b = scale8(qsub8(sin8(7 * k + wave3), lvl), val);
            leds[k] = CRGB(r, g, b);
        }
    }
};

// ---------------------------------------------------------------------------
// Two Sin nPsy: psychedelic dual cubic waves with complementary rotating hues
// and beat-modulated cutoffs.
class TwoSinPsyAnimation : public Animation {
    uint8_t   thisphase;
    uint8_t   thatphase;
    uint8_t   thisrot;
    BeatClock clock;

public:
    TwoSinPsyAnimation() : thisphase(0), thatphase(0), thisrot(0) {}

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        clock.update(f, 105.0f);
        const float tempoMul = 0.5f + (clock.bpmUsed / 120.0f);
        thisphase += uint8_t(clock.dt * 70.0f * tempoMul + 1.0f);
        thatphase += uint8_t(clock.dt * 50.0f * tempoMul + 1.0f);
        thisrot   += uint8_t(clock.dt * 20.0f * (0.5f + theme::clamp01(f.music.brightness.value)));

        const uint8_t valScale = uint8_t(255.0f * f.hsvLevel());
        const uint8_t thishue = thisrot;
        const uint8_t thathue = thisrot + 128;
        const uint8_t allfreq = 28;
        const uint8_t cutoff  = uint8_t(70.0f - 30.0f * clock.pulse(2.0f));

        for (int k = 0; k < n; ++k) {
            const uint8_t b1 = scale8(qsub8(cubicwave8((k * allfreq) + thisphase), cutoff), valScale);
            const uint8_t b2 = scale8(qsub8(cubicwave8((k * allfreq) + 128 + thatphase), cutoff), valScale);
            leds[k]  = CHSV(thishue, 240, b1);
            leds[k] += CHSV(thathue, 240, b2);
        }
    }
};

// ---------------------------------------------------------------------------
// Rainbow with Glitter: smooth scrolling rainbow march with sparkling glitter
// driven by treble and beat onsets.
class RainbowWithGlitterAnimation : public Animation {
    float     hue;
    BeatClock clock;

public:
    RainbowWithGlitterAnimation() : hue(0.0f) {}

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        clock.update(f, 100.0f);
        hue += clock.dt * 100.0f * (clock.bpmUsed / 120.0f);
        while (hue >= 256.0f) hue -= 256.0f;

        fill_rainbow(leds, n, uint8_t(hue), 7);
        const float level = f.hsvLevel();
        const uint8_t val = uint8_t(255.0f * level);
        for (int i = 0; i < n; ++i) leds[i].nscale8_video(val);

        const float glitterRate = 0.1f + 3.0f * theme::clamp01(f.trebleLevel);
        int glitterCount = int(glitterRate) + (clock.wrapped ? 2 : 0);
        if (random8() < uint8_t((glitterRate - float(int(glitterRate))) * 255.0f)) ++glitterCount;
        for (int g = 0; g < glitterCount; ++g) {
            leds[random16(n)] += CRGB(val, val, val);
        }
    }
};

