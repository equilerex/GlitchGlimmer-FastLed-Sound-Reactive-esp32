// BeatClock.h
#pragma once
#include <Arduino.h>
#include <cmath>
#include "../audio/AudioFeatures.h"

// A beat phase an animation can draw from without caring whether the music is
// currently trackable.
//
// f.beatPhase is only meaningful while f.beatConfidence is high, and it jumps when
// the tracker relocks. Reading it raw makes a sweep teleport on every relock and
// stall through a beatless passage. This clock free-runs on its own oscillator and
// is pulled toward f.beatPhase by an amount that scales with how much the tracker
// is trusted, so a relock is a slew rather than a jump and a beatless passage
// carries on at the last tempo instead of freezing.
//
// One instance per animation. Fixed size, no allocation.
class BeatClock {
public:
    // Confidence at which the tracker starts steering the clock, and the range
    // over which its influence ramps to full. Below kLockLo the clock is free.
    static constexpr float kLockLo = 0.30f;
    static constexpr float kLockHi = 0.60f;
    // Fraction of the wrapped phase error removed per frame at full lock.
    static constexpr float kPull = 0.25f;

    float    phase     = 0.0f;   // 0..1 through the current beat, 0 = on the beat
    float    lock      = 0.0f;   // 0..1 how much the music is steering the clock
    uint32_t beatCount = 0;      // wraps of phase since construction
    bool     wrapped   = false;  // true on the one update where phase crossed 1.0
    float    dt        = 0.0f;   // seconds since the last update, clamped
    float    bpmUsed   = 0.0f;   // the tempo the clock actually advanced at

    // fallbackBpm is the tempo used while unlocked, so an animation keeps its
    // own character in silence. While locked the tracked bpm takes over.
    void update(const AudioFeatures& f, float fallbackBpm) {
        const uint32_t now = millis();
        float dt = (lastMs == 0) ? 0.0f : (now - lastMs) * 0.001f;
        lastMs = now;
        if (dt > 0.25f) dt = 0.25f;   // a stall must not fling the phase
        this->dt = dt;

        const float target = f.beatConfidence >= kLockHi ? 1.0f
            : (f.beatConfidence <= kLockLo ? 0.0f
               : (f.beatConfidence - kLockLo) / (kLockHi - kLockLo));
        lock += (target - lock) * 0.1f;

        float bpm = fallbackBpm;
        if (lock > 0.5f && f.bpm >= 50.0f && f.bpm <= 200.0f) bpm = f.bpm;
        bpmUsed = bpm;
        phase += dt * bpm * (1.0f / 60.0f);

        if (lock > 0.01f) {
            float err = f.beatPhase - phase;
            err -= floorf(err + 0.5f);            // wrap to -0.5..0.5
            phase += err * kPull * lock;
        }

        wrapped = false;
        while (phase >= 1.0f) { phase -= 1.0f; ++beatCount; wrapped = true; }
        while (phase < 0.0f)  { phase += 1.0f; }
    }

    // Phase across two beats, 0..1. Lets a back-and-forth motion take one beat out
    // and one beat back without a second clock.
    float phase2() const { return ((beatCount & 1u) + phase) * 0.5f; }

    // 1.0 on the beat, falling to 0 by the next. `sharp` > 1 tightens the hit.
    float pulse(float sharp = 2.0f) const {
        return powf(1.0f - phase, sharp);
    }

private:
    uint32_t lastMs = 0;
};

// Same 1..0 shape as BeatClock::pulse, for a phase measured from some other point,
// such as the second thump of a heartbeat. Negative offsets (before the hit) give 0.
inline float beatPulseAt(float phase, float offset, float sharp = 2.0f) {
    float p = phase - offset;
    if (p < 0.0f) return 0.0f;
    return powf(1.0f - p, sharp);
}

// How far into a build the music is, 0..1. It follows the firmware's buildup
// episode: it develops over the episode's own length, so a twenty second buildup
// takes twenty seconds to reach full where a raw displacement would have pinned it in
// two. While the episode waits out its window it holds, and once the episode is over
// it falls back quickly. Idle it reads 0, which is honest: a riser drawn when nothing
// is building should not pretend otherwise.
//
// A block built by hand with no episode but a positive f.buildup is still read the
// old way, since the harness builds features that way and a producer that does not
// know about episodes has to get normal behaviour.
class TensionRamp {
public:
    static constexpr float kRisePerSec = 0.20f;   // never faster than about 5 s from nothing to full
    static constexpr float kFallPerSec = 0.80f;
    static constexpr float kFullMs     = 12000.0f; // the episode length that reads as full tension

    float value = 0.0f;

    float update(const AudioFeatures& f, float dt) {
        const EpisodeStatus& e = f.episode[SIG_BUILDUP];
        if (e.state == EP_ACTIVE || e.state == EP_FADING) {
            const float grown = float(e.elapsedMs) >= kFullMs ? 1.0f : float(e.elapsedMs) / kFullMs;
            if (value < grown) {
                value += kRisePerSec * dt;
                if (value > grown) value = grown;
            }
            // FADING holds: the condition has lapsed but the section has not ended.
        } else if (f.buildup > 0.0f) {
            const float joined = f.buildup > 1.0f ? 1.0f : f.buildup;
            value += kRisePerSec * dt;
            if (value < joined) value = joined;   // joining a build already under way
            if (value > 1.0f) value = 1.0f;
        } else {
            value -= kFallPerSec * dt;
            if (value < 0.0f) value = 0.0f;
        }
        return value;
    }
};

// A two-state switch with a dead band and a minimum hold, for animations that
// used to flip a mode on a timer and now follow a music coordinate instead. The
// dead band stops a value sitting on the threshold from alternating, and the hold
// keeps a look on screen long enough to be read as a look.
class HoldLatch {
public:
    bool     high = false;
    uint32_t since = 0;

    bool update(float v, float lo, float hi, uint32_t holdMs) {
        const uint32_t now = millis();
        if (now - since >= holdMs) {
            if (!high && v > hi)      { high = true;  since = now; }
            else if (high && v < lo)  { high = false; since = now; }
        }
        return high;
    }
};

// The same idea across n choices: the incumbent is replaced only by a choice that
// beats it by `margin` and only after `holdMs` on screen.
class HoldSelect {
public:
    int      index = 0;
    uint32_t since = 0;

    int update(const float* score, int n, float margin, uint32_t holdMs) {
        int best = 0;
        for (int i = 1; i < n; ++i) if (score[i] > score[best]) best = i;
        const uint32_t now = millis();
        if (best != index && score[best] > score[index] + margin && now - since >= holdMs) {
            index = best;
            since = now;
        }
        return index;
    }
};
